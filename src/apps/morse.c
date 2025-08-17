#include "morse.h"
#include "../driver/bk4819.h"
#include "../driver/uart.h"
#include "../external/FreeRTOS/include/task.h"
#include "../helper/measurements.h"
#include "../radio.h"
#include "../ui/graphics.h"
#include "../ui/statusline.h"
#include "apps.h"
#include "finput.h"
#include "textinput.h"
#include <ctype.h>

// --- Morse Code Definitions (trailing spaces removed to save space) ---

static const char *MORSE_TABLE[40] = {
    "-----", // 0
    ".----", // 1
    "..---", // 2
    "...--", // 3
    "....-", // 4
    ".....", // 5
    "-....", // 6
    "--...", // 7
    "---..", // 8
    "----.", // 9
    " ",     // 10: space character marker
    "",      // 11
    "",      // 12
    "",      // 13
    "",      // 14
    "",      // 15
    "",      // 16
    ".-",    // A
    "-...",  // B
    "-.-.",  // C
    "-..",   // D
    ".",     // E
    "..-.",  // F
    "--.",   // G
    "....",  // H
    "..",    // I
    ".---",  // J
    "-.- ",  // K
    ".-..",  // L
    "--",    // M
    "-.",    // N
    "---",   // O
    ".--.",  // P
    "--.-",  // Q
    ".-.",   // R
    "...",   // S
    "-",     // T
    "..-",   // U
    "...-",  // V
    ".--",   // W
};

static uint16_t dotDuration = 100; // ms (default is 12 WPM)
static const uint16_t TONE_FREQ = 700;   // Hz

// --- App State ---

typedef enum {
  STATE_IDLE,
  STATE_STARTING,
  STATE_NEXT_CHAR,
  STATE_LETTER_GAP,
  STATE_WORD_GAP,
  STATE_NEXT_SYMBOL,
  STATE_TX_TONE,
  STATE_SYMBOL_GAP,
  STATE_STOPPING,
} MorseState;

static char inputText[32];
static uint8_t textPos;
static uint8_t morsePos;
static MorseState morseState = STATE_IDLE;
static uint32_t stateTimer;

// --- Helper Functions ---

static const char *getMorseString(char c) {
  c = toupper(c);
  if (c >= '0' && c <= '9') {
    return MORSE_TABLE[c - '0'];
  }
  if (c >= 'A' && c <= 'W') {
    return MORSE_TABLE[c - 'A' + 17];
  }
  // Fallback for space or unsupported characters
  return MORSE_TABLE[10];
}

// --- App Functions ---

static void morse_set_wpm(uint32_t wpm) {
  if (wpm > 0 && wpm <= 60) { // Sanity check WPM
    dotDuration = 1200 / wpm;
  }
}

static void textInputCallback(void) { morseState = STATE_IDLE; }

static void launchTextInput(void) {
  gTextinputText = inputText;
  gTextInputSize = sizeof(inputText) - 1;
  gTextInputCallback = textInputCallback;
  APPS_run(APP_TEXTINPUT);
}

void MORSE_init(void) {
  memset(inputText, 0, sizeof(inputText));
  launchTextInput();
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
      BK4819_TransmitTone(TONE_FREQ);
      BK4819_EnterTxMute();
      stateTimer = Now() + 1000; // 1-sec squelch delay
      morseState = STATE_NEXT_CHAR;
    }
    break;

  case STATE_NEXT_CHAR:
    if (textPos >= strlen(inputText)) {
      morseState = STATE_STOPPING;
      break;
    }
    if (inputText[textPos] == ' ') {
      morseState = STATE_WORD_GAP;
    } else {
      morsePos = 0;
      morseState = STATE_NEXT_SYMBOL;
    }
    break;

  case STATE_WORD_GAP:
    // 7-dot gap, but a 3-dot letter gap was already added, so add 4 more.
    BK4819_EnterTxMute();
    stateTimer = Now() + (dotDuration * 4);
    textPos++;
    morseState = STATE_NEXT_CHAR;
    break;

  case STATE_NEXT_SYMBOL: {
    const char *morseStr = getMorseString(inputText[textPos]);
    char symbol = morseStr[morsePos];

    if (symbol == '\0') { // End of the current character's morse string
      morseState = STATE_LETTER_GAP;
      break;
    }
    morseState = STATE_TX_TONE;
    break;
  }

  case STATE_LETTER_GAP:
    // 3-dot gap, but a 1-dot symbol gap was already added, so add 2 more.
    BK4819_EnterTxMute();
    stateTimer = Now() + (dotDuration * 2);
    textPos++;
    morseState = STATE_NEXT_CHAR;
    break;

  case STATE_TX_TONE: {
    const char *morseStr = getMorseString(inputText[textPos]);
    char symbol = morseStr[morsePos];
    BK4819_ExitTxMute();
    uint16_t duration = (symbol == '.') ? dotDuration : (dotDuration * 3);
    stateTimer = Now() + duration;
    morseState = STATE_SYMBOL_GAP;
    break;
  }

  case STATE_SYMBOL_GAP:
    BK4819_EnterTxMute();
    morsePos++;
    stateTimer = Now() + dotDuration; // 1-dot inter-symbol gap
    morseState = STATE_NEXT_SYMBOL;
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

  PrintMediumEx(LCD_XCENTER, 10, POS_C, C_FILL, "Morse Code");
  uint16_t wpm = 1200 / dotDuration;
  PrintSmallEx(LCD_XCENTER, 22, POS_C, C_FILL, "Speed: %u WPM", wpm);
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
      textPos = 0;
      morsePos = 0;
      morseState = STATE_STARTING;
    } else {
      morseState = STATE_STOPPING;
    }
    MORSE_update();
    return true;

  case KEY_1:
    gFInputCallback = morse_set_wpm;
    APPS_run(APP_FINPUT);
    return true;

  case KEY_5:
    launchTextInput();
    return true;

  case KEY_EXIT:
    APPS_exit();
    return true;

  default:
    break;
  }
  return false;
}