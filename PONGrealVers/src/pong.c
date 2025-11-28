#include "pong.h"
#include "input.h"        // GameInput_t
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "LCD/arrow.h"

extern QueueHandle_t xInputQueue;

// ================== Globalt spelstate ==================

static PongState_t g_state;

// Föregående positioner för partial redraw
static Paddle_t g_prev_p1;
static Paddle_t g_prev_p2;
static Ball_t   g_prev_ball;

// Svårighetsgrad för AI
typedef enum {
    PONG_DIFF_EASY = 0,
    PONG_DIFF_HARD = 1
} PongDifficulty_t;

static PongDifficulty_t g_diff = PONG_DIFF_EASY;

// Inre spel-faser (när vi är inne i själva spelet)
typedef enum {
    PONG_PHASE_SERVE = 0,
    PONG_PHASE_PLAY  = 1,
    PONG_PHASE_GAME_OVER = 2
} PongPhase_t;

static PongPhase_t g_phase;

// Yttre “mode”: meny / diff-val / highscore / spel / paus
typedef enum {
    PONG_MODE_MENU = 0,
    PONG_MODE_DIFF_SELECT,
    PONG_MODE_HIGHSCORE,
    PONG_MODE_GAME,
    PONG_MODE_PAUSE          // pausmeny
} PongMode_t;

static PongMode_t g_mode;

// Serve / nedräkning
static int g_serve_player = 1;       // 1 = P1, 2 = P2
static int g_serve_count = 3;        // 3,2,1 → sen spel
static int g_serve_frame_counter = 0;
static int g_prev_countdown = 0;

// Game over
static int g_winner = 0;             // 0=ingen, 1=P1, 2=P2
static int g_prev_winner_drawn = 0;

// Meny-state
static int g_menu_index       = 0;   // 0 = Start, 1 = Highscore, 2 = Exit
static int g_prev_menu_index  = -1;

static int g_diff_index       = 0;   // 0 = Easy, 1 = Hard
static int g_prev_diff_index  = -1;

static int g_pause_index      = 0;   // 0 = Resume, 1 = Difficulty, 2 = Main Menu
static int g_prev_pause_index = -1;

// Highscore (sessionbaserad)
static int g_games_played = 0;
static int g_p1_wins      = 0;
static int g_p2_wins      = 0;
static int g_best_margin  = 0;       // största vinstmarginal för P1

// Knappar – edge-detektion
static uint8_t g_prev_btn_up    = 0;
static uint8_t g_prev_btn_down  = 0;
static uint8_t g_prev_btn_fire  = 0;
static uint8_t g_prev_btn_pause = 0;

// ================== Hjälpfunktioner (spel) ==================

// Hastighet beroende på svårighetsgrad
static int pong_ball_speed_x(void)
{
    // Easy: 1 px/tick, Hard: 2 px/tick
    return (g_diff == PONG_DIFF_HARD) ? 2 : 1;
}

static int pong_ball_speed_y(void)
{
    // Easy: 1 px/tick, Hard: 2 px/tick
    return (g_diff == PONG_DIFF_HARD) ? 2 : 1;
}

static void clamp_paddle(Paddle_t *p)
{
    int half = p->h / 2;

    if (p->y < half)
        p->y = half;

    if (p->y > (PONG_FIELD_H - 1 - half))
        p->y = PONG_FIELD_H - 1 - half;
}

// Placera bollen fast på serverns paddel under serve-fasen
static void pong_attach_ball_to_server(void)
{
    if (g_serve_player == 1) {
        g_state.ball.x = g_state.p1.x + PADDLE_W + 1;      // precis till höger om P1
        g_state.ball.y = g_state.p1.y;
    } else {
        g_state.ball.x = g_state.p2.x - BALL_SIZE - 1;     // precis till vänster om P2
        g_state.ball.y = g_state.p2.y;
    }
}

// Enkel AI för P2-paddeln
static void pong_ai_update(void)
{
    int ai_speed;
    int target_y;

    if (g_diff == PONG_DIFF_HARD) {
        // HARD: följer bollen bra men inte perfekt
        ai_speed = 2;                              // lika snabb som bollen
        target_y = g_state.ball.y + (rand() % 5 - 2); // liten slump
    } else {
        // EASY: långsammare och dum
        ai_speed = 1;

        if (g_state.ball.vx > 0 && g_state.ball.x > (PONG_FIELD_W / 2)) {
            target_y = g_state.ball.y + 16;
        } else {
            target_y = PONG_FIELD_H / 2;
        }
    }

    if (target_y < g_state.p2.y) {
        g_state.p2.y -= ai_speed;
    } else if (target_y > g_state.p2.y) {
        g_state.p2.y += ai_speed;
    }

    clamp_paddle(&g_state.p2);
}

// Pingis-regel: först till 11, men vinn med minst 2
static int pong_check_winner(void)
{
    int p1 = g_state.score_p1;
    int p2 = g_state.score_p2;

    if (p1 >= 11 || p2 >= 11) {
        int diff = p1 - p2;
        if (diff >= 2) return 1;   // P1 vinner
        if (diff <= -2) return 2;  // P2 vinner
    }
    return 0;
}

// Initiera själva spelet (när vi lämnar menyerna)
static void pong_init_state(void)
{
    g_state.p1.h = PADDLE_H;
    g_state.p2.h = PADDLE_H;

    g_state.p1.x = PADDLE_MARGIN;
    g_state.p2.x = PONG_FIELD_W - PADDLE_MARGIN - PADDLE_W;

    // starta i mitten vertikalt
    g_state.p1.y = PONG_FIELD_H / 2;
    g_state.p2.y = PONG_FIELD_H / 2;

    g_state.score_p1 = 0;
    g_state.score_p2 = 0;

    // Första serven i den här matchen
    g_phase               = PONG_PHASE_SERVE;
    g_serve_player        = 1;              // P1 börjar serva
    g_serve_count         = 3;
    g_serve_frame_counter = 0;
    g_prev_countdown      = 0;

    g_winner              = 0;
    g_prev_winner_drawn   = 0;

    int sx = pong_ball_speed_x();
    int sy = pong_ball_speed_y();

    // P1 servar först → boll åt höger
    g_state.ball.vx = sx;
    g_state.ball.vy = sy;

    BACK_COLOR = BLACK;
    LCD_Clear(BLACK);

    // Placera bollen på serverns paddel
    pong_attach_ball_to_server();

    // Spara initiala positioner som "föregående" för partial redraw
    g_prev_p1   = g_state.p1;
    g_prev_p2   = g_state.p2;
    g_prev_ball = g_state.ball;
}

// ================== Speluppdatering ==================

static void pong_update(const GameInput_t *in)
{
    // --- 1. Paddelrörelse från input (P1) ---
    if (in->up   && g_state.p1.y > PADDLE_H / 2)
        g_state.p1.y -= PADDLE_SPEED;

    if (in->down && g_state.p1.y < PONG_FIELD_H - PADDLE_H / 2)
        g_state.p1.y += PADDLE_SPEED;

    clamp_paddle(&g_state.p1);

    // --- 2. P2 styrs av AI ---
    pong_ai_update();

    // --- 3. GAME OVER-läge ---
    if (g_phase == PONG_PHASE_GAME_OVER) {
        // Ny match direkt på FIRE (utan att gå till meny)
        if (in->fire) {
            g_state.score_p1 = 0;
            g_state.score_p2 = 0;

            g_phase               = PONG_PHASE_SERVE;
            g_serve_count         = 3;
            g_serve_frame_counter = 0;
            g_prev_countdown      = 0;
            g_prev_winner_drawn   = 0;

            int sx = pong_ball_speed_x();
            int sy = pong_ball_speed_y();

            // Vinnaren servar nästa game
            if (g_winner == 1) {
                g_serve_player   = 1;
                g_state.ball.vx  = sx;
            } else if (g_winner == 2) {
                g_serve_player   = 2;
                g_state.ball.vx  = -sx;
            } else {
                g_serve_player   = 1;
                g_state.ball.vx  = sx;
            }
            g_state.ball.vy = sy;
            pong_attach_ball_to_server();
            g_winner = 0;
        }

        // Ingen boll-rörelse i GAME OVER, bara paddlar/AI
        return;
    }

    // --- 4. SERVE-läge: bollen sitter på serverns paddel ---
    if (g_phase == PONG_PHASE_SERVE && g_serve_count > 0) {
        pong_attach_ball_to_server();
        return;
    }

    // När nedräkningen nått 0 i vPongTask sätts g_phase till PLAY.
    if (g_phase == PONG_PHASE_SERVE && g_serve_count <= 0) {
        g_phase = PONG_PHASE_PLAY;
    }

    if (g_phase != PONG_PHASE_PLAY) {
        return;
    }

    // --- 5. PLAY-läge: flytta bollen ---
    g_state.ball.x += g_state.ball.vx;
    g_state.ball.y += g_state.ball.vy;

    // --- 6. Kollision med topp/botten-vägg ---
    if (g_state.ball.y <= 0) {
        g_state.ball.y = 0;
        g_state.ball.vy = -g_state.ball.vy;
    }

    if (g_state.ball.y + BALL_SIZE >= PONG_FIELD_H) {
        g_state.ball.y = PONG_FIELD_H - BALL_SIZE;
        g_state.ball.vy = -g_state.ball.vy;
    }

    // --- 7. Kollision med paddlar ---
    int p1_top    = g_state.p1.y - g_state.p1.h / 2;
    int p1_bottom = g_state.p1.y + g_state.p1.h / 2;
    int p2_top    = g_state.p2.y - g_state.p2.h / 2;
    int p2_bottom = g_state.p2.y + g_state.p2.h / 2;

    // vänster paddel (P1)
    if (g_state.ball.x <= g_state.p1.x + PADDLE_W &&
        g_state.ball.x + BALL_SIZE >= g_state.p1.x &&
        g_state.ball.y + BALL_SIZE >= p1_top &&
        g_state.ball.y <= p1_bottom)
    {
        g_state.ball.x = g_state.p1.x + PADDLE_W;
        g_state.ball.vx = (g_state.ball.vx > 0) ? g_state.ball.vx : -g_state.ball.vx;

        int hit_pos = g_state.ball.y - g_state.p1.y;
        int sy = pong_ball_speed_y();

        if (hit_pos < 0) g_state.ball.vy = -sy;
        if (hit_pos > 0) g_state.ball.vy =  sy;
    }

    // höger paddel (P2)
    if (g_state.ball.x + BALL_SIZE >= g_state.p2.x &&
        g_state.ball.x <= g_state.p2.x + PADDLE_W &&
        g_state.ball.y + BALL_SIZE >= p2_top &&
        g_state.ball.y <= p2_bottom)
    {
        g_state.ball.x = g_state.p2.x - BALL_SIZE;
        g_state.ball.vx = (g_state.ball.vx < 0) ? g_state.ball.vx : -g_state.ball.vx;

        int hit_pos = g_state.ball.y - g_state.p2.y;
        int sy = pong_ball_speed_y();

        if (hit_pos < 0) g_state.ball.vy = -sy;
        if (hit_pos > 0) g_state.ball.vy =  sy;
    }

    // --- 8. Mål / poäng + pingis-logik / game over / ny serve ---
    if (g_state.ball.x < 0) {
        // P2 gör poäng
        g_state.score_p2++;

        int winner = pong_check_winner();

        if (winner != 0) {
            // Registrera highscore/statistik
            int diff = g_state.score_p1 - g_state.score_p2;
            g_games_played++;
            if (winner == 1) {
                g_p1_wins++;
                int margin = diff;
                if (margin < 0) margin = -margin;
                if (margin > g_best_margin) g_best_margin = margin;
            } else {
                g_p2_wins++;
            }

            // GAME OVER
            g_phase             = PONG_PHASE_GAME_OVER;
            g_winner            = winner;
            g_prev_countdown    = 0;
            g_prev_winner_drawn = 0;
        } else {
            // Ny serve från P2
            g_phase               = PONG_PHASE_SERVE;
            g_serve_player        = 2;
            g_serve_count         = 3;
            g_serve_frame_counter = 0;
            g_prev_countdown      = 0;

            int sx = pong_ball_speed_x();
            int sy = pong_ball_speed_y();

            g_state.ball.vx = -sx;   // mot vänster (mot P1)
            g_state.ball.vy =  sy;
            pong_attach_ball_to_server();
        }
    }
    else if (g_state.ball.x > PONG_FIELD_W) {
        // P1 gör poäng
        g_state.score_p1++;

        int winner = pong_check_winner();

        if (winner != 0) {
            int diff = g_state.score_p1 - g_state.score_p2;
            g_games_played++;
            if (winner == 1) {
                g_p1_wins++;
                int margin = diff;
                if (margin < 0) margin = -margin;
                if (margin > g_best_margin) g_best_margin = margin;
            } else {
                g_p2_wins++;
            }

            g_phase             = PONG_PHASE_GAME_OVER;
            g_winner            = winner;
            g_prev_countdown    = 0;
            g_prev_winner_drawn = 0;
        } else {
            // Ny serve från P1
            g_phase               = PONG_PHASE_SERVE;
            g_serve_player        = 1;
            g_serve_count         = 3;
            g_serve_frame_counter = 0;
            g_prev_countdown      = 0;

            int sx = pong_ball_speed_x();
            int sy = pong_ball_speed_y();

            g_state.ball.vx =  sx;   // mot höger (mot P2)
            g_state.ball.vy =  sy;
            pong_attach_ball_to_server();
        }
    }
}

// ================== Rendering (spel) ==================

static void draw_paddle(const Paddle_t *p, uint16_t color)
{
    int top    = p->y - p->h / 2;
    int bottom = p->y + p->h / 2;

    if (top < 0) top = 0;
    if (bottom >= PONG_FIELD_H) bottom = PONG_FIELD_H - 1;

    LCD_Fill(p->x,
             top,
             p->x + PADDLE_W - 1,
             bottom,
             color);
}

// Raderar bara "svansen" av paddeln från föregående läge
static void erase_paddle_tail(const Paddle_t *prev, const Paddle_t *curr)
{
    int prev_top    = prev->y - prev->h / 2;
    int prev_bottom = prev->y + prev->h / 2;
    int curr_top    = curr->y - curr->h / 2;
    int curr_bottom = curr->y + curr->h / 2;

    // Klipp inom spelplanen
    if (prev_top < 0) prev_top = 0;
    if (prev_bottom >= PONG_FIELD_H) prev_bottom = PONG_FIELD_H - 1;
    if (curr_top < 0) curr_top = 0;
    if (curr_bottom >= PONG_FIELD_H) curr_bottom = PONG_FIELD_H - 1;

    // Om ingen överlappning → radera hela gamla paddeln
    if (curr_bottom < prev_top || curr_top > prev_bottom) {
        LCD_Fill(prev->x,
                 prev_top,
                 prev->x + PADDLE_W - 1,
                 prev_bottom,
                 BLACK);
        return;
    }

    // Flytt uppåt: radera den nedre delen som blivit "svans"
    if (curr_top < prev_top) {
        int clear_top    = curr_bottom + 1;
        int clear_bottom = prev_bottom;
        if (clear_top <= clear_bottom) {
            LCD_Fill(prev->x,
                     clear_top,
                     prev->x + PADDLE_W - 1,
                     clear_bottom,
                     BLACK);
        }
    }
    // Flytt nedåt: radera den övre delen som blivit "svans"
    else if (curr_top > prev_top) {
        int clear_top    = prev_top;
        int clear_bottom = curr_top - 1;
        if (clear_top <= clear_bottom) {
            LCD_Fill(prev->x,
                     clear_top,
                     prev->x + PADDLE_W - 1,
                     clear_bottom,
                     BLACK);
        }
    }
}

static void draw_ball(const Ball_t *b, uint16_t color)
{
    LCD_Fill(b->x,
             b->y,
             b->x + BALL_SIZE - 1,
             b->y + BALL_SIZE - 1,
             color);
}

// Partial redraw + nedräkning + winner-text + scoreboard
static void pong_render(void)
{
    // 1) Rita paddlar och boll på NYA positioner först
    draw_paddle(&g_state.p1, WHITE);
    draw_paddle(&g_state.p2, WHITE);
    draw_ball(&g_state.ball, WHITE);

    // 2) Radera bara "svanstycken" från gamla paddelpositioner
    erase_paddle_tail(&g_prev_p1, &g_state.p1);
    erase_paddle_tail(&g_prev_p2, &g_state.p2);

    // 3) Radera bollen på gamla positionen
    draw_ball(&g_prev_ball, BLACK);

    // 4) Nedräkning / winner-text i mitten
    int cx1 = 0;
    int cy1 = PONG_FIELD_H / 2 - 10;
    int cx2 = PONG_FIELD_W - 1;
    int cy2 = PONG_FIELD_H / 2 + 10;

    if (g_phase == PONG_PHASE_SERVE && g_serve_count > 0) {
        if (g_prev_winner_drawn) {
            LCD_Fill(cx1, cy1, cx2, cy2, BLACK);
            g_prev_winner_drawn = 0;
        }

        if (g_serve_count != g_prev_countdown) {
            LCD_Fill(cx1, cy1, cx2, cy2, BLACK);
            LCD_ShowNum(PONG_FIELD_W / 2 - 3,
                        PONG_FIELD_H / 2 - 6,
                        g_serve_count,
                        1,
                        WHITE);
            g_prev_countdown = g_serve_count;
        }
    }
    else if (g_phase == PONG_PHASE_GAME_OVER && g_winner != 0) {
        if (g_prev_countdown != 0) {
            LCD_Fill(cx1, cy1, cx2, cy2, BLACK);
            g_prev_countdown = 0;
        }

        if (!g_prev_winner_drawn) {
            LCD_Fill(cx1, cy1, cx2, cy2, BLACK);
            const char *msg = (g_winner == 1) ? "P1 WINS" : "P2 WINS";
            LCD_ShowString(PONG_FIELD_W / 2 - 24,
                           PONG_FIELD_H / 2 - 6,
                           (u8*)msg,
                           WHITE);
            g_prev_winner_drawn = 1;
        }
    }
    else {
        if (g_prev_countdown != 0 || g_prev_winner_drawn) {
            LCD_Fill(cx1, cy1, cx2, cy2, BLACK);
            g_prev_countdown    = 0;
            g_prev_winner_drawn = 0;
        }
    }

    // 5) Scoreboard överst – liten bar
    LCD_Fill(0, 0, PONG_FIELD_W - 1, 10, BLACK);
    LCD_ShowNum(2, 2, g_state.score_p1, 2, WHITE);
    LCD_ShowNum(PONG_FIELD_W - 18, 2, g_state.score_p2, 2, WHITE);

    // 6) Uppdatera previous-structar till nästa frame
    g_prev_p1   = g_state.p1;
    g_prev_p2   = g_state.p2;
    g_prev_ball = g_state.ball;
}

// ================== Rendering (menyer / highscore) ==================

// MAIN MENU – styled like console menu
static void draw_main_menu(void)
{
    if (g_prev_menu_index == g_menu_index)
        return;

    BACK_COLOR = BLACK;
    LCD_Clear(BLACK);

    LCD_ShowStr(5, 8,  (u8*)"Pong", WHITE, OPAQUE);

    LCD_ShowStr(5, 30, (u8*)"1. Start Game", WHITE, OPAQUE);
    LCD_ShowStr(5, 45, (u8*)"2. Highscore",  WHITE, OPAQUE);
    LCD_ShowStr(5, 60, (u8*)"3. Exit",       WHITE, OPAQUE);

    // Arrow on the right side (same coordinates as console menu)
    Arrow_Show(g_menu_index);

    LCD_Wait_On_Queue();
    g_prev_menu_index = g_menu_index;
}

// DIFFICULTY MENU – same style
static void draw_diff_menu(void)
{
    if (g_prev_diff_index == g_diff_index)
        return;

    BACK_COLOR = BLACK;
    LCD_Clear(BLACK);

    LCD_ShowStr(5, 8, (u8*)"Difficulty", WHITE, OPAQUE);

    LCD_ShowStr(5, 30, (u8*)"1. Easy", WHITE, OPAQUE);
    LCD_ShowStr(5, 45, (u8*)"2. Hard", WHITE, OPAQUE);

    Arrow_Show(g_diff_index);

    LCD_Wait_On_Queue();
    g_prev_diff_index = g_diff_index;
}

// PAUSE MENU – same style
static void draw_pause_menu(void)
{
    if (g_prev_pause_index == g_pause_index)
        return;

    BACK_COLOR = BLACK;
    LCD_Clear(BLACK);

    LCD_ShowStr(5, 8, (u8*)"Paused", WHITE, OPAQUE);

    const char *item0 = "1. Resume Game";
    const char *item2 = "3. Main Menu";
    const char *item1 = (g_diff == PONG_DIFF_EASY)
                        ? "2. Difficulty: Easy"
                        : "2. Difficulty: Hard";

    LCD_ShowStr(5, 30, (u8*)item0, WHITE, OPAQUE);
    LCD_ShowStr(5, 45, (u8*)item1, WHITE, OPAQUE);
    LCD_ShowStr(5, 60, (u8*)item2, WHITE, OPAQUE);

    Arrow_Show(g_pause_index);

    LCD_Wait_On_Queue();
    g_prev_pause_index = g_pause_index;
}

static void draw_highscore_screen(void)
{
    BACK_COLOR = BLACK;
    LCD_Clear(BLACK);
    LCD_ShowString(40, 5, (u8*)"HIGHSCORE", WHITE);

    LCD_ShowString(5, 20, (u8*)"GAMES", WHITE);
    LCD_ShowNum(70, 20, g_games_played, 2, WHITE);

    LCD_ShowString(5, 34, (u8*)"P1 WINS", WHITE);
    LCD_ShowNum(70, 34, g_p1_wins, 2, WHITE);

    LCD_ShowString(5, 48, (u8*)"P2 WINS", WHITE);
    LCD_ShowNum(70, 48, g_p2_wins, 2, WHITE);

    LCD_ShowString(5, 62, (u8*)"BEST P1 +", WHITE);
    LCD_ShowNum(70, 62, g_best_margin, 2, WHITE);
}

// ================== FreeRTOS-task ==================

void vPongTask(void *pvParameters)
{
    (void)pvParameters;

    const TickType_t xTick = pdMS_TO_TICKS(PONG_TICK_MS);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    GameInput_t input = {0};

    // Starta i huvudmenyn
    g_mode = PONG_MODE_MENU;
    g_menu_index = 0;
    g_prev_menu_index = -1;
    BACK_COLOR = BLACK;
    LCD_Clear(BLACK);
    draw_main_menu();

    for (;;)
    {
        // hämta senaste knappstatus (icke-blockerande)
        GameInput_t newInput;
        if (xQueueReceive(xInputQueue, &newInput, 0) == pdPASS) {
            input = newInput;
        }

        // Edge-detektion för knappar (för menyer)
        uint8_t up_edge    = (input.up    && !g_prev_btn_up);
        uint8_t down_edge  = (input.down  && !g_prev_btn_down);
        uint8_t fire_edge  = (input.fire  && !g_prev_btn_fire);
        uint8_t pause_edge = (input.pause && !g_prev_btn_pause);

        g_prev_btn_up    = input.up;
        g_prev_btn_down  = input.down;
        g_prev_btn_fire  = input.fire;
        g_prev_btn_pause = input.pause;

        // Serve-nedräkning: bara när vi är i GAME-mode och i SERVE-fas
        if (g_mode == PONG_MODE_GAME &&
            g_phase == PONG_PHASE_SERVE &&
            g_serve_count > 0)
        {
            g_serve_frame_counter++;

            int frames_per_second = 1000 / PONG_TICK_MS; // t.ex. 100 vid 10 ms
            if (g_serve_frame_counter >= frames_per_second) {
                g_serve_frame_counter = 0;
                g_serve_count--;
                // När g_serve_count blir 0, får pong_update byta till PLAY
            }
        }

        // Mode-hantering
        switch (g_mode) {
        case PONG_MODE_MENU:
            // Flytta markör
            if (up_edge && g_menu_index > 0) g_menu_index--;
            if (down_edge && g_menu_index < 2) g_menu_index++;

            // Välj
            if (fire_edge) {
                if (g_menu_index == 0) {
                    // Start new game -> diff menu
                    g_mode = PONG_MODE_DIFF_SELECT;
                    g_diff_index = (g_diff == PONG_DIFF_EASY) ? 0 : 1;
                    g_prev_diff_index = -1;
                    BACK_COLOR = BLACK;
                    LCD_Clear(BLACK);
                    draw_diff_menu();
                } else if (g_menu_index == 1) {
                    // Highscore
                    g_mode = PONG_MODE_HIGHSCORE;
                    draw_highscore_screen();
                } else if (g_menu_index == 2) {
                    // Exit game → avsluta Pong-task
                    BACK_COLOR = BLACK;
                    LCD_Clear(BLACK);
                    LCD_ShowString(30, PONG_FIELD_H / 2 - 6, (u8*)"EXIT PONG", WHITE);
                    LCD_Wait_On_Queue();
                    vTaskDelay(pdMS_TO_TICKS(500));
                    vTaskDelete(NULL);  // senare: hoppa tillbaka till konsolmeny
                }
            }

            draw_main_menu();
            break;

        case PONG_MODE_DIFF_SELECT:
            // Toggle mellan Easy/Hard på valfri upp/ner
            if (up_edge || down_edge) {
                g_diff_index ^= 1; // 0↔1
            }

            if (fire_edge) {
                g_diff = (g_diff_index == 0) ? PONG_DIFF_EASY : PONG_DIFF_HARD;

                // Starta spelet
                pong_init_state();
                g_mode = PONG_MODE_GAME;
            }

            draw_diff_menu();
            break;

        case PONG_MODE_HIGHSCORE:
            // Tillbaka till main menu på FIRE
            if (fire_edge) {
                g_mode = PONG_MODE_MENU;
                g_prev_menu_index = -1;
                BACK_COLOR = BLACK;
                LCD_Clear(BLACK);
                draw_main_menu();
            }
            break;

        case PONG_MODE_GAME:
            // Öppna pausmeny på PAUSE-knappens kant,
            // men inte när vi redan är i GAME OVER
            if (pause_edge && g_phase != PONG_PHASE_GAME_OVER) {
                g_mode = PONG_MODE_PAUSE;
                g_pause_index = 0;
                g_prev_pause_index = -1;

                BACK_COLOR = BLACK;
                LCD_Clear(BLACK);
                draw_pause_menu();
                break;
            }

            // Vanlig spel-logik + rendering
            pong_update(&input);
            pong_render();
            break;

        case PONG_MODE_PAUSE:
            // Flytta markören upp/ner
            if (up_edge && g_pause_index > 0)       g_pause_index--;
            if (down_edge && g_pause_index < 2)     g_pause_index++;

            if (fire_edge) {
                if (g_pause_index == 0) {
                    // [0] RESUME GAME
                    BACK_COLOR = BLACK;
                    LCD_Clear(BLACK);

                    g_mode = PONG_MODE_GAME;
                }
                else if (g_pause_index == 1) {
                    // [1] ÄNDRA SVÅRIGHET – toggla EASY/HARD men stanna kvar i paus
                    if (g_diff == PONG_DIFF_EASY) {
                        g_diff = PONG_DIFF_HARD;
                    } else {
                        g_diff = PONG_DIFF_EASY;
                    }
                    g_prev_pause_index = -1; // tvinga omritning
                }
                else if (g_pause_index == 2) {
                    // [2] MAIN MENU – avsluta matchen och gå tillbaka
                    g_mode = PONG_MODE_MENU;
                    g_prev_menu_index = -1;

                    BACK_COLOR = BLACK;
                    LCD_Clear(BLACK);
                    draw_main_menu();
                    break;
                }
            }

            draw_pause_menu();
            break;
        }

        vTaskDelayUntil(&xLastWakeTime, xTick);
    }
}
