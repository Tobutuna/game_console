#ifndef PONG_H
#define PONG_H

#include "LCD/lcd.h"
#include <stdint.h>

#define PONG_FIELD_W   160
#define PONG_FIELD_H   128

#define PADDLE_H        16
#define PADDLE_W         2

#define PADDLE_MARGIN    4
#define BALL_SIZE        2

#define PADDLE_SPEED     2    // snabbare paddel = mer responsiv
#define BALL_SPEED_X     1    // lugnare boll
#define BALL_SPEED_Y     1

#define PONG_TICK_MS    10

typedef struct {
    int x;
    int y;
    int vx;
    int vy;
} Ball_t;

typedef struct {
    int x;
    int y;      // mittpunkt
    int h;      // höjd (PADDLE_H)
} Paddle_t;

typedef struct {
    Ball_t   ball;
    Paddle_t p1;
    Paddle_t p2;
    int score_p1;
    int score_p2;
} PongState_t;

// FreeRTOS-task för spelet
void vPongTask(void *pvParameters);

#endif // PONG_H

/*
 * README – pong.h
 * ===============
 *
 * Syfte:
 *  - Samlar alla konstanter, datastrukturer och typer som hör till Pong-spelet.
 *  - Ger ett rent gränssnitt (vPongTask) som kan användas från main.c.
 *
 * KONSTANTER OCH GEOMETRI
 * ------------------------
 *  PONG_FIELD_W, PONG_FIELD_H:
 *      - Spelplanens storlek i pixlar.
 *      - Bestämmer koordinatsystemet för paddlar och boll.
 *
 *  PADDLE_H, PADDLE_W:
 *      - Paddelns höjd och bredd i pixlar.
 *
 *  PADDLE_MARGIN:
 *      - Avståndet mellan paddelns x-position och skärmens kant.
 *
 *  BALL_SIZE:
 *      - Bollens storlek (kvadratisk) i pixlar.
 *
 *  PADDLE_SPEED:
 *      - Hur många pixlar per tick spelarpaddeln rör sig i Y-led.
 *      - Ju större värde, desto mer "snabb" känsla.
 *
 *  BALL_SPEED_X / BALL_SPEED_Y:
 *      - Grundhastighet för bollen i X/Y.
 *      - Nuvarande kod använder istället funktionerna pong_ball_speed_x/y()
 *        i pong.c som beror på svårighetsgrad, men dessa konstanter kan
 *        fortfarande användas om man vill ha en enkel variant.
 *
 *  PONG_TICK_MS:
 *      - RTOS-tick intervall för vPongTask.
 *      - 10 ms innebär att spelet uppdateras ~100 gånger per sekund.
 *      - Ändrar du detta måste du också tänka på:
 *          * Boll- och paddelhastighet (pixlar per tick).
 *          * Serve-nedräkning (3..1) räknas i "frames" per sekund.
 *
 * DATASTRUKTURER
 * --------------
 *  Ball_t:
 *      - x, y:
 *          * Bollens övre vänstra hörn i pixlar (0..PONG_FIELD_W-1, 0..PONG_FIELD_H-1).
 *      - vx, vy:
 *          * Hastighet i pixlar per tick (kan vara negativ).
 *
 *  Paddle_t:
 *      - x:
 *          * Fix x-position för paddeln (vänster- eller högerspelare).
 *      - y:
 *          * Mittpunktens y-position.
 *      - h:
 *          * Paddelns höjd (oftast PADDLE_H).
 *
 *  PongState_t:
 *      - Samlar allt "primärt" spelstate:
 *          * ball: bollens position + hastighet.
 *          * p1, p2: paddlarnas position + storlek.
 *          * score_p1, score_p2: respektive spelares poäng.
 *      - Mer avancerat state (menyer, svårighetsgrad, serve-logik, statistik)
 *        hanteras av statiska variabler inne i pong.c.
 *
 * FUNKTIONER
 * ----------
 *  void vPongTask(void *pvParameters);
 *      - FreeRTOS-task som:
 *          * Läser GameInput_t från xInputQueue (definieras i main.c/input.c).
 *          * Uppdaterar spel-logik (boll, paddlar, poäng, AI, menyer).
 *          * Ritar allt på LCD via lcd.c.
 *      - Skapas i main.c med xTaskCreate().
 *
 * ANVÄNDNING
 * ----------
 *  - Inkludera "pong.h" i main.c:
 *        #include "pong.h"
 *
 *  - Skapa task:
 *        xTaskCreate(vPongTask, "PONG", 512, NULL, 1, NULL);
 *
 *  - Resten av spelets implementation ligger i pong.c och är
 *    oberoende av hur main.c ser ut, så länge vPongTask kallas via RTOS.
 */