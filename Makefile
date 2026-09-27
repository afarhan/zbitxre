# zbitxre
#
#   make          build everything into build/
#   make check    run the tests that need no radio
#   make radio    run the tests that need the radio (stop the radio program first)

CC     = gcc
CFLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Ihw -Itests
BUILD  = build

TESTS  = $(BUILD)/test_si5351_math $(BUILD)/test_si5351

.PHONY: all check radio clean

all: $(TESTS)

check: $(BUILD)/test_si5351_math
	$(BUILD)/test_si5351_math

radio: $(BUILD)/test_si5351
	$(BUILD)/test_si5351

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_si5351_math: tests/test_si5351_math.c hw/si5351.c \
		hw/si5351.h hw/i2c.h tests/si5351_golden.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_si5351_math.c hw/si5351.c

$(BUILD)/test_si5351: tests/test_si5351.c hw/si5351.c hw/i2c.c \
		hw/si5351.h hw/i2c.h tests/si5351_golden.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_si5351.c hw/si5351.c hw/i2c.c

clean:
	rm -rf $(BUILD)
