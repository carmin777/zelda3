# Voxel renderer plugin (R-4), built separately from the engine.
#
#   make -C plugins/voxel
#   ./zelda3 --render-plugin plugins/voxel/voxel.so ...
#
# The .so/.dll is a build artifact and is not committed.

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c99
INCLUDES := -I../../third_party/cjson
SRC := voxel.c voxel_gl.c voxel_profile.c
CJSON_SRC := ../../third_party/cjson/cJSON.c
HEADERS := voxel.c voxel_gl.c voxel_profile.c voxel_gl.h voxel_profile.h \
           ../../src/mod/render_plugin.h ../../src/mod/frame_desc.h \
           ../../src/types.h ../../third_party/cjson/cJSON.h

ifeq ($(OS),Windows_NT)
  EXT := dll
  PICFLAGS :=
  LDFLAGS := -shared
  PLUGIN_LIBS :=
else
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    EXT := dylib
    PICFLAGS := -fPIC
    LDFLAGS := -dynamiclib
    PLUGIN_LIBS :=
  else
    EXT := so
    PICFLAGS := -fPIC
    LDFLAGS := -shared
    PLUGIN_LIBS := -lEGL -lGL -lm
  endif
endif

TARGET := voxel.$(EXT)

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(SRC) $(CJSON_SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(PICFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ \
	    $(SRC) $(CJSON_SRC) $(PLUGIN_LIBS)

clean:
	rm -f $(TARGET)
