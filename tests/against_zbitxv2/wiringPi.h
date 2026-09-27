// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// wiringPi.h — what zbitxv2's sbitx.c needs of wiringPi in order to compile
// where there is no wiringPi. stand_ins.c has the functions, which do nothing.

#pragma once

#define INPUT 0
#define OUTPUT 1
#define LOW 0
#define HIGH 1

void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int digitalRead(int pin);
void delay(unsigned int ms);
unsigned int millis(void);
