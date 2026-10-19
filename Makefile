# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# zbitxre
#
#   make          build the radio program here, and the tests into build/
#   make check    run the tests that need no radio
#   make radio    run the tests that need the radio (stop the radio program first)
#   make gui      build the desktop's window on the radio, here (needs GTK 3)
#   make remote   build the program that lets the owner reach the radio from afar
#   make panel    build the front panel's firmware
#   make panel-flash   put it on a Pico that is in its loader
#
#   ./sdr         the radio, run from a terminal; see tools/sdr.c
#   ./zbitx_gui   the desktop's window on the radio; see clients/gtk/zbitx_gui.c
#   ./zbitx_remote  the radio's way out to my.sbitx.net; see clients/remote/zbitx_remote.c
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
	hw/radio_clock.h hw/swr_bridge.h \
	$(LINK_HEADERS)
DSP_HEADERS = dsp/sdr.h modem.h control.h macros.h messages.h rtp_audio.h rtp_microphone.h rtp_voip.h modems/cw_keyer.h \
	modems/cw_text.h modems/modem_cw.h modems/ft8_monitor.h modems/ft8_signal.h modems/modem_ft8.h \
	modems/ft8_callsign_hash.h dsp/rade_codec.h
HW          = hw/i2c.c hw/si5351.c hw/gpio.c hw/board.c hw/audio.c hw/panel.c hw/key.c \
	hw/radio_clock.c hw/swr_bridge.c \
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
# RADE, FreeDV's digital voice, is rade_c's, in modems/rade_c, on the part
# of Opus in modems/opus_dnn; their READMEs say what they are. They are
# built as they are, each file on its own with its warnings off, and with
# include paths of their own: Opus has a kiss_fft.h that is not the
# radio's. dsp/rade_codec.c is the radio's one way in to them and is built
# with them; the rest of the radio sees only dsp/rade_codec.h. The weights
# of the networks are not in the repository, and RADE_WEIGHTS says so.
RADE_C_FILES = rade_api rade_enc rade_enc_data rade_dec rade_dec_data rade_dsp rade_ofdm \
	rade_bpf rade_acq rade_tx rade_rx rade_enc_v2 rade_enc_v2_data rade_dec_v2 \
	rade_dec_v2_data rade_sync rade_sync_data rade_v2_ofdm rade_tx_v2 rade_rx_v2
OPUS_CELT_FILES = celt_lpc kiss_fft pitch
OPUS_DNN_FILES = burg fargan fargan_data freq lpcnet_enc lpcnet_tables nnet nnet_default \
	parse_lpcnet_weights pitchdnn pitchdnn_data
OPUS_TOP_FILES = celt_fatal radae_vocoder
RADE_WEIGHTS = modems/rade_c/rade_enc_data.c modems/rade_c/rade_dec_data.c \
	modems/rade_c/rade_enc_v2_data.c modems/rade_c/rade_dec_v2_data.c \
	modems/opus_dnn/dnn/fargan_data.c
RADE_INCLUDES = -DHAVE_CONFIG_H -Imodems/opus_dnn -Imodems/opus_dnn/include \
	-Imodems/opus_dnn/celt -Imodems/opus_dnn/dnn -Imodems/rade_c
# on a 32-bit ARM the compiler has to be told to use NEON, without which the
# vocoder alone needs more than two of a Pi Zero 2 W's processors
ifeq ($(shell uname -m),armv7l)
RADE_ARM = -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard
endif
RADE_CC     = $(CC) -std=gnu11 -O2 -w $(RADE_ARM) $(RADE_INCLUDES)
RADE_LIB    = $(patsubst %,$(BUILD)/rade_c_%.o,$(RADE_C_FILES)) \
	$(patsubst %,$(BUILD)/opus_celt_%.o,$(OPUS_CELT_FILES)) \
	$(patsubst %,$(BUILD)/opus_dnn_%.o,$(OPUS_DNN_FILES)) \
	$(patsubst %,$(BUILD)/opus_top_%.o,$(OPUS_TOP_FILES)) \
	$(BUILD)/rade_codec.o
DSP         = core_sdr.c control.c macros.c messages.c rtp_audio.c rtp_microphone.c rtp_voip.c dsp/filter.c dsp/vfo.c $(KISS_FFT) \
	$(MODEMS) $(MODEM_LIST) $(FT8_LIB) $(RADE_LIB)
RADIO_LIBS  = -lasound -lpthread -lm

HOST_TESTS  = $(BUILD)/test_si5351_math $(BUILD)/test_am $(BUILD)/test_link \
	$(BUILD)/test_cw_keyer $(BUILD)/test_server \
	$(BUILD)/test_panel_controls $(BUILD)/test_start_check $(BUILD)/test_cw_text \
	$(BUILD)/test_settings $(BUILD)/test_panel_wifi $(BUILD)/test_radio_clock \
	$(BUILD)/test_logbook $(BUILD)/test_ft8 $(BUILD)/test_messages \
	$(BUILD)/test_ft8_qso $(BUILD)/test_cw_decoder $(BUILD)/test_logger \
	$(BUILD)/test_sbitxv3 $(BUILD)/test_sha256 $(BUILD)/test_rtp_audio \
	$(BUILD)/test_rtp_microphone $(BUILD)/test_rtp_voip $(BUILD)/test_telephone $(BUILD)/test_cat \
	$(BUILD)/test_rade_codec $(BUILD)/test_rade
RADIO_TESTS = $(BUILD)/test_si5351 $(BUILD)/test_board $(BUILD)/test_audio \
	$(BUILD)/test_panel
TOOLS       = sdr
GUI         = zbitx_gui
REMOTE      = zbitx_remote

# The front panel is an Arduino sketch for the Pico W. It is built with
# arduino-cli, which needs the rp2040 core and the lvgl library, version 9.3.0.
PANEL        = front_panel
PANEL_BUILD  = $(CURDIR)/$(BUILD)/$(PANEL)
# 1 MB for the sketch and 1 MB of filesystem, which updater.cpp keeps a new
# image in while the radio sends it; the touch calibration is past both
PANEL_BOARD  = rp2040:rp2040:rpipico:flash=2097152_1048576
# the firmware's version, which the panel tells the radio: the last commit
# that changed the panel
PANEL_VERSION := $(shell git log -1 --format=%h -- $(PANEL) 2>/dev/null)
PANEL_FLAGS  = -DLV_CONF_INCLUDE_SIMPLE -I$(CURDIR)/$(PANEL) -DPANEL_FIRMWARE=\"$(PANEL_VERSION)\"
PANEL_UF2    = $(PANEL_BUILD)/$(PANEL).ino.uf2
PANEL_DRIVE  = /media/$(USER)/RPI-RP2

.PHONY: all check radio clean panel panel-release panel-flash gui remote FORCE

all: $(HOST_TESTS) $(RADIO_TESTS) $(TOOLS) $(REMOTE) data/cw.mc

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
	$(BUILD)/test_sha256
	$(BUILD)/test_rtp_audio
	$(BUILD)/test_rtp_microphone
	$(BUILD)/test_rtp_voip
	$(BUILD)/test_telephone
	$(BUILD)/test_cat
	$(BUILD)/test_rade_codec
	$(BUILD)/test_rade

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
		panel_controls.c panel_wifi.c panel_wifi.h wifi.c wifi.h settings.c settings.h radio.h \
		panel_log.c panel_log.h logbook.c logbook.h \
		panel_messages.c panel_messages.h ft8_qso.c ft8_qso.h panel_cw.c panel_cw.h \
		logger.c logger.h server.h control.h meter.c meter.h \
		panel_controls.h $(DSP) $(HW) $(DSP_HEADERS) $(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -Itools -o $@ tools/sdr.c tools/start_check.c radio.c server.c \
		panel_controls.c panel_wifi.c wifi.c panel_log.c panel_messages.c ft8_qso.c panel_cw.c \
		logbook.c logger.c settings.c meter.c \
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

$(BUILD)/test_server: tests/test_server.c radio.c server.c settings.c logbook.c radio.h \
		server.h settings.h control.h logbook.h $(DSP) $(STAND_INS) $(DSP_HEADERS) \
		$(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_server.c radio.c server.c settings.c logbook.c $(DSP) \
		$(STAND_INS) -lpthread -lm -lsqlite3

# RADE1 and RADE2 through the whole radio, sent and received; it takes half a minute,
# giving the radio its blocks at a sound card's pace
$(BUILD)/test_rade: tests/test_rade.c radio.c server.c settings.c logbook.c radio.h \
		server.h settings.h control.h logbook.h $(DSP) $(STAND_INS) $(DSP_HEADERS) \
		$(HW_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_rade.c radio.c server.c settings.c logbook.c $(DSP) \
		$(STAND_INS) -lpthread -lm -lsqlite3

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

$(BUILD)/test_panel_wifi: tests/test_panel_wifi.c panel_wifi.c panel_wifi.h wifi.c wifi.h \
		control.c control.h $(LINK) $(LINK_HEADERS) hw/panel.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_panel_wifi.c panel_wifi.c wifi.c control.c $(LINK)

$(BUILD)/test_radio_clock: tests/test_radio_clock.c hw/radio_clock.c \
		hw/radio_clock.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_radio_clock.c hw/radio_clock.c

$(BUILD)/test_logbook: tests/test_logbook.c logbook.c logbook.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_logbook.c logbook.c -lsqlite3

$(BUILD)/test_logger: tests/test_logger.c logger.c logger.h control.c control.h \
		logbook.c logbook.h hw/radio_clock.c hw/radio_clock.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_logger.c logger.c control.c logbook.c \
		hw/radio_clock.c -lsqlite3

$(RADE_WEIGHTS):
	@echo "$@ is not here: the weights of RADE's networks are not kept in"
	@echo "the repository. Run setup/fetch_rade_weights.sh, once, to fetch them."
	@false

$(BUILD)/rade_c_%.o: modems/rade_c/%.c $(wildcard modems/rade_c/*.h) | $(BUILD)
	$(RADE_CC) -c -o $@ $<

$(BUILD)/opus_celt_%.o: modems/opus_dnn/celt/%.c | $(BUILD)
	$(RADE_CC) $(OPUS_CELT_FLAGS) -c -o $@ $<

# RADE version 1's pilot search is done by FFT, with Opus's, at a size Opus
# has no table for; with CUSTOM_MODES its kiss_fft.c can make the plan.
# Nothing else in this subset of Opus uses CUSTOM_MODES, and only that file
# is built with it. modems/rade_c/rade_acq.c says why
$(BUILD)/opus_celt_kiss_fft.o: OPUS_CELT_FLAGS = -DCUSTOM_MODES
# and it is made again when this file changes, so that one built before
# this, without the flag, is not kept
$(BUILD)/opus_celt_kiss_fft.o: Makefile

$(BUILD)/opus_dnn_%.o: modems/opus_dnn/dnn/%.c | $(BUILD)
	$(RADE_CC) -c -o $@ $<

$(BUILD)/opus_top_%.o: modems/opus_dnn/%.c modems/opus_dnn/config.h | $(BUILD)
	$(RADE_CC) -c -o $@ $<

$(BUILD)/rade_codec.o: dsp/rade_codec.c dsp/rade_codec.h | $(BUILD)
	$(RADE_CC) -g -Wall -Wextra -Idsp -c -o $@ dsp/rade_codec.c

$(BUILD)/test_rade_codec: tests/test_rade_codec.c dsp/rade_codec.h $(RADE_LIB) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_rade_codec.c $(RADE_LIB) -lm

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

# the window reads the sbitx's two knobs itself, with its own knob reader;
# of hw/ it has the pins, and the boards for the radio's name
# of clients/remote it has the keyed hash, to answer the service's
# challenge with when it reaches a radio through my.sbitx.net
GUI_PARTS = clients/gtk/knob.c clients/gtk/logbook_window.c clients/gtk/keyboard.c \
	wifi.c hw/gpio.c hw/board.c \
	clients/remote/sha256.c

$(GUI): clients/gtk/zbitx_gui.c $(GUI_PARTS) clients/gtk/knob.h \
		clients/gtk/logbook_window.h clients/gtk/keyboard.h wifi.h hw/gpio.h hw/board.h \
		clients/remote/sha256.h
	$(CC) $(CFLAGS) -Iclients/gtk -Iclients/remote $$(pkg-config --cflags gtk+-3.0) -o $@ clients/gtk/zbitx_gui.c \
		$(GUI_PARTS) $$(pkg-config --libs gtk+-3.0) -lpthread -lm

# the radio's way out to my.sbitx.net, a program of its own beside sdr;
# clients/remote/zbitx_remote.c says why. It needs nothing but the C library
remote: $(REMOTE)

REMOTE_PARTS = clients/remote/sha256.c clients/remote/telephone.c clients/remote/cat.c

$(REMOTE): clients/remote/zbitx_remote.c $(REMOTE_PARTS) clients/remote/sha256.h \
		clients/remote/telephone.h clients/remote/cat.h rtp_audio.h rtp_microphone.h
	$(CC) $(CFLAGS) -Iclients/remote -o $@ clients/remote/zbitx_remote.c $(REMOTE_PARTS) -lm

$(BUILD)/test_cat: tests/test_cat.c clients/remote/cat.c clients/remote/cat.h | $(BUILD)
	$(CC) $(CFLAGS) -Iclients/remote -o $@ tests/test_cat.c clients/remote/cat.c -lm

$(BUILD)/test_telephone: tests/test_telephone.c clients/remote/telephone.c \
		clients/remote/telephone.h | $(BUILD)
	$(CC) $(CFLAGS) -Iclients/remote -o $@ tests/test_telephone.c clients/remote/telephone.c -lm

$(BUILD)/test_rtp_audio: tests/test_rtp_audio.c rtp_audio.c rtp_audio.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_rtp_audio.c rtp_audio.c -lm

$(BUILD)/test_rtp_microphone: tests/test_rtp_microphone.c rtp_microphone.c rtp_microphone.h \
		rtp_voip.c rtp_voip.h rtp_audio.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_rtp_microphone.c rtp_microphone.c rtp_voip.c -lm

$(BUILD)/test_rtp_voip: tests/test_rtp_voip.c rtp_voip.c rtp_voip.h rtp_microphone.c \
		rtp_microphone.h rtp_audio.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_rtp_voip.c rtp_voip.c rtp_microphone.c -lm

$(BUILD)/test_sha256: tests/test_sha256.c clients/remote/sha256.c clients/remote/sha256.h \
		| $(BUILD)
	$(CC) $(CFLAGS) -Iclients/remote -o $@ tests/test_sha256.c clients/remote/sha256.c

panel:
	arduino-cli compile --fqbn $(PANEL_BOARD) --build-path $(PANEL_BUILD) \
		--build-property "compiler.c.extra_flags=$(PANEL_FLAGS)" \
		--build-property "compiler.cpp.extra_flags=$(PANEL_FLAGS)" \
		--build-property "compiler.S.extra_flags=$(PANEL_FLAGS)" \
		$(PANEL)

# gives the panel new firmware over I2C, from the radio: tools/panel_update.c
panel_update: tools/panel_update.c tools/md5.c tools/md5.h hw/i2c.c hw/i2c.h $(LINK) $(LINK_HEADERS)
	$(CC) $(CFLAGS) -Itools -o $@ tools/panel_update.c tools/md5.c hw/i2c.c $(LINK)

# the firmware as the radio gives it to the panel, compressed; the panel's
# loader takes it so
$(PANEL_BUILD)/$(PANEL).ino.bin.gz: FORCE
	gzip -9 -k -f -n $(PANEL_BUILD)/$(PANEL).ino.bin

# what a release carries for the zbitx's panel, which owners cannot build:
# the firmware gzipped for I2C (setup/panel_firmware.sh), the same as a .uf2
# for the USB cable, and the version the panel will say it has. Out of the
# panel's folder, so that committing it does not change that version
panel-release: panel $(PANEL_BUILD)/$(PANEL).ino.bin.gz
	mkdir -p release/panel
	cp $(PANEL_BUILD)/$(PANEL).ino.bin.gz release/panel/front_panel.bin.gz
	cp $(PANEL_UF2) release/panel/front_panel.uf2
	echo $(PANEL_VERSION) > release/panel/VERSION

panel-flash: panel
	test -d $(PANEL_DRIVE)
	cp $(PANEL_UF2) $(PANEL_DRIVE)/
	sync

clean:
	rm -rf $(BUILD) $(TOOLS) $(GUI) panel_update
