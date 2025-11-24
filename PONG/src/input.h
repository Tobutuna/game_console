#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

// Generisk spel-input, kan återanvändas av Pong och Space Invaders
typedef struct {
    uint8_t up;
    uint8_t down;
    uint8_t fire;
    uint8_t pause;
} GameInput_t;

void Input_HwInit(void);
void vInputTask(void *pvParameters);

#endif // INPUT_H

/*
 * README – input.h
 * ================
 *
 * Syfte:
 *  - Exponerar input-modulens publika API mot resten av projektet.
 *  - Definierar GameInput_t, som är en generisk struktur för spelkontroller.
 *
 * GameInput_t:
 * ------------
 *  - up:
 *      * Används i Pong för att flytta P1-paddeln uppåt.
 *      * Kan återanvändas i andra spel som "vänster" eller "jump" osv.
 *
 *  - down:
 *      * Används i Pong för att flytta P1-paddeln nedåt.
 *
 *  - fire:
 *      * Används i Pong för:
 *          - Menyval (bekräfta i menyer).
 *          - Starta ny omgång efter GAME OVER.
 *      * Kan i andra spel användas som skottknapp.
 *
 *  - pause:
 *      * Används i Pong för:
 *          - Öppna pausmenyn mitt i spelet.
 *      * Kan också tolkas som "back" eller "menu" i andra spel.
 *
 * FUNKTIONER/GRÄNSSNITT
 * ---------------------
 *  void Input_HwInit(void);
 *      - Kallas från main.c innan FreeRTOS startar.
 *      - Initierar GPIO för inmatning:
 *          * I nuvarande version: 4x4-keypad på GPIOA (matris).
 *          * I en framtida version: separata knappar på valfria pinnar.
 *      - All hårdvaruspecifik GPIO-konfiguration ligger i input.c,
 *        inte här.
 *
 *  void vInputTask(void *pvParameters);
 *      - FreeRTOS-task som:
 *          * Scannar keypaden/knapparna regelbundet (t.ex. var 10 ms).
 *          * Debouncar (kräver flera identiska läsningar innan ett
 *            knappbyte accepteras).
 *          * Fyller en GameInput_t med aktuell knappstatus.
 *          * Skickar den till xInputQueue (deklarerad i main.c) med
 *            xQueueOverwrite().
 *
 * DESIGNIDÉ
 * ---------
 *  - Alla spel (Pong, ev. Space Invaders) använder bara GameInput_t.
 *  - Ingen spelkod behöver veta:
 *      * Vilka GPIO-pinnar som används.
 *      * Om det är en 4x4-keypad eller 4 separata knappar.
 *      * Hur debounce görs.
 *
 *  - För att byta hårdvara ändrar du endast input.c, inte spelen.
 */