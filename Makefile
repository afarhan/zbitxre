# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# zbitxre
#
#   make          build everything into build/
#   make check    run the tests that need no radio
#   make radio    run the tests that need the radio (stop the radio program first)

CC     = gcc
CFLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Ihw -Itests
BUILD  = build

HW_HEADERS  = hw/i2c.h hw/si5351.h hw/gpio.h hw/board.h hw/audio.h
HOST_TESTS  = $(BUILD)/test_si5351_math
RADIO_TESTS = $(BUILD)/test_si5351 $(BUILD)/test_board $(BUILD)/test_audio

.PHONY: all check radio clean

all: $(HOST_TESTS) $(RADIO_TESTS)

check: $(HOST_TESTS)
	$(BUILD)/test_si5351_math

radio: $(RADIO_TESTS)
	$(BUILD)/test_si5351
	$(BUILD)/test_board
	$(BUILD)/test_audio

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_si5351_math: tests/test_si5351_math.c hw/si5351.c \
		$(HW_HEADERS) tests/si5351_golden.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_si5351_math.c hw/si5351.c

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
