#pragma once
//
// ili9488.h — a minimal ILI9488 driver for the text engine. No TFT_eSPI, no
// Adafruit_GFX, no LVGL: just SPI bring-up, an address window, and a fast bulk
// pixel push.
//
// THE ONE FACT THAT SHAPES EVERYTHING: the ILI9488 over SPI is an 18-bit panel.
// It takes 3 bytes per pixel (RGB666) over SPI, NOT RGB565. So a full
// 480x320 frame is 460,800 bytes; at 66 MHz that is ~56 ms and full-frame
// repaints are off the table. One 480x24 text line is ~34.5 KB, ~4.2 ms.
// That asymmetry is why the text engine composes a line and blits it once.
//
// The register sequences and the SPI fast paths here were lifted from TFT_eSPI
// after identifying which of its many #ifdef branches actually compile for this
// board (probing with #error, since the library's nesting makes reading it
// unreliable). The live ones were: the generic setWindow's RP2040 arm,
// SPI_18BIT pushPixels, and TFT_Drivers/ILI9488_Init.h.
//
#include <stdint.h>

#define ILI_W 480          // landscape (rotation 3 on the zbitx)
#define ILI_H 320


// Bring up SPI1 + GPIO, reset, run the init sequence, set landscape rotation.
void ili_init(void);

// The touch controller shares the display's SPI port. ili_release() lets go
// of the display so that something else can use the port, and ili_select()
// takes the display back and puts the port's speed back. Nothing may be sent
// to the display between the two.
void ili_release(void);
void ili_select(void);

// Actual negotiated SPI clock + peripheral clock, valid after ili_init().
extern uint32_t ili_spi_hz, ili_peri_hz;

// Set the drawing rectangle. Every subsequent pixel push fills it left-to-right,
// top-to-bottom, and the panel wraps within the window on its own.
void ili_set_window(int x, int y, int w, int h);

// Fill the current window with one colour, n pixels' worth.
void ili_fill_run(uint16_t rgb565, uint32_t n);

// Push n RGB565 pixels into the current window (expanded to RGB666 on the way).
void ili_push_pixels(const uint16_t *px, uint32_t n);

// Convenience: set the window and fill it.
void ili_fill_rect(int x, int y, int w, int h, uint16_t rgb565);

// Push a packed 4bpp COVERAGE band (the text engine's compose format) as a
// blend between bg and fg. `cov` is row-major, `stride` bytes per row, two
// pixels per byte (high nibble = even x). This is the hot path: the 16 possible
// coverage values are pre-blended into a lookup table, so per pixel it is one
// nibble extract and three table-driven byte writes — no arithmetic.
void ili_push_cov4(int x, int y, int w, int h,
                   const uint8_t *cov, int stride,
                   uint16_t fg, uint16_t bg);

// rgb() lives in text_engine.h: it builds a colour, it does not touch the panel,
// and the layers above must be able to name a colour without including this.
