// the display is 320 by 480 as it is made, and is used on its side, so the
// screen is 480 wide and 320 high. ili9488.cpp does the turning.
#include "link.h"

#define TFT_HOR_RES   320
#define TFT_VER_RES   480
#define SCREEN_WIDTH  TFT_VER_RES
#define SCREEN_HEIGHT TFT_HOR_RES

//Some hardware definitions/pins
#define ENC_S 1
#define ENC_A 2
#define ENC_B 3

//colours, as the display takes them: 5 bits of red, 6 of green, 5 of blue
#define TFT_BLACK       0x0000
#define TFT_WHITE       0xFFFF
#define TFT_RED         0xF800
#define TFT_BLUE        0x001F
#define TFT_ORANGE      0xFD20
#define TFT_DARKGREEN   0x03E0

//the encoder's switch and the two inputs from the knob are low when closed
#define ENC_PRESSED LOW
#define PULLUPS_SETTLE_MS 10
//the knob gives two counts to a click of its detent, and a press held this
//long is a long press
#define KNOB_COUNTS_A_STEP 2
#define KNOB_HELD_MS 600

//the pins to the display are driven as gently as they can be. Painting the
//display puts a hash in the receiver, and at 2 mA with slow edges it was
//heard some 3 dB lower than at 12 mA with sharp ones
#define DISPLAY_DRIVE_MA 2

#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES / 10 * (LV_COLOR_DEPTH / 8))

void ui_init();
void ui_slice();
void ui_screen_to_serial();
void ui_report();

// the link to the radio
void i2c_link_init();
void i2c_link_slice();
void i2c_link_changed(int control, int32_t value);
void i2c_link_report();
void main_radio_says(int control, int32_t value);
void main_set_aside_slice();

// the SWR bridge and the battery
void meter_init();
void meter_slice();
void meter_print();
void main_meter(const char *text);

/* saved block of user data*/
extern struct saved block;
bool block_read();
void block_write();
void block_dump();

// this is stored in the last 4K of the flash, and it is laid out as the
// panels in use already have it, unpacked, so that a panel keeps its access
// points and its touch calibration when this firmware is put on it
#define MAX_APS 5
struct ap {
	char ssid[32];
	char key[32];
};

struct saved {
	uint32_t magic;
	uint64_t my_id;
	uint16_t calibration_data[10];
	struct ap ap_list[MAX_APS];	
};

// the main screen
void main_ui();
void main_controls_described();
void main_knob_turned(int steps);
void main_knob_clicked();
void main_knob_held();
void main_select(const char *name);
void main_status(const char *text);
void main_qso(const char *text);
void main_cw_text(const char *text);
void main_hide();
void main_radio_says_text(int number, const char *text);

// the list of copied messages, in FT8
// the list runs from where the graph begins to the screen's foot
#define SCREEN_BOTTOM 320
void messages_create();
void messages_part(const uint8_t *bytes, int count);
void messages_slice();
void messages_set_shown(bool shown);
void messages_set_selected(bool selected);
void messages_scroll(int steps);
void messages_show_newest();
bool messages_answer_marked();

// the link, for text
void i2c_link_send_text(int number, const char *text);
bool i2c_link_is_sending_text();

// the other screens
void screens_open_settings();
void screens_open_log();
void screens_open_cw_keys();
bool screens_cw_keys_up();
void screens_cw_keys_hide();
void screens_cw_stopped();
void screens_cw_sent(const char *text);
void screens_log_part(const uint8_t *bytes, int count);
void screens_close();
bool screens_is_open();
void main_paint_when_free();
int main_selection_moves(int notches);
void screens_radio_said();
void screens_set_text(char *control_and_text);
void screens_select(const char *name);
void screens_wifi_listed(const struct link_wifi_point *points, int count);
void screens_slice();
void screens_knob_turned(int steps);
void screens_knob_clicked();
void screens_status(const char *text);
void screens_update_status(const char *text);
void main_note(const char *text);
void main_note_large(const char *text);
