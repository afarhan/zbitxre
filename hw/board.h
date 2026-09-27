// board.h — the zbitx board: what each of the Pi's pins does. See board.c.

#pragma once

#include <stdint.h>

#define BOARD_PIN_PTT          4
#define BOARD_PIN_KEY          5
#define BOARD_PIN_RECEIVER     15
#define BOARD_PIN_TRANSMITTER  23
#define BOARD_PIN_FILTER_A     24
#define BOARD_PIN_FILTER_B     25
#define BOARD_PIN_FILTER_C     8
#define BOARD_PIN_FILTER_D     7

#define BOARD_FILTERS          4
#define BOARD_HIGHEST_HZ       30000000u

int board_open(void);
void board_close(void);
void board_receiver_power(int on);
void board_transmitter_power(int on);
int board_filter_pin_for(uint32_t hz);
void board_select_filter(uint32_t hz);
void board_release_filters(void);
int board_ptt_closed(void);
int board_key_closed(void);
