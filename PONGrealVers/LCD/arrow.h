#ifndef ARROW_H
#define ARROW_H

#include <stdint.h>
#include "lcd.h"

// Initialize all menu/game buttons:
//   PB4  = Left
//   PB5  = Select
//   PB6  = Up
//   PB7  = Down
//   PB8  = Back
//   PB9  = Right
//   PA9  = Select_2 (ALT fire)
void Arrow_Init(void);

// Draw arrow at menu entry 0,1,2 (right side of screen)
void Arrow_Show(int selected);

// Optional helpers for menu navigation
void Arrow_Up(void);
void Arrow_Down(void);
uint8_t Arrow_GetSelection(void);

// Edge-detected button presses (debounced)
// Returns 1 exactly once per press, 0 otherwise.
uint8_t Arrow_Up_Pressed(void);
uint8_t Arrow_Down_Pressed(void);
uint8_t Arrow_Select_Pressed(void);
uint8_t Arrow_Back_Pressed(void);
uint8_t Arrow_Left_Pressed(void);
uint8_t Arrow_Right_Pressed(void);
uint8_t Arrow_Select2_Pressed(void);

// Level (held) state for continuous motion in games
// Returns 1 as long as the button is physically held down.
uint8_t Arrow_Left_IsDown(void);
uint8_t Arrow_Right_IsDown(void);

#endif
