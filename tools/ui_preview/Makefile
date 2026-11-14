# Host preview of the vault UI. `make` builds, renders every scene and writes
# PNG stills, contact sheets and GIFs to build/out (scale 4x).
#   make SHOTS=1   also copies the curated set to docs/screenshots/vault/
UFBT_HOME ?= $(HOME)/.ufbt-ui
ROOT := ../..
BUILD := build
CC ?= cc
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
	-Istub -I$(BUILD) -I$(ROOT)/src/ui -DZF_UI_PREVIEW=1
UI_SRC := $(ROOT)/src/ui/vault_gfx.c $(ROOT)/src/ui/vault_door.c \
	$(wildcard $(ROOT)/src/ui/vault_sigil.c) $(wildcard $(ROOT)/src/ui/scene_*.c)
SRC := main.c recorder.c canvas_stub.c $(UI_SRC)
UV := uv run --no-project
PY := $(UV) python -I

all: render

$(BUILD)/fonts.h: extract_fonts.py
	mkdir -p $(BUILD)
	$(PY) extract_fonts.py $(UFBT_HOME)/current/firmware.elf $@

$(BUILD)/preview: $(SRC) $(BUILD)/fonts.h $(wildcard stub/gui/*.h) $(wildcard $(ROOT)/src/ui/vault_*.h) $(wildcard $(ROOT)/src/ui/scene*.h) $(UI_SRC) preview.h
	$(CC) $(CFLAGS) -o $@ $(SRC)

render: $(BUILD)/preview
	rm -rf $(BUILD)/frames $(BUILD)/out
	mkdir -p $(BUILD)/frames
	cd $(BUILD) && ./preview
	$(UV) --with pillow python -I render.py $(BUILD)/frames $(BUILD)/out 4
ifeq ($(SHOTS),1)
	mkdir -p $(ROOT)/docs/screenshots/vault
	cp $(BUILD)/out/*.png $(BUILD)/out/*.gif $(ROOT)/docs/screenshots/vault/
endif

clean:
	rm -rf $(BUILD)

.PHONY: all render clean
