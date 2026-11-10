#include "coop.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "../../snes/ppu.h"
#include "../config.h"
#include "../load_gfx.h"
#include "../overworld.h"
#include "../variables.h"
#include "../zelda_rtl.h"
#include "draw.h"

// Classic ALttPO compatibility:
//   UDP envelope 0x02 (header 0x651F, 20-byte group, kind, index)
//   serialization 0x13 (team, frame, TLVs)
// The protocol is described in docs/engine/COOP-ALTTPO.md. This is an
// independent implementation from that description.
enum {
  kCoopHeader = 0x651F,
  kCoopProtocol = 0x02,
  kCoopSerialization = 0x13,
  kCoopGroupLen = 20,
  kCoopNameLen = 20,
  kCoopMaxPacket = 1452,
  kCoopMaxPlayers = 16,
  kCoopMaxSprites = 128,
  kCoopMaxChars = 512,
  kCoopNumPalettes = 8,
  kCoopNumLinkSprites = 12,
  kCoopTtlFrames = 255,
  kCoopSramSize = 0x1500,
  kCoopWorkingSramSize = 0x500,
  kCoopMaxDatagrams = 64,
  kCoopMaxSpritePacket = 4096,
};

enum {
  kCoopKind_RequestIndex = 0x00,
  kCoopKind_Broadcast = 0x01,
  kCoopKind_BroadcastSector = 0x02,
  kCoopKind_IndexReply = 0x80,
  kCoopKind_Received = 0x81,
  kCoopKind_ReceivedSector = 0x82,
};

enum {
  kCoopTlv_Location = 0x01,
  kCoopTlv_Sfx = 0x02,
  kCoopTlv_Sprites = 0x03,
  kCoopTlv_SpritesCont = 0x04,
  kCoopTlv_Wram = 0x05,
  kCoopTlv_Sram = 0x06,
  kCoopTlv_Tilemap = 0x07,
  kCoopTlv_Objects = 0x08,
  kCoopTlv_Ancilla = 0x09,
  kCoopTlv_Torches = 0x0A,
  kCoopTlv_Pvp = 0x0B,
  kCoopTlv_Name = 0x0C,
  kCoopTlv_SmEvents = 0x0D,
  kCoopTlv_SramBuffer = 0x0E,
  kCoopTlv_SmLocation = 0x0F,
  kCoopTlv_SmSprite = 0x10,
};

enum {
  kCoopSpriteMaxPixels = 16,
};

typedef struct CoopWriter {
  uint8 *data;
  int cap;
  int len;
  bool ok;
} CoopWriter;

typedef struct CoopReader {
  const uint8 *data;
  int len;
  int pos;
  bool ok;
} CoopReader;

typedef struct CoopSprite {
  uint8 index;
  uint8 b0, b1, b2, b3, b4;
  uint16 chr;
  int x, y;
  uint8 palette, priority;
  bool hflip, vflip, size, enabled;
} CoopSprite;

typedef struct CoopPlayer {
  bool used;
  bool active;
  bool have_sram;
  bool name_set;
  uint16 index;
  uint8 team;
  uint8 frame;
  int ttl;
  uint8 module, sub_module, sub_sub_module;
  uint32 location, actual_location;
  int16 xoffs, yoffs;
  uint16 x, y, dungeon, dungeon_entrance;
  uint16 last_ow_x, last_ow_y;
  uint16 color;
  uint8 in_sm;
  char name[kCoopNameLen + 1];
  int sprite_count;
  CoopSprite sprites[kCoopMaxSprites];
  bool chr_valid[kCoopMaxChars];
  uint16 chr[kCoopMaxChars][16];
  bool palette_valid[kCoopNumPalettes];
  uint16 palette[kCoopNumPalettes][16];
  uint8 sram[kCoopSramSize];
} CoopPlayer;

typedef struct CoopLocal {
  uint8 module, sub_module, sub_sub_module;
  uint32 location, actual_location, last_location;
  int16 xoffs, yoffs;
} CoopLocal;

bool g_coop_changed_content;

enum {
  kCoopLocalInboxSize = 64,
};

typedef struct CoopPacket {
  int len;
  uint8 data[kCoopMaxPacket];
} CoopPacket;

// Everything the ALttPO code needs for one player. Network mode uses only
// context 0; local split-screen gives each instance its own context and swaps
// the pointer per frame. Packets still go through the same serialization and
// merge code, only the transport changes.
typedef struct CoopContext {
  bool started;
  bool active;
  bool changed_content;
  uint16 frame;
  uint16 index;
  uint8 team;
  uint8 group[kCoopGroupLen];
  uint8 name[kCoopNameLen];
  uint16 color;
  CoopPlayer players[kCoopMaxPlayers];
  CoopLocal local;
  int inbox_head, inbox_count;
  CoopPacket inbox[kCoopLocalInboxSize];
} CoopContext;

static CoopContext s_ctx[2];
static CoopContext *s_ctx_cur = &s_ctx[0];
static bool s_local_mode;
#ifdef _WIN32
static SOCKET s_sock = INVALID_SOCKET;
static bool s_wsa_started;
#else
static int s_sock = -1;
#endif
static struct sockaddr_in s_server;

#define s_started (s_ctx_cur->started)
#define s_active (s_ctx_cur->active)
#define s_frame (s_ctx_cur->frame)
#define s_index (s_ctx_cur->index)
#define s_team (s_ctx_cur->team)
#define s_group (s_ctx_cur->group)
#define s_name (s_ctx_cur->name)
#define s_players (s_ctx_cur->players)
#define s_local (s_ctx_cur->local)

// ---------------------------------------------------------------------------
// Small byte reader/writer used by both the packet builder and parser.

static void CoopWriteBytes(CoopWriter *w, const void *p, int n) {
  if (!w->ok || n < 0 || w->len + n > w->cap) {
    w->ok = false;
    return;
  }
  memcpy(w->data + w->len, p, (size_t)n);
  w->len += n;
}

static void CoopWrite8(CoopWriter *w, uint8 v) {
  CoopWriteBytes(w, &v, 1);
}

static void CoopWrite16(CoopWriter *w, uint16 v) {
  uint8 b[2] = { (uint8)v, (uint8)(v >> 8) };
  CoopWriteBytes(w, b, 2);
}

static void CoopWrite24(CoopWriter *w, uint32 v) {
  uint8 b[3] = { (uint8)v, (uint8)(v >> 8), (uint8)(v >> 16) };
  CoopWriteBytes(w, b, 3);
}

static void CoopWrite32(CoopWriter *w, uint32 v) {
  uint8 b[4] = { (uint8)v, (uint8)(v >> 8), (uint8)(v >> 16),
                 (uint8)(v >> 24) };
  CoopWriteBytes(w, b, 4);
}

static uint8 CoopRead8(CoopReader *r) {
  if (!r->ok || r->pos >= r->len) {
    r->ok = false;
    return 0;
  }
  return r->data[r->pos++];
}

static uint16 CoopRead16(CoopReader *r) {
  uint16 lo = CoopRead8(r);
  uint16 hi = CoopRead8(r);
  return lo | (hi << 8);
}

static uint32 CoopRead24(CoopReader *r) {
  uint32 lo = CoopRead8(r);
  uint32 mid = CoopRead8(r);
  uint32 hi = CoopRead8(r);
  return lo | (mid << 8) | (hi << 16);
}

static uint32 CoopRead32(CoopReader *r) {
  uint32 lo = CoopRead16(r);
  uint32 hi = CoopRead16(r);
  return lo | (hi << 16);
}

static void CoopReadSkip(CoopReader *r, int n) {
  if (!r->ok || n < 0 || r->pos + n > r->len) {
    r->ok = false;
    return;
  }
  r->pos += n;
}

static const uint8 *CoopReadBytes(CoopReader *r, int n) {
  if (!r->ok || n < 0 || r->pos + n > r->len) {
    r->ok = false;
    return NULL;
  }
  const uint8 *p = r->data + r->pos;
  r->pos += n;
  return p;
}

// ---------------------------------------------------------------------------
// Socket helpers.

static bool CoopWouldBlock(void) {
#ifdef _WIN32
  int e = WSAGetLastError();
  return e == WSAEWOULDBLOCK || e == WSAEINTR;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

static void CoopCloseSocket(void) {
#ifdef _WIN32
  if (s_sock != INVALID_SOCKET) {
    closesocket(s_sock);
    s_sock = INVALID_SOCKET;
  }
  if (s_wsa_started) {
    WSACleanup();
    s_wsa_started = false;
  }
#else
  if (s_sock >= 0) {
    close(s_sock);
    s_sock = -1;
  }
#endif
}

static bool CoopSetNonBlocking(void) {
#ifdef _WIN32
  u_long mode = 1;
  return ioctlsocket(s_sock, FIONBIO, &mode) == 0;
#else
  int flags = fcntl(s_sock, F_GETFL, 0);
  if (flags < 0)
    return false;
  return fcntl(s_sock, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static bool CoopOpenSocket(void) {
  if (s_started)
    return true;

#ifdef _WIN32
  if (!s_wsa_started) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
      fprintf(stderr, "coop: WSAStartup failed\n");
      return false;
    }
    s_wsa_started = true;
  }
#endif

  char port[16];
  snprintf(port, sizeof(port), "%u", (unsigned)g_config.coop_port);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  struct addrinfo *result = NULL;
  int err = getaddrinfo(g_config.coop_server, port, &hints, &result);
  if (err != 0 || result == NULL) {
    fprintf(stderr, "coop: cannot resolve '%s': %s\n",
            g_config.coop_server,
#ifdef _WIN32
            "getaddrinfo failed"
#else
            gai_strerror(err)
#endif
    );
    return false;
  }

  memcpy(&s_server, result->ai_addr, sizeof(s_server));
  freeaddrinfo(result);

#ifdef _WIN32
  s_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (s_sock == INVALID_SOCKET) {
    fprintf(stderr, "coop: socket() failed (%d)\n", (int)WSAGetLastError());
    return false;
  }
#else
  s_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (s_sock < 0) {
    fprintf(stderr, "coop: socket() failed: %s\n", strerror(errno));
    return false;
  }
#endif

  if (!CoopSetNonBlocking()) {
    fprintf(stderr, "coop: cannot make the UDP socket non-blocking\n");
    CoopCloseSocket();
    return false;
  }

  if (connect(s_sock, (struct sockaddr *)&s_server, sizeof(s_server)) != 0) {
    fprintf(stderr, "coop: connect() to %s:%u failed\n",
            g_config.coop_server, (unsigned)g_config.coop_port);
    CoopCloseSocket();
    return false;
  }

  s_started = true;
  fprintf(stderr, "coop: group '%.20s' -> %s:%u as '%s'\n",
          (const char *)s_group, g_config.coop_server,
          (unsigned)g_config.coop_port, (const char *)s_name);
  fflush(stderr);
  return true;
}

static void CoopSend(const CoopWriter *w) {
  if (!s_started || !w->ok || w->len <= 0 || w->len > kCoopMaxPacket)
    return;
  if (s_local_mode) {
    if (w->data[23] == kCoopKind_RequestIndex)
      return;
    CoopContext *peer = &s_ctx[(s_ctx_cur == &s_ctx[0]) ? 1 : 0];
    if (peer->inbox_count >= kCoopLocalInboxSize)
      return;  // drop, like a full UDP receive buffer

    // Turn the outgoing envelope into the one the relay would deliver:
    // broadcast -> received, and sector broadcasts lose their routing field.
    // Header: u16 magic, u8 protocol, group[20], u8 kind, u16 sender,
    //         [u32 sector], payload.
    bool sector = w->data[23] == kCoopKind_BroadcastSector;
    int head_len = 26;
    int payload_pos = sector ? 30 : 26;
    CoopPacket *pkt = &peer->inbox[(peer->inbox_head + peer->inbox_count) %
                                   kCoopLocalInboxSize];
    memcpy(pkt->data, w->data, (size_t)head_len);
    pkt->data[23] = sector ? kCoopKind_ReceivedSector : kCoopKind_Received;
    memcpy(pkt->data + head_len, w->data + payload_pos,
           (size_t)(w->len - payload_pos));
    pkt->len = head_len + w->len - payload_pos;
    peer->inbox_count++;
    return;
  }
  int sent = (int)send(s_sock, (const char *)w->data, w->len, 0);
  if (sent < 0 && !CoopWouldBlock())
    fprintf(stderr, "coop: send failed: %s\n", strerror(errno));
}

static CoopWriter CoopBeginPacket(uint8 *buffer, int capacity, uint8 kind,
                                  uint16 index, uint16 sector) {
  CoopWriter w;
  memset(&w, 0, sizeof(w));
  w.data = buffer;
  w.cap = capacity;
  w.ok = true;
  CoopWrite16(&w, kCoopHeader);
  CoopWrite8(&w, kCoopProtocol);
  CoopWriteBytes(&w, s_group, kCoopGroupLen);
  CoopWrite8(&w, kind);
  CoopWrite16(&w, index);
  if (kind == kCoopKind_BroadcastSector)
    CoopWrite32(&w, sector);
  CoopWrite8(&w, kCoopSerialization);
  CoopWrite8(&w, s_team);
  CoopWrite8(&w, (uint8)s_frame);
  return w;
}

// ---------------------------------------------------------------------------
// Local player state used for visibility and the ghost offset.

static uint32 CoopLocalLocation(void) {
  uint32 dark = is_in_dark_world & 1;
  uint32 indoor = player_is_indoors & 1;
  uint16 room = indoor ? dungeon_room_index : overworld_screen_index;
  return (dark << 17) | (indoor << 16) | room;
}

static bool CoopInScreenTransition(uint8 module, uint8 sub_module) {
  if (module == 0x09 && sub_module >= 0x01)
    return true;
  if (module == 0x07 && (sub_module == 0x02 || sub_module == 0x19))
    return true;
  return false;
}

static bool CoopCanSampleLocation(uint8 module, uint8 sub_module,
                                  uint8 sub_sub_module) {
  switch (module) {
  case 0x07:
    if (sub_module == 0x0e)
      return sub_sub_module > 0x02;
    return true;
  case 0x09:
    if (sub_module >= 0x01 && sub_module <= 0x08)
      return false;
    if ((sub_module == 0x23 || sub_module == 0x2c) && sub_sub_module < 0x03)
      return false;
    return true;
  case 0x0e:
    return sub_module != 0x07;
  default:
    return true;
  }
}

static bool CoopIsBadTime(uint8 module, uint8 sub_module) {
  if (module <= 0x05 || module == 0x14 || module >= 0x1b)
    return true;
  if (module == 0x0e && sub_module == 0x07)
    return true;
  return false;
}

static void CoopUpdateLocal(void) {
  s_local.module = main_module_index;
  s_local.sub_module = submodule_index;
  s_local.sub_sub_module = subsubmodule_index;

  uint32 actual = CoopLocalLocation();
  s_local.last_location = s_local.location;
  s_local.actual_location = actual;
  if (CoopCanSampleLocation(s_local.module, s_local.sub_module,
                            s_local.sub_sub_module) &&
      !CoopInScreenTransition(s_local.module, s_local.sub_module)) {
    s_local.location = actual;
  }
  s_local.xoffs = (int16)(BG2HOFS_copy2 - bg1_x_offset);
  s_local.yoffs = (int16)(BG2VOFS_copy2 - bg1_y_offset);
}

static bool CoopLocationsEqual(uint32 a, uint32 b) {
  if (a == b)
    return true;
  if ((a & 0x010000) && (b & 0x010000) &&
      (a & 0xffff) == (b & 0xffff))
    return true;
  return false;
}

static bool CoopCanSee(const CoopPlayer *p) {
  if (CoopLocationsEqual(s_local.location, p->location))
    return true;
  if (CoopInScreenTransition(s_local.module, s_local.sub_module)) {
    if (CoopLocationsEqual(s_local.actual_location, p->location))
      return true;
    return CoopLocationsEqual(s_local.last_location, p->location);
  }
  return false;
}

// ---------------------------------------------------------------------------
// Remote packet parsing.

static CoopPlayer *CoopGetPlayer(uint16 index) {
  if (index >= kCoopMaxPlayers)
    return NULL;
  CoopPlayer *p = &s_players[index];
  if (!p->used) {
    memset(p, 0, sizeof(*p));
    p->used = true;
    p->index = index;
  }
  return p;
}

static void CoopDecodeSprite(CoopSprite *s) {
  memset(s, 0, sizeof(*s));
}

static void CoopFinishSprite(CoopSprite *s) {
  s->enabled = false;
  if (s->b1 == 0xf0)
    return;
  s->x = (int)(s->b0 | ((s->b4 & 1) << 8));
  if (s->x >= 256)
    s->x -= 512;
  s->size = (s->b4 >> 1) & 1;
  int px = s->size ? 16 : 8;
  if (s->x <= -px || s->x >= 256)
    return;
  s->y = s->b1;
  s->chr = (uint16)(s->b2 | ((s->b3 & 1) << 8));
  s->palette = (s->b3 >> 1) & 7;
  s->priority = (s->b3 >> 4) & 3;
  s->hflip = (s->b3 & 0x40) != 0;
  s->vflip = (s->b3 & 0x80) != 0;
  s->enabled = true;
}

static void CoopReadChr(CoopReader *r, CoopPlayer *p, uint16 chr) {
  if (chr >= kCoopMaxChars) {
    CoopReadSkip(r, 32);
    return;
  }
  for (int i = 0; i < 16; i++)
    p->chr[chr][i] = CoopRead16(r);
  p->chr_valid[chr] = true;
}

static void CoopParseSprites(CoopReader *r, CoopPlayer *p, bool continuation) {
  uint32 start = continuation ? CoopRead8(r) : 0;
  uint32 count = CoopRead8(r);
  if (!continuation)
    p->sprite_count = 0;
  if (count != 0 && start + count > (uint32)p->sprite_count)
    p->sprite_count = (int)(start + count);
  if (p->sprite_count > kCoopMaxSprites)
    p->sprite_count = kCoopMaxSprites;

  for (uint32 i = start; i < start + count; i++) {
    CoopSprite spr;
    CoopDecodeSprite(&spr);
    uint8 flags = CoopRead8(r);
    spr.index = flags & 0x7f;
    spr.b0 = CoopRead8(r);
    spr.b1 = CoopRead8(r);
    spr.b2 = CoopRead8(r);
    spr.b3 = CoopRead8(r);
    uint8 b4raw = CoopRead8(r);
    spr.b4 = b4raw & 0x7f;
    CoopFinishSprite(&spr);

    if (flags & 0x80) {
      CoopReadChr(r, p, spr.chr);
      if (spr.size) {
        CoopReadChr(r, p, (uint16)(spr.chr + 0x01));
        CoopReadChr(r, p, (uint16)(spr.chr + 0x10));
        CoopReadChr(r, p, (uint16)(spr.chr + 0x11));
      }
    }
    if (b4raw & 0x80) {
      uint32 pal = spr.palette;
      if (pal < kCoopNumPalettes) {
        for (int k = 0; k < 16; k++)
          p->palette[pal][k] = CoopRead16(r);
        p->palette_valid[pal] = true;
      } else {
        CoopReadSkip(r, 32);
      }
    }
    if (i < kCoopMaxSprites)
      p->sprites[i] = spr;
  }
}

static void CoopParseLocation(CoopReader *r, CoopPlayer *p) {
  p->module = CoopRead8(r);
  p->sub_module = CoopRead8(r);
  p->sub_sub_module = CoopRead8(r);
  p->location = CoopRead24(r);
  p->actual_location = p->location;
  p->x = CoopRead16(r);
  p->y = CoopRead16(r);
  p->dungeon = CoopRead16(r);
  p->dungeon_entrance = CoopRead16(r);
  p->last_ow_x = CoopRead16(r);
  p->last_ow_y = CoopRead16(r);
  p->xoffs = (int16)CoopRead16(r);
  p->yoffs = (int16)CoopRead16(r);
  p->color = CoopRead16(r);
  p->in_sm = CoopRead8(r);
}

static void CoopParseSram(CoopReader *r, CoopPlayer *p) {
  uint8 temp = CoopRead8(r);
  CoopRead8(r);  // in_sm_for_items when temp != 0; always present
  uint16 start = CoopRead16(r);
  uint16 count = CoopRead16(r);
  (void)temp;
  if (start + count > kCoopSramSize) {
    CoopReadSkip(r, count);
    return;
  }
  const uint8 *data = CoopReadBytes(r, count);
  if (data != NULL) {
    memcpy(p->sram + start, data, count);
    p->have_sram = true;
  }
}

static void CoopSkipTilemap(CoopReader *r) {
  CoopRead32(r);  // timestamp
  CoopRead24(r);  // location
  CoopRead8(r);   // start
  uint8 len = CoopRead8(r);
  for (uint8 i = 0; i < len && r->ok; i++) {
    uint16 offs = CoopRead16(r);
    uint8 count = CoopRead8(r);
    if (offs & 0x8000)
      CoopReadSkip(r, 3);
    else
      CoopReadSkip(r, count * 3);
  }
}

static void CoopSkipAncilla(CoopReader *r) {
  uint8 count = CoopRead8(r);
  for (uint8 i = 0; i < count && r->ok; i++) {
    uint8 index = CoopRead8(r);
    CoopReadSkip(r, index < 5 ? 0x20 : 0x16);
  }
}

static void CoopSkipPvp(CoopReader *r) {
  uint8 active = CoopRead8(r);
  if (active)
    CoopReadSkip(r, 8);
  CoopReadSkip(r, 2);  // sword type, room level
  uint8 len = CoopRead8(r);
  CoopReadSkip(r, len * 9);
}

static void CoopParsePayload(uint16 sender, CoopReader *r) {
  CoopPlayer *p = CoopGetPlayer(sender);
  if (p == NULL)
    return;

  if (CoopRead8(r) != kCoopSerialization)
    return;
  p->team = CoopRead8(r);
  uint8 frame = CoopRead8(r);
  if (frame < p->frame && p->frame < 0xff) {
    p->frame = frame;
    return;
  }
  p->frame = frame;
  p->active = true;
  p->ttl = kCoopTtlFrames;

  while (r->ok && r->pos < r->len) {
    uint8 type = CoopRead8(r);
    switch (type) {
    case kCoopTlv_Location:
      CoopParseLocation(r, p);
      break;
    case kCoopTlv_Sfx:
      CoopReadSkip(r, 2);
      break;
    case kCoopTlv_Sprites:
      CoopParseSprites(r, p, false);
      break;
    case kCoopTlv_SpritesCont:
      CoopParseSprites(r, p, true);
      break;
    case kCoopTlv_Wram: {
      uint8 count = CoopRead8(r);
      CoopRead16(r);
      CoopReadSkip(r, count * 6);
      break;
    }
    case kCoopTlv_Sram:
      CoopParseSram(r, p);
      break;
    case kCoopTlv_Tilemap:
      CoopSkipTilemap(r);
      break;
    case kCoopTlv_Objects:
      CoopReadSkip(r, 0x2a0);
      break;
    case kCoopTlv_Ancilla:
      CoopSkipAncilla(r);
      break;
    case kCoopTlv_Torches: {
      uint8 count = CoopRead8(r);
      CoopReadSkip(r, count * 2);
      break;
    }
    case kCoopTlv_Pvp:
      CoopSkipPvp(r);
      break;
    case kCoopTlv_Name: {
      const uint8 *name = CoopReadBytes(r, kCoopNameLen);
      if (name != NULL) {
        int begin = 0, end = kCoopNameLen;
        while (begin < end && name[begin] == ' ')
          begin++;
        while (end > begin && name[end - 1] == ' ')
          end--;
        memcpy(p->name, name + begin, (size_t)(end - begin));
        p->name[end - begin] = 0;
        p->name_set = true;
      }
      break;
    }
    case kCoopTlv_SmEvents:
      CoopReadSkip(r, 0x54 + 2);
      break;
    case kCoopTlv_SramBuffer: {
      CoopRead16(r);
      uint16 count = CoopRead16(r);
      CoopReadSkip(r, count);
      break;
    }
    case kCoopTlv_SmLocation:
      CoopReadSkip(r, 9);
      break;
    case kCoopTlv_SmSprite:
      CoopReadSkip(r, 4 + 32);
      break;
    default:
      // Unknown TLV: cannot know its length safely.
      return;
    }
  }
}

static void CoopHandleDatagram(const uint8 *data, int len) {
  CoopReader r = { data, len, 0, true };
  if (CoopRead16(&r) != kCoopHeader)
    return;
  if (CoopRead8(&r) != kCoopProtocol)
    return;
  CoopReadSkip(&r, kCoopGroupLen);
  uint8 kind = CoopRead8(&r);
  if (!r.ok)
    return;

  if (kind == kCoopKind_IndexReply) {
    uint16 index = CoopRead16(&r);
    if (!r.ok)
      return;
    if (s_index != index) {
      s_index = index;
      fprintf(stderr, "coop: assigned player index %u\n", (unsigned)index);
      fflush(stderr);
    }
    return;
  }
  if (kind != kCoopKind_Received && kind != kCoopKind_ReceivedSector)
    return;

  uint16 sender = CoopRead16(&r);
  if (!r.ok || sender == s_index)
    return;

  CoopParsePayload(sender, &r);
}

static void CoopDrainSocket(void) {
  if (s_local_mode) {
    for (int i = 0; i < kCoopMaxDatagrams && s_ctx_cur->inbox_count > 0; i++) {
      CoopPacket *pkt = &s_ctx_cur->inbox[s_ctx_cur->inbox_head];
      s_ctx_cur->inbox_head = (s_ctx_cur->inbox_head + 1) % kCoopLocalInboxSize;
      s_ctx_cur->inbox_count--;
      CoopHandleDatagram(pkt->data, pkt->len);
    }
    return;
  }
  uint8 buffer[2048];
  for (int i = 0; i < kCoopMaxDatagrams; i++) {
    int n = (int)recv(s_sock, (char *)buffer, sizeof(buffer), 0);
    if (n < 0) {
      if (CoopWouldBlock())
        return;
#ifdef _WIN32
      fprintf(stderr, "coop: recv failed (%d)\n", (int)WSAGetLastError());
#else
      fprintf(stderr, "coop: recv failed: %s\n", strerror(errno));
#endif
      return;
    }
    if (n == 0)
      return;
    CoopHandleDatagram(buffer, n);
  }
}

// ---------------------------------------------------------------------------
// Outgoing packets.

static void CoopSendIndexRequest(void) {
  if (s_index != 0xFFFF)
    return;
  uint8 buffer[128];
  CoopWriter w = CoopBeginPacket(buffer, sizeof(buffer),
                                 kCoopKind_RequestIndex, 0xFFFF, 0);
  CoopSend(&w);
}

static void CoopSendMainPacket(void) {
  uint8 buffer[256];
  CoopWriter w = CoopBeginPacket(buffer, sizeof(buffer), kCoopKind_Broadcast,
                                 s_index, 0);
  CoopWrite8(&w, kCoopTlv_Location);
  CoopWrite8(&w, s_local.module);
  CoopWrite8(&w, s_local.sub_module);
  CoopWrite8(&w, s_local.sub_sub_module);
  CoopWrite24(&w, s_local.location);
  CoopWrite16(&w, link_x_coord);
  CoopWrite16(&w, link_y_coord);
  CoopWrite16(&w, cur_palace_index_x2);
  CoopWrite16(&w, which_entrance);
  CoopWrite16(&w, link_x_coord);
  CoopWrite16(&w, link_y_coord);
  CoopWrite16(&w, (uint16)s_local.xoffs);
  CoopWrite16(&w, (uint16)s_local.yoffs);
  CoopWrite16(&w, s_ctx_cur->color);
  CoopWrite8(&w, 0);  // in_sm

  CoopWrite8(&w, kCoopTlv_Name);
  CoopWriteBytes(&w, s_name, kCoopNameLen);

  CoopWrite8(&w, kCoopTlv_Sfx);
  CoopWrite8(&w, 0);
  CoopWrite8(&w, 0);

  CoopSend(&w);
}

static void CoopSramTlv(CoopWriter *w, uint16 start, uint16 count) {
  CoopWrite8(w, kCoopTlv_Sram);
  CoopWrite8(w, start == 0 ? 1 : 0);
  CoopWrite8(w, 0);  // in_sm_for_items
  CoopWrite16(w, start);
  CoopWrite16(w, count);
  CoopWriteBytes(w, g_ram + 0xf000 + start, count);
}

static void CoopSendProgress(void) {
  if (s_index == 0xFFFF)
    return;

  if ((s_frame & 15) == 0) {
    uint8 buffer[256];
    CoopWriter w = CoopBeginPacket(buffer, sizeof(buffer),
                                   kCoopKind_Broadcast, s_index, 0);
    CoopSramTlv(&w, 0x340, 0x37c - 0x340);
    CoopSramTlv(&w, 0x3c5, 0x3ca - 0x3c5);
    CoopSend(&w);
  }
  if ((s_frame & 31) == 0) {
    uint8 buffer[768];
    CoopWriter w = CoopBeginPacket(buffer, sizeof(buffer),
                                   kCoopKind_Broadcast, s_index, 0);
    CoopSramTlv(&w, 0x000, 0x250);
    CoopSend(&w);
  }
  if ((s_frame & 31) == 16) {
    uint8 buffer[320];
    CoopWriter w = CoopBeginPacket(buffer, sizeof(buffer),
                                   kCoopKind_Broadcast, s_index, 0);
    CoopSramTlv(&w, 0x280, 0x340 - 0x280);
    CoopSend(&w);
  }
}

static void CoopReadLocalChr(uint16 chr, uint16 out[16]) {
  uint32 base = 0x4000u + (uint32)chr * 16u;
  for (int i = 0; i < 16; i++)
    out[i] = g_zenv.ppu->vram[(base + i) & 0x7fff];
}

static bool CoopLocalHas16x16(uint8 b4) {
  return ((b4 >> 1) & 1) != 0;
}

static void CoopWriteTile(CoopWriter *w, const uint16 tile[16]) {
  for (int i = 0; i < 16; i++)
    CoopWrite16(w, tile[i]);
}

static void CoopSendSprites(void) {
  if (s_index == 0xFFFF)
    return;
  if (CoopIsBadTime(s_local.module, s_local.sub_module))
    return;

  const uint8 *oam = (const uint8 *)g_zenv.ppu->oam;
  int link_start = (sort_sprites_offset_into_oam_buffer >> 2) & 0x7f;

  // Gather the up to 12 OAM entries that belong to the local Link.
  CoopSprite spr[kCoopNumLinkSprites];
  int num = 0;
  for (int j = 0; j < kCoopNumLinkSprites; j++) {
    int i = (link_start + j) & 0x7f;
    CoopSprite s;
    memset(&s, 0, sizeof(s));
    s.index = (uint8)i;
    s.b0 = oam[i * 4 + 0];
    s.b1 = oam[i * 4 + 1];
    s.b2 = oam[i * 4 + 2];
    s.b3 = oam[i * 4 + 3];
    uint8 raw4 = oam[0x200 + (i >> 2)];
    s.b4 = (raw4 >> ((i & 3) * 2)) & 3;
    CoopFinishSprite(&s);
    if (!s.enabled)
      continue;
    spr[num++] = s;
  }
  if (num == 0)
    return;

  bool chr_sent[kCoopMaxChars];
  bool pal_sent[kCoopNumPalettes];
  memset(chr_sent, 0, sizeof(chr_sent));
  memset(pal_sent, 0, sizeof(pal_sent));
  // Shadow and bomb tiles are local-only in the classic client. Marking them
  // as sent makes the receiver use its own VRAM copy, like ALttPO does.
  for (int i = 0x6c; i <= 0x7f; i++)
    chr_sent[i] = true;

  int start = 0;
  while (start < num) {
    uint8 buffer[kCoopMaxSpritePacket];
    uint16 sector = (uint16)s_local.actual_location;
    CoopWriter w = CoopBeginPacket(buffer, sizeof(buffer),
                                   kCoopKind_BroadcastSector, s_index, sector);
    if (start == 0) {
      CoopWrite8(&w, kCoopTlv_Sprites);
    } else {
      CoopWrite8(&w, kCoopTlv_SpritesCont);
      CoopWrite8(&w, (uint8)start);
    }
    int count_pos = w.len;
    CoopWrite8(&w, 0);
    int i = start;
    for (; i < num; i++) {
      const CoopSprite *s = &spr[i];
      bool send_chr = s->chr < 0x80 && !chr_sent[s->chr];
      bool send_pal = !pal_sent[s->palette];
      int mark = w.len;

      CoopWrite8(&w, (uint8)(s->index | (send_chr ? 0x80 : 0)));
      CoopWrite8(&w, s->b0);
      CoopWrite8(&w, s->b1);
      CoopWrite8(&w, s->b2);
      CoopWrite8(&w, s->b3);
      CoopWrite8(&w, (uint8)(s->b4 | (send_pal ? 0x80 : 0)));
      if (send_chr) {
        uint16 tile[16];
        CoopReadLocalChr(s->chr, tile);
        CoopWriteTile(&w, tile);
        if (CoopLocalHas16x16(s->b4)) {
          CoopReadLocalChr((uint16)(s->chr + 0x01), tile);
          CoopWriteTile(&w, tile);
          CoopReadLocalChr((uint16)(s->chr + 0x10), tile);
          CoopWriteTile(&w, tile);
          CoopReadLocalChr((uint16)(s->chr + 0x11), tile);
          CoopWriteTile(&w, tile);
        }
      }
      if (send_pal) {
        uint32 base = 0x80u + (uint32)s->palette * 16u;
        for (int k = 0; k < 16; k++)
          CoopWrite16(&w, g_zenv.ppu->cgram[base + k]);
      }
      if (!w.ok || w.len > kCoopMaxPacket) {
        w.len = mark;
        break;
      }
      if (send_chr) {
        chr_sent[s->chr] = true;
        if (CoopLocalHas16x16(s->b4)) {
          if (s->chr + 0x01 < kCoopMaxChars)
            chr_sent[s->chr + 0x01] = true;
          if (s->chr + 0x10 < kCoopMaxChars)
            chr_sent[s->chr + 0x10] = true;
          if (s->chr + 0x11 < kCoopMaxChars)
            chr_sent[s->chr + 0x11] = true;
        }
      }
      if (send_pal)
        pal_sent[s->palette] = true;
    }
    if (i == start)
      break;  // a single sprite cannot fit; should not happen
    buffer[count_pos] = (uint8)(i - start);
    CoopSend(&w);
    start = i;
  }
}

// ---------------------------------------------------------------------------
// Progress merge rules from the ALttPO study.

static uint16 CoopSramGet16(const uint8 *s, uint16 off) {
  return (uint16)(s[off] | (s[off + 1] << 8));
}

static void CoopSramSet8(uint8 *s, uint16 off, uint8 value, bool *changed) {
  if (s[off] != value) {
    s[off] = value;
    *changed = true;
  }
}

static void CoopSramSet16(uint8 *s, uint16 off, uint16 value, bool *changed) {
  CoopSramSet8(s, off, (uint8)value, changed);
  CoopSramSet8(s, (uint16)(off + 1), (uint8)(value >> 8), changed);
}

static void CoopMergeMax8(uint8 *s, uint16 off, uint8 remote, bool *changed) {
  if (remote > s[off])
    CoopSramSet8(s, off, remote, changed);
}

static void CoopMergeOr8(uint8 *s, uint16 off, uint8 remote, bool *changed) {
  uint8 value = s[off] | remote;
  if (value != s[off])
    CoopSramSet8(s, off, value, changed);
}

static void CoopMergeOr16(uint8 *s, uint16 off, uint16 remote,
                         bool *changed) {
  uint16 value = CoopSramGet16(s, off) | remote;
  if (value != CoopSramGet16(s, off))
    CoopSramSet16(s, off, value, changed);
}

static void CoopApplyPlayerProgress(uint8 *s, const CoopPlayer *p,
                                    bool *graphics_sword,
                                    bool *graphics_shield,
                                    bool *graphics_armor,
                                    bool *pyramid_hole) {
  bool changed = false;

  // 0x340..0x35F: earned items. 0x343 (bombs) and 0x34F (bottle selection)
  // are intentionally not synchronized.
  static const uint16 kMaxItems[] = {
    0x340, 0x341, 0x342, 0x344, 0x345, 0x346, 0x347, 0x348, 0x349,
    0x34a, 0x34b, 0x34c, 0x34d, 0x34e, 0x350, 0x351, 0x352, 0x353,
    0x355, 0x356, 0x357,
  };
  for (size_t i = 0; i < arraysize(kMaxItems); i++)
    CoopMergeMax8(s, kMaxItems[i], p->sram[kMaxItems[i]], &changed);

  // Gloves (0x354): max value and update the glove palette.
  if (p->sram[0x354] > s[0x354]) {
    CoopSramSet8(s, 0x354, p->sram[0x354], &changed);
    *graphics_armor = true;
  }

  // Sword (0x359): max of 1..4; the 0xFF value from the swordsmith is ignored.
  if (p->sram[0x359] >= 1 && p->sram[0x359] <= 4 &&
      p->sram[0x359] > s[0x359]) {
    CoopSramSet8(s, 0x359, p->sram[0x359], &changed);
    *graphics_sword = true;
  }

  // Shield (0x35A): max value and update the shield graphics/palette.
  if (p->sram[0x35a] > s[0x35a]) {
    CoopSramSet8(s, 0x35a, p->sram[0x35a], &changed);
    *graphics_shield = true;
  }

  // Armor (0x35B): max value and update the armor palette.
  if (p->sram[0x35b] > s[0x35b]) {
    CoopSramSet8(s, 0x35b, p->sram[0x35b], &changed);
    *graphics_armor = true;
  }

  // Bottles (0x35C..0x35F): only 0 -> non-zero, never replace the contents.
  for (uint16 off = 0x35c; off <= 0x35f; off++) {
    if (s[off] == 0 && p->sram[off] != 0)
      CoopSramSet8(s, off, p->sram[off], &changed);
  }

  // Dungeon compasses, big keys and maps: bitwise OR.
  for (uint16 off = 0x364; off <= 0x369; off++)
    CoopMergeOr8(s, off, p->sram[off], &changed);

  // Heart pieces (0x36B) and heart capacity (0x36C) are merged together.
  {
    uint16 old_value = (uint16)((s[0x36c] & ~7) | (s[0x36b] & 3));
    uint16 new_value = (uint16)((p->sram[0x36c] & ~7) | (p->sram[0x36b] & 3));
    if (new_value > old_value) {
      CoopSramSet8(s, 0x36c, (uint8)(new_value & ~7), &changed);
      CoopSramSet8(s, 0x36b, (uint8)(new_value & 3), &changed);
    }
  }

  // Bomb and arrow capacities.
  CoopMergeMax8(s, 0x370, p->sram[0x370], &changed);
  CoopMergeMax8(s, 0x371, p->sram[0x371], &changed);

  // Pendants, ability flags and crystals: bitwise OR (16-bit ranges).
  CoopMergeOr16(s, 0x374, CoopSramGet16(p->sram, 0x374), &changed);
  CoopMergeOr16(s, 0x379, CoopSramGet16(p->sram, 0x379), &changed);
  CoopMergeOr16(s, 0x37a, CoopSramGet16(p->sram, 0x37a), &changed);

  // Magic consumption level.
  CoopMergeMax8(s, 0x37b, p->sram[0x37b], &changed);

  // 0x3C5: world state, max. Reaching >= 2 leaves the rain intro.
  if (p->sram[0x3c5] > s[0x3c5]) {
    bool leaving_rain = s[0x3c5] < 2 && p->sram[0x3c5] >= 2;
    CoopSramSet8(s, 0x3c5, p->sram[0x3c5], &changed);
    if (leaving_rain && s_local.module == 0x09 && s_local.sub_module == 0) {
      overlay_index = 0;
      sound_effect_ambient = 5;
    }
  }

  // 0x3C6: progress flags 1, OR plus follower cleanup.
  {
    uint8 remote = p->sram[0x3c6];
    if ((s[0x3c6] & 0x10) == 0)
      remote &= ~0x10;
    if ((p->sram[0x3c6] & 0x01) && s[0x3cc] == 0x05)
      CoopSramSet8(s, 0x3cc, 0, &changed);
    CoopMergeOr8(s, 0x3c6, remote, &changed);
  }

  // 0x3C7: shown map icons, max.
  CoopMergeMax8(s, 0x3c7, p->sram[0x3c7], &changed);

  // 0x3C9: progress flags 2, OR plus follower cleanup.
  {
    uint8 remote = p->sram[0x3c9];
    if ((p->sram[0x3c9] & 0x20) &&
        (s[0x3cc] == 0x07 || s[0x3cc] == 0x08))
      CoopSramSet8(s, 0x3cc, 0, &changed);
    if ((p->sram[0x3c9] & 0x10) && s[0x3cc] == 0x0c)
      CoopSramSet8(s, 0x3cc, 0, &changed);
    CoopMergeOr8(s, 0x3c9, remote, &changed);
  }

  // Dungeon rooms: OR of the 16-bit room state, with the ALttPO masks.
  for (uint16 room = 0; room < 0x128; room++) {
    uint16 off = (uint16)(room * 2);
    uint16 mask = 0xffff;
    if (room == 0x035)
      mask = 0xff7f;
    if (room == 0x10b)
      mask = 0xff7f;
    if (room == 0x028)
      mask = 0xfeff;
    uint16 remote = (uint16)(CoopSramGet16(p->sram, off) & mask);
    CoopMergeOr16(s, off, remote, &changed);
  }

  // Overworld event bytes: OR, with the swamp/watergate and pyramid cases.
  for (uint16 area = 0; area < 0x82; area++) {
    uint16 off = (uint16)(0x280 + area);
    uint8 remote = p->sram[off];
    if (area == 0x3b || area == 0x7b)
      remote &= 0xdf;
    if (area == 0x5b) {
      if ((s[off] & 0x20) == 0 && (remote & 0x20) &&
          s_local.module == 0x09 && overworld_screen_index == 0x5b)
        *pyramid_hole = true;
    }
    CoopMergeOr8(s, off, remote, &changed);
  }

  (void)changed;
}

static void CoopApplyRemoteProgress(void) {
  if (!s_active || g_config.coop_race_mode)
    return;
  if (CoopIsBadTime(s_local.module, s_local.sub_module))
    return;
  if (flag_is_link_immobilized)
    return;

  uint8 merged[kCoopWorkingSramSize];
  memcpy(merged, g_ram + 0xf000, sizeof(merged));

  bool graphics_sword = false;
  bool graphics_shield = false;
  bool graphics_armor = false;
  bool pyramid_hole = false;

  for (int i = 0; i < kCoopMaxPlayers; i++) {
    CoopPlayer *p = &s_players[i];
    if (!p->used || !p->active || p->ttl <= 0)
      continue;
    if (p->team != s_team || !p->have_sram)
      continue;
    CoopApplyPlayerProgress(merged, p, &graphics_sword, &graphics_shield,
                            &graphics_armor, &pyramid_hole);
  }

  bool changed = false;
  for (int i = 0; i < kCoopWorkingSramSize; i++) {
    if (merged[i] != g_ram[0xf000 + i]) {
      g_ram[0xf000 + i] = merged[i];
      changed = true;
    }
  }

  if (changed && !s_ctx_cur->changed_content) {
    s_ctx_cur->changed_content = true;
    g_coop_changed_content = true;
    fprintf(stderr, "coop: received progress; compare: off\n");
    fflush(stderr);
  }

  if (graphics_sword) {
    DecompressSwordGraphics();
    Palette_Load_Sword();
  }
  if (graphics_shield) {
    DecompressShieldGraphics();
    Palette_Load_Shield();
  }
  if (graphics_armor)
    Palette_Load_LinkArmorAndGloves();
  if (pyramid_hole)
    CreatePyramidHole();
}

// ---------------------------------------------------------------------------
// Ghost drawing.

static uint32 CoopSnesToRgb(uint16 c) {
  uint32 r = c & 31;
  uint32 g = (c >> 5) & 31;
  uint32 b = (c >> 10) & 31;
  r = (r << 3) | (r >> 2);
  g = (g << 3) | (g >> 2);
  b = (b << 3) | (b >> 2);
  return (r << 16) | (g << 8) | b;
}

static void CoopGetTile(const CoopPlayer *p, uint16 chr, uint16 out[16]) {
  if (chr < kCoopMaxChars && p->chr_valid[chr]) {
    memcpy(out, p->chr[chr], sizeof(uint16) * 16);
    return;
  }
  uint32 base = 0x4000u + (uint32)chr * 16u;
  for (int i = 0; i < 16; i++)
    out[i] = g_zenv.ppu->vram[(base + i) & 0x7fff];
}

static int CoopTilePixel(const uint16 tile[16], int row, int px, bool hflip) {
  uint32 plane = tile[row] | ((uint32)tile[8 + row] << 16);
  int shift = hflip ? px : 7 - px;
  uint32 bits = plane >> shift;
  return (int)((bits & 1) | ((bits >> 7) & 2) | ((bits >> 14) & 4) |
               ((bits >> 21) & 8));
}

static void CoopPutPixel(uint8 *buffer, size_t pitch, int width, int height,
                         int scale, int x, int y, uint32 rgb) {
  if ((unsigned)x >= (unsigned)width || (unsigned)y >= (unsigned)height)
    return;
  int px0 = x * scale;
  int py0 = y * scale;
  for (int dy = 0; dy < scale; dy++) {
    int py = py0 + dy;
    if ((unsigned)py >= (unsigned)(height * scale))
      continue;
    uint32 *dst = (uint32 *)(buffer + (size_t)py * pitch) + px0;
    for (int dx = 0; dx < scale; dx++) {
      int px = px0 + dx;
      if ((unsigned)px >= (unsigned)(width * scale))
        continue;
      dst[dx] = (dst[dx] & 0xff000000u) | rgb;
    }
  }
}

static void CoopDrawSprite(const CoopPlayer *p, const CoopSprite *s,
                           int rx, int ry, uint8 *buffer, size_t pitch,
                           int width, int height, int scale) {
  if (!s->enabled)
    return;

  int base_x = s->x + rx;
  int base_y = s->y + 1 + ry;
  int size = s->size ? 16 : 8;

  uint16 tiles[4][16];
  int num_tiles = s->size ? 4 : 1;
  CoopGetTile(p, s->chr, tiles[0]);
  if (s->size) {
    CoopGetTile(p, (uint16)(s->chr + 0x01), tiles[1]);
    CoopGetTile(p, (uint16)(s->chr + 0x10), tiles[2]);
    CoopGetTile(p, (uint16)(s->chr + 0x11), tiles[3]);
  }

  for (int sy = 0; sy < size; sy++) {
    int tile_row = sy >> 3;
    int row = sy & 7;
    if (s->vflip) {
      tile_row = 1 - tile_row;
      row = 7 - row;
    }
    for (int sx = 0; sx < size; sx++) {
      int tile_col = sx >> 3;
      int px = sx & 7;
      if (s->hflip)
        tile_col = 1 - tile_col;
      int ti = s->size ? (tile_row * 2 + tile_col) : 0;
      int color_index = CoopTilePixel(tiles[ti], row, px, s->hflip);
      if (color_index == 0)
        continue;

      uint16 color;
      if (s->palette < kCoopNumPalettes &&
          p->palette_valid[s->palette]) {
        color = p->palette[s->palette][color_index];
      } else {
        color = g_zenv.ppu->cgram[0x80 + s->palette * 16 + color_index];
      }
      CoopPutPixel(buffer, pitch, width, height, scale,
                   base_x + sx, base_y + sy, CoopSnesToRgb(color));
    }
  }
  (void)num_tiles;
}

static bool CoopPlayerVisible(const CoopPlayer *p) {
  if (!p->used || !p->active || p->ttl <= 0)
    return false;
  if (CoopIsBadTime(p->module, p->sub_module))
    return false;
  return CoopCanSee(p);
}

void Coop_DrawOverlay(uint8 *buffer, size_t pitch, int width, int height,
                      int scale) {
  if (!s_active || buffer == NULL || scale < 1)
    return;
  CoopUpdateLocal();
  if (CoopIsBadTime(s_local.module, s_local.sub_module))
    return;

  for (int i = 0; i < kCoopMaxPlayers; i++) {
    CoopPlayer *p = &s_players[i];
    if (!CoopPlayerVisible(p))
      continue;

    int rx = p->xoffs - s_local.xoffs;
    int ry = p->yoffs - s_local.yoffs;
    for (int j = 0; j < p->sprite_count && j < kCoopMaxSprites; j++)
      CoopDrawSprite(p, &p->sprites[j], rx, ry, buffer, pitch, width, height,
                     scale);

    if (g_config.coop_show_names && p->name_set && p->name[0] != 0) {
      int len = (int)strlen(p->name);
      int tx = p->x - s_local.xoffs + 8 - len * 3;
      int ty = p->y - s_local.yoffs + 25;
      uint32 rgb = CoopSnesToRgb(p->color);
      ModDraw_AddText(tx, ty, p->name, (size_t)len, 0xff000000u | rgb);
    }
  }
}

// ---------------------------------------------------------------------------
// Lifecycle.

static void CoopInitContext(CoopContext *ctx, int index, const char *name,
                            uint16 color, bool network) {
  memset(ctx, 0, sizeof(*ctx));
  memset(ctx->group, ' ', sizeof(ctx->group));
  memset(ctx->name, ' ', sizeof(ctx->name));
  ctx->index = network ? 0xffff : (uint16)index;
  ctx->color = color;

  size_t group_len = strlen(g_config.coop_group);
  if (group_len > kCoopGroupLen)
    group_len = kCoopGroupLen;
  memcpy(ctx->group, g_config.coop_group, group_len);

  size_t name_len = strlen(name);
  if (name_len > kCoopNameLen)
    name_len = kCoopNameLen;
  memcpy(ctx->name, name, name_len);
}

void Coop_Start(void) {
  if (Coop_IsActive())
    return;

  if (!g_config.coop_local && !g_config.coop_enabled)
    return;

  s_local_mode = g_config.coop_local;
  if (s_local_mode) {
    CoopInitContext(&s_ctx[0], 0, g_config.coop_player_name,
                    g_config.coop_player_color, false);
    CoopInitContext(&s_ctx[1], 1, g_config.coop_player_name2,
                    g_config.coop_player_color2, false);
    s_ctx[0].started = s_ctx[0].active = true;
    s_ctx[1].started = s_ctx[1].active = true;
    for (int i = 0; i < 2; i++) {
      s_ctx_cur = &s_ctx[i];
      CoopUpdateLocal();
      s_local.location = s_local.actual_location;
      s_local.last_location = s_local.location;
    }
    s_ctx_cur = &s_ctx[0];
    g_coop_changed_content = false;
    fprintf(stderr, "coop: local split-screen as '%.20s' and '%.20s'\n",
            g_config.coop_player_name, g_config.coop_player_name2);
    return;
  }

  CoopInitContext(&s_ctx[0], 0, g_config.coop_player_name,
                  g_config.coop_player_color, true);
  s_ctx_cur = &s_ctx[0];
  if (!CoopOpenSocket())
    return;

  s_active = true;
  CoopUpdateLocal();
  s_local.location = s_local.actual_location;
  s_local.last_location = s_local.location;
}

void Coop_Stop(void) {
  if (s_local_mode) {
    memset(s_ctx, 0, sizeof(s_ctx));
    s_ctx_cur = &s_ctx[0];
    s_local_mode = false;
    g_coop_changed_content = false;
    return;
  }
  if (!s_started && !s_active)
    return;
  CoopCloseSocket();
  memset(s_ctx, 0, sizeof(s_ctx));
  s_ctx_cur = &s_ctx[0];
  g_coop_changed_content = false;
}

bool Coop_IsActive(void) {
  return s_ctx[0].active || s_ctx[1].active;
}

bool Coop_IsLocal(void) {
  return s_local_mode;
}

void Coop_SelectInstance(int index) {
  if (!s_local_mode) {
    s_ctx_cur = &s_ctx[0];
    return;
  }
  if (index < 0 || index > 1)
    index = 0;
  s_ctx_cur = &s_ctx[index];
  g_coop_changed_content = s_ctx_cur->changed_content;
}

void Coop_FrameStep(void) {
  if (!s_active || !s_started)
    return;

  s_frame++;
  CoopUpdateLocal();

  CoopSendIndexRequest();
  CoopDrainSocket();
  CoopSendMainPacket();
  CoopSendSprites();
  if (!g_config.coop_race_mode)
    CoopSendProgress();
  CoopApplyRemoteProgress();

  for (int i = 0; i < kCoopMaxPlayers; i++) {
    CoopPlayer *p = &s_players[i];
    if (!p->used || !p->active)
      continue;
    if (p->ttl > 0) {
      p->ttl--;
      if (p->ttl == 0) {
        p->active = false;
        fprintf(stderr, "coop: player %u '%s' left\n", (unsigned)p->index,
                p->name_set ? p->name : "");
        fflush(stderr);
      }
    }
  }
}
