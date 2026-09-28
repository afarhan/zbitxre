// the display is 320 by 480 as it is made, and is used on its side, so the
// screen is 480 wide and 320 high. ili9488.cpp does the turning.
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
#define KNOB_COUNTS_IGNORED 1

//the pins to the display are driven as gently as they can be. Painting the
//display puts a hash in the receiver, and at 2 mA with slow edges it was
//heard some 3 dB lower than at 12 mA with sharp ones
#define DISPLAY_DRIVE_MA 2

#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES / 10 * (LV_COLOR_DEPTH / 8))

void ui_init();
void ui_slice();
void ui_screen_to_serial();

// the link to the radio
void i2c_link_init();
void i2c_link_slice();
void i2c_link_changed(int control, int32_t value);
void i2c_link_report();
void main_radio_says(int control, int32_t value);

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

// screens
void main_ui();
void main_knob_turned(int steps);
void main_knob_pressed();
void main_note(const char *text);

#ifndef FIELDS_H
#define FIELDS_H
#endif

#define FIELD_TEXT_MAX_LENGTH 128

#define FIELD_NUMBER 0
#define FIELD_BUTTON 1
#define FIELD_TOGGLE 2
#define FIELD_SELECTION 3
#define FIELD_TEXT 4
#define FIELD_STATIC 5
#define FIELD_CONSOLE 6
#define FIELD_KEY 7
#define FIELD_FREQ 8
#define FIELD_WATERFALL 9
#define FIELD_FT8 10
#define FIELD_LOGBOOK 11
#define FIELD_SMETER 12
#define FIELD_TITLE 13

struct field {
  int16_t type;
  uint16_t x;
  uint16_t y;
  uint16_t w;
  uint16_t h;
  uint16_t  color_scheme;
  const char *label;
  char value[FIELD_TEXT_MAX_LENGTH];
	char selection[FIELD_TEXT_MAX_LENGTH];
	void (*draw)(struct field *)=NULL;
  void *data;
  bool is_visible;
  bool redraw;
  bool update_to_radio;
	unsigned int last_user_change;
	//this is set whenever the value is changed
	//we compare this with the millis() at which
	//we receive the update from the radio
	//WIP	
	unsigned int last_update;
};