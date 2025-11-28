// lcdtask.c
#include "LCD/lcd.h"
#include "FreeRTOS.h"
#include "task.h"

void vLcdTask(void *pvParameters)
{
    // Initiera LCD en gång i början
    Lcd_SetType(LCD_NORMAL);   // nu när färgerna är rätt
    Lcd_Init();
    LCD_Clear(BLACK);

    for (;;)
    {
        // Pumpa ut allt som ligger i LCD-kön till skärmen
        LCD_WR_Queue();

        // Liten paus så tasken inte spinner tokfort
        vTaskDelay(1);
    }
}
