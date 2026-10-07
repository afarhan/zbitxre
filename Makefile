# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# zbitxre
#
#   make          build the radio program here, and the tests into build/
#   make check    run the tests that need no radio
#   make radio    run the tests that need the radio (stop the radio program first)
#   make gui      build the desktop's window on the radio, here (needs GTK 3)
#   make panel    build the front panel's firmware
#   make panel-flash   put it on a Pico that is in its loader
#
#   ./sdr         the radio, run from a terminal; see tools/sdr.c
#   ./zbitx_gui   the desktop's window on the radio; see clients/gtk/zbitx_gui.c
#
# The programs an operator runs are built in this folder, where they are
# easy to find. The tests, and what is made on the way, go into build/.

CC     = gcc
CFLAGS = -std=gnu11 -O2 -g -Wall -Wextra -I. -Ihw -Idsp -Idsp/kiss_fft -Itests -Ifront_panel
BUILD  = build

# what the radio and the panel say to each other is in the panel's folder,
# and both are built with it
LINK        = front_panel/link.c
LINK_HEADERS = front_panel/link.h

HW_HEADERS  = hw/i2c.h hw/si5351.h hw/gpio.h hw/board.h hw/audio.h hw/panel.h hw/key.h \
	hw/radio_clock.h \
	$(LINK_HEADERS)
DSP_HEADERS = dsp/sdr.h modem.h control.h macros.h messages.h modems/cw_keyer.h \
	modems/cw_text.h modems/modem_cw.h modems/ft8_monitor.h modems/ft8_signal.h modems/modem_ft8.h \
	modems/ft8_callsign_hash.h
HW          = hw/i2c.c hw/si5351.c hw/gpio.c hw/board.c hw/audio.c hw/panel.c hw/key.c \
	hw/radio_clock.c \
	$(LINK)
# every file in modems/ is a modem, and the list of them is made from the
# files at build time; modem.h says how
MODEMS      = $(wildcard modems/*.c)
MODEM_LIST  = $(BUILD)/modem_list.c
# the transforms are kiss_fft's. Its source is in dsp/kiss_fft, as its
# authors wrote it, and is built on its own with its warnings off, so that
# the warnings shown are all the radio's own
KISS_FFT    = $(BUILD)/kiss_fft.o
# FT8's coding is ft8_lib's, in modems/ft8_lib as zbitxv2 had it, and is
# built in the same way; modems/ft8_lib/README says what it is
FT8_LIB     = $(patsubst modems/ft8_lib/%.c,$(BUILD)/ft8_lib_%.o,$(wildcard modems/ft8_lib/*.c))
DSP         = core_sdr.c control.c macros.c messages.c dsp/filter.c dsp/vfo.c $(KISS_FFT) \
	$(MODEMS) $(MODEM_LIST) $(FT8_LIB)
RADIO_LIBS  = -lasound -lpthread -lm

HOST_TESTS  = $(BUILD)/test_si5351_math $(BUILD)/test_am $(BUILD)/test_link \
	$(BUILD)/test_cw_keyer $(BUILD)/test_server \
	$(BUILD)/test_panel_controls $(BUILD)/test_start_check $(BUILD)/test_cw_text \
	$(BUILD)/test_settings $(BUILD)/test_panel_wifi $(BUILD)/test_radio_clock \
	$(BUILD)/test_logbook $(BUILD)/test_ft8 $(BUILD)/test_messages \
	$(BUILD)/test_ft8_qso $(BUILD)/test_cw_decoder $(BUILD)/test_logger \
	$(BUILD)/test_sbitxv3
RADIO_TESTS = $(BUILD)/test_si5351 $(BUILD)/test_board $(BUILD)/test_audio \
	$(BUILD)/test_panel
TOOLS       = sdr
GUI         = zbitx_gui

# The front panel is an Arduino sketch for the Pico W. It is built with
# arduino-cli, which needs the rp2040 core and the lvgl library, version 9.3.0.
PANEL        = front_panel
PANEL_BUILD  = $(CURDIR)/$(BUILD)/$(PANEL)
PANEL_BOARD  = rp2040:rp2040:rpipico
PANEL_FLAGS  = -DLV_CONF_INCLUDE_SIMPLE -I$(CURDIR)/$(PANEL)
PANEL_UF2    = $(PANEL_BUILD)/$(PANEL).ino.uf2
PANEL_DRIVE  = /media/$(USER)/RPI-RP2

.PHONY: all check radio clean panel panel-flash gui FORCE

all: $(HOST_TESTS) $(RADIO_TESTS) $(TOOLS) data/cw.mc

check: $(HOST_TESTS) data/cw.mc
	$(BUILD)/test_si5351_math
	$(BUILD)/test_am
	$(BUILD)/test_link
	$(BUILD)/test_cw_keyer
	$(BUILD)/test_server
	$(BUILD)/test_panel_controls
	$(BUILD)/test_start_check
	$(BUILD)/test_cw_text
	$(BUILD)/test_settings
	$(BUILD)/test_panel_wifi
	$(BUILD)/test_radio_clock
	$(BUILD)/test_logbook
	$(BUILD)/test_ft8
	$(BUILD)/test_messages
	$(BUILD)/test_ft8_qso
	$(BUILD)/test_cw_decoder
	$(BUILD)/test_logger
	$(BUILD)/test_sbitxv3

radio: $(RADIO_TESTS)
	$(BUILD)/test_si5351
	$(BUILD)/test_board
	$(BUILD)/test_audio
	$(BUILD)/test_panel

$(BUILD):
	mkdir -p $(BUILD) data

# the CW macros are the operator's own, in data/, which is not kept in the
# repository; a radio that has none is given the sample
data/cw.mc: | $(BUILD)
	cp setup/cw.mc data/cw.mc

# the list is made afresh on every build, because a modem taken out of
# modems/ leaves no newer file behind to say so; it replaces the old list
# only when it differs, so that nothing is rebuilt for nothing
$(MODEM_LIST): $(MODEMS) tools/make_modem_list.sh FORCE | $(BUILD)
	tools/make_modem_list.sh $(MODEMS) > $@.new
	if cmp -s $@.new $@; then rm $@.new; else mv $@.new $@; fi

FORCE:

$(KISS_FFT): dsp/kiss_fft/kiss_fft.c dsp/kiss_fft/kiss_fft.h \
		dsp/kiss_fft/_kiss_fft_guts.h | $(BUILD)
	$(CC) -std=gnu11 -O2 -g -w -Idsp/kiss_fft -c -o $@ dsp/kiss_fft/kiss_fft.c

sdr: tools/sdr.c tools/start_check.c tools/start_check.h radio.c server.c \
		panel_controls.c panel_wifi.c panel_wifi.h settings.c settings.h radio.h \
		panel_log.c panel_log.h logbook.c logbook.h \
		panel_messages.c panel_messages.h ft8_qso.c ft8_qso.h panel_cw.c panel_cw.h \
		logger.c logger.h server.h control.h \
		panel_controls.h $(DSP) $(HW) $(DSP_HEADERS) $(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -Itools -o $@ tools/sdr.c tools/start_check.c radio.c server.c \
		panel_controls.c panel_wifi.c panel_log.c panel_messages.c ft8_qso.c panel_cw.c logbook.c \
		logger.c settings.c \
		$(DSP) $(HW) $(RADIO_LIBS) -lsqlite3

$(BUILD)/test_si5351_math: tests/test_si5351_math.c hw/si5351.c \
		$(HW_HEADERS) tests/si5351_golden.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_si5351_math.c hw/si5351.c

# a test that needs no radio is built with the hardware stood in for
STAND_INS   = tests/hw_stand_ins.c hw/board.c hw/si5351.c hw/radio_clock.c

$(BUILD)/test_am: tests/test_am.c $(DSP) $(STAND_INS) $(DSP_HEADERS) $(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_am.c $(DSP) $(STAND_INS) -lpthread -lm

$(BUILD)/test_sbitxv3: tests/test_sbitxv3.c $(DSP) $(STAND_INS) $(DSP_HEADERS) $(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_sbitxv3.c $(DSP) $(STAND_INS) -lpthread -lm

$(BUILD)/test_cw_decoder: tests/test_cw_decoder.c $(DSP) $(STAND_INS) $(DSP_HEADERS) \
		$(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_cw_decoder.c $(DSP) $(STAND_INS) -lpthread -lm

# a recording made on the radio with "record=", played through the
# receiver and both CW decoders; not a test, a tool for working on them
$(BUILD)/play_cw_record: tests/play_cw_record.c $(DSP) $(STAND_INS) $(DSP_HEADERS) \
		$(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/play_cw_record.c $(DSP) $(STAND_INS) -lpthread -lm

$(BUILD)/test_server: tests/test_server.c radio.c server.c settings.c radio.h server.h \
		settings.h control.h $(DSP) $(STAND_INS) $(DSP_HEADERS) $(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_server.c radio.c server.c settings.c $(DSP) \
		$(STAND_INS) -lpthread -lm

$(BUILD)/test_cw_keyer: tests/test_cw_keyer.c modems/cw_keyer.c modems/cw_keyer.h | $(BUILD)
	$(CC) $(CFLAGS) -Imodems -o $@ tests/test_cw_keyer.c modems/cw_keyer.c -lm

$(BUILD)/test_panel_controls: tests/test_panel_controls.c panel_controls.c \
		panel_controls.h control.c control.h $(LINK) $(LINK_HEADERS) hw/panel.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_panel_controls.c panel_controls.c control.c $(LINK)

$(BUILD)/test_start_check: tests/test_start_check.c tools/start_check.c \
		tools/start_check.h $(KISS_FFT) | $(BUILD)
	$(CC) $(CFLAGS) -Itools -o $@ tests/test_start_check.c tools/start_check.c \
		$(KISS_FFT) -lm

$(BUILD)/test_cw_text: tests/test_cw_text.c modems/cw_text.c modems/cw_text.h \
		modems/cw_keyer.h | $(BUILD)
	$(CC) $(CFLAGS) -Imodems -o $@ tests/test_cw_text.c modems/cw_text.c

$(BUILD)/test_settings: tests/test_settings.c settings.c settings.h control.c \
		control.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_settings.c settings.c control.c

$(BUILD)/test_panel_wifi: tests/test_panel_wifi.c panel_wifi.c panel_wifi.h \
		control.c control.h $(LINK) $(LINK_HEADERS) hw/panel.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_panel_wifi.c panel_wifi.c control.c $(LINK)

$(BUILD)/test_radio_clock: tests/test_radio_clock.c hw/radio_clock.c \
		hw/radio_clock.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_radio_clock.c hw/radio_clock.c

$(BUILD)/test_logbook: tests/test_logbook.c logbook.c logbook.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_logbook.c logbook.c -lsqlite3

$(BUILD)/test_logger: tests/test_logger.c logger.c logger.h control.c control.h \
		logbook.c logbook.h hw/radio_clock.c hw/radio_clock.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_logger.c logger.c control.c logbook.c \
		hw/radio_clock.c -lsqlite3

$(BUILD)/ft8_lib_%.o: modems/ft8_lib/%.c $(wildcard modems/ft8_lib/*.h) | $(BUILD)
	$(CC) -std=gnu11 -O2 -g -w -c -o $@ $<

$(BUILD)/test_ft8: tests/test_ft8.c modems/ft8_monitor.c modems/ft8_monitor.h \
		modems/ft8_signal.c modems/ft8_signal.h modems/ft8_callsign_hash.c \
		modems/ft8_callsign_hash.h $(FT8_LIB) $(KISS_FFT) | $(BUILD)
	$(CC) $(CFLAGS) -Imodems -o $@ tests/test_ft8.c modems/ft8_monitor.c \
		modems/ft8_signal.c modems/ft8_callsign_hash.c $(FT8_LIB) $(KISS_FFT) -lpthread -lm

$(BUILD)/test_messages: tests/test_messages.c messages.c messages.h \
		panel_messages.c panel_messages.h $(LINK) $(LINK_HEADERS) hw/panel.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_messages.c messages.c panel_messages.c $(LINK) \
		-lpthread

$(BUILD)/test_ft8_qso: tests/test_ft8_qso.c ft8_qso.c ft8_qso.h control.c control.h \
		messages.c messages.h logbook.c logbook.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_ft8_qso.c ft8_qso.c control.c messages.c logbook.c \
		-lsqlite3 -lpthread

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

# the desktop's window on the radio; it needs GTK 3, and is built apart
# from the rest because the radio has no need of it
gui: $(GUI)

$(GUI): clients/gtk/zbitx_gui.c
	$(CC) $(CFLAGS) $$(pkg-config --cflags gtk+-3.0) -o $@ clients/gtk/zbitx_gui.c \
		$$(pkg-config --libs gtk+-3.0)

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
	rm -rf $(BUILD) $(TOOLS) $(GUI)
