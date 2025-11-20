/*
  Minimal entry for a Fly 'n' Shoot prototype.
  Initializes LCD and keyboard, then runs a simple game loop.
  The game module (game.c/game.h) implements a tiny player/bullet/enemy loop
  using the existing LCD drawing helpers in this project.
*/

#include "gd32vf103.h"
#include "drivers.h"
#include "lcd.h"
#include "game.h"
// headers used for enabling timer IRQ
#include "n200_eclic.h"
#include "gd32vf103_timer.h"

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"

// freertos tasks helper
void freertos_tasks_init(void);
extern TaskHandle_t xInputTaskHandle;

extern void lcd_delay_1ms(uint32_t count); // implemented in lcd.c

int main(void)
{
	/* Initialize hardware */
	// Initialize hardware subsystems used by keyboard driver
	// Initialize peripheral hardware first
	colinit();   // init column driver (cycles outputs to keyboard columns)
	// l88init();   // init 8x8 LED row driver (used by example keyboard routines)
	keyinit();
	Lcd_Init();
	Lcd_SetType(LCD_INVERTED); // or use LCD_INVERTED!

	// Start the 1ms timer used by keyboard debounce/scan
	t5omsi();

	// Initialize game state and FreeRTOS tasks
	Game_Init();
	freertos_tasks_init();

	// enable TIMER5 update interrupt so the ISR will notify the InputTask
	timer_interrupt_enable(TIMER5, TIMER_INT_UP);
	eclic_enable_interrupt(TIMER5_IRQn);
	eclic_set_irq_lvl_abs(TIMER5_IRQn, 1);
	eclic_global_interrupt_enable();

	// start the scheduler (does not return on success)
	vTaskStartScheduler();

	/* If scheduler exits, fall back to an idle loop */
	for(;;) __asm volatile("wfi");

	return 0;
}
