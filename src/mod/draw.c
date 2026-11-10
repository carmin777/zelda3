#include "draw.h"

#include <stddef.h>
#include <string.h>

// A 5x7 bitmap font for ASCII 32..126, written as five columns per glyph.
// Bit 0 is the top row, so a glyph is at most 5x7 pixels. Text advances six
// pixels per character (one blank column between glyphs).
enum {
  kDrawGlyphWidth = 5,
  kDrawGlyphHeight = 7,
  kDrawCharAdvance = 6,
  kDrawTextCapacity = 65536,
  kDrawMaxTextLength = 65535,
  kDrawCoordLimit = 1000000,
};

static const uint8 kFont5x7[95][kDrawGlyphWidth] = {
  { 0x00, 0x00, 0x00, 0x00, 0x00 }, //  32
  { 0x00, 0x00, 0x5F, 0x00, 0x00 }, //  33 !
  { 0x00, 0x07, 0x00, 0x07, 0x00 }, //  34 "
  { 0x14, 0x7F, 0x14, 0x7F, 0x14 }, //  35 #
  { 0x24, 0x2A, 0x7F, 0x2A, 0x12 }, //  36 $
  { 0x23, 0x13, 0x08, 0x64, 0x62 }, //  37 %
  { 0x36, 0x49, 0x55, 0x22, 0x50 }, //  38 &
  { 0x00, 0x05, 0x03, 0x00, 0x00 }, //  39 '
  { 0x00, 0x1C, 0x22, 0x41, 0x00 }, //  40 (
  { 0x00, 0x41, 0x22, 0x1C, 0x00 }, //  41 )
  { 0x14, 0x08, 0x3E, 0x08, 0x14 }, //  42 *
  { 0x08, 0x08, 0x3E, 0x08, 0x08 }, //  43 +
  { 0x00, 0x50, 0x30, 0x00, 0x00 }, //  44 ,
  { 0x08, 0x08, 0x08, 0x08, 0x08 }, //  45 -
  { 0x00, 0x60, 0x60, 0x00, 0x00 }, //  46 .
  { 0x20, 0x10, 0x08, 0x04, 0x02 }, //  47 /
  { 0x3E, 0x51, 0x49, 0x45, 0x3E }, //  48 0
  { 0x00, 0x42, 0x7F, 0x40, 0x00 }, //  49 1
  { 0x42, 0x61, 0x51, 0x49, 0x46 }, //  50 2
  { 0x21, 0x41, 0x45, 0x4B, 0x31 }, //  51 3
  { 0x18, 0x14, 0x12, 0x7F, 0x10 }, //  52 4
  { 0x27, 0x45, 0x45, 0x45, 0x39 }, //  53 5
  { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, //  54 6
  { 0x01, 0x71, 0x09, 0x05, 0x03 }, //  55 7
  { 0x36, 0x49, 0x49, 0x49, 0x36 }, //  56 8
  { 0x06, 0x49, 0x49, 0x29, 0x1E }, //  57 9
  { 0x00, 0x36, 0x36, 0x00, 0x00 }, //  58 :
  { 0x00, 0x56, 0x36, 0x00, 0x00 }, //  59 ;
  { 0x08, 0x14, 0x22, 0x41, 0x00 }, //  60 <
  { 0x14, 0x14, 0x14, 0x14, 0x14 }, //  61 =
  { 0x00, 0x41, 0x22, 0x14, 0x08 }, //  62 >
  { 0x02, 0x01, 0x51, 0x09, 0x06 }, //  63 ?
  { 0x32, 0x49, 0x79, 0x41, 0x3E }, //  64 @
  { 0x7E, 0x11, 0x11, 0x11, 0x7E }, //  65 A
  { 0x7F, 0x49, 0x49, 0x49, 0x36 }, //  66 B
  { 0x3E, 0x41, 0x41, 0x41, 0x22 }, //  67 C
  { 0x7F, 0x41, 0x41, 0x22, 0x1C }, //  68 D
  { 0x7F, 0x49, 0x49, 0x49, 0x41 }, //  69 E
  { 0x7F, 0x09, 0x09, 0x09, 0x01 }, //  70 F
  { 0x3E, 0x41, 0x49, 0x49, 0x7A }, //  71 G
  { 0x7F, 0x08, 0x08, 0x08, 0x7F }, //  72 H
  { 0x00, 0x41, 0x7F, 0x41, 0x00 }, //  73 I
  { 0x20, 0x40, 0x41, 0x3F, 0x01 }, //  74 J
  { 0x7F, 0x08, 0x14, 0x22, 0x41 }, //  75 K
  { 0x7F, 0x40, 0x40, 0x40, 0x40 }, //  76 L
  { 0x7F, 0x02, 0x0C, 0x02, 0x7F }, //  77 M
  { 0x7F, 0x04, 0x08, 0x10, 0x7F }, //  78 N
  { 0x3E, 0x41, 0x41, 0x41, 0x3E }, //  79 O
  { 0x7F, 0x09, 0x09, 0x09, 0x06 }, //  80 P
  { 0x3E, 0x41, 0x51, 0x21, 0x5E }, //  81 Q
  { 0x7F, 0x09, 0x19, 0x29, 0x46 }, //  82 R
  { 0x46, 0x49, 0x49, 0x49, 0x31 }, //  83 S
  { 0x01, 0x01, 0x7F, 0x01, 0x01 }, //  84 T
  { 0x3F, 0x40, 0x40, 0x40, 0x3F }, //  85 U
  { 0x1F, 0x20, 0x40, 0x20, 0x1F }, //  86 V
  { 0x3F, 0x40, 0x38, 0x40, 0x3F }, //  87 W
  { 0x63, 0x14, 0x08, 0x14, 0x63 }, //  88 X
  { 0x07, 0x08, 0x70, 0x08, 0x07 }, //  89 Y
  { 0x61, 0x51, 0x49, 0x45, 0x43 }, //  90 Z
  { 0x00, 0x7F, 0x41, 0x41, 0x00 }, //  91 [
  { 0x02, 0x04, 0x08, 0x10, 0x20 }, //  92 backslash
  { 0x00, 0x41, 0x41, 0x7F, 0x00 }, //  93 ]
  { 0x04, 0x02, 0x01, 0x02, 0x04 }, //  94 ^
  { 0x40, 0x40, 0x40, 0x40, 0x40 }, //  95 _
  { 0x00, 0x01, 0x02, 0x04, 0x00 }, //  96 `
  { 0x20, 0x54, 0x54, 0x54, 0x78 }, //  97 a
  { 0x7F, 0x48, 0x44, 0x44, 0x38 }, //  98 b
  { 0x38, 0x44, 0x44, 0x44, 0x20 }, //  99 c
  { 0x38, 0x44, 0x44, 0x48, 0x7F }, // 100 d
  { 0x38, 0x54, 0x54, 0x54, 0x18 }, // 101 e
  { 0x08, 0x7E, 0x09, 0x01, 0x02 }, // 102 f
  { 0x0C, 0x52, 0x52, 0x52, 0x3E }, // 103 g
  { 0x7F, 0x08, 0x04, 0x04, 0x78 }, // 104 h
  { 0x00, 0x44, 0x7D, 0x40, 0x00 }, // 105 i
  { 0x20, 0x40, 0x44, 0x3D, 0x00 }, // 106 j
  { 0x7F, 0x10, 0x28, 0x44, 0x00 }, // 107 k
  { 0x00, 0x41, 0x7F, 0x40, 0x00 }, // 108 l
  { 0x7C, 0x04, 0x18, 0x04, 0x78 }, // 109 m
  { 0x7C, 0x08, 0x04, 0x04, 0x78 }, // 110 n
  { 0x38, 0x44, 0x44, 0x44, 0x38 }, // 111 o
  { 0x7C, 0x14, 0x14, 0x14, 0x08 }, // 112 p
  { 0x08, 0x14, 0x14, 0x18, 0x7C }, // 113 q
  { 0x7C, 0x08, 0x04, 0x04, 0x08 }, // 114 r
  { 0x48, 0x54, 0x54, 0x54, 0x20 }, // 115 s
  { 0x04, 0x3F, 0x44, 0x40, 0x20 }, // 116 t
  { 0x3C, 0x40, 0x40, 0x20, 0x7C }, // 117 u
  { 0x1C, 0x20, 0x40, 0x20, 0x1C }, // 118 v
  { 0x3C, 0x40, 0x30, 0x40, 0x3C }, // 119 w
  { 0x44, 0x28, 0x10, 0x28, 0x44 }, // 120 x
  { 0x0C, 0x50, 0x50, 0x50, 0x3C }, // 121 y
  { 0x44, 0x64, 0x54, 0x4C, 0x44 }, // 122 z
  { 0x00, 0x08, 0x36, 0x41, 0x00 }, // 123 {
  { 0x00, 0x00, 0x7F, 0x00, 0x00 }, // 124 |
  { 0x00, 0x41, 0x36, 0x08, 0x00 }, // 125 }
  { 0x08, 0x08, 0x2A, 0x1C, 0x08 }, // 126 ~
};

typedef enum ModDrawCommandType {
  kModDrawCmd_Rect,
  kModDrawCmd_Text,
} ModDrawCommandType;

typedef struct ModDrawCommand {
  int x, y, w, h;
  uint32 color;
  uint32 text_offset;
  uint16 text_len;
  uint8 type;
} ModDrawCommand;

static ModDrawCommand g_commands[kModDrawMaxCommands];
static int g_command_count;
static char g_text[kDrawTextCapacity];
static uint32 g_text_used;

static uint8 *g_buffer;
static size_t g_pitch;
static int g_width, g_height;
static int g_pixel_width, g_pixel_height;
static int g_scale;
static bool g_active;

static void PutPixel(int x, int y, uint32 color) {
  if ((unsigned)x >= (unsigned)g_pixel_width ||
      (unsigned)y >= (unsigned)g_pixel_height)
    return;

  uint32 *dst = (uint32 *)(g_buffer + (size_t)y * g_pitch) + x;
  uint32 alpha = color >> 24;
  if (alpha == 0)
    return;

  uint32 src = color & 0xFFFFFFu;
  if (alpha == 255) {
    *dst = (*dst & 0xFF000000u) | src;
    return;
  }

  uint32 old = *dst;
  uint32 inv = 255 - alpha;
  uint32 r = (((src >> 16) & 0xFF) * alpha + ((old >> 16) & 0xFF) * inv + 127) / 255;
  uint32 g = (((src >> 8) & 0xFF) * alpha + ((old >> 8) & 0xFF) * inv + 127) / 255;
  uint32 b = ((src & 0xFF) * alpha + (old & 0xFF) * inv + 127) / 255;
  *dst = (old & 0xFF000000u) | r << 16 | g << 8 | b;
}

static void PutSnesPixel(int x, int y, uint32 color) {
  int px = x * g_scale;
  int py = y * g_scale;
  for (int dy = 0; dy < g_scale; dy++) {
    for (int dx = 0; dx < g_scale; dx++)
      PutPixel(px + dx, py + dy, color);
  }
}

static void DrawRectCommand(const ModDrawCommand *cmd) {
  int x0 = cmd->x * g_scale;
  int y0 = cmd->y * g_scale;
  int x1 = (cmd->x + cmd->w) * g_scale;
  int y1 = (cmd->y + cmd->h) * g_scale;
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 > g_pixel_width)
    x1 = g_pixel_width;
  if (y1 > g_pixel_height)
    y1 = g_pixel_height;
  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++)
      PutPixel(x, y, cmd->color);
  }
}

static void DrawTextCommand(const ModDrawCommand *cmd) {
  const char *text = &g_text[cmd->text_offset];
  int pen_x = cmd->x;
  for (uint16 i = 0; i < cmd->text_len; i++) {
    uint8 ch = (uint8)text[i];
    if (ch < 32 || ch > 126)
      ch = '?';
    const uint8 *glyph = kFont5x7[ch - 32];
    for (int col = 0; col < kDrawGlyphWidth; col++) {
      uint8 bits = glyph[col];
      for (int row = 0; row < kDrawGlyphHeight; row++) {
        if (bits & (1u << row))
          PutSnesPixel(pen_x + col, cmd->y + row, cmd->color);
      }
    }
    pen_x += kDrawCharAdvance;
  }
}

void ModDraw_BeginFrame(uint8 *buffer, size_t pitch, int width, int height,
                        int scale) {
  g_buffer = buffer;
  g_pitch = pitch;
  g_width = width;
  g_height = height;
  g_scale = scale > 0 ? scale : 1;
  g_pixel_width = width * g_scale;
  g_pixel_height = height * g_scale;
  g_command_count = 0;
  g_text_used = 0;
  g_active = true;
}

void ModDraw_EndFrame(void) {
  if (!g_active)
    return;

  for (int i = 0; i < g_command_count; i++) {
    const ModDrawCommand *cmd = &g_commands[i];
    if (cmd->type == kModDrawCmd_Text)
      DrawTextCommand(cmd);
    else
      DrawRectCommand(cmd);
  }

  g_active = false;
  g_buffer = NULL;
  g_pitch = 0;
  g_command_count = 0;
  g_text_used = 0;
}

bool ModDraw_FrameActive(void) {
  return g_active;
}

int ModDraw_Width(void) {
  return g_width;
}

int ModDraw_Height(void) {
  return g_height;
}

static const char *CheckRect(const char *name, int x, int y, int w, int h) {
  (void)name;
  if (x < -kDrawCoordLimit || x > kDrawCoordLimit ||
      y < -kDrawCoordLimit || y > kDrawCoordLimit)
    return "draw coordinates are out of range";
  if (w < 0 || h < 0)
    return "draw size must not be negative";
  if (w > kDrawCoordLimit || h > kDrawCoordLimit)
    return "draw size is out of range";
  return NULL;
}

const char *ModDraw_AddRect(int x, int y, int w, int h, uint32 color) {
  if (!g_active)
    return "zelda.draw is only valid inside a FrameDraw handler";
  const char *err = CheckRect("rect", x, y, w, h);
  if (err != NULL)
    return err;
  if (w == 0 || h == 0)
    return NULL;
  if (g_command_count >= kModDrawMaxCommands)
    return "FrameDraw command limit exceeded (4096 commands)";

  ModDrawCommand *cmd = &g_commands[g_command_count++];
  cmd->type = kModDrawCmd_Rect;
  cmd->x = x;
  cmd->y = y;
  cmd->w = w;
  cmd->h = h;
  cmd->color = color;
  return NULL;
}

const char *ModDraw_AddText(int x, int y, const char *text, size_t length,
                            uint32 color) {
  if (!g_active)
    return "zelda.draw is only valid inside a FrameDraw handler";
  const char *err = CheckRect("text", x, y, 0, 0);
  if (err != NULL)
    return err;
  if (length == 0)
    return NULL;
  if (length > kDrawMaxTextLength)
    return "FrameDraw text is too long (max 65535 bytes)";
  if (g_command_count >= kModDrawMaxCommands)
    return "FrameDraw command limit exceeded (4096 commands)";
  if (g_text_used + length > kDrawTextCapacity)
    return "FrameDraw text buffer is full";

  ModDrawCommand *cmd = &g_commands[g_command_count++];
  cmd->type = kModDrawCmd_Text;
  cmd->x = x;
  cmd->y = y;
  cmd->w = 0;
  cmd->h = 0;
  cmd->color = color;
  cmd->text_offset = g_text_used;
  cmd->text_len = (uint16)length;
  memcpy(g_text + g_text_used, text, length);
  g_text_used += (uint32)length;
  return NULL;
}
