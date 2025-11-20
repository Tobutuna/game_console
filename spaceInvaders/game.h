/* Simple game API for Fly 'n' Shoot prototype */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

void Game_Init(void);
void Game_Update(void);
void Game_Render(void);

// Adjust enemy falling speed (pixels per frame). Call before or during runtime.
void Game_SetEnemySpeed(int speed);
// Toggle pause/unpause (implemented in freertos tasks layer using a semaphore)
void Game_TogglePause(void);
// Force pause state: pass non-zero to pause, zero to resume
void Game_SetPause(int pause);

// Return current score for display in overlays
int Game_GetScore(void);
void Game_Reset(void);


// Called from a timer interrupt or from the main loop to advance the
// keyboard scanner by one tick (1 ms). Returns 1 if a key was seen.
// Making this public lets an IRQ handler call it so scanning continues
// even while the LCD blocks.
int KeyScan_Tick(void);

// Mapping constants for logical keys (can be tuned to your keyboard)
#define KEY_LEFT_ID  4
#define KEY_RIGHT_ID 6
#define KEY_FIRE_ID  2
// Alternate fire (missile) mapped key id. Change if your keypad layout differs.
// Set to logical id '3' so pressing the physical key '3' triggers missiles.
#define KEY_FIRE_ALT_ID 3

// Convert a raw scanner index (0..15) to the logical mapped id using
// the project's lookup table. Returns -1 for invalid inputs.
int Game_MapRawKey(int raw);

// Game event API: high-level actions driven by input tasks
typedef enum { GE_NONE = 0, GE_LEFT, GE_RIGHT, GE_FIRE, GE_FIRE_ALT } GameEvent_t;
void Game_HandleEvent(GameEvent_t ev);

#endif // GAME_H
