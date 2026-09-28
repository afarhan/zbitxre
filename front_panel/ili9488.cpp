// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// This is the driver of ltp1_text_ui, which is fully debugged, and it is
// changed for the zbitx's panel in four places only, each marked ZBITX below:
//
//   1. The display is mounted the other way up, so MADCTL is 0xE8 and not
//      0x28. It is what the zbitx's shipping firmware calls rotation 3.
//   2. There is no backlight pin. The zbitx's backlight is always on.
//   3. The touch controller is on the same SPI port, so MISO is set up too.
//   4. The display's chip select is not held low for ever. ili_release()
//      lets go of it so that touch.cpp can talk to the touch controller, and
//      ili_select() takes it back.
//
// The Pico W of the zbitx has a peripheral clock of 125 MHz, not 150, so the
// rates the port can run at are 62.5, 31.25 and 20.8 MHz. Asking for 37.5
// gives 31.25. ili_spi_hz says what was got.
//
// ili9488.cpp — see ili9488.h. Talks to the SPI hardware registers directly
// (spi_get_hw()->dr) rather than through the Arduino SPI class: that is what
// TFT_eSPI's RP2040 path does, and the per-call overhead of the class API is
// significant when pushing hundreds of thousands of bytes per frame.

#include <Arduino.h>
#include <hardware/spi.h>
#include <hardware/gpio.h>
#include <hardware/clocks.h>
#include "ili9488.h"

// Pins — same wiring as the xyfr ILI9488 build (SPI1).
#define PIN_SCK   10
#define PIN_MOSI  11
#define PIN_MISO  12          // ZBITX: the touch controller answers on this
#define PIN_DC     4
#define PIN_RST    5
#define PIN_CS    13

#define SPI_PORT  spi1
// clk_peri is 150 MHz and the divider is clk_peri/(CPSDVSR*(1+SCR)) with CPSDVSR
// EVEN, so the ladder is 75 / 37.5 / 25 / 18.75 MHz — nothing in between. Asking
// for 66 silently lands on 37.5, which is what the xyfr build has always run at.
//
// 75 MHz was TRIED and FAILS: rectangles come out with dropped rows/columns, as
// if the paint flaked off. The ILI9488 spec is a 66 ns write cycle (~15 MHz), so
// 37.5 is already a 2.5x overclock that happens to latch; 75 is past the glass.
// Do not raise this without re-testing the random-rectangle screen.
#define SPI_HZ    37500000u

#define SPI_HW    spi_get_hw(SPI_PORT)

// Reported by the harness: what the SPI peripheral ACTUALLY negotiated, which
// is not necessarily what was asked for (the divider is prescale*postdiv off
// clk_peri, so most requested rates round down).
uint32_t ili_spi_hz  = 0;
uint32_t ili_peri_hz = 0;

// Wait for the shifter to drain. Needed before flipping DC or the data-size
// register, or the change lands mid-byte.
static inline void spi_wait_idle(void) { while (SPI_HW->sr & SPI_SSPSR_BSY_BITS) {} }
static inline void spi_put(uint8_t b)  { while (!spi_is_writable(SPI_PORT)) {} SPI_HW->dr = b; }

static inline void dc_cmd(void)  { spi_wait_idle(); gpio_put(PIN_DC, 0); }
static inline void dc_data(void) { spi_wait_idle(); gpio_put(PIN_DC, 1); }

// Switch the peripheral between 8- and 16-bit frames. The 16-bit mode is how
// three RGB666 bytes get pushed as 1.5 words instead of 3 byte-writes — it
// halves the register traffic, which is the whole trick in TFT_eSPI's 18-bit
// fast path.
static inline void spi_bits(int n) {
	spi_wait_idle();
	hw_write_masked(&SPI_HW->cr0, (uint32_t)(n - 1) << SPI_SSPCR0_DSS_LSB, SPI_SSPCR0_DSS_BITS);
}

static void wr_cmd(uint8_t c)  { dc_cmd();  spi_put(c); }
static void wr_data(uint8_t d) { dc_data(); spi_put(d); }

// ---- init -------------------------------------------------------------------
// Sequence from TFT_Drivers/ILI9488_Init.h, with 0x3A = 0x66 (18-bit) since we
// are on SPI, and MADCTL set for landscape rather than the header's portrait.

static const uint8_t init_gamma_pos[] = {
	0x00,0x03,0x09,0x08,0x16,0x0A,0x3F,0x78,0x4C,0x09,0x0A,0x08,0x16,0x1A,0x0F };
static const uint8_t init_gamma_neg[] = {
	0x00,0x16,0x19,0x03,0x0F,0x05,0x32,0x45,0x46,0x04,0x0E,0x0D,0x35,0x37,0x0F };

static void wr_cmd_n(uint8_t c, const uint8_t *d, int n) {
	wr_cmd(c);
	dc_data();
	for (int i = 0; i < n; i++)
		spi_put(d[i]);
}

void ili_init(void) {
	gpio_init(PIN_DC);  gpio_set_dir(PIN_DC, GPIO_OUT);
	gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);
	gpio_init(PIN_CS);  gpio_set_dir(PIN_CS, GPIO_OUT);
	gpio_put(PIN_CS, 1);                    // ZBITX: no backlight pin

	ili_spi_hz  = spi_init(SPI_PORT, SPI_HZ);
	ili_peri_hz = clock_get_hz(clk_peri);
	gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
	gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
	gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);   // ZBITX
	spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

	gpio_put(PIN_RST, 0); delay(20);
	gpio_put(PIN_RST, 1); delay(150);

	gpio_put(PIN_CS, 0);                    // held low, but see ili_release()

	wr_cmd_n(0xE0, init_gamma_pos, sizeof init_gamma_pos);
	wr_cmd_n(0xE1, init_gamma_neg, sizeof init_gamma_neg);
	{ const uint8_t d[] = {0x17,0x15}; wr_cmd_n(0xC0, d, 2); }   // Power Control 1
	{ const uint8_t d[] = {0x41};      wr_cmd_n(0xC1, d, 1); }   // Power Control 2
	{ const uint8_t d[] = {0x00,0x12,0x80}; wr_cmd_n(0xC5, d, 3); } // VCOM

	// MADCTL: landscape. ZBITX: 0xE8 = MX|MY|MV|BGR — the rotation-3 value
	// from ILI9488_Rotation.h, the display being mounted the other way up.
	{ const uint8_t d[] = {0xE8}; wr_cmd_n(0x36, d, 1); }
	{ const uint8_t d[] = {0x66}; wr_cmd_n(0x3A, d, 1); }        // 18-bit, SPI
	{ const uint8_t d[] = {0x00}; wr_cmd_n(0xB0, d, 1); }
	{ const uint8_t d[] = {0xA0}; wr_cmd_n(0xB1, d, 1); }
	{ const uint8_t d[] = {0x02}; wr_cmd_n(0xB4, d, 1); }
	{ const uint8_t d[] = {0x02,0x02,0x3B}; wr_cmd_n(0xB6, d, 3); }
	{ const uint8_t d[] = {0xC6}; wr_cmd_n(0xB7, d, 1); }
	{ const uint8_t d[] = {0xA9,0x51,0x2C,0x82}; wr_cmd_n(0xF7, d, 4); }

	wr_cmd(0x11); delay(120);               // sleep out
	wr_cmd(0x29); delay(25);                // display on
	spi_wait_idle();
}

// ---- ZBITX: sharing the port with the touch controller ----------------------

void ili_release(void) {
	spi_wait_idle();
	gpio_put(PIN_CS, 1);
}

void ili_select(void) {
	spi_wait_idle();
	spi_set_baudrate(SPI_PORT, SPI_HZ);
	gpio_put(PIN_CS, 0);
}

// ---- address window ---------------------------------------------------------

void ili_set_window(int x, int y, int w, int h) {
	int x1 = x + w - 1, y1 = y + h - 1;
	dc_cmd();  spi_put(0x2A);                                   // CASET
	dc_data(); spi_put(x >> 8); spi_put(x); spi_put(x1 >> 8); spi_put(x1);
	dc_cmd();  spi_put(0x2B);                                   // PASET
	dc_data(); spi_put(y >> 8); spi_put(y); spi_put(y1 >> 8); spi_put(y1);
	dc_cmd();  spi_put(0x2C);                                   // RAMWR
	dc_data();
}

// ---- pixel pushes -----------------------------------------------------------

void ili_fill_run(uint16_t c, uint32_t n) {
	uint8_t r = (c & 0xF800) >> 8, g = (c & 0x07E0) >> 3, b = (c & 0x001F) << 3;
	// >32 px: pack the 3-byte pattern into 16-bit frames. Three pixels' nine
	// bytes tile as rg|br|gb across 1.5 words, so two pixels cost three writes
	// instead of six.
	if (n > 32) {
		uint16_t rg = (uint16_t)(r << 8 | g), br = (uint16_t)(b << 8 | r), gb = (uint16_t)(g << 8 | b);
		spi_bits(16);
		while (n > 1) {
			while (!spi_is_writable(SPI_PORT)) {} SPI_HW->dr = rg;
			while (!spi_is_writable(SPI_PORT)) {} SPI_HW->dr = br;
			while (!spi_is_writable(SPI_PORT)) {} SPI_HW->dr = gb;
			n -= 2;
		}
		spi_bits(8);
	}
	while (n--) {
		spi_put(r);
		spi_put(g);
		spi_put(b);
	}
}

void ili_push_pixels(const uint16_t *px, uint32_t n) {
	while (n--) {
		uint16_t c = *px++;
		spi_put((c & 0xF800) >> 8);
		spi_put((c & 0x07E0) >> 3);
		spi_put((c & 0x001F) << 3);
	}
}

void ili_fill_rect(int x, int y, int w, int h, uint16_t c) {
	if (w <= 0 || h <= 0)
		return;
	ili_set_window(x, y, w, h);
	ili_fill_run(c, (uint32_t)w * h);
	spi_wait_idle();
}

// ---- 4bpp coverage band -----------------------------------------------------

void ili_push_cov4(int x, int y, int w, int h,
                   const uint8_t *cov, int stride,
                   uint16_t fg, uint16_t bg) {
	if (w <= 0 || h <= 0)
		return;

	// Pre-blend the 16 coverage levels once per call. Per pixel this turns into
	// three loads and three register writes with no arithmetic at all.
	uint8_t lut[16][3];
	int fr = (fg & 0xF800) >> 8, fgn = (fg & 0x07E0) >> 3, fb = (fg & 0x001F) << 3;
	int br = (bg & 0xF800) >> 8, bgn = (bg & 0x07E0) >> 3, bb = (bg & 0x001F) << 3;
	for (int i = 0; i < 16; i++) {
		lut[i][0] = (uint8_t)(br  + (fr  - br)  * i / 15);
		lut[i][1] = (uint8_t)(bgn + (fgn - bgn) * i / 15);
		lut[i][2] = (uint8_t)(bb  + (fb  - bb)  * i / 15);
	}

	ili_set_window(x, y, w, h);
	for (int row = 0; row < h; row++) {
		const uint8_t *p = cov + (size_t)row * stride;
		for (int cx = 0; cx < w; cx++) {
			uint8_t v = (cx & 1) ? (p[cx >> 1] & 0x0F) : (p[cx >> 1] >> 4);
			const uint8_t *t = lut[v];
			spi_put(t[0]); spi_put(t[1]); spi_put(t[2]);
		}
	}
	spi_wait_idle();
}
