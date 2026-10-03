#include <Arduino.h>
#include <EEPROM.h>
#include <lvgl.h>
#include "ui.h"


struct saved block;
boolean block_dirty = false;

bool block_read(){
  uint8_t buff[512];
  int index = 0, i;

  EEPROM.begin(512);

  Serial.println("reading block");
  byte *p = (byte *)buff;
  for (int i = 0; i < sizeof(block); i++){
    byte b = EEPROM.read(i);
    *p++ = b;  
  }
 
  struct saved *q = (struct saved *)buff;
  if (q->magic != 0x00C0FFEE){
    Serial.println("block was uninitialized");
    memset((void *)&block, 0, sizeof(block));
    return false;
  }
  
  memcpy((void *)&block, buff, sizeof(block));
  block_dump();
  return true;
}

//don't do this too often
void block_write(){
  uint8_t buff[512];
  int index = 0, i;

  //read the last saved
  for (i = 0; i < sizeof(buff); i++)
    buff[i] = EEPROM.read(i);

  Serial.println("block checking");
  //if it is the same, don't do anything
  struct saved *p = (struct saved *)buff;
  if (p->magic == 0x00C0FFEE && !memcmp(&block, buff, sizeof(block))){
    Serial.println("Block is unchanged and not updated");
    return;
  }

  Serial.println("block writing");

  //write the block
  block.magic = 0x00C0FFEE;
  byte *q = (byte *)&block;
  for (int i = 0; i < sizeof(block); i++){
    byte b = *q++;
    EEPROM.write(i, b);
  }
  Serial.printf("%d bytes of block written", i);
  EEPROM.commit();
  Serial.println("block commited");
  block_dump();
}

void block_dump(){
  Serial.printf("block magic id %x\n", block.magic);
  Serial.printf("userid : %u\n", block.my_id);
  for (int i = 0; i < 6; i++)
    Serial.printf("%d ", block.calibration_data[i]);
  Serial.print("APs:\n");
  for (int i = 0; i < MAX_APS; i++)
    Serial.printf("%d. [%s]\n", i+1, block.ap_list[i].ssid);
}
