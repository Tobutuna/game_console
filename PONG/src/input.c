#include "gd32vf103.h"
#include "gd32vf103_gpio.h"
#include "gd32vf103_rcu.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "input.h"      // GameInput_t
#include "LCD/lcd.h"

extern QueueHandle_t xInputQueue;

// Key codes för spelkontroller (sätt värden efter vad ni mäter upp)
#define P1_UP_KEY      0   // exempel: '4'
#define P1_DOWN_KEY    4   // exempel: '1'
#define FIRE_KEY       1   // exempel: '5'
#define PAUSE_KEY      2   // exempel: '6'

// -------------------- PIN-MAPPING --------------------
// Här antar vi:
//   - keypad pins 1–4 (rader) -> PA0..PA3
//   - keypad pins 5–8 (kolumner) -> PA4..PA7
// Justera vid behov efter faktisk koppling.

#define KB_PORT_COLS      GPIOA
#define KB_PORT_ROWS      GPIOA

static const uint16_t row_pins[4] = {
    GPIO_PIN_0,
    GPIO_PIN_1,
    GPIO_PIN_2,
    GPIO_PIN_3
};

static const uint16_t col_pins[4] = {
    GPIO_PIN_4,
    GPIO_PIN_5,
    GPIO_PIN_6,
    GPIO_PIN_7
};

// -------------------- HW-init --------------------
void Input_HwInit(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);

    // RADER: input med pull-up
    gpio_init(GPIOA,
              GPIO_MODE_IPU,
              GPIO_OSPEED_50MHZ,
              GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);

    // KOLUMNER: output push-pull
    gpio_init(GPIOA,
              GPIO_MODE_OUT_PP,
              GPIO_OSPEED_50MHZ,
              GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);

    // ingen kolumn aktiv (alla höga)
    gpio_bit_set(GPIOA,
                 GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
}

// -------------------- Scan-funktion --------------------
static int Keyboard_Scan(void)
{
    for (int c = 0; c < 4; ++c) {

        // alla kolumner höga
        gpio_bit_set(GPIOA,
                     GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);

        // aktivera aktuell kolumn (låg)
        gpio_bit_reset(GPIOA, col_pins[c]);

        // kort delay för att signalerna ska stabiliseras
        for (volatile int d = 0; d < 100; ++d) { }

        // läs rader
        for (int r = 0; r < 4; ++r) {
            if (gpio_input_bit_get(GPIOA, row_pins[r]) == RESET) {
                return r * 4 + c;   // 0..15
            }
        }
    }

    return -1;   // ingen knapp
}

// -------------------- Key -> GameInput --------------------
static void Input_TranslateKey(int key, GameInput_t *in)
{
    // nollställ alla flaggor
    in->up    = 0;
    in->down  = 0;
    in->fire  = 0;
    in->pause = 0;

    switch (key) {
        case P1_UP_KEY:
            in->up = 1;
            break;

        case P1_DOWN_KEY:
            in->down = 1;
            break;

        case FIRE_KEY:
            in->fire = 1;
            break;

        case PAUSE_KEY:
            in->pause = 1;
            break;

        default:
            break;
    }
}

// -------------------- FreeRTOS-task (med debounce) --------------------
void vInputTask(void *pvParameters)
{
    (void)pvParameters;
    GameInput_t in;

    // initialt: inget tryckt
    in.up    = 0;
    in.down  = 0;
    in.fire  = 0;
    in.pause = 0;

    // skicka första "allt släppt"
    xQueueOverwrite(xInputQueue, &in);

    // Debounce-state
    int last_raw_key    = -1;   // senaste råa keypadkod
    int stable_key      = -1;   // "debouncad" key som vi tror på
    int stable_counter  = 0;    // hur många gånger i rad vi sett samma raw
    GameInput_t last_sent = in; // senast skickade GameInput_t

    for (;;)
    {
        int raw = Keyboard_Scan();  // -1 om ingen knapp, annars 0..15

        if (raw == last_raw_key) {
            // samma som förra mätningen → öka stabilitet
            if (stable_counter < 255) {
                stable_counter++;
            }
        } else {
            // nytt råvärde → börja om
            stable_counter = 0;
            last_raw_key   = raw;
        }

        // kräv t.ex. 5 likadana mätningar (≈50 ms) för stabilt värde
        if (stable_counter >= 5) {
            stable_key = raw;
        }

        // Bygg "in" utifrån stabil_key
        if (stable_key >= 0) {
            // någon knapp anses nedtryckt
            Input_TranslateKey(stable_key, &in);
        } else {
            // ingen stabil knapp → allt släppt
            in.up    = 0;
            in.down  = 0;
            in.fire  = 0;
            in.pause = 0;
        }

        // Skicka bara om något faktiskt ändrats → mindre ”flimmer”
        if ( (in.up    != last_sent.up)   ||
             (in.down  != last_sent.down) ||
             (in.fire  != last_sent.fire) ||
             (in.pause != last_sent.pause) )
        {
            xQueueOverwrite(xInputQueue, &in);
            last_sent = in;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


/*
 * README – input.c
 * ================
 *
 * Syfte:
 *  - Läser av en 4x4-keypad via GPIOA (rad/kolumn-matris).
 *  - Debouncar knapparna.
 *  - Översätter rå keypad-kod (0..15) till ett generiskt GameInput_t
 *    (up, down, fire, pause).
 *  - Skickar kontinuerligt senaste GameInput_t till xInputQueue (FreeRTOS-kö).
 *
 * HÅRDVARA: 4x4-KEYPAD PÅ GPIOA
 * --------------------------------
 *  Antagen koppling:
 *    - Rader (rows)   → PA0..PA3 (input med intern pull-up).
 *    - Kolumner (cols)→ PA4..PA7 (output push-pull).
 *
 *  Matrisprincip:
 *    - Varje knapp ligger i skärningspunkten mellan en rad och en kolumn.
 *      Exempel:
 *        Row 0, Col 0 → key 0
 *        Row 0, Col 1 → key 1
 *        ...
 *        Row r, Col c → key = r*4 + c
 *
 * Input_HwInit()
 * --------------
 *  - Slår på klockan för GPIOA:
 *      rcu_periph_clock_enable(RCU_GPIOA);
 *
 *  - Initierar:
 *      * PA0..PA3 som "input pull-up" (GPIO_MODE_IPU):
 *          - När ingen knapp är nedtryckt → rad ligger högt (1).
 *          - När en knapp är nedtryckt och kolumnen dras låg (0) → raden blir låg.
 *
 *      * PA4..PA7 som "output push-pull" (GPIO_MODE_OUT_PP):
 *          - Dessa driver kolumnerna höga eller låga.
 *
 *  - Sätter alla kolumner höga (ingen aktiv):
 *      gpio_bit_set(GPIOA, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
 *
 * Keyboard_Scan()
 * ---------------
 *  - Loopar över alla 4 kolumner:
 *
 *      for (c = 0; c < 4; ++c) {
 *          // 1) Sätt alla kolumner höga (ingen aktiv).
 *          // 2) Dra en kolumn låg (aktivera just den).
 *          // 3) Vänta lite (for-loop) för att låta signaler stabiliseras.
 *          // 4) Läs alla rader (PA0..PA3).
 *      }
 *
 *  - Om någon rad är låg (RESET) när en viss kolumn är aktiv:
 *      → en knapp i den raden/kolumnen är nedtryckt.
 *      → key = row*4 + col (intervall 0..15).
 *
 *  - Om ingen rad är låg för någon kolumn:
 *      → returnerar -1 (ingen knapp).
 *
 * Input_TranslateKey()
 * --------------------
 *  - Tar en key-kod (0..15) och fyller en GameInput_t-struktur:
 *
 *      case P1_UP_KEY:   in->up    = 1; break;
 *      case P1_DOWN_KEY: in->down  = 1; break;
 *      case FIRE_KEY:    in->fire  = 1; break;
 *      case PAUSE_KEY:   in->pause = 1; break;
 *
 *  - Alla andra knappar ignoreras (up/down/fire/pause=0)
 *    tills du vid behov mappar fler koder.
 *
 * vInputTask() – FreeRTOS-task med debounce
 * -----------------------------------------
 *  - En separat RTOS-task som kör i en evig loop:
 *
 *      1) Anropar Keyboard_Scan() → får raw key (0..15) eller -1.
 *      2) Jämför med föregående raw (last_raw_key):
 *           - Om samma: öka stable_counter.
 *           - Om olika: nollställ stable_counter och uppdatera last_raw_key.
 *      3) När stable_counter >= 5:
 *           - anser vi att knappen är "stabil".
 *           - stable_key = raw.
 *      4) Om stable_key >= 0:
 *           - Input_TranslateKey(stable_key, &in);
 *         Annars:
 *           - in.up/down/fire/pause = 0 (allt släppt).
 *      5) Jämför in med last_sent:
 *           - Bara om något ändrats skickas ny GameInput_t med xQueueOverwrite().
 *           - Minskar "flimmer" och onödiga kö-skrivningar.
 *      6) vTaskDelay(pdMS_TO_TICKS(10)) → tasken sover ~10 ms mellan scan.
 *
 *  - xQueueOverwrite(xInputQueue, &in):
 *      * Ser till att kön alltid innehåller senaste knappstatus.
 *      * Om Pong är lite senare med att läsa spelar det ingen roll,
 *        det hämtar alltid "senaste värdet".
 *
 * KOPPLING TILL PONG
 * ------------------
 *  - vPongTask (pong.c) hämtar GameInput_t så här:
 *
 *      GameInput_t input;
 *      if (xQueueReceive(xInputQueue, &newInput, 0) == pdPASS) {
 *          input = newInput;
 *      }
 *
 *  - Pong behöver inte veta något om keypad-matrisen eller GPIO.
 *    Det enda den bryr sig om är:
 *      input.up / input.down / input.fire / input.pause.
 *
 * ANPASSNING TILL RIKTIGA KNAPPAR (ISTÄLLET FÖR KEYPAD)
 * -----------------------------------------------------
 *  När ni går från 4x4-keypad till separata knappar (t.ex. 4 tryckknappar):
 *
 *  1) Input_HwInit():
 *      - Ändra pin-mapping:
 *          * Sätt varje knapp till en egen GPIO-pin (t.ex. PC0 = UP, PC1 = DOWN, PB5 = FIRE, PB6 = PAUSE).
 *          * Initiera dessa pinnar som input med pull-up eller input-float med extern pull-up.
 *      - Exempel (konceptuellt):
 *          gpio_init(GPIOC, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, GPIO_PIN_0 | GPIO_PIN_1);
 *          gpio_init(GPIOB, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, GPIO_PIN_5 | GPIO_PIN_6);
 *
 *  2) Keyboard_Scan():
 *      - Du kan helt skippa matrislogik och bara läsa pinnar direkt:
 *
 *          static int DirectButtons_Scan(void) {
 *              if (gpio_input_bit_get(GPIOC, GPIO_PIN_0) == RESET) return P1_UP_KEY;
 *              if (gpio_input_bit_get(GPIOC, GPIO_PIN_1) == RESET) return P1_DOWN_KEY;
 *              if (gpio_input_bit_get(GPIOB, GPIO_PIN_5) == RESET) return FIRE_KEY;
 *              if (gpio_input_bit_get(GPIOB, GPIO_PIN_6) == RESET) return PAUSE_KEY;
 *              return -1;
 *          }
 *
 *      - Antingen:
 *          * Byt namnet på Keyboard_Scan() till DirectButtons_Scan() och uppdatera vInputTask,
 *            eller
 *          * Behåll namnet Keyboard_Scan() men implementera den som direkt knappskanning.
 *
 *  3) P1_UP_KEY/P1_DOWN_KEY/FIRE_KEY/PAUSE_KEY:
 *      - De kan fortfarande användas som symboliska "koder" även om ni inte längre
 *        har en 4x4-matris.
 *      - Då får ni samma Input_TranslateKey()-logik som idag.
 *
 *  4) Resten av koden:
 *      - Behöver inte ändras. vPongTask ser fortfarande samma GameInput_t.
 *
 * SAMMANFATTNING
 * --------------
 *  - input.c kapslar in all hårdvarulogik för inmatning.
 *  - Ni kan byta från keypad till riktiga knappar genom att:
 *      * Uppdatera Input_HwInit() (GPIO-konfiguration).
 *      * Ändra Keyboard_Scan() så att den läser de nya pinnarna.
 *  - vPongTask och GameInput_t kan lämnas helt orörda.
 */