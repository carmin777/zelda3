TARGET_EXEC:=zelda3
SRCS:=$(wildcard src/*.c src/mod/*.c snes/*.c) third_party/gl_core/gl_core_3_1.c third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c third_party/cjson/cJSON.c
OBJS:=$(SRCS:%.c=%.o)
PYTHON:=/usr/bin/env python3
CFLAGS:=$(if $(CFLAGS),$(CFLAGS),-O2 -Werror) -I .
CFLAGS:=${CFLAGS} $(shell sdl2-config --cflags) -DSYSTEM_VOLUME_MIXER_AVAILABLE=0

ifeq (${OS},Windows_NT)
    WINDRES:=windres
    RES:=zelda3.res
    SDLFLAGS:=-Wl,-Bstatic $(shell sdl2-config --static-libs)
else
    UNAME_S:=$(shell uname -s)
    DL_LIBS:=$(if $(filter Linux,$(UNAME_S)),-ldl)
    SDLFLAGS:=$(shell sdl2-config --libs) -lm $(DL_LIBS)
endif

# Lua 5.4 mod scripting. Build with `make NO_LUA=1` to use the warning-only
# stubs instead; the game still runs, scripts are just ignored.
ifneq ($(NO_LUA),1)
    LUA_CFLAGS:=$(shell pkg-config --cflags lua5.4)
    LUA_LIBS:=$(shell pkg-config --libs lua5.4)
    CFLAGS+=-DZELDA3_LUA $(LUA_CFLAGS)
    SDLFLAGS+=$(LUA_LIBS)
endif

# Switching NO_LUA changes the compiler flags without changing any source, so
# keep a stamp that forces all objects to rebuild when the mode changes.
BUILD_MODE_FILE:=.build-mode
BUILD_MODE:=$(if $(filter 1,$(NO_LUA)),nolua,lua)
$(shell test "$$(cat $(BUILD_MODE_FILE) 2>/dev/null)" = "$(BUILD_MODE)" || echo $(BUILD_MODE) > $(BUILD_MODE_FILE))

.PHONY: all clean clean_obj clean_gen asset-names state-table sprite-names

all: $(TARGET_EXEC) zelda3_assets.dat

$(OBJS): $(BUILD_MODE_FILE)
asset-names:
	$(PYTHON) tools/gen_asset_names.py
state-table:
	$(PYTHON) tools/gen_state_table.py
sprite-names:
	$(PYTHON) tools/gen_sprite_names.py
$(TARGET_EXEC): $(OBJS) $(RES)
	$(CC) $^ -o $@ $(LDFLAGS) $(SDLFLAGS)
# -MMD -MP writes a .d file per object listing the headers it includes, so a
# change to a header (for example a struct layout) rebuilds every object that
# uses it instead of linking stale objects.
%.o : %.c
	$(CC) -c $(CFLAGS) -MMD -MP $< -o $@

-include $(OBJS:.o=.d)

$(RES): src/platform/win32/zelda3.rc
	@echo "Generating Windows resources"
	@$(WINDRES) $< -O coff -o $@

zelda3_assets.dat:
	@echo "Extracting game resources"
	$(PYTHON) assets/restool.py --extract-from-rom

clean: clean_obj clean_gen
clean_obj:
	@$(RM) $(OBJS) $(OBJS:.o=.d) $(TARGET_EXEC) $(BUILD_MODE_FILE)
clean_gen:
	@$(RM) $(RES) zelda3_assets.dat
	@rm -rf assets/__pycache__
