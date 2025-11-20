#include "gd32vf103.h"
#include "game.h"

#include "n200_eclic.h"
#include "gd32vf103_timer.h"

// FreeRTOS API used to notify the input task from the ISR
#include "FreeRTOS.h"
#include "task.h"

// Input task handle (created by freertos_tasks_init)
extern TaskHandle_t xInputTaskHandle;

void TIMER5_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // clear timer update interrupt flag
    timer_interrupt_flag_clear(TIMER5, TIMER_INT_UP);

    // notify the input task so it runs the 1ms sampling path
    if (xInputTaskHandle) {
        vTaskNotifyGiveFromISR(xInputTaskHandle, &xHigherPriorityTaskWoken);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
