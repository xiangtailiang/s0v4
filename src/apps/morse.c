#include "morse.h"
#include "../driver/bk4819.h"
#include "../driver/uart.h"
#include "../helper/measurements.h"
#include "../radio.h"
#include "../ui/graphics.h"
#include "../ui/statusline.h"
#include "apps.h"
#include "textinput.h"

// --- Morse Code Definitions ---

static const char *MORSE_TABLE[40] = {
    "----- ", // 0
    ".---- ", // 1
    "..--- ", // 2
    "...-- ", // 3
    "....- ", // 4
    "..... ", // 5
    "-.... ", // 6
    "--... ", // 7
    "---.. ", // 8
    "----. ", // 9
    "       ",  // 10: space
    "       ",  // 11: space
    "       ",  // 12: space
    "       ",  // 13: space
    "       ",  // 14: space
    "       ",  // 15: space
    "       ",  // 16: space
    ".- ",      // A
    "-... ",    // B
    "-.-. ",    // C
    "-.. ",     // D
    ". ",       // E
    "..-. ",    // F
    "--. ",     // G
    ".... ",    // H
    ".. ",      // I
    ".--- ",    // J
    "-.- ",     // K
    ".-.. ",    // L
    "-- ",      // M
    "-. ",      // N
    "--- ",     // O
    ".--. ",    // P
    "--.- ",    // Q
    ".-. ",     // R
    "... ",     // S
    "- ",       // T
    "..- ",     // U
    "...- ",    // V
    ".-- ",     // W
};

static const uint16_t DOT_DURATION = 100; // ms
static const uint16_t TONE_FREQ = 700;    // Hz

// --- App State ---

typedef enum {
  STATE_IDLE,
  STATE_STARTING,
  STATE_NEXT_CHAR,
  STATE_NEXT_SYMBOL,
  STATE_TX_TONE,
  STATE_TX_GAP,
  STATE_STOPPING,
} MorseState;

static char inputText[32];
static uint8_t textPos;
static uint8_t morsePos;
static MorseState morseState = STATE_IDLE;
static uint32_t stateTimer;

// --- Helper Functions ---

static const char *getMorseString(char c) {
  if (c >= '0' && c <= '9') {
    return MORSE_TABLE[c - '0'];
  }
  if (c >= 'A' && c <= 'W') {
    return MORSE_TABLE[c - 'A' + 17];
  }
  if (c >= 'a' && c <= 'w') {
    return MORSE_TABLE[c - 'a' + 17];
  }
  // Fallback for space or unsupported characters
  return MORSE_TABLE[10];
}

// --- App Functions ---

static void textInputCallback(void) { morseState = STATE_IDLE; }

void MORSE_init(void) {
  memset(inputText, 0, sizeof(inputText));
  gTextinputText = inputText;
  gTextInputSize = sizeof(inputText) - 1;
  gTextInputCallback = textInputCallback;
  APPS_run(APP_TEXTINPUT);
}

void MORSE_deinit(void) {
  if (gTxState == TX_ON) {
    RADIO_ToggleTX(false);
  }
}

void MORSE_update(void) {
  if (morseState == STATE_IDLE) {
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
  }

  if (Now() < stateTimer) {
    return;
  }

  switch (morseState) {
  case STATE_STARTING:
    RADIO_ToggleTX(true);
    if (gTxState == TX_ON) {
      // Set up the tone generator, but keep it muted
      BK4819_TransmitTone(TONE_FREQ);
      BK4819_EnterTxMute();

      // Set the timer for the 1-second squelch delay
      stateTimer = Now() + 1000;

      // Set the next state to begin sending the actual symbols
      morseState = STATE_NEXT_CHAR;
    }
    break;

  case STATE_NEXT_CHAR:
    if (textPos >= strlen(inputText)) {
      morseState = STATE_STOPPING;
      break;
    }
    morsePos = 0;
    morseState = STATE_NEXT_SYMBOL;
    break;

  case STATE_NEXT_SYMBOL: {
    const char *morseStr = getMorseString(inputText[textPos]);
    char symbol = morseStr[morsePos];

    if (symbol == '\0') { // End of character
      textPos++;
      morseState = STATE_NEXT_CHAR;
      break;
    }

    if (symbol == ' ') { // Word gap
      BK4819_EnterTxMute();
      stateTimer = Now() + (DOT_DURATION * 7);
    } else { // Dot or Dash
      BK4819_ExitTxMute();
      uint16_t duration = (symbol == '.') ? DOT_DURATION : (DOT_DURATION * 3);
      stateTimer = Now() + duration;
    }
    morseState = STATE_TX_GAP;
    break;
  }

  case STATE_TX_GAP:
    BK4819_EnterTxMute();
    morsePos++;
    morseState = STATE_NEXT_SYMBOL;
    stateTimer = Now() + DOT_DURATION; // Inter-symbol gap
    break;

  case STATE_STOPPING:
    RADIO_ToggleTX(false);
    morseState = STATE_IDLE;
    break;

  default:
    morseState = STATE_IDLE;
    break;
  }
}

void MORSE_render() {
  char status[32];
  switch (morseState) {
  case STATE_IDLE:
    sprintf(status, "Press PTT to start");
    break;
  default:
    sprintf(status, "Sending...");
    break;
  }

  PrintMediumEx(LCD_XCENTER, 15, POS_C, C_FILL, "Morse Code");
  PrintMediumEx(LCD_XCENTER, 35, POS_C, C_FILL, status);

  if (strlen(inputText) > 0) {
    PrintMediumEx(LCD_XCENTER, 50, POS_C, C_FILL, "Msg: %s", inputText);
  }
}

bool MORSE_key(KEY_Code_t key, Key_State_t state) {
  if (state != KEY_RELEASED) {
    return false;
  }

  switch (key) {
  case KEY_PTT:
    if (morseState == STATE_IDLE) {
      // Reset state for new transmission
      textPos = 0;
      morsePos = 0;
      morseState = STATE_STARTING;
    } else {
      morseState = STATE_STOPPING;
    }
    MORSE_update(); // Run update once to start/stop immediately
    return true;

  case KEY_EXIT:
    APPS_exit();
    return true;

  default:
    break;
  }
  return false;
}