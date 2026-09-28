#include <Arduino.h>
#include <EEPROM.h>
#include <lvgl.h>
#include <WiFi.h>
#include "ui.h"

static lv_obj_t *wifi_label = NULL;
static lv_obj_t *wifi_btn = NULL;

lv_obj_t *main_screen = NULL;

struct field main_list[] = {

  //the first eight fields are always visible
  {FIELD_BUTTON, 0, 0, 44, 44,  TFT_ORANGE, "MENU", "" },  
  {FIELD_SELECTION, 48, 0, 46, 46,  TFT_BLACK, "MODE", "USB", "USB\nLSB\nCW/CWR\nFT8\nAMDIGI\n2TONE"},
  {FIELD_NUMBER, 96, 0, 46, 46,  TFT_BLACK, "DRIVE", "100", "0\n100\n5"},   
  {FIELD_NUMBER, 144, 0, 46, 46,  TFT_BLACK, "IF", "40", "0\n100\n1"},
  {FIELD_SELECTION, 192, 0, 46, 46,  TFT_BLACK, "RIT", "OFF", "ON\nOFF"},
  {FIELD_FREQ, 240, 0, 190, 46,  TFT_BLACK, "FREQ", "14074000", "500000\n30000000\n1"}, 
  {FIELD_NUMBER, 432, 0, 46, 46,  TFT_BLACK, "AUDIO", "95", "0\n100\n1"},

  {FIELD_SELECTION, 288, 48, 60, 60,  TFT_BLACK, "SPAN", "25K", "25K\n10K\n6K\n2.5K"},
  //{FIELD_SELECTION, 336, 48, 48, 48,  TFT_BLACK, "RIT", "OFF", "ON/OFF"},  
  {FIELD_NUMBER, 336, 48, 46, 46,  TFT_BLACK, "BW", "2200", "50\n5000\n50"},
  {FIELD_SELECTION, 384, 48, 46, 46,  TFT_BLACK, "STEP", "1K", "10K\n1K\n500H\n100H\n10H"},
  {FIELD_BUTTON, 432, 48, 46, 46,  TFT_BLUE, "SET", ""},
 
   //LOGGER FIELDS
  {FIELD_BUTTON, 0, 48, 46, 46,  TFT_DARKGREEN, "OPEN"},
  {FIELD_BUTTON, 48, 48, 46, 46,  TFT_DARKGREEN, "WIPE"},
  {FIELD_BUTTON, 240, 48, 46, 46,  TFT_DARKGREEN, "SAVE"},
  {FIELD_TEXT, 96,48, 94, 22, TFT_BLACK, "CALL", "", "0,10"},
  {FIELD_TEXT, 192, 48, 46, 22, TFT_BLACK, "EXCH", "", "0,10"},
  {FIELD_TEXT, 96, 72, 46, 22, TFT_BLACK, "RECV", "", "0,10"},
  {FIELD_TEXT, 144, 72, 46, 22, TFT_BLACK, "SENT", "", "0,10"},
  {FIELD_TEXT, 192, 72, 46, 22, TFT_BLACK, "NR", "", "0,10"},
	
  {-1}
};

lv_obj_t *create_button(lv_obj_t *mom,  struct field *f){
  lv_obj_t *w, *label;

  static lv_style_t style;
  lv_style_init(&style);
  lv_style_set_radius(&style, 3);
  lv_style_set_pad_top(&style, 2);
  lv_style_set_pad_left(&style, 2);

  w = lv_btn_create(lv_scr_act());
  lv_obj_add_flag(w, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_size(w, f->w, f->h);
  lv_obj_add_style(w, &style, 0);
  lv_obj_align(w, LV_ALIGN_TOP_LEFT, f->x, f->y);

  label = lv_label_create(w);
  lv_label_set_text(label, f->label);
  lv_obj_align(label, LV_ALIGN_CENTER, 5, 5);
		  
  return w;
}

lv_obj_t *create_selection(lv_obj_t *mom,  struct field *f){
  lv_obj_t *base, *dd, *label;
  char list[1000];

  static lv_style_t style;

  lv_style_init(&style);
  lv_style_set_radius(&style, 3);
  lv_style_set_pad_top(&style, 0);
  lv_style_set_pad_left(&style, 0);
  lv_style_set_bg_color(&style, lv_color_hex(0x2F0000));

  base = lv_obj_create(lv_screen_active());
	lv_obj_add_flag(base, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_size(base, f->w, f->h);
  lv_obj_add_style(base, &style, 0);
  lv_obj_align(base, LV_ALIGN_TOP_LEFT, f->x, f->y);

  label = lv_label_create(base);
  lv_label_set_text(label, f->label);
  lv_obj_set_size(label, f->w-2, 15);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 1, 1);
 
  dd = lv_dropdown_create(base);
  lv_obj_set_style_pad_left(dd, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_top(dd, 0, LV_PART_MAIN);

 	lv_obj_add_flag(dd, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_size(dd, f->w-2, 20);
  lv_obj_add_style(dd, &style, 0);
  lv_obj_align(dd, LV_ALIGN_TOP_LEFT, 1, 18);

  lv_dropdown_set_options(dd, f->selection);
/*  strcpy(list, f->selection);
  char *p = strtok(list, "/");

  Serial.printf("\n%s:", f->label);
  while (p){
    lv_obj_t *btn = lv_list_add_button(wifi_list, LV_SYMBOL_WIFI, buff);
		lv_obj_add_event_cb(btn, on_wifi_selected, LV_EVENT_CLICKED, NULL);

    //field_show(p, true);
    Serial.print(p);
    p = strtok(NULL, "/");
    Serial.print(',');
  }

//      lv_obj_set_style_pad_left(w, 2, LV_PART_MAIN);
//      lv_obj_set_style_pad_top(w, 2, LV_PART_MAIN);

    //lv_obj_set_style_bg_color(w, lv_color_hex(0xFF0000), LV_PART_MAIN);

  label = lv_label_create(w);
  lv_label_set_text(label, f->label);
  //lv_obj_align(label, LV_ALIGN_CENTE, 5, 5);
  lv_obj_align(w, LV_ALIGN_TOP_LEFT, f->x, f->y);
*/
  return base;
}

void main_ui(){

	if (!main_screen){
		
		Serial.println("Creating the main screen");
		main_screen = lv_obj_create(lv_scr_act());
  	lv_obj_set_size(main_screen, TFT_VER_RES, TFT_HOR_RES);
  	lv_obj_align(main_screen, LV_ALIGN_TOP_LEFT, 0, 0);

    for (int i = 0; main_list[i].type != -1; i++){
      switch(main_list[i].type){
      case FIELD_BUTTON:
        create_button(main_screen, main_list+i);
        break;
      case FIELD_SELECTION:
        create_selection(main_screen, main_list+i);
        break;
    }
  }
/*
		wifi_btn = lv_btn_create(main_screen);
		lv_obj_set_size(wifi_btn, 140, 30);
		lv_obj_add_flag(wifi_btn, LV_OBJ_FLAG_EVENT_BUBBLE);

		wifi_label = lv_label_create(wifi_btn);
		lv_label_set_text(wifi_label, LV_SYMBOL_WIFI " Offline");

		lv_obj_align(wifi_btn, LV_ALIGN_TOP_LEFT, 5, 5);
		lv_obj_set_style_bg_color(wifi_btn, lv_color_hex(0xFF0000), LV_PART_MAIN);
*/	
  	//lv_obj_set_style_bg_color(coverflow_screen, lv_color_hex(0xFF0000), LV_PART_MAIN);
	}
	lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
	lv_group_focus_obj(main_screen);
}