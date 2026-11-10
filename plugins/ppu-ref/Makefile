# Reference renderer plugin, built separately from the engine.
#
#   make -C plugins/ppu-ref
#   ./zelda3 --render-plugin plugins/ppu-ref/ppu-ref.so ...
#
# The shared implementation is src/mod/render_plugin_ref.c, the same file the
# engine compiles as builtin:ppu. The .so/.dll is a build artifact and is not
# committed.

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c99
SRC := ppu_ref.c
HEADERS := ../../src/mod/render_plugin.h ../../src/mod/render_plugin_ref.c \
           ../../src/mod/frame_desc.h ../../src/types.h

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

TARGET := ppu-ref.$(EXT)

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(PICFLAGS) $(LDFLAGS) -o $@ $(SRC)

clean:
	rm -f $(TARGET)
