# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# zbitxre
#
#   make          build everything into build/
#   make check    run the tests that need no radio
#   make radio    run the tests that need the radio (stop the radio program first)
#   make panel    build the front panel's firmware
#   make panel-flash   put it on a Pico that is in its loader
#
#   build/sdr     the radio, run from a terminal; see tools/sdr.c

CC     = gcc
CFLAGS = -std=gnu11 -O2 -g -Wall -Wextra -I. -Ihw -Idsp -Itests -Ifront_panel
BUILD  = build

# what the radio and the panel say to each other is in the panel's folder,
# and both are built with it
LINK        = front_panel/link.c
LINK_HEADERS = front_panel/link.h

HW_HEADERS  = hw/i2c.h hw/si5351.h hw/gpio.h hw/board.h hw/audio.h hw/panel.h \
	$(LINK_HEADERS)
DSP_HEADERS = dsp/sdr.h signal_block.h modems.h
HW          = hw/i2c.c hw/si5351.c hw/gpio.c hw/board.c hw/audio.c hw/panel.c \
	$(LINK)
DSP         = core_sdr.c signal_block.c modems.c dsp/filter.c dsp/vfo.c
RADIO_LIBS  = -lfftw3 -lfftw3f -lasound -lpthread -lm

HOST_TESTS  = $(BUILD)/test_si5351_math $(BUILD)/test_am $(BUILD)/test_link \
	$(BUILD)/test_chain
RADIO_TESTS = $(BUILD)/test_si5351 $(BUILD)/test_board $(BUILD)/test_audio \
	$(BUILD)/test_panel
TOOLS       = $(BUILD)/sdr

# The front panel is an Arduino sketch for the Pico W. It is built with
# arduino-cli, which needs the rp2040 core and the lvgl library, version 9.3.0.
PANEL        = front_panel
PANEL_BUILD  = $(CURDIR)/$(BUILD)/$(PANEL)
PANEL_BOARD  = rp2040:rp2040:rpipicow
PANEL_FLAGS  = -DLV_CONF_INCLUDE_SIMPLE -I$(CURDIR)/$(PANEL)
PANEL_UF2    = $(PANEL_BUILD)/$(PANEL).ino.uf2
PANEL_DRIVE  = /media/$(USER)/RPI-RP2

.PHONY: all check radio clean panel panel-flash

all: $(HOST_TESTS) $(RADIO_TESTS) $(TOOLS)

check: $(HOST_TESTS)
	$(BUILD)/test_si5351_math
	$(BUILD)/test_am
	$(BUILD)/test_link
	$(BUILD)/test_chain

radio: $(RADIO_TESTS)
	$(BUILD)/test_si5351
	$(BUILD)/test_board
	$(BUILD)/test_audio
	$(BUILD)/test_panel

$(BUILD):
	mkdir -p $(BUILD) data

$(BUILD)/sdr: tools/sdr.c $(DSP) $(HW) $(DSP_HEADERS) $(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tools/sdr.c $(DSP) $(HW) $(RADIO_LIBS)

$(BUILD)/test_si5351_math: tests/test_si5351_math.c hw/si5351.c \
		$(HW_HEADERS) tests/si5351_golden.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_si5351_math.c hw/si5351.c

# a test that needs no radio is built with the hardware stood in for
STAND_INS   = tests/hw_stand_ins.c hw/board.c hw/si5351.c

$(BUILD)/test_am: tests/test_am.c $(DSP) $(STAND_INS) $(DSP_HEADERS) $(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_am.c $(DSP) $(STAND_INS) -lfftw3 -lfftw3f -lm

$(BUILD)/test_chain: tests/test_chain.c signal_block.c modems.c signal_block.h modems.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_chain.c signal_block.c modems.c

$(BUILD)/test_link: tests/test_link.c $(LINK) $(LINK_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_link.c $(LINK)

$(BUILD)/test_panel: tests/test_panel.c hw/panel.c hw/i2c.c $(LINK) \
		$(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_panel.c hw/panel.c hw/i2c.c $(LINK)

$(BUILD)/test_si5351: tests/test_si5351.c hw/si5351.c hw/i2c.c \
		$(HW_HEADERS) tests/si5351_golden.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_si5351.c hw/si5351.c hw/i2c.c

$(BUILD)/test_board: tests/test_board.c hw/board.c hw/gpio.c \
		$(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_board.c hw/board.c hw/gpio.c

$(BUILD)/test_audio: tests/test_audio.c hw/audio.c $(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_audio.c hw/audio.c -lasound -lpthread -lm

panel:
	arduino-cli compile --fqbn $(PANEL_BOARD) --build-path $(PANEL_BUILD) \
		--build-property "compiler.c.extra_flags=$(PANEL_FLAGS)" \
		--build-property "compiler.cpp.extra_flags=$(PANEL_FLAGS)" \
		--build-property "compiler.S.extra_flags=$(PANEL_FLAGS)" \
		$(PANEL)

panel-flash: panel
	test -d $(PANEL_DRIVE)
	cp $(PANEL_UF2) $(PANEL_DRIVE)/
	sync

clean:
	rm -rf $(BUILD)
