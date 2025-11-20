/* Minimal Fly 'n' Shoot prototype
   - Player: one rectangle at bottom, can move left/right and shoot
   - Bullets: simple vertical lines
   - Enemies: simple rectangles that move down
   Uses existing LCD drawing functions and keyscan() for input
*/

#include "game.h"
#include "lcd.h"
#include "drivers.h" // for keyscan
#include <stdlib.h>

#include <stdio.h>

// Game constants
#define PLAYER_W 12
#define PLAYER_H 6
#define BULLET_W 4
#define BULLET_H 4
#define ENEMY_W 12
#define ENEMY_H 6
#define MISSILE_W 8
#define MISSILE_H 8
#define MAX_BULLETS 8
#define MAX_ENEMIES 6

// Auto-fire interval (ms) when holding the fire key
#define FIRE_INTERVAL_MS 150

static int player_x, player_y;

typedef struct
{
    int x, y;
    int prev_x, prev_y;
    int state;
    int type; // 0 = normal bullet, 1 = missile
    int prev_type;
} bullet_t;
static bullet_t bullets[MAX_BULLETS];

typedef struct
{
    int x, y;
    int prev_x, prev_y;
    int state;
    int hit_timer; // frames remaining for explosion/explode effect
} enemy_t;
static enemy_t enemies[MAX_ENEMIES];

// Explosion effects triggered by missiles. Explosions are independent from
// enemies and provide a short-lived visual and apply damage to enemies
// inside the explosion rect.
#define MAX_EXPLOSIONS 4
#define EXPLOSION_DURATION 6
typedef struct
{
    int active;
    int x, y;
    int w, h;
    int timer; // frames remaining
} explosion_t;
static explosion_t explosions[MAX_EXPLOSIONS];

static int frame_count;
// game score (displayed in corner)
static int score = 0;
// enemy falling speed in pixels per frame; changeable at runtime via Game_SetEnemySpeed
static int enemy_speed = 1;
// player health
static int player_health = 3;
// debug values to show last seen raw/mapped key
static int debug_raw = -1;
static int debug_mapped = -1;
// key repeat helper from your snippet
static int pKey = -1;
static int crep = 0;
// debug: raw GPIOA input port value
static int debug_port = 0;

// Fire cooldown in milliseconds; when zero we may fire again.
static int fire_cooldown_ms = 0;

// human-readable action for last seen mapped key
static const char *debug_action = NULL;

// Keyboard lookup from the project (maps raw scanner index to logical key id)
static const int lookUpTbl[16] = {1, 4, 7, 14, 2, 5, 8, 0, 3, 6, 9, 15, 10, 11, 12, 13};

// Bullet/enemy state enums
#define BS_INACTIVE 0
#define BS_ACTIVE 1

#define ES_DEAD 0
#define ES_ALIVE 1
#define ES_EXPLODING 2

// The public KEY_* macros are declared in game.h. Provide a helper to map
// raw indices to their logical ids.
int Game_MapRawKey(int raw)
{
    if (raw < 0 || raw >= 16)
        return -1;
    return lookUpTbl[raw];
}

// Helper: clipped fill to avoid passing negative coordinates to LCD_Fill
static void draw_clipped(int x1, int y1, int x2, int y2, u16 color)
{
    int xa = x1 < 0 ? 0 : x1;
    int ya = y1 < 0 ? 0 : y1;
    int xb = x2 > (LCD_W - 1) ? (LCD_W - 1) : x2;
    int yb = y2 > (LCD_H - 1) ? (LCD_H - 1) : y2;
    if (xa > xb || ya > yb)
        return;
    LCD_Fill((u16)xa, (u16)ya, (u16)xb, (u16)yb, color);
}

// forward declarations so helper can call them without implicit non-static prototypes
static void fire_bullet(void);
static void fire_projectile(int type);

// Perform one keyboard scan tick: advance column, read key, update pKey/crep and debug vars.
// Returns 1 if a key was seen, 0 otherwise.
int KeyScan_Tick(void)
{
    int key = keyscan();
    if (key >= 0)
    {
        // decrement fire cooldown (KeyScan_Tick is called every 1 ms)
        if (fire_cooldown_ms > 0)
            fire_cooldown_ms--;

        if (pKey == key)
            crep++;
        else
        {
            crep = 0;
            pKey = key;
        }
        int raw = key;
        int mapped = -1;
        if (raw >= 0 && raw < 16)
            // mapped = lookUpTbl[raw];
            mapped = Game_MapRawKey(raw);
        debug_raw = raw;
        debug_mapped = mapped;
        // act on first press and on repeats
        if (crep == 0 || (crep > 3 && (crep % 3) == 0))
        {
            if (mapped == KEY_LEFT_ID)
            {
                player_x -= 2;
                debug_action = "LEFT";
            }
            else if (mapped == KEY_RIGHT_ID)
            {
                player_x += 2;
                debug_action = "RIGHT";
            }
            else if (mapped == KEY_FIRE_ID || mapped == KEY_FIRE_ALT_ID)
            {
                // support continuous fire while holding: enforce a small cooldown
                if (fire_cooldown_ms == 0)
                {
                    if (mapped == KEY_FIRE_ID)
                    {
                        fire_projectile(0);
                        debug_action = "FIRE";
                        debug_mapped = KEY_FIRE_ID;
                    }
                    else
                    {
                        fire_projectile(1);
                        debug_action = "MISSILE";
                        debug_mapped = KEY_FIRE_ALT_ID;
                    }
                    fire_cooldown_ms = FIRE_INTERVAL_MS;
                }
                else
                {
                    debug_action = "HOLD"; // indicate button held but cooling down
                }
            }
            else
                debug_action = "KEY";
        }
        return 1;
    }
    return 0;
}

void spawn_enemy(int i, int x)
{
    enemies[i].x = x;
    enemies[i].y = 0;
    enemies[i].state = ES_ALIVE;
    enemies[i].prev_x = enemies[i].x;
    enemies[i].prev_y = enemies[i].y;
    enemies[i].hit_timer = 0;
}

void Game_Init(void)
{
    // Place player near bottom center
    player_x = (LCD_W - PLAYER_W) / 2;
    player_y = LCD_H - PLAYER_H - 2;

    for (int i = 0; i < MAX_BULLETS; i++)
    {
        bullets[i].state = BS_INACTIVE;
        bullets[i].type = 0;
        bullets[i].prev_type = 0;
    }
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        enemies[i].state = ES_DEAD;
        enemies[i].prev_x = 0;
        enemies[i].prev_y = 0;
    }
    for (int i = 0; i < MAX_EXPLOSIONS; i++)
    {
        explosions[i].active = 0;
        explosions[i].timer = 0;
        explosions[i].x = explosions[i].y = explosions[i].w = explosions[i].h = 0;
    }
    frame_count = 0;

    LCD_Clear(BLACK);
}

// type: 0 = normal bullet, 1 = missile
static void fire_projectile(int type)
{
    // If firing a missile (type==1) ensure only one missile can exist at a time.
    if (type == 1)
    {
        for (int mi = 0; mi < MAX_BULLETS; mi++)
        {
            if (bullets[mi].state == BS_ACTIVE && bullets[mi].type == 1)
                return; // already have an active missile
        }
    }
    for (int i = 0; i < MAX_BULLETS; i++)
    {
        if (bullets[i].state == BS_INACTIVE)
        {
            if (type == 0)
            {
                bullets[i].x = player_x + PLAYER_W / 2 - BULLET_W / 2;
                bullets[i].y = player_y - BULLET_H;
            }
            else
            {
                bullets[i].x = player_x + PLAYER_W / 2 - MISSILE_W / 2;
                bullets[i].y = player_y - MISSILE_H;
            }
            bullets[i].prev_x = bullets[i].x;
            bullets[i].prev_y = bullets[i].y;
            bullets[i].type = type;
            bullets[i].prev_type = type;
            bullets[i].state = BS_ACTIVE;
            break;
        }
    }
}

static void fire_bullet(void)
{
    fire_projectile(0);
}

// High-level event handler used by FreeRTOS GameTask: performs actions
// such as moving the player or firing while keeping state encapsulated.
void Game_HandleEvent(GameEvent_t ev)
{
    if (ev == GE_LEFT)
    {
        player_x -= PLAYER_W;
        debug_action = "LEFT";
        debug_mapped = KEY_LEFT_ID;
    }
    else if (ev == GE_RIGHT)
    {
        player_x += PLAYER_W;
        debug_action = "RIGHT";
        debug_mapped = KEY_RIGHT_ID;
    }
    else if (ev == GE_FIRE)
    {
        fire_bullet();
        debug_action = "FIRE";
        debug_mapped = KEY_FIRE_ID;
    }
    else if (ev == GE_FIRE_ALT)
    {
        fire_projectile(1);
        debug_action = "MISSILE";
        debug_mapped = KEY_FIRE_ALT_ID;
    }

    if (player_x < 0)
        player_x = 0;
    if (player_x > LCD_W - PLAYER_W)
        player_x = LCD_W - PLAYER_W;
}

void Game_Update(void)
{
    frame_count++;

    // Process any pending 1ms keyboard ticks so keyscan() debounce/repeat works
    // This mirrors the example main loop which calls t5expq, colset, l88row, keyscan every ms.
    int raw = -1;
    int mapped = -1;
    // Do not clear debug_action or debug_mapped here; events should set them
    // from Game_HandleEvent so they remain visible after event processing.

    while (t5expq())
    {
        KeyScan_Tick();
    }

    // If no key was reported during the t5expq drains, do a quick scan across columns
    // This helps when the timer isn't ticking or to snapshot the inputs once per frame.
    if (debug_raw < 0)
    {
        for (int i = 0; i < 4; i++)
        {
            if (KeyScan_Tick())
                break;
        }
    }

    // Read raw GPIOA input status to help debugging (bits 5..8 used by keyboard)
    debug_port = gpio_input_port_get(GPIOA);

    if (player_x < 0)
        player_x = 0;
    if (player_x > LCD_W - PLAYER_W)
        player_x = LCD_W - PLAYER_W;

    // Update bullets (store previous positions for partial redraw)
    for (int i = 0; i < MAX_BULLETS; i++)
    {
        if (bullets[i].state == BS_ACTIVE)
        {
            bullets[i].prev_x = bullets[i].x;
            bullets[i].prev_y = bullets[i].y;
            // speed depends on projectile type
            int speed = (bullets[i].type == 1) ? 3 : 4;
            bullets[i].y -= speed; // speed per frame (tuned with FPS)
            // If projectile moved off the top of the screen, handle deactivation
            if (bullets[i].type == 1)
            {
                if (bullets[i].y + MISSILE_H <= 0)
                {
                    // trigger explosion at top and deactivate
                    // create explosion centered on missile
                    int cx = bullets[i].x + MISSILE_W / 2;
                    int cy = bullets[i].y + MISSILE_H / 2;
                    // explosion rectangle size: enemy width/height as requested
                    int ex = cx - (ENEMY_W / 2);
                    int ey = cy - (ENEMY_H / 2);
                    // create explosion slot
                    for (int exi = 0; exi < MAX_EXPLOSIONS; exi++)
                    {
                        if (!explosions[exi].active)
                        {
                            explosions[exi].active = 1;
                            explosions[exi].x = ex;
                            explosions[exi].y = ey;
                            explosions[exi].w = ENEMY_W;
                            explosions[exi].h = ENEMY_H;
                            explosions[exi].timer = EXPLOSION_DURATION;
                            break;
                        }
                    }
                    bullets[i].state = BS_INACTIVE;
                }
            }
            else
            {
                if (bullets[i].y + BULLET_H <= 0)
                {
                    bullets[i].state = BS_INACTIVE;
                }
            }
        }
    }

    // Spawn enemies periodically
    if ((frame_count & 31) == 0)
    {
        // find free enemy slot
        for (int i = 0; i < MAX_ENEMIES; i++)
        {
            if (enemies[i].state == ES_DEAD)
            {
                // choose a spawn column aligned to PLAYER_W so the player can stand under it
                int maxOffset = LCD_W - ENEMY_W;
                int columns = (maxOffset / PLAYER_W) + 1; // how many aligned columns fit
                if (columns <= 0)
                    columns = 1;
                int colIndex = rand() % columns;
                int x = colIndex * PLAYER_W;
                // clamp just in case
                if (x > maxOffset)
                    x = maxOffset;
                spawn_enemy(i, x);
                break;
            }
        }
    }

    // Move enemies
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        if (enemies[i].state == ES_ALIVE || enemies[i].state == ES_EXPLODING)
        {
            enemies[i].prev_x = enemies[i].x;
            enemies[i].prev_y = enemies[i].y;
            // if enemy is in explosion state, decrement timer and remove when finished
            if (enemies[i].hit_timer > 0)
            {
                enemies[i].hit_timer--;
                if (enemies[i].hit_timer == 0)
                {
                    enemies[i].state = ES_DEAD;
                }
            }
            else
            {
                enemies[i].y += enemy_speed; // slow descent (pixels per frame)
                if (enemies[i].y > LCD_H)
                    enemies[i].state = ES_DEAD;
            }
        }
    }

    // Collision: enemies vs player
    for (int e = 0; e < MAX_ENEMIES; e++)
    {
        if (enemies[e].state == ES_DEAD)
            continue;
        if (enemies[e].x + ENEMY_W > player_x && enemies[e].x < player_x + PLAYER_W &&
            enemies[e].y + ENEMY_H > player_y && enemies[e].y < player_y + PLAYER_H){
            // enemy touches player: remove enemy and damage player
            enemies[e].state = ES_DEAD;
            // give a small visual hit effect via hit_timer so renderer clears properly
            enemies[e].hit_timer = 0;
            if (player_health > 0)
                player_health -= 1;
            if (player_health <= 0)
            {
                player_health = 0;
                debug_action = "GAMEOVER";
                // pause the game when player dies
                Game_SetPause(1);
            }
            else
            {
                debug_action = "HIT";
            }
        }
    }

    // Collision: bullets vs enemies
    for (int b = 0; b < MAX_BULLETS; b++)
        if (bullets[b].state == BS_ACTIVE)
        {
            for (int e = 0; e < MAX_ENEMIES; e++)
                if (enemies[e].state != ES_DEAD)
                {
                    // Check collision using appropriate projectile dimensions
                    int bw = (bullets[b].type == 1) ? MISSILE_W : BULLET_W;
                    int bh = (bullets[b].type == 1) ? MISSILE_H : BULLET_H;
                    if (bullets[b].x + bw > enemies[e].x && bullets[b].x < enemies[e].x + ENEMY_W &&
                        bullets[b].y < enemies[e].y + ENEMY_H && bullets[b].y + bh > enemies[e].y)
                    {
                        if (bullets[b].type == 1)
                        {
                            // Missile: explode with the width of an enemy and damage all enemies in radius
                            int cx = bullets[b].x + bw / 2;
                            int cy = bullets[b].y + bh / 2;
                            int ex = cx - (ENEMY_W / 2);
                            int ey = cy - (ENEMY_H / 2);
                            // create explosion slot
                            for (int exi = 0; exi < MAX_EXPLOSIONS; exi++)
                            {
                                if (!explosions[exi].active)
                                {
                                    explosions[exi].active = 1;
                                    explosions[exi].x = ex;
                                    explosions[exi].y = ey;
                                    explosions[exi].w = ENEMY_W;
                                    explosions[exi].h = ENEMY_H;
                                    explosions[exi].timer = EXPLOSION_DURATION;
                                    break;
                                }
                            }
                            // apply damage to all enemies overlapping the explosion rect
                            for (int ee = 0; ee < MAX_ENEMIES; ee++)
                            {
                                if (enemies[ee].state == ES_DEAD)
                                    continue;
                                if (enemies[ee].x + ENEMY_W > ex && enemies[ee].x < ex + ENEMY_W &&
                                    enemies[ee].y + ENEMY_H > ey && enemies[ee].y < ey + ENEMY_H)
                                {
                                    enemies[ee].hit_timer = EXPLOSION_DURATION;
                                    enemies[ee].state = ES_EXPLODING;
                                    score += 10; // missile gives more points per enemy
                                    enemies[ee].prev_x = enemies[ee].x;
                                    enemies[ee].prev_y = enemies[ee].y;
                                }
                            }
                            // deactivate missile
                            bullets[b].state = BS_INACTIVE;
                        }
                        else
                        {
                            // normal bullet: single-target hit
                            bullets[b].state = BS_INACTIVE;
                            enemies[e].hit_timer = 5; // number of frames to show explosion
                            enemies[e].state = ES_EXPLODING;
                            score += 10;
                            enemies[e].prev_x = enemies[e].x;
                            enemies[e].prev_y = enemies[e].y;
                        }
                    }
                }
            }

            // Update explosions: countdown timers and clear finished ones
    for (int i = 0; i < MAX_EXPLOSIONS; i++)
    {
        if (explosions[i].active)
        {
            explosions[i].timer--;
            if (explosions[i].timer <= 0)
            {
                // clear explosion rect immediately so renderer won't leave artifacts
                draw_clipped(explosions[i].x, explosions[i].y, explosions[i].x + explosions[i].w - 1,
                             explosions[i].y + explosions[i].h - 1, BLACK);
                explosions[i].active = 0;
                explosions[i].timer = 0;
            }
        }
    }
}

void Game_Render(void)
{
    // Partial redraw: erase previous rects and draw only changed areas.

    
    // ---------- Debug: show score and mapped key values at top-left ----------
    // Score replaces the previous raw lookup display
    LCD_ShowNum(0, 0, (u16)score, 3, WHITE);
    if (debug_mapped >= 0){
        // LCD_ShowString(40, 0, (const u8 *)"SCORE", WHITE);
        LCD_ShowNum(40, 0, (u16)debug_mapped, 2, WHITE);
    }
    // else
    //     LCD_ShowString(40, 0, (const u8 *)"--", WHITE);

    // // Show last action string at top-right
    // if (debug_action)
    //     LCD_ShowString(130, 0, (const u8 *)debug_action, WHITE);
    // else
    //     LCD_ShowString(130, 0, (const u8 *)"    ", WHITE);
    
    // // Show raw GPIOA port (shifted) so we can see row bits (display at x=140)
    // int rows = (debug_port >> 5) & 0x0F;
    // LCD_ShowNum(140, 0, (u16)rows, 1, WHITE);
    // ----------------------------------------------------------------------
    
    // Show player health in the top-right (replaces debug_action)
    LCD_ShowString(100, 0, (const u8 *)"HP", WHITE);
    LCD_ShowNum(116, 0, (u16)player_health, 1, WHITE);

    // Erase and redraw player
    static int player_prev_x = -1, player_prev_y = -1;
    if (player_prev_x >= 0)
    {
        draw_clipped(player_prev_x, player_prev_y, player_prev_x + PLAYER_W - 1, player_prev_y + PLAYER_H - 1, BLACK);
    }
    draw_clipped(player_x, player_y, player_x + PLAYER_W - 1, player_y + PLAYER_H - 1, GREEN);
    player_prev_x = player_x;
    player_prev_y = player_y;

    // Erase and redraw bullets
    for (int i = 0; i < MAX_BULLETS; i++)
    {
        int px = bullets[i].prev_x;
        int py = bullets[i].prev_y;
        if (bullets[i].state == BS_ACTIVE)
        {
            // erase previous position using previous projectile size
            {
                int p_w = (bullets[i].prev_type == 1) ? MISSILE_W : BULLET_W;
                int p_h = (bullets[i].prev_type == 1) ? MISSILE_H : BULLET_H;
                draw_clipped(px, py, px + p_w - 1, py + p_h - 1, BLACK);
            }
            // draw current using current projectile size and color
            if (bullets[i].type == 1)
                draw_clipped(bullets[i].x, bullets[i].y, bullets[i].x + MISSILE_W - 1, bullets[i].y + MISSILE_H - 1, WHITE);
            else
                draw_clipped(bullets[i].x, bullets[i].y, bullets[i].x + BULLET_W - 1, bullets[i].y + BULLET_H - 1, YELLOW);
        }
        else
        {
            // if recently deactivated, ensure previous area is cleared
            if (px != 0 || py != 0)
            {
                int p_w = (bullets[i].prev_type == 1) ? MISSILE_W : BULLET_W;
                int p_h = (bullets[i].prev_type == 1) ? MISSILE_H : BULLET_H;
                draw_clipped(px, py, px + p_w - 1, py + p_h - 1, BLACK);
            }
        }
    }

    // Erase and redraw enemies
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        int px = enemies[i].prev_x;
        int py = enemies[i].prev_y;
        if (enemies[i].state != ES_DEAD)
        {
            // erase previous position
            draw_clipped(px, py, px + ENEMY_W - 1, py + ENEMY_H - 1, BLACK);
            // if hit_timer > 0 show explosion color (use bullet color YELLOW), else normal color
            if (enemies[i].hit_timer > 0)
            {
                draw_clipped(enemies[i].x, enemies[i].y, enemies[i].x + ENEMY_W - 1, enemies[i].y + ENEMY_H - 1, YELLOW);
            }
            else
            {
                draw_clipped(enemies[i].x, enemies[i].y, enemies[i].x + ENEMY_W - 1, enemies[i].y + ENEMY_H - 1, BLUE);
            }
        }
        else
        {
            if (px != 0 || py != 0)
                draw_clipped(px, py, px + ENEMY_W - 1, py + ENEMY_H - 1, BLACK);
        }
    }
    
    // Draw active explosions on top
    for (int i = 0; i < MAX_EXPLOSIONS; i++)
    {
        if (explosions[i].active)
        {
            draw_clipped(explosions[i].x, explosions[i].y, explosions[i].x + explosions[i].w - 1,
                         explosions[i].y + explosions[i].h - 1, YELLOW);
        }
    }
}

// Setter to adjust enemy falling speed (pixels per frame). Use 0 to pause enemies.
void Game_SetEnemySpeed(int speed)
{
    if (speed < 0)
        speed = 0;
    enemy_speed = speed;
}

// Score accessor
int Game_GetScore(void)
{
    return score;
}

void Game_Reset(void)
{
    score = 0;
    player_health = 3;
    player_x = (LCD_W - PLAYER_W) / 2;
    player_y = LCD_H - PLAYER_H - 2;
    frame_count = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        enemies[i].state = ES_DEAD; // reset enemies
    }
    // clear screen
    LCD_Clear(BLACK);
}