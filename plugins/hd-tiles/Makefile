# HD tile replacement plugin, built separately from the engine.
#
#   make -C plugins/hd-tiles
#   ZELDA3_HD_PACK=plugins/hd-tiles/example-pack \
#     ./zelda3 --render-plugin plugins/hd-tiles/hd-tiles.so ...
#
# The .so/.dll is a build artifact and is not committed.

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c99
INCLUDES := -I../../third_party/cjson -I../../third_party/stb
SRC := hd_tiles.c
CJSON_SRC := ../../third_party/cjson/cJSON.c
HEADERS := hd_tiles.c ../../src/mod/render_plugin.h \
           ../../src/mod/frame_desc.h ../../src/mod/hd_tile_hash.h \
           ../../src/types.h ../../third_party/cjson/cJSON.h \
           ../../third_party/stb/stb_image.h

ifeq ($(OS),Windows_NT)
  EXT := dll
  PICFLAGS :=
  LDFLAGS := -shared
else
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    EXT := dylib
    PICFLAGS := -fPIC
    LDFLAGS := -dynamiclib
  else
    EXT := so
    PICFLAGS := -fPIC
    LDFLAGS := -shared
  endif
endif

TARGET := hd-tiles.$(EXT)

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(SRC) $(CJSON_SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(PICFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ \
	    $(SRC) $(CJSON_SRC)

clean:
	rm -f $(TARGET)
