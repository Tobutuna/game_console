#include "arrow.h"
#include "delay.h"
#include "gd32vf103.h"
#include "lcd.h"

// -----------------------------------------------------------------------------
// Pin mapping
// -----------------------------------------------------------------------------
#define BUTTON_UP_PIN        GPIO_PIN_6    // PB6
#define BUTTON_DOWN_PIN      GPIO_PIN_7    // PB7
#define BUTTON_SELECT_PIN    GPIO_PIN_5    // PB5 → Select
#define BUTTON_BACK_PIN      GPIO_PIN_8    // PB8 → Back
#define BUTTON_LEFT_PIN      GPIO_PIN_4    // PB4 → Left
#define BUTTON_RIGHT_PIN     GPIO_PIN_9    // PB9 → Right
#define BUTTON_PORT          GPIOB
#define BUTTON_SELECT2_PIN   GPIO_PIN_8    // PA8 → Select_2 (ALT fire)
#define BUTTON_SELECT2_PORT  GPIOA

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------
static int current_selection = 0;

// One static state per button for edge-detection
static uint8_t up_state    = 0;
static uint8_t down_state  = 0;
static uint8_t sel_state   = 0;
static uint8_t back_state  = 0;
static uint8_t left_state  = 0;
static uint8_t right_state = 0;
static uint8_t sel2_state  = 0;

// -----------------------------------------------------------------------------
// Generic helpers
// -----------------------------------------------------------------------------

// Edge detector for active-low buttons (returns 1 once per press)
static uint8_t btn_edge(uint32_t port, uint16_t pin, uint8_t *state)
{
    // Active-low: pressed when pin reads 0
    uint8_t pressed_now = (gpio_input_bit_get(port, pin) == RESET);

    if (pressed_now && !(*state)) {
        // simple debounce
        delay_1ms(20);
        if (gpio_input_bit_get(port, pin) == RESET) {
            *state = 1;
            return 1;   // new press
        }
    } else if (!pressed_now && *state) {
        // released -> arm for next press
        *state = 0;
    }

    return 0;
}

// Level (held) state for active-low buttons (no edge detection)
static uint8_t btn_level(uint32_t port, uint16_t pin)
{
    return (gpio_input_bit_get(port, pin) == RESET) ? 1 : 0;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void Arrow_Init(void)
{
    // GPIO clocks are enabled in main.c, but it is safe to enable again if needed.
    // rcu_periph_clock_enable(RCU_GPIOA);
    // rcu_periph_clock_enable(RCU_GPIOB);

    // Configure PB4,5,6,7,8,9 as input with pull-up
    gpio_init(BUTTON_PORT,
              GPIO_MODE_IPU,
              GPIO_OSPEED_50MHZ,
              BUTTON_UP_PIN    |
              BUTTON_DOWN_PIN  |
              BUTTON_SELECT_PIN|
              BUTTON_BACK_PIN  |
              BUTTON_LEFT_PIN  |
              BUTTON_RIGHT_PIN);

    // Configure PA9 (Select_2) as input with pull-up
    gpio_init(BUTTON_SELECT2_PORT,
              GPIO_MODE_IPU,
              GPIO_OSPEED_50MHZ,
              BUTTON_SELECT2_PIN);

    current_selection = 0;

    up_state    = 0;
    down_state  = 0;
    sel_state   = 0;
    back_state  = 0;
    left_state  = 0;
    right_state = 0;
    sel2_state  = 0;
}

void Arrow_Show(int selected)
{
    const uint16_t base_y = 30;
    const uint16_t step   = 15;

    // We now support up to 4 menu entries (0..3).
    // This matches the y-positions of your text:
    // 0 -> 30, 1 -> 45, 2 -> 60, 3 -> 75.
    for (int i = 0; i < 4; ++i) {
        LCD_ShowStr(147, base_y + i * step,
                    (const uint8_t *)" ",
                    BLACK, OPAQUE);
    }

    // Draw arrow at new position if it's within 0..3
    if (selected >= 0 && selected < 4) {
        LCD_ShowStr(147, base_y + selected * step,
                    (const uint8_t *)"<",
                    YELLOW, OPAQUE);
    }

    LCD_Wait_On_Queue();
}


void Arrow_Up(void)
{
    current_selection = (current_selection == 0) ? 2 : (current_selection - 1);
    Arrow_Show(current_selection);
}

void Arrow_Down(void)
{
    current_selection = (current_selection == 2) ? 0 : (current_selection + 1);
    Arrow_Show(current_selection);
}

uint8_t Arrow_GetSelection(void)
{
    return (uint8_t)current_selection;
}

// Edge-detected button queries
uint8_t Arrow_Up_Pressed(void)
{
    return btn_edge(BUTTON_PORT, BUTTON_UP_PIN, &up_state);
}

uint8_t Arrow_Down_Pressed(void)
{
    return btn_edge(BUTTON_PORT, BUTTON_DOWN_PIN, &down_state);
}

uint8_t Arrow_Select_Pressed(void)
{
    return btn_edge(BUTTON_PORT, BUTTON_SELECT_PIN, &sel_state);
}

uint8_t Arrow_Back_Pressed(void)
{
    return btn_edge(BUTTON_PORT, BUTTON_BACK_PIN, &back_state);
}

uint8_t Arrow_Left_Pressed(void)
{
    return btn_edge(BUTTON_PORT, BUTTON_LEFT_PIN, &left_state);
}

uint8_t Arrow_Right_Pressed(void)
{
    return btn_edge(BUTTON_PORT, BUTTON_RIGHT_PIN, &right_state);
}

uint8_t Arrow_Select2_Pressed(void)
{
    return btn_edge(BUTTON_SELECT2_PORT, BUTTON_SELECT2_PIN, &sel2_state);
}

// Level (held) state for continuous game motion
uint8_t Arrow_Left_IsDown(void)
{
    return btn_level(BUTTON_PORT, BUTTON_LEFT_PIN);
}

uint8_t Arrow_Right_IsDown(void)
{
    return btn_level(BUTTON_PORT, BUTTON_RIGHT_PIN);
}
