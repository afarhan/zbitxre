#include <lvgl.h>
#include <Arduino.h>
#include <pico/bootrom.h>
#include "ili9488.h"
#include "touch.h"
#include "testcard.h"
#include "waterfall.h"
#include <EEPROM.h>
#include "ui.h"

bool core1_separate_stack = true;
extern volatile lv_obj_t *coverflow_screen;

bool mouse_down = false;
uint8_t encoder_state = 0;
boolean encoder_switch = false;
unsigned int wheel_count = 0;
int wheel_move = 0;

/*LVGL draw into this buffer, 1/10 screen size usually works well. The size is in bytes*/
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

/* user interface */
/* LVGL calls it when a rendered image needs to copied to the display*/
static bool copying_to_serial = false;
//how much LVGL has painted, to tell how much of the screen a change costs
static unsigned long flushes;
static unsigned long pixels_flushed;

void my_disp_flush( lv_display_t *disp, const lv_area_t *area, uint8_t * px_map){
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    flushes++;
    pixels_flushed += (unsigned long)w * h;
    if (copying_to_serial){
      Serial.printf("AREA %d %d %d %d\n", (int)area->x1, (int)area->y1, (int)w, (int)h);
      Serial.write(px_map, (size_t)w * h * sizeof(uint16_t));
    }
    ili_set_window(area->x1, area->y1, w, h);
    ili_push_pixels((const uint16_t *)px_map, (uint32_t)w * h);
    lv_display_flush_ready(disp);
}

int enc_state(){
  return  (digitalRead(ENC_A)? 1:0) + (digitalRead(ENC_B) ? 2:0);
}

void on_enc(){

  uint8_t encoder_now = enc_state();
	if (encoder_now == encoder_state)
		return;

	if (enc_state() != encoder_now)
		return;
  
  if ((encoder_state == 0 && encoder_now == 1) 
    || (encoder_state == 1 && encoder_now == 3) 
    || (encoder_state == 3 && encoder_now == 2)
    || (encoder_state == 2 && encoder_now == 0)) {
    	wheel_move--;
			wheel_count++;
	}
  else if ((encoder_state == 0 && encoder_now == 2)
    || (encoder_state == 2 && encoder_now == 3)
    || (encoder_state == 3 && encoder_now == 1)
    || (encoder_state == 1 && encoder_now == 0)){
      wheel_move++;
			wheel_count++;
	}
  encoder_state = encoder_now;    
  //Serial.printf("Encoder %d\n", encoder_state);
}

/*Read the touchpad*/
void on_touch( lv_indev_t * indev, lv_indev_data_t * data )
{
    //For example  ("my_..." functions needs to be implemented by you)
    uint16_t x, y;
    bool touched = touch_read( &x, &y );

    if(!touched) {
        data->state = LV_INDEV_STATE_RELEASED;
    } else {
        Serial.printf("touch %u, %u\n", x, y);
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } 
}

/*use Arduinos millis() as tick source*/
static uint32_t lvgl_tick(void){
  return millis();
}

static void ta_event_cb(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * ta = (lv_obj_t *)lv_event_get_target((lv_event_t *)e);
  lv_obj_t * kb = (lv_obj_t *)lv_event_get_user_data((lv_event_t *)e);
  if(code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }

 if(code == LV_EVENT_DEFOCUSED) {
   lv_keyboard_set_textarea(kb, NULL);
   lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
}


static uint32_t btn_cnt = 1;

static lv_obj_t *settings;
static lv_obj_t *settingBtn;
static lv_obj_t *settingCloseBtn;
static lv_obj_t *settingWiFiSwitch;
static lv_obj_t *wfList;
static lv_obj_t *settinglabel;

static void btn_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
	if (code == LV_EVENT_CLICKED){
		if (btn == settingCloseBtn){
      lv_obj_add_flag(settings, LV_OBJ_FLAG_HIDDEN);
		}
	}
}

static void buildSettings() {
  settings = lv_obj_create(lv_scr_act());
  //lv_obj_add_style(settings, &border_style, 0);
  lv_obj_set_size(settings, SCREEN_WIDTH - 100, SCREEN_HEIGHT - 40);
  lv_obj_align(settings, LV_ALIGN_TOP_RIGHT, -20, 20);

  settinglabel = lv_label_create(settings);
  lv_label_set_text(settinglabel, "Settings " LV_SYMBOL_SETTINGS);
  lv_obj_align(settinglabel, LV_ALIGN_TOP_LEFT, 0, 0);

  settingCloseBtn = lv_btn_create(settings);
  lv_obj_set_size(settingCloseBtn, 30, 30);
  lv_obj_align(settingCloseBtn, LV_ALIGN_TOP_RIGHT, 0, -10);
  lv_obj_add_event_cb(settingCloseBtn, btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_t *btnSymbol = lv_label_create(settingCloseBtn);
  lv_label_set_text(btnSymbol, LV_SYMBOL_CLOSE);
  lv_obj_center(btnSymbol);

  settingWiFiSwitch = lv_switch_create(settings);
  //lv_obj_add_event_cb(settingWiFiSwitch, btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_align_to(settingWiFiSwitch, settinglabel, LV_ALIGN_TOP_RIGHT, 60, -10);

  wfList = lv_list_create(settings);
  lv_obj_set_size(wfList, SCREEN_WIDTH - 140, 210);
  lv_obj_align_to(wfList, settinglabel, LV_ALIGN_TOP_LEFT, 0, 30);
}

void ui_init(){
	uint16_t calibration_data[10];

  Serial.println("Starting core1");
  if (block.magic == 0x00C0FFEE)
    return;

  uint16_t x, y;
	pinMode(ENC_S, INPUT_PULLUP);
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);

  //hold the encoder down while the panel powers up, and the Pico
  //goes to its loader, ready for new firmware over USB
  delay(PULLUPS_SETTLE_MS);
  if (digitalRead(ENC_S) == ENC_PRESSED)
    reset_usb_boot(0,0);

  encoder_state = digitalRead(ENC_A) + (2 * digitalRead(ENC_B));

	attachInterrupt(ENC_A, on_enc, CHANGE);
	attachInterrupt(ENC_B, on_enc, CHANGE);

  block_read();
  ili_init();
  ili_set_drive(DISPLAY_DRIVE_MA, false);
  touch_init();
  Serial.printf("the display's port runs at %u Hz, from a clock of %u Hz\n", ili_spi_hz, ili_peri_hz);
  testcard_show();
    
  if (touch_read(&x, &y)){
    Serial.printf("touched at %u, %u as the panel started\n", x, y);
    while(touch_read(&x, &y))
      delay(100);
    delay(200);
    Serial.println("Start of calibration");
    touch_calibrate(block.calibration_data);
    Serial.println("End of calibration");
    block_write();
    Serial.println("Block updated");
  }
  touch_set_calibration(block.calibration_data);
  
  lv_init();
  lv_tick_set_cb(lvgl_tick); //set the update ticker

  /*LVGL draws a part of the screen at a time into draw_buf, and my_disp_flush sends it to the display*/
  lv_display_t * disp;
  disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

  // touch pad
  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); /*Touchpad should have POINTER type*/
  lv_indev_set_read_cb(indev, on_touch);

  main_ui();
/*
  if (digitalRead(ENC_S) == LOW)
    userid_dialog();
  else if (!block.ap_list[0].ssid[0])
	  wifi_open();
  else
	  coverflow();
*/
}

/* how much LVGL has painted since this was last asked */
void ui_report(){
  lv_mem_monitor_t memory;

  Serial.printf("screen: painted %lu times, %lu pixels, since the last report\n",
    flushes, pixels_flushed);
  flushes = 0;
  pixels_flushed = 0;
  lv_mem_monitor(&memory);
  Serial.printf("lvgl memory: %lu of %lu bytes used, %lu free in one piece at most\n",
    (unsigned long)(memory.total_size - memory.free_size), (unsigned long)memory.total_size,
    (unsigned long)memory.free_biggest_size);
}

/* sends the whole screen, as LVGL draws it, to whoever is on the serial port.
   tools/panel_screen.py asks for it and makes a picture of it */
void ui_screen_to_serial(){
  Serial.printf("SCREEN %d %d\n", SCREEN_WIDTH, SCREEN_HEIGHT);
  copying_to_serial = true;
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(NULL);
  copying_to_serial = false;
  //the graph and the waterfall are not LVGL's, and are copied after it, when
  //they are on the display: not under another screen, nor in FT8, where the
  //list of messages is in their place
  if (!screens_is_open() && waterfall_is_painting())
    waterfall_to_serial();
  Serial.printf("END\n");
  Serial.flush();
}

/* the knob is read here and not by LVGL, because the panel has its own way
   with a knob: main.cpp says what turning, a click and a long press do.
   A click is a press let go before KNOB_HELD_MS; a press still down then is
   a long press, taken as soon as that time has passed. Every step the knob
   has made since the last look is handed over, so that tuning is as fast
   as the hand */
static unsigned long knob_down_since;
static bool knob_was_held;

void knob_slice(){
	bool is_down = digitalRead(ENC_S) == ENC_PRESSED;
	int steps;

	if (is_down && !encoder_switch){
		encoder_switch = true;
		knob_was_held = false;
		knob_down_since = millis();
	}
	if (is_down && !knob_was_held && millis() - knob_down_since >= KNOB_HELD_MS){
		knob_was_held = true;
		main_knob_held();
	}
	if (!is_down && encoder_switch){
		encoder_switch = false;
		if (!knob_was_held)
			main_knob_clicked();
	}

	noInterrupts();
	steps = wheel_move / KNOB_COUNTS_A_STEP;
	wheel_move -= steps * KNOB_COUNTS_A_STEP;
	interrupts();
	if (steps != 0)
		main_knob_turned(steps);
}

void ui_slice(){
  knob_slice();
  main_set_aside_slice();
  lv_timer_handler(); /* let the GUI do its work */
}
