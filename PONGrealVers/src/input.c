#include "gd32vf103.h"
#include "gd32vf103_gpio.h"
#include "gd32vf103_rcu.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "LCD/lcd.h"
#include "LCD/arrow.h"   // we only use Arrow_Init(), pins are hard-coded below
#include "input.h"

extern QueueHandle_t xInputQueue;

/*
 * Pin mapping – must match arrow.c
 *
 *   PB6 = Up
 *   PB7 = Down
 *   PB5 = Select (fire)
 *   PB8 = Back   (pause)
 */
#define BTN_PORT        GPIOB
#define BTN_UP_PIN      GPIO_PIN_6
#define BTN_DOWN_PIN    GPIO_PIN_7
#define BTN_SELECT_PIN  GPIO_PIN_5
#define BTN_BACK_PIN    GPIO_PIN_8

// -------------------- HW-init --------------------
void Input_HwInit(void)
{
    /* Ensure GPIOA / GPIOB clocks are on before Arrow_Init
       (safe even if they are already enabled somewhere else). */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);

    /* Configure pins exactly like the console code does */
    Arrow_Init();
}

// -------------------- FreeRTOS-task --------------------
void vInputTask(void *pvParameters)
{
    (void)pvParameters;

    GameInput_t in        = {0};
    GameInput_t last_sent = {0};

    // Initial state: everything released
    xQueueOverwrite(xInputQueue, &in);

    for (;;)
    {
        /* Active-low buttons: pressed == RESET */
        in.up    = (gpio_input_bit_get(BTN_PORT, BTN_UP_PIN)     == RESET);
        in.down  = (gpio_input_bit_get(BTN_PORT, BTN_DOWN_PIN)   == RESET);
        in.fire  = (gpio_input_bit_get(BTN_PORT, BTN_SELECT_PIN) == RESET);
        in.pause = (gpio_input_bit_get(BTN_PORT, BTN_BACK_PIN)   == RESET);

        /* Only send if something actually changed */
        if ( in.up    != last_sent.up   ||
             in.down  != last_sent.down ||
             in.fire  != last_sent.fire ||
             in.pause != last_sent.pause )
        {
            xQueueOverwrite(xInputQueue, &in);
            last_sent = in;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
