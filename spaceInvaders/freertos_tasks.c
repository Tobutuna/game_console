/* FreeRTOS tasks implementing an active-object style input/update/render
   system for the Fly 'n' Shoot prototype.

   - InputTask is notified from TIMER5 IRQ (1ms). It samples the keyboard,
     handles debounce/repeat and submits higher-level key events to the
     Game queue.
   - GameTask consumes key events and advances game state (bullets, enemies)
     at ~60Hz. It owns the game state and therefore serializes updates.
   - RenderTask renders the current game state at ~60Hz. It takes the
     game mutex when reading state so rendering and updates don't race.
*/

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "game.h"
#include "lcd.h"
#include "drivers.h" // colset, l88row, keyscan

#include <stdlib.h>

// Key event types sent from InputTask to GameTask
typedef enum
{
    KEY_EVT_NONE = 0,
    KEY_EVT_LEFT,
    KEY_EVT_RIGHT,
    KEY_EVT_FIRE,
    KEY_EVT_FIRE_ALT
} KeyEvt_t;

// Queue and mutex handles
static QueueHandle_t keyQueue = NULL;
static SemaphoreHandle_t gameMutex = NULL;
// Pause control: when paused, `pauseRequested`==pdTRUE and Game/Render will
// block on `resumeSem`. To resume, code gives `resumeSem` twice so both
// consumers wake. This avoids deadlocks when Game_SetPause is called from
// within the GameTask.
static SemaphoreHandle_t resumeSem = NULL;
static volatile BaseType_t pauseRequested = pdFALSE;
// Simple pause overlay bookkeeping
static BaseType_t pause_overlay_drawn = pdFALSE;
static int pause_prev_score = -1;

// Task handles (exposed to ISR)
TaskHandle_t xInputTaskHandle = NULL;
static TaskHandle_t xGameTaskHandle = NULL;
static TaskHandle_t xRenderTaskHandle = NULL;

// We'll map raw keys using the game-provided helper and use the public
// KEY_* macros from game.h.

// Fire cooldown handled in InputTask to shape the input events (ms)
#define INPUT_FIRE_INTERVAL_MS 150

// Input task: waits for notification from IRQ, samples keyboard and enqueues events
static void InputTask(void *pv)
{
    int fire_cooldown = 0; // ms
    int move_cooldown = 0; // ms
    const int MOVE_INTERVAL_MS = 80; // how often movement events are emitted when holding

    for (;;)
    {
        // Wait for notification from TIMER5 IRQ
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Called roughly every 1 ms. Decrement cooldowns.
        if (fire_cooldown > 0)
            fire_cooldown--;
        if (move_cooldown > 0)
            move_cooldown--;

        // Do a short 4-column scan and collect all keys found in this tick.
        int found_left = 0;
        int found_right = 0;
        int found_fire = 0;
        int found_fire_alt = 0;
        for (int i = 0; i < 4; ++i)
        {
            colset();
            int k = keyscan();
            if (k >= 0)
            {
                int mapped = Game_MapRawKey(k);
                if (mapped == KEY_LEFT_ID)
                    found_left = 1;
                else if (mapped == KEY_RIGHT_ID)
                    found_right = 1;
                else if (mapped == KEY_FIRE_ID)
                    found_fire = 1;
                else if (mapped == KEY_FIRE_ALT_ID)
                    found_fire_alt = 1;
                // continue scanning to allow detecting multiple keys
            }
        }

        // If any key was seen while paused, resume and consume the input
        if ((found_left || found_right || found_fire) && pauseRequested)
        {
            Game_SetPause(0);
            Game_Reset();
            continue;
        }

        // Emit movement events at a controlled interval so player can move while
        // holding and still fire independently.
        if (found_left && move_cooldown == 0)
        {
            KeyEvt_t evt = KEY_EVT_LEFT;
            if (keyQueue)
                xQueueSend(keyQueue, &evt, 0);
            move_cooldown = MOVE_INTERVAL_MS;
        }
        else if (found_right && move_cooldown == 0)
        {
            KeyEvt_t evt = KEY_EVT_RIGHT;
            if (keyQueue)
                xQueueSend(keyQueue, &evt, 0);
            move_cooldown = MOVE_INTERVAL_MS;
        }

        // Fire is throttled independently so movement and shooting can overlap
        if (found_fire && fire_cooldown == 0)
        {
            KeyEvt_t evt = KEY_EVT_FIRE;
            if (keyQueue)
                xQueueSend(keyQueue, &evt, 0);
            fire_cooldown = INPUT_FIRE_INTERVAL_MS;
        }
        // Alternate fire (missile)
        if (found_fire_alt && fire_cooldown == 0)
        {
            KeyEvt_t evt = KEY_EVT_FIRE_ALT;
            if (keyQueue)
                xQueueSend(keyQueue, &evt, 0);
            fire_cooldown = INPUT_FIRE_INTERVAL_MS;
        }
    }
}

// GameTask: owns the game state updates. Runs at ~30Hz and processes key events.
static void GameTask(void *pv)
{
    const TickType_t period = pdMS_TO_TICKS(33); // ~30 FPS
    TickType_t last = xTaskGetTickCount();

    for (;;)
    {
        // Drain and process key events by delegating to game API
        KeyEvt_t evt;
        while (keyQueue && xQueueReceive(keyQueue, &evt, 0) == pdTRUE)
        {
            if (xSemaphoreTake(gameMutex, pdMS_TO_TICKS(5)) == pdTRUE)
            {
                if (evt == KEY_EVT_LEFT)
                    Game_HandleEvent(GE_LEFT);
                else if (evt == KEY_EVT_RIGHT)
                    Game_HandleEvent(GE_RIGHT);
                else if (evt == KEY_EVT_FIRE)
                    Game_HandleEvent(GE_FIRE);
                else if (evt == KEY_EVT_FIRE_ALT)
                    Game_HandleEvent(GE_FIRE_ALT);
                xSemaphoreGive(gameMutex);
            }
        }

        // If a pause was requested, block until resumeSem is given twice
        if (pauseRequested)
        {
            if (resumeSem)
                xSemaphoreTake(resumeSem, portMAX_DELAY);
        }
        // Perform a full game update (uses internal state in game.c)
        if (xSemaphoreTake(gameMutex, pdMS_TO_TICKS(5)) == pdTRUE)
        {
            Game_Update();
            xSemaphoreGive(gameMutex);
        }
        // no resume token give here; resume tokens are managed only by
        // Game_SetPause when unpausing.

        vTaskDelayUntil(&last, period);
    }
}

// RenderTask: draws the game state, protected by gameMutex
static void RenderTask(void *pv)
{
    const TickType_t period = pdMS_TO_TICKS(16);
    TickType_t last = xTaskGetTickCount();

    for (;;)
    {
        // If paused, display a simple GAME OVER overlay with score.
        if (pauseRequested)
        {
            if (xSemaphoreTake(gameMutex, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                if (!pause_overlay_drawn)
                {
                    // Draw overlay once
                    // simple centered strings
                    LCD_Fill(0, 0, 140, 80, BLACK);
                    LCD_ShowString(40, 40, (const u8 *)"GAME OVER", WHITE);
                    int score = Game_GetScore();
                    LCD_ShowString(40, 60, (const u8 *)"SCORE:", WHITE);
                    LCD_ShowNum(88, 60, (u16)score, 4, WHITE);
                    pause_prev_score = score;
                    pause_overlay_drawn = pdTRUE;
                }
                else
                {
                    // Only update score if it changed (shouldn't while paused)
                    int score = Game_GetScore();
                    if (score != pause_prev_score)
                    {
                        LCD_ShowNum(88, 60, (u16)score, 4, WHITE);
                        pause_prev_score = score;
                    }
                }
                xSemaphoreGive(gameMutex);
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // If we had drawn the overlay and now resumed, clear the small area
        if (pause_overlay_drawn)
        {
            if (xSemaphoreTake(gameMutex, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                // Clear the text area (simple black box) so Game_Render repaints
                LCD_Fill(36, 36, 140, 80, BLACK);
                pause_overlay_drawn = pdFALSE;
                pause_prev_score = -1;
                xSemaphoreGive(gameMutex);
            }
        }

        if (xSemaphoreTake(gameMutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            // Game_Render reads global state and draws it
            Game_Render();
            xSemaphoreGive(gameMutex);
        }
        // resume tokens are not given here; Game_SetPause handles resume.
        vTaskDelayUntil(&last, period);
    }
}

// Initialization helper: create queue, mutex and tasks. Call before vTaskStartScheduler.
void freertos_tasks_init(void)
{
    // create queue for key events
    keyQueue = xQueueCreate(16, sizeof(KeyEvt_t));
    gameMutex = xSemaphoreCreateMutex();
    // create counting semaphore used to resume blocked tasks; initially empty
    resumeSem = xSemaphoreCreateCounting(2, 0);

    // Create tasks: Input high priority, Game medium, Render low
    xTaskCreate(InputTask, "Input", 256, NULL, configMAX_PRIORITIES - 1, &xInputTaskHandle);
    xTaskCreate(GameTask, "Game", 512, NULL, configMAX_PRIORITIES - 2, &xGameTaskHandle);
    xTaskCreate(RenderTask, "Render", 512, NULL, tskIDLE_PRIORITY + 1, &xRenderTaskHandle);
}

// Toggle pause: when pausing, take both counting tokens and hold them; when
// unpausing, give them back. We wait briefly for current frame tokens to be
// released so pause happens cleanly at frame boundary.
void Game_TogglePause(void)
{
    taskENTER_CRITICAL();
    if (!pauseRequested)
    {
        pauseRequested = pdTRUE;
    }
    else
    {
        // resume: clear request and give two resume tokens
        pauseRequested = pdFALSE;
        if (resumeSem)
        {
            xSemaphoreGive(resumeSem);
            xSemaphoreGive(resumeSem);
        }
    }
    taskEXIT_CRITICAL();
}

// Force pause/resume: pause non-zero to pause, zero to resume
void Game_SetPause(int pause)
{
    taskENTER_CRITICAL();
    if (pause)
    {
        pauseRequested = pdTRUE;
    }
    else
    {
        if (pauseRequested)
        {
            pauseRequested = pdFALSE;
            if (resumeSem)
            {
                xSemaphoreGive(resumeSem);
                xSemaphoreGive(resumeSem);
            }
        }
    }
    taskEXIT_CRITICAL();
}
