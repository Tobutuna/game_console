#include "gd32vf103.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "pong.h"
#include "input.h"
#include "LCD/lcd.h"

QueueHandle_t xInputQueue;

int main(void)
{
    // SystemInit(); // om ni använder den i ert projekt

    Lcd_Init();      // initiera LCD
    BACK_COLOR = BLACK;  // ← VIKTIGT: standard-bakgrund för all text


    Input_HwInit();  // initiera keypad-GPIO

    // Skapa kö för GameInput_t (senaste knappstatus)
    xInputQueue = xQueueCreate(1, sizeof(GameInput_t));

    // Input-task: läser keypad och skriver GameInput_t till kön
    xTaskCreate(vInputTask, "INPUT", 256, NULL, 1, NULL);

    // Pong-task: spel-loop (läser GameInput_t + ritar spelet)
    xTaskCreate(vPongTask,  "PONG",  512, NULL, 1, NULL);

    vTaskStartScheduler();

    // Kommer i princip aldrig hit
    while (1) {}
}

/*
 * README – main.c
 * =================
 *
 * ÖVERSIKT
 * --------
 * Den här filen är "entry point" för Pong-projektet:
 *  - Initierar skärmen (LCD).
 *  - Initierar inmatningshårdvaran (keypad / knappar).
 *  - Skapar en FreeRTOS-kö för spelkontroller (GameInput_t).
 *  - Skapar två FreeRTOS-taskar:
 *      * vInputTask  (input.c) – läser knappar och fyller GameInput_t.
 *      * vPongTask   (pong.c)  – spelmotorn som uppdaterar spelet + ritar.
 *  - Startar FreeRTOS-schemaläggaren.
 *
 * HUR RTOS-ARKITEKTUREN FUNGERAR
 * ------------------------------
 * FreeRTOS är ett realtids-OS som kör flera "taskar" (trådar) "samtidigt"
 * på MCU:n genom tidsdelning:
 *
 *  - vInputTask:
 *      * Kör i en egen while(1)-loop.
 *      * Läser av keypaden var ~10 ms (vTaskDelay).
 *      * Debouncar knappar och bygger en GameInput_t-struktur.
 *      * Lägger alltid senaste GameInput_t i xInputQueue med xQueueOverwrite().
 *
 *  - vPongTask:
 *      * Kör också i en egen while(1)-loop.
 *      * Läser icke-blockerande från xInputQueue (xQueueReceive med timeout 0).
 *      * Sparar senaste GameInput_t och använder den för att styra P1-paddeln
 *        samt menyer/paus.
 *      * Uppdaterar spel-logik (boll, poäng, AI, serve, game over).
 *      * Ritar bara det som behövs på LCD:n (partial redraw).
 *      * Väntar sedan PONG_TICK_MS millisekunder med vTaskDelayUntil().
 *
 *  - vTaskStartScheduler():
 *      * Startar hårdvarutimern som genererar RTOS-"ticks".
 *      * Bestämmer vilken task som ska köra härnäst utifrån prioritet och delay.
 *      * main() kommer inte tillbaka hit om allt fungerar korrekt.
 *
 * KOMMUNIKATION MELLAN TASKAR
 * ---------------------------
 *  - xInputQueue är en kö med exakt 1 element av typen GameInput_t.
 *  - vInputTask skriver till kön med xQueueOverwrite():
 *      → kön innehåller ALLTID "senaste kända" knappstatus.
 *  - vPongTask läser med xQueueReceive(..., 0):
 *      → om det finns ny input uppdateras dess lokala GameInput_t.
 *      → om inte, används senaste kända värde.
 *
 * På det sättet är input och spel logiskt separerade:
 *  - input.c bryr sig inte om spelet.
 *  - pong.c bryr sig inte om exakt hur keypad/knappar är kopplade.
 *
 * SKÄRM-HANTERING (LCD)
 * ---------------------
 *  - Lcd_Init():
 *      * Står i LCD-drivrutinen (lcd.c). Den sätter upp SPI, GPIO och LCD-kontrollern.
 *  - BACK_COLOR:
 *      * Global variabel i lcd.c som anger bakgrundsfärg när text ritas.
 *      * Vi sätter den till BLACK direkt efter Lcd_Init() så att all text och
 *        skärmrensning får svart bakgrund.
 *  - Pong-koden använder funktioner som:
 *      * LCD_Clear(color)         – fyller hela skärmen.
 *      * LCD_Fill(x1,y1,x2,y2,c)  – fyller rektangel.
 *      * LCD_ShowString(x,y,str,c)– skriver text.
 *      * LCD_ShowNum(x,y,num,len,c) – skriver tal.
 *      * LCD_Wait_On_Queue()      – ser till att alla skrivkommandon verkligen
 *                                    skickas ut via SPI (om LCD-drivern jobbar kö-baserat).
 *
 * TANGENTBORD / KNAPPAR
 * ----------------------
 *  - Input_HwInit():
 *      * Ligger i input.c.
 *      * Sätter upp GPIOA-pinnar som antingen input med pull-up (rader)
 *        eller output push-pull (kolumner) för en 4x4-keypad.
 *  - vInputTask():
 *      * Skannar matrisen kolumn för kolumn.
 *      * Kollar vilka rader som går låga → räknar ut knappindex 0..15.
 *      * Gör debounce och översätter knappkod → GameInput_t:
 *          up, down, fire, pause.
 *
 * VAD SOM MÅSTE ÄNDRAS FÖR "RIKTIG" SKÄRM OCH KNAPPAR
 * ---------------------------------------------------
 * När ni går från Longan Nano + 4x4-keypad till er riktiga prototyp
 * (extern LCD + riktiga knappar), är det här filen där själva "systemet"
 * fortfarande ser likadant ut. Ni behöver inte ändra RTOS-upplägget.
 *
 * Ni kommer främst att ändra:
 *
 *  1) Skärm (LCD):
 *     - Byt ut LCD-drivrutinen mot den som hör till den riktiga skärmen.
 *       Det innebär praktiskt:
 *         * Inkludera rätt header i stället för "LCD/lcd.h", t.ex:
 *             #include "my_real_lcd.h"
 *         * Se till att motsvarande funktioner finns:
 *             Lcd_Init()
 *             LCD_Clear()
 *             LCD_Fill()
 *             LCD_ShowString()
 *             LCD_ShowNum()
 *             BACK_COLOR (global bakgrundsfylld-färg)
 *             LCD_Wait_On_Queue() (eller ta bort anropen om ni inte har kö)
 *
 *     - Om den nya drivrutinen har andra funktionsnamn:
 *         * Antingen skriver ni wrapper-funktioner med samma namn som ovan,
 *           eller så uppdaterar ni alla anrop i pong.c till de nya namnen.
 *
 *  2) Input (knappar i stället för keypad):
 *     - Ändringar sker i input.c och input.h – inte här:
 *         * Input_HwInit() måste ställas om till de GPIO-pinnar som de riktiga
 *           knapparna sitter på (t.ex. PC0, PC1, PB5 osv).
 *         * Keyboard_Scan() kan ersättas/ändras så att den läser separata knappar
 *           i stället för 4x4-matris.
 *         * P1_UP_KEY, P1_DOWN_KEY, FIRE_KEY, PAUSE_KEY kan:
 *              - Tas bort om ni går över till direkt-bitläsning (då behövs inte
 *                "key-koder", utan ni sätter in->up = (gpio_input_bit_get(...) == 0)).
 *     - GRUNDIDÉ: Behåll GameInput_t och vInputTask-gränssnittet exakt likadant.
 *       Då behöver ni inte röra vPongTask eller resten av spelet.
 *
 *  3) SystemInit / klockor:
 *     - På en ny hårdvara kan ni behöva:
 *         * Anropa SystemInit() eller någon form av klock-init innan Lcd_Init()
 *           och Input_HwInit(), beroende på hur ert startupprojekt ser ut.
 *     - Detta är plattformsberoende och hanteras ofta i startup-kod eller i en
 *       separat "board_init.c".
 *
 * SAMMANFATTNING
 * --------------
 *  - main.c beskriver alltså bara hög-nivå-flödet:
 *      init LCD → init input → skapa kö → skapa taskar → starta RTOS.
 *  - All hårdvaruspecifik logik ligger i:
 *      * LCD-drivrutinen (skärm).
 *      * input.c (knappar/keypad).
 *      * Pong-spelslogiken är helt hårdvaruagnostisk förutom att den kallar
 *        LCD-funktioner för att rita på skärmen och läser GameInput_t via kö.
 */