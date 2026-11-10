#include "nwa_server.h"

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
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "../../snes/cart.h"
#include "../../snes/ppu.h"
#include "../../snes/snes.h"
#include "../util.h"
#include "../variables.h"
#include "../zelda_cpu_infra.h"
#include "../zelda_rtl.h"

// Set by bCORE_WRITE when it changes guest memory. Once this happens the
// side-by-side comparison against the reference ROM is no longer meaningful.
bool g_nwa_changed_content;

#define NWA_EMULATOR_NAME "zelda3"
#define NWA_EMULATOR_VERSION "1.0"
#define NWA_PROTOCOL_VERSION "1.0"

enum {
  kNwaDefaultPort = 48879,
  kNwaPortTries = 10,
  kNwaMaxConnections = 8,
  kNwaMaxLine = 16384,
  kNwaMaxRegions = 512,
  kNwaMaxBinary = 8 * 1024 * 1024,
  kNwaMaxInput = kNwaMaxBinary + kNwaMaxLine + 5,
  kNwaMaxOutput = 16 * 1024 * 1024,
};

#ifdef _WIN32
typedef SOCKET NwaSocket;
#define kNwaInvalidSocket INVALID_SOCKET
#else
typedef int NwaSocket;
#define kNwaInvalidSocket (-1)
#endif

typedef struct NwaBuffer {
  uint8_t *data;
  size_t len, cap;
} NwaBuffer;

typedef struct NwaConn {
  NwaSocket fd;
  NwaBuffer in, out;
  char name[64];
  char pending_line[kNwaMaxLine + 1];
  size_t pending_len;
  bool expecting_binary;
  bool close_after_flush;
  bool failed;
} NwaConn;

typedef struct NwaMemory {
  const char *name;
  uint8_t *data;
  uint32_t size;
  bool writable;
} NwaMemory;

typedef struct NwaRegion {
  uint32_t offset, size;
} NwaRegion;

static bool s_running;
static bool s_paused;
static bool s_allow_write;
static uint16_t s_port;
static NwaSocket s_listen_fd = kNwaInvalidSocket;
static char *s_rom_path;
static Snes *s_emu_snes;
static NwaConn s_connections[kNwaMaxConnections];
static bool s_connections_initialized;
#ifdef _WIN32
static bool s_wsa_started;
#endif

static void NwaEnsureConnectionsInitialized(void) {
  if (s_connections_initialized)
    return;
  for (int i = 0; i < kNwaMaxConnections; i++)
    s_connections[i].fd = kNwaInvalidSocket;
  s_connections_initialized = true;
}

static bool NwaBufferReserve(NwaBuffer *b, size_t extra) {
  if (extra > SIZE_MAX - b->len)
    return false;
  size_t need = b->len + extra;
  if (need <= b->cap)
    return true;
  size_t cap = b->cap ? b->cap : 256;
  while (cap < need) {
    if (cap > SIZE_MAX / 2) {
      cap = need;
      break;
    }
    cap *= 2;
  }
  uint8_t *data = (uint8_t *)realloc(b->data, cap);
  if (!data)
    return false;
  b->data = data;
  b->cap = cap;
  return true;
}

static void NwaBufferFree(NwaBuffer *b) {
  free(b->data);
  b->data = NULL;
  b->len = b->cap = 0;
}

static bool NwaConnAppend(NwaConn *c, const void *data, size_t len) {
  if (c->failed)
    return false;
  if (len == 0)
    return true;
  if (c->out.len > kNwaMaxOutput || len > kNwaMaxOutput - c->out.len) {
    c->failed = true;
    return false;
  }
  if (!NwaBufferReserve(&c->out, len)) {
    c->failed = true;
    return false;
  }
  memcpy(c->out.data + c->out.len, data, len);
  c->out.len += len;
  return true;
}

static void NwaConnAppendStr(NwaConn *c, const char *s) {
  NwaConnAppend(c, s, strlen(s));
}

static void NwaConnAppendChar(NwaConn *c, char ch) {
  NwaConnAppend(c, &ch, 1);
}

static void NwaReplyBegin(NwaConn *c) {
  NwaConnAppendChar(c, '\n');
}

static void NwaReplyEnd(NwaConn *c) {
  NwaConnAppendChar(c, '\n');
}

static void NwaReplyKey(NwaConn *c, const char *key, const char *value) {
  NwaConnAppendStr(c, key);
  NwaConnAppendChar(c, ':');
  NwaConnAppendStr(c, value ? value : "");
  NwaConnAppendChar(c, '\n');
}

static void NwaReplyKeyUint(NwaConn *c, const char *key, uint32_t value) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%u", (unsigned)value);
  NwaReplyKey(c, key, buf);
}

static void NwaReplyError(NwaConn *c, const char *type, const char *reason) {
  NwaReplyBegin(c);
  NwaReplyKey(c, "error", type);
  NwaReplyKey(c, "reason", reason);
  NwaReplyEnd(c);
}

static void NwaProtocolError(NwaConn *c, const char *reason) {
  NwaReplyError(c, "protocol_error", reason);
  c->close_after_flush = true;
}

static char *NwaTrim(char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  size_t n = strlen(s);
  while (n > 0) {
    char ch = s[n - 1];
    if (ch != ' ' && ch != '\t' && ch != '\r')
      break;
    s[--n] = 0;
  }
  return s;
}

static bool NwaParseNumber(const char *s, uint64_t *value) {
  while (*s == ' ' || *s == '\t')
    s++;
  if (*s == 0)
    return false;
  int base = 10;
  if (*s == '$') {
    base = 16;
    s++;
  }
  if (*s == 0)
    return false;
  char *end;
  errno = 0;
  unsigned long long v = strtoull(s, &end, base);
  if (errno != 0 || end == s)
    return false;
  while (*end == ' ' || *end == '\t')
    end++;
  if (*end != 0)
    return false;
  *value = (uint64_t)v;
  return true;
}

static size_t NwaSplit(char *s, char delim, char **out, size_t max) {
  size_t n = 0;
  char *p = s;
  for (;;) {
    if (n >= max)
      return (size_t)-1;
    out[n++] = p;
    char *q = strchr(p, delim);
    if (!q)
      break;
    *q = 0;
    p = q + 1;
  }
  return n;
}

static const char *NwaBaseName(const char *path) {
  const char *base = path;
  if (!path)
    return "";
  for (const char *p = path; *p; p++) {
    if (*p == '/' || *p == '\\')
      base = p + 1;
  }
  return base;
}

static Snes *NwaActiveSnes(void) {
  return s_emu_snes != NULL ? s_emu_snes : g_snes;
}

static Cart *NwaActiveCart(void) {
  Snes *snes = NwaActiveSnes();
  return snes != NULL ? snes->cart : NULL;
}

static bool NwaHasCartRom(void) {
  Cart *cart = NwaActiveCart();
  return cart != NULL && cart->rom != NULL && cart->romSize != 0;
}

static bool NwaLookupMemory(const char *name, NwaMemory *mem) {
  mem->name = name;
  mem->data = NULL;
  mem->size = 0;
  mem->writable = false;

  if (s_emu_snes != NULL) {
    Snes *snes = s_emu_snes;
    if (StringEqualsNoCase(name, "WRAM")) {
      mem->data = snes->ram;
      mem->size = 0x20000;
      mem->writable = true;
      return true;
    }
    if (StringEqualsNoCase(name, "SRAM")) {
      if (snes->cart != NULL && snes->cart->ram != NULL) {
        mem->data = snes->cart->ram;
        mem->size = snes->cart->ramSize;
        mem->writable = true;
      }
      return true;
    }
    if (StringEqualsNoCase(name, "CARTROM")) {
      if (snes->cart != NULL && snes->cart->rom != NULL) {
        mem->data = snes->cart->rom;
        mem->size = snes->cart->romSize;
      }
      return true;
    }
    if (StringEqualsNoCase(name, "VRAM")) {
      mem->data = (uint8_t *)snes->ppu->vram;
      mem->size = 0x8000 * 2;
      return true;
    }
    if (StringEqualsNoCase(name, "OAM")) {
      mem->data = (uint8_t *)snes->ppu->oam;
      mem->size = 0x110 * 2;
      return true;
    }
    if (StringEqualsNoCase(name, "CGRAM")) {
      mem->data = (uint8_t *)snes->ppu->cgram;
      mem->size = 0x100 * 2;
      return true;
    }
    return false;
  }

  if (StringEqualsNoCase(name, "WRAM")) {
    mem->data = g_ram;
    mem->size = 0x20000;
    mem->writable = true;
    return true;
  }
  if (StringEqualsNoCase(name, "SRAM")) {
    if (g_zenv.sram != NULL) {
      mem->data = g_zenv.sram;
      mem->size = 0x2000;
      mem->writable = true;
    }
    return true;
  }
  if (StringEqualsNoCase(name, "CARTROM")) {
    if (NwaHasCartRom()) {
      mem->data = g_snes->cart->rom;
      mem->size = g_snes->cart->romSize;
    }
    return true;
  }
  if (StringEqualsNoCase(name, "VRAM")) {
    if (g_zenv.ppu != NULL && g_zenv.vram != NULL) {
      mem->data = (uint8_t *)g_zenv.vram;
      mem->size = 0x8000 * 2;
    }
    return true;
  }
  if (StringEqualsNoCase(name, "OAM")) {
    if (g_zenv.ppu != NULL) {
      mem->data = (uint8_t *)g_zenv.ppu->oam;
      mem->size = 0x110 * 2;
    }
    return true;
  }
  if (StringEqualsNoCase(name, "CGRAM")) {
    if (g_zenv.ppu != NULL) {
      mem->data = (uint8_t *)g_zenv.ppu->cgram;
      mem->size = 0x100 * 2;
    }
    return true;
  }
  return false;
}

static const char *NwaMemoryAccess(const NwaMemory *mem) {
  if (mem->writable && s_allow_write)
    return "rw";
  return "r";
}

static bool NwaAddReadRegion(NwaRegion *regions, size_t *count, uint64_t *total,
                             uint64_t offset, uint64_t size, uint32_t mem_size,
                             bool last) {
  if (*count >= kNwaMaxRegions)
    return false;
  if (offset > mem_size) {
    if (!last)
      return false;
    offset = mem_size;
  }
  if (size > (uint64_t)mem_size - offset) {
    if (!last)
      return false;
    size = (uint64_t)mem_size - offset;
  }
  if (size > UINT32_MAX)
    return false;
  regions[*count].offset = (uint32_t)offset;
  regions[*count].size = (uint32_t)size;
  (*count)++;
  *total += size;
  return true;
}

static bool NwaParseReadRegions(char *args, uint32_t mem_size,
                                NwaRegion *regions, size_t *count,
                                uint64_t *total) {
  *count = 0;
  *total = 0;
  if (args == NULL || *args == 0)
    return NwaAddReadRegion(regions, count, total, 0, mem_size, mem_size, true);

  char *tokens[kNwaMaxRegions * 2];
  size_t n = NwaSplit(args, ';', tokens, kNwaMaxRegions * 2);
  if (n == (size_t)-1)
    return false;
  if (n == 1 && tokens[0][0] == 0)
    return NwaAddReadRegion(regions, count, total, 0, mem_size, mem_size, true);
  if (n == 1) {
    uint64_t offset;
    if (!NwaParseNumber(tokens[0], &offset))
      return false;
    return NwaAddReadRegion(regions, count, total, offset,
                            offset <= mem_size ? mem_size - offset : 0,
                            mem_size, true);
  }
  if ((n & 1) != 0)
    return false;

  for (size_t i = 0; i < n; i += 2) {
    uint64_t offset;
    uint64_t size;
    if (!NwaParseNumber(tokens[i], &offset))
      return false;
    bool last = i + 2 == n;
    if (tokens[i + 1][0] == 0) {
      if (i != 0)
        return false;
      size = offset <= mem_size ? mem_size - offset : 0;
    } else if (!NwaParseNumber(tokens[i + 1], &size)) {
      return false;
    }
    if (!NwaAddReadRegion(regions, count, total, offset, size, mem_size, last))
      return false;
  }
  return true;
}

static bool NwaParseWriteRegions(char *args, uint32_t mem_size,
                                 uint32_t data_size, NwaRegion *regions,
                                 size_t *count, uint64_t *total) {
  *count = 0;
  *total = 0;
  if (args == NULL || *args == 0) {
    if (data_size > mem_size)
      return false;
    regions[0].offset = 0;
    regions[0].size = data_size;
    *count = 1;
    *total = data_size;
    return true;
  }

  char *tokens[kNwaMaxRegions * 2];
  size_t n = NwaSplit(args, ';', tokens, kNwaMaxRegions * 2);
  if (n == (size_t)-1)
    return false;
  if (n == 1 && tokens[0][0] == 0) {
    if (data_size > mem_size)
      return false;
    regions[0].offset = 0;
    regions[0].size = data_size;
    *count = 1;
    *total = data_size;
    return true;
  }
  if (n == 1) {
    uint64_t offset;
    if (!NwaParseNumber(tokens[0], &offset))
      return false;
    if (offset > mem_size || data_size > mem_size - offset)
      return false;
    regions[0].offset = (uint32_t)offset;
    regions[0].size = data_size;
    *count = 1;
    *total = data_size;
    return true;
  }
  if (n == 2 && tokens[1][0] == 0) {
    uint64_t offset;
    if (!NwaParseNumber(tokens[0], &offset))
      return false;
    if (offset > mem_size || data_size > mem_size - offset)
      return false;
    regions[0].offset = (uint32_t)offset;
    regions[0].size = data_size;
    *count = 1;
    *total = data_size;
    return true;
  }
  if ((n & 1) != 0)
    return false;

  for (size_t i = 0; i < n; i += 2) {
    uint64_t offset, size;
    if (!NwaParseNumber(tokens[i], &offset))
      return false;
    if (tokens[i + 1][0] == 0 || !NwaParseNumber(tokens[i + 1], &size))
      return false;
    if (offset > mem_size || size > mem_size - offset)
      return false;
    if (*count >= kNwaMaxRegions)
      return false;
    regions[*count].offset = (uint32_t)offset;
    regions[*count].size = (uint32_t)size;
    (*count)++;
    *total += size;
  }
  return *total == data_size;
}

static void NwaCmdEmulatorInfo(NwaConn *c) {
  char id[32];
  snprintf(id, sizeof(id), "zelda3-%u", (unsigned)s_port);
  NwaReplyBegin(c);
  NwaReplyKey(c, "name", NWA_EMULATOR_NAME);
  NwaReplyKey(c, "version", NWA_EMULATOR_VERSION);
  NwaReplyKey(c, "nwa_version", NWA_PROTOCOL_VERSION);
  NwaReplyKey(c, "id", id);
  NwaReplyKey(c, "commands",
              "EMULATOR_INFO,EMULATION_STATUS,EMULATION_PAUSE,"
              "EMULATION_RESUME,EMULATION_RESET,GAME_INFO,CORES_LIST,"
              "CORE_INFO,CORE_CURRENT_INFO,CORE_MEMORIES,MY_NAME_IS,"
              "CORE_READ,CORE_WRITE");
  NwaReplyEnd(c);
}

static void NwaCmdEmulationStatus(NwaConn *c) {
  NwaReplyBegin(c);
  if (!NwaHasCartRom()) {
    NwaReplyKey(c, "state", "no_game");
  } else {
    NwaReplyKey(c, "state", s_paused ? "paused" : "running");
    NwaReplyKey(c, "game",
                s_rom_path ? NwaBaseName(s_rom_path) : NWA_EMULATOR_NAME);
  }
  NwaReplyEnd(c);
}

static void NwaCmdGameInfo(NwaConn *c) {
  if (!NwaHasCartRom()) {
    NwaReplyError(c, "command_error", "no game loaded");
    return;
  }
  const char *file = s_rom_path ? s_rom_path : "";
  const char *name = s_rom_path ? NwaBaseName(s_rom_path) : NWA_EMULATOR_NAME;
  Cart *cart = NwaActiveCart();
  NwaReplyBegin(c);
  NwaReplyKey(c, "name", name);
  NwaReplyKey(c, "file", file);
  NwaReplyKey(c, "region", "ntsc");
  NwaReplyKey(c, "type", cart != NULL && cart->type == 2 ? "hirom" : "lorom");
  NwaReplyEnd(c);
}

static void NwaCmdCoresList(NwaConn *c) {
  NwaReplyBegin(c);
  NwaReplyKey(c, "name", NWA_EMULATOR_NAME);
  NwaReplyKey(c, "platform", "snes");
  NwaReplyEnd(c);
}

static void NwaCmdCoreInfo(NwaConn *c) {
  NwaReplyBegin(c);
  NwaReplyKey(c, "platform", "snes");
  NwaReplyKey(c, "name", NWA_EMULATOR_NAME);
  NwaReplyKey(c, "version", NWA_EMULATOR_VERSION);
  NwaReplyKey(c, "file", NWA_EMULATOR_NAME);
  NwaReplyEnd(c);
}

static void NwaCmdCoreCurrentInfo(NwaConn *c) {
  NwaReplyBegin(c);
  NwaReplyKey(c, "platform", "snes");
  NwaReplyKey(c, "name", NWA_EMULATOR_NAME);
  NwaReplyKey(c, "version", NWA_EMULATOR_VERSION);
  NwaReplyKey(c, "file", NWA_EMULATOR_NAME);
  NwaReplyEnd(c);
}

static void NwaCmdCoreMemories(NwaConn *c) {
  static const char *const names[] = {
    "WRAM", "SRAM", "CARTROM", "VRAM", "OAM", "CGRAM",
  };
  NwaReplyBegin(c);
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    NwaMemory mem;
    if (!NwaLookupMemory(names[i], &mem))
      continue;
    NwaReplyKey(c, "name", names[i]);
    NwaReplyKey(c, "access", NwaMemoryAccess(&mem));
    NwaReplyKeyUint(c, "size", mem.size);
  }
  NwaReplyEnd(c);
}

static void NwaCmdMyNameIs(NwaConn *c, char *args) {
  if (args == NULL || *args == 0) {
    NwaReplyError(c, "invalid_argument", "MY_NAME_IS requires a name");
    return;
  }
  snprintf(c->name, sizeof(c->name), "%s", args);
  NwaReplyBegin(c);
  NwaReplyKey(c, "name", args);
  NwaReplyEnd(c);
}

static void NwaCmdCoreRead(NwaConn *c, char *args) {
  if (args == NULL || *args == 0) {
    NwaReplyError(c, "invalid_argument", "CORE_READ requires a memory name");
    return;
  }

  char *regions_arg = NULL;
  char *semi = strchr(args, ';');
  if (semi != NULL) {
    *semi = 0;
    regions_arg = semi + 1;
  }
  char *mem_name = NwaTrim(args);

  NwaMemory mem;
  if (!NwaLookupMemory(mem_name, &mem)) {
    NwaReplyError(c, "invalid_argument", "unknown memory name");
    return;
  }

  NwaRegion regions[kNwaMaxRegions];
  size_t count;
  uint64_t total;
  if (!NwaParseReadRegions(regions_arg, mem.size, regions, &count, &total)) {
    NwaReplyError(c, "invalid_argument", "invalid read ranges");
    return;
  }
  if (total > UINT32_MAX) {
    NwaReplyError(c, "command_error", "read is too large");
    return;
  }

  if (c->out.len > kNwaMaxOutput - (size_t)(5 + total)) {
    c->failed = true;
    return;
  }
  if (!NwaBufferReserve(&c->out, 5 + (size_t)total)) {
    c->failed = true;
    return;
  }

  uint8_t *dst = c->out.data + c->out.len;
  dst[0] = 0;
  dst[1] = (uint8_t)(total >> 24);
  dst[2] = (uint8_t)(total >> 16);
  dst[3] = (uint8_t)(total >> 8);
  dst[4] = (uint8_t)total;
  c->out.len += 5;
  for (size_t i = 0; i < count; i++) {
    if (regions[i].size != 0) {
      memcpy(c->out.data + c->out.len, mem.data + regions[i].offset,
             regions[i].size);
      c->out.len += regions[i].size;
    }
  }
}

static void NwaCmdCoreWrite(NwaConn *c, char *args, const uint8_t *data,
                            size_t data_size) {
  if (!s_allow_write) {
    NwaReplyError(c, "not_allowed",
                  "writes are disabled (EmuNwaAllowWrite = 0)");
    return;
  }
  if (args == NULL || *args == 0) {
    NwaReplyError(c, "invalid_argument", "CORE_WRITE requires a memory name");
    return;
  }
  if (data_size > UINT32_MAX) {
    NwaReplyError(c, "invalid_argument", "binary block is too large");
    return;
  }

  char *regions_arg = NULL;
  char *semi = strchr(args, ';');
  if (semi != NULL) {
    *semi = 0;
    regions_arg = semi + 1;
  }
  char *mem_name = NwaTrim(args);

  NwaMemory mem;
  if (!NwaLookupMemory(mem_name, &mem)) {
    NwaReplyError(c, "invalid_argument", "unknown memory name");
    return;
  }
  if (!mem.writable) {
    NwaReplyError(c, "not_allowed", "memory is read-only");
    return;
  }

  NwaRegion regions[kNwaMaxRegions];
  size_t count;
  uint64_t total;
  if (!NwaParseWriteRegions(regions_arg, mem.size, (uint32_t)data_size,
                            regions, &count, &total)) {
    NwaReplyError(c, "invalid_argument", "invalid write ranges");
    return;
  }
  if (total != data_size) {
    NwaReplyError(c, "invalid_argument", "size does not match binary data");
    return;
  }

  size_t pos = 0;
  for (size_t i = 0; i < count; i++) {
    if (regions[i].size != 0) {
      memcpy(mem.data + regions[i].offset, data + pos, regions[i].size);
      pos += regions[i].size;
    }
  }
  if (total != 0 && !g_nwa_changed_content) {
    fprintf(stderr, "nwa: write to %s; compare: off\n", mem.name);
    g_nwa_changed_content = true;
  }
  NwaReplyBegin(c);
  NwaReplyEnd(c);
}

static void NwaHandleCommand(NwaConn *c, char *line, const uint8_t *data,
                             size_t data_size, bool has_binary) {
  bool binary_prefix = line[0] == 'b';
  char *cmd = line + (binary_prefix ? 1 : 0);
  char *args = cmd;
  while (*args != 0 && *args != ' ')
    args++;
  if (*args != 0)
    *args++ = 0;
  args = NwaTrim(args);
  cmd = NwaTrim(cmd);

  if (*cmd == 0) {
    NwaReplyError(c, "invalid_command", "empty command");
    return;
  }

  if (binary_prefix && !StringEqualsNoCase(cmd, "CORE_WRITE")) {
    NwaReplyError(c, "invalid_command",
                  "binary prefix is only supported for CORE_WRITE");
    return;
  }

  if (StringEqualsNoCase(cmd, "EMULATOR_INFO")) {
    NwaCmdEmulatorInfo(c);
  } else if (StringEqualsNoCase(cmd, "EMULATION_STATUS")) {
    NwaCmdEmulationStatus(c);
  } else if (StringEqualsNoCase(cmd, "EMULATION_PAUSE")) {
    s_paused = true;
    NwaReplyBegin(c);
    NwaReplyEnd(c);
  } else if (StringEqualsNoCase(cmd, "EMULATION_RESUME")) {
    s_paused = false;
    NwaReplyBegin(c);
    NwaReplyEnd(c);
  } else if (StringEqualsNoCase(cmd, "EMULATION_RESET")) {
    if (!NwaHasCartRom()) {
      NwaReplyError(c, "command_error", "no game loaded");
      return;
    }
    if (s_emu_snes != NULL) {
      snes_softResetPreserveSram(s_emu_snes);
    } else {
      ZeldaReset(true);
    }
    NwaReplyBegin(c);
    NwaReplyEnd(c);
  } else if (StringEqualsNoCase(cmd, "GAME_INFO")) {
    NwaCmdGameInfo(c);
  } else if (StringEqualsNoCase(cmd, "CORES_LIST")) {
    NwaCmdCoresList(c);
  } else if (StringEqualsNoCase(cmd, "CORE_INFO")) {
    NwaCmdCoreInfo(c);
  } else if (StringEqualsNoCase(cmd, "CORE_CURRENT_INFO")) {
    NwaCmdCoreCurrentInfo(c);
  } else if (StringEqualsNoCase(cmd, "CORE_MEMORIES")) {
    NwaCmdCoreMemories(c);
  } else if (StringEqualsNoCase(cmd, "MY_NAME_IS")) {
    NwaCmdMyNameIs(c, args);
  } else if (StringEqualsNoCase(cmd, "CORE_READ")) {
    NwaCmdCoreRead(c, args);
  } else if (StringEqualsNoCase(cmd, "CORE_WRITE")) {
    if (!binary_prefix || !has_binary) {
      NwaReplyError(c, "invalid_command",
                    "CORE_WRITE requires the b prefix and binary data");
      return;
    }
    NwaCmdCoreWrite(c, args, data, data_size);
  } else {
    NwaReplyError(c, "invalid_command", "unknown command");
  }
}

static void NwaConnConsume(NwaConn *c, size_t n) {
  if (n >= c->in.len) {
    c->in.len = 0;
    return;
  }
  memmove(c->in.data, c->in.data + n, c->in.len - n);
  c->in.len -= n;
}

static void NwaConnProcess(NwaConn *c) {
  char line[kNwaMaxLine + 1];
  for (;;) {
    if (c->failed || c->close_after_flush)
      return;

    if (c->expecting_binary) {
      if (c->in.len < 5)
        return;
      if (c->in.data[0] != 0) {
        NwaProtocolError(c, "binary block must start with a zero byte");
        return;
      }
      uint32_t size = (uint32_t)c->in.data[1] << 24 |
                      (uint32_t)c->in.data[2] << 16 |
                      (uint32_t)c->in.data[3] << 8 |
                      (uint32_t)c->in.data[4];
      if (size > kNwaMaxBinary) {
        NwaProtocolError(c, "binary block is too large");
        return;
      }
      if (c->in.len < (size_t)size + 5)
        return;
      NwaHandleCommand(c, c->pending_line, c->in.data + 5, size, true);
      c->expecting_binary = false;
      c->pending_len = 0;
      NwaConnConsume(c, (size_t)size + 5);
      continue;
    }

    if (c->in.len == 0)
      return;
    uint8_t *newline = (uint8_t *)memchr(c->in.data, '\n', c->in.len);
    if (newline == NULL) {
      if (c->in.len > kNwaMaxLine)
        NwaProtocolError(c, "command line is too long");
      return;
    }
    size_t line_len = (size_t)(newline - c->in.data);
    if (line_len > kNwaMaxLine) {
      NwaProtocolError(c, "command line is too long");
      return;
    }
    memcpy(line, c->in.data, line_len);
    line[line_len] = 0;
    NwaConnConsume(c, line_len + 1);

    while (line_len > 0 && (line[line_len - 1] == '\r' ||
                            line[line_len - 1] == ' ' ||
                            line[line_len - 1] == '\t'))
      line[--line_len] = 0;
    char *trimmed = NwaTrim(line);
    if (*trimmed == 0)
      continue;

    if (trimmed[0] == 'b') {
      size_t pending_len = strlen(trimmed);
      if (pending_len > kNwaMaxLine) {
        NwaProtocolError(c, "command line is too long");
        return;
      }
      memcpy(c->pending_line, trimmed, pending_len + 1);
      c->pending_len = pending_len;
      c->expecting_binary = true;
      continue;
    }

    NwaHandleCommand(c, trimmed, NULL, 0, false);
  }
}

static bool NwaWouldBlock(void) {
#ifdef _WIN32
  return WSAGetLastError() == WSAEWOULDBLOCK;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static bool NwaInterrupted(void) {
#ifdef _WIN32
  return WSAGetLastError() == WSAEINTR;
#else
  return errno == EINTR;
#endif
}

static int NwaSend(NwaSocket fd, const uint8_t *data, size_t len) {
  int n = len > 0x7fffffff ? 0x7fffffff : (int)len;
#ifdef _WIN32
  return send(fd, (const char *)data, n, 0);
#else
#ifdef MSG_NOSIGNAL
  return (int)send(fd, data, (size_t)n, MSG_NOSIGNAL);
#else
  return (int)send(fd, data, (size_t)n, 0);
#endif
#endif
}

static int NwaRecv(NwaSocket fd, uint8_t *data, size_t len) {
  int n = len > 0x7fffffff ? 0x7fffffff : (int)len;
#ifdef _WIN32
  return recv(fd, (char *)data, n, 0);
#else
  return (int)recv(fd, data, (size_t)n, 0);
#endif
}

static void NwaCloseSocket(NwaSocket fd) {
  if (fd == kNwaInvalidSocket)
    return;
#ifdef _WIN32
  closesocket(fd);
#else
  close(fd);
#endif
}

static bool NwaSetNonBlocking(NwaSocket fd) {
#ifdef _WIN32
  u_long mode = 1;
  return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
    return false;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static void NwaLogSocketError(const char *what) {
#ifdef _WIN32
  fprintf(stderr, "nwa: %s failed (Winsock error %d)\n", what,
          (int)WSAGetLastError());
#else
  fprintf(stderr, "nwa: %s failed: %s\n", what, strerror(errno));
#endif
}

static void NwaConnReset(NwaConn *c) {
  NwaCloseSocket(c->fd);
  c->fd = kNwaInvalidSocket;
  NwaBufferFree(&c->in);
  NwaBufferFree(&c->out);
  c->name[0] = 0;
  c->pending_line[0] = 0;
  c->pending_len = 0;
  c->expecting_binary = false;
  c->close_after_flush = false;
  c->failed = false;
}

static void NwaConnFlush(NwaConn *c) {
  while (!c->failed && c->out.len != 0) {
    int n = NwaSend(c->fd, c->out.data, c->out.len);
    if (n > 0) {
      if ((size_t)n >= c->out.len) {
        c->out.len = 0;
      } else {
        memmove(c->out.data, c->out.data + n, c->out.len - (size_t)n);
        c->out.len -= (size_t)n;
      }
      continue;
    }
    if (n == 0) {
      c->failed = true;
      break;
    }
    if (NwaWouldBlock())
      break;
    if (NwaInterrupted())
      continue;
    c->failed = true;
    break;
  }
}

static void NwaConnRead(NwaConn *c) {
  uint8_t buf[4096];
  for (;;) {
    if (c->failed || c->close_after_flush)
      return;
    int n = NwaRecv(c->fd, buf, sizeof(buf));
    if (n > 0) {
      if (c->in.len > kNwaMaxInput || (size_t)n > kNwaMaxInput - c->in.len) {
        NwaProtocolError(c, "input buffer is too large");
        return;
      }
      if (!NwaBufferReserve(&c->in, (size_t)n)) {
        c->failed = true;
        return;
      }
      memcpy(c->in.data + c->in.len, buf, (size_t)n);
      c->in.len += (size_t)n;
    } else if (n == 0) {
      c->failed = true;
      return;
    } else if (NwaWouldBlock()) {
      return;
    } else if (NwaInterrupted()) {
      continue;
    } else {
      c->failed = true;
      return;
    }
  }
}

static void NwaPollConnection(NwaConn *c) {
  NwaConnFlush(c);
  NwaConnRead(c);
  NwaConnProcess(c);
  NwaConnFlush(c);
  if (c->failed || (c->close_after_flush && c->out.len == 0))
    NwaConnReset(c);
}

static void NwaAcceptConnections(void) {
  for (;;) {
    struct sockaddr_in addr;
#ifdef _WIN32
    int addr_len = sizeof(addr);
#else
    socklen_t addr_len = sizeof(addr);
#endif
    NwaSocket fd = accept(s_listen_fd, (struct sockaddr *)&addr, &addr_len);
    if (fd == kNwaInvalidSocket) {
      if (!NwaWouldBlock())
        NwaLogSocketError("accept");
      return;
    }
    if (!NwaSetNonBlocking(fd)) {
      NwaCloseSocket(fd);
      continue;
    }

    int slot = -1;
    for (int i = 0; i < kNwaMaxConnections; i++) {
      if (s_connections[i].fd == kNwaInvalidSocket) {
        slot = i;
        break;
      }
    }
    if (slot < 0) {
      NwaCloseSocket(fd);
      continue;
    }
    NwaConn *c = &s_connections[slot];
    memset(c, 0, sizeof(*c));
    c->fd = fd;
    c->out.cap = 0;
  }
}

void NwaServer_SetEmulatorSnes(Snes *snes) {
  s_emu_snes = snes;
}

void NwaServer_Start(bool enabled, uint16_t port, bool allow_write,
                     const char *rom_path) {
  NwaEnsureConnectionsInitialized();
  NwaServer_Stop();
  if (!enabled)
    return;

#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    fprintf(stderr, "nwa: WSAStartup failed (Winsock error %d)\n",
            (int)WSAGetLastError());
    return;
  }
  s_wsa_started = true;
#endif

  if (rom_path != NULL && *rom_path != 0) {
    size_t len = strlen(rom_path) + 1;
    s_rom_path = (char *)malloc(len);
    if (s_rom_path != NULL)
      memcpy(s_rom_path, rom_path, len);
  }

  NwaSocket fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == kNwaInvalidSocket) {
    NwaLogSocketError("socket");
    NwaServer_Stop();
    return;
  }

  int yes = 1;
#ifndef _WIN32
  // On Windows SO_REUSEADDR would let a second instance steal the port, so
  // leave it off there and rely on the port+1 fallback.
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#else
  (void)yes;
#endif

  int first = port ? port : kNwaDefaultPort;
  int last = first + kNwaPortTries - 1;
  if (last > 65535)
    last = 65535;

  bool bound = false;
  for (int p = first; p <= last; p++) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)p);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
      s_port = (uint16_t)p;
      bound = true;
      break;
    }
  }
  if (!bound) {
    NwaLogSocketError("bind 127.0.0.1");
    NwaCloseSocket(fd);
    NwaServer_Stop();
    return;
  }

  if (listen(fd, 4) != 0) {
    NwaLogSocketError("listen");
    NwaCloseSocket(fd);
    NwaServer_Stop();
    return;
  }
  if (!NwaSetNonBlocking(fd)) {
    NwaLogSocketError("set non-blocking");
    NwaCloseSocket(fd);
    NwaServer_Stop();
    return;
  }

  s_listen_fd = fd;
  s_allow_write = allow_write;
  s_paused = false;
  s_running = true;
  fprintf(stderr, "nwa: listening on 127.0.0.1:%u (writes %s)\n",
          (unsigned)s_port, s_allow_write ? "enabled" : "disabled");
}

void NwaServer_Stop(void) {
  NwaEnsureConnectionsInitialized();
  for (int i = 0; i < kNwaMaxConnections; i++) {
    if (s_connections[i].fd != kNwaInvalidSocket ||
        s_connections[i].in.data != NULL || s_connections[i].out.data != NULL)
      NwaConnReset(&s_connections[i]);
  }
  NwaCloseSocket(s_listen_fd);
  s_listen_fd = kNwaInvalidSocket;
  free(s_rom_path);
  s_rom_path = NULL;
  s_emu_snes = NULL;
  s_running = false;
  s_paused = false;
  s_allow_write = false;
  s_port = 0;
#ifdef _WIN32
  if (s_wsa_started) {
    WSACleanup();
    s_wsa_started = false;
  }
#endif
}

void NwaServer_Poll(void) {
  if (!s_running)
    return;
  NwaAcceptConnections();
  for (int i = 0; i < kNwaMaxConnections; i++) {
    if (s_connections[i].fd != kNwaInvalidSocket)
      NwaPollConnection(&s_connections[i]);
  }
}

bool NwaServer_IsPaused(void) {
  return s_running && s_paused;
}

bool NwaServer_IsRunning(void) {
  return s_running;
}
