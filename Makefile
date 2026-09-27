# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# zbitxre
#
#   make          build everything into build/
#   make check    run the tests that need no radio
#   make radio    run the tests that need the radio (stop the radio program first)
#
#   build/sdr     the radio, run from a terminal; see tools/sdr.c

CC     = gcc
CFLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Ihw -Idsp -Itests
BUILD  = build

HW_HEADERS  = hw/i2c.h hw/si5351.h hw/gpio.h hw/board.h hw/audio.h
DSP_HEADERS = dsp/sdr.h
HW          = hw/i2c.c hw/si5351.c hw/gpio.c hw/board.c hw/audio.c
DSP         = dsp/sdr.c dsp/filter.c dsp/vfo.c
RADIO_LIBS  = -lfftw3 -lfftw3f -lasound -lpthread -lm

HOST_TESTS  = $(BUILD)/test_si5351_math $(BUILD)/test_am
RADIO_TESTS = $(BUILD)/test_si5351 $(BUILD)/test_board $(BUILD)/test_audio
TOOLS       = $(BUILD)/sdr

.PHONY: all check radio clean

all: $(HOST_TESTS) $(RADIO_TESTS) $(TOOLS)

check: $(HOST_TESTS)
	$(BUILD)/test_si5351_math
	$(BUILD)/test_am

radio: $(RADIO_TESTS)
	$(BUILD)/test_si5351
	$(BUILD)/test_board
	$(BUILD)/test_audio

$(BUILD):
	mkdir -p $(BUILD) data

$(BUILD)/sdr: tools/sdr.c $(DSP) $(HW) $(DSP_HEADERS) $(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tools/sdr.c $(DSP) $(HW) $(RADIO_LIBS)

$(BUILD)/test_si5351_math: tests/test_si5351_math.c hw/si5351.c \
		$(HW_HEADERS) tests/si5351_golden.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_si5351_math.c hw/si5351.c

$(BUILD)/test_am: tests/test_am.c $(DSP) $(HW) $(DSP_HEADERS) $(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_am.c $(DSP) $(HW) $(RADIO_LIBS)

$(BUILD)/test_si5351: tests/test_si5351.c hw/si5351.c hw/i2c.c \
		$(HW_HEADERS) tests/si5351_golden.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_si5351.c hw/si5351.c hw/i2c.c

$(BUILD)/test_board: tests/test_board.c hw/board.c hw/gpio.c \
		$(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_board.c hw/board.c hw/gpio.c

$(BUILD)/test_audio: tests/test_audio.c hw/audio.c $(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_audio.c hw/audio.c -lasound -lpthread -lm

clean:
	rm -rf $(BUILD)
