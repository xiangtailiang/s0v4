#include "finput.h"
#include "../driver/bk4819.h"
#include "../driver/st7565.h"
#include "../ui/graphics.h"
#include "apps.h"

uint32_t gFInputTempFreq;
void (*gFInputCallback)(uint32_t f);

static const uint8_t FREQ_INPUT_LENGTH = 10;

static KEY_Code_t freqInputArr[10];

static uint8_t freqInputDotIndex = 0;
static uint8_t freqInputIndex = 0;

static bool dotBlink = true;

static uint32_t calculateFrequency(const uint8_t *inputArr, uint8_t length,
                                   uint8_t dotIndex) {
  uint32_t frequency = 0;

  uint32_t base = MHZ;
  for (int i = dotIndex - 1; i >= 0; --i) {
    frequency += inputArr[i] * base;
    base *= 10;
  }

  base = MHZ / 10;
  for (uint8_t i = dotIndex + 1; i < length; ++i) {
    frequency += inputArr[i] * base;
    base /= 10;
  }

  return frequency;
}

static void input(KEY_Code_t key) {
  if (key != KEY_EXIT && freqInputIndex >= FREQ_INPUT_LENGTH) {
    return;
  }

  if (key == KEY_STAR) {
    if (freqInputDotIndex == 0) {
      freqInputDotIndex = freqInputIndex;
    }
    return;
  }

  if (key == KEY_EXIT) {
    if (freqInputIndex > 0) {
      if (freqInputArr[freqInputIndex - 1] == KEY_STAR) {
        freqInputDotIndex = 0;
      }
      freqInputIndex--;
    }
    return;
  }

  freqInputArr[freqInputIndex++] = key;

  uint8_t dotIndex =
      freqInputDotIndex == 0 ? freqInputIndex : freqInputDotIndex;
  gFInputTempFreq = calculateFrequency(freqInputArr, freqInputIndex, dotIndex);
}

static void fillFromTempFreq(void) {
  if (gFInputTempFreq == 0) {
    return;
  }
  uint32_t mul = 1000000000;
  uint32_t v = gFInputTempFreq;
  gFInputTempFreq = 0;

  while (mul > v) {
    mul /= 10;
  }

  while (v) {
    uint8_t t = (v / mul) % 10;
    input(KEY_0 + t);
    v -= t * mul;
    mul /= 10;
    if (freqInputDotIndex == 0 && v < 100000 && v > 0) {
      input(KEY_STAR);
    }
  }
}

void FINPUT_init(void) {
  // UI_ClearStatus();
  freqInputIndex = 0;
  freqInputDotIndex = 0;
  fillFromTempFreq();
}

void FINPUT_update() {
  if (!freqInputDotIndex) {
    dotBlink = !dotBlink;
    gRedrawScreen = true;
  }

  vTaskDelay(pdMS_TO_TICKS(500));
}

void FINPUT_deinit(void) {}

bool FINPUT_key(KEY_Code_t key, Key_State_t state) {
  if (state == KEY_LONG_PRESSED && key == KEY_EXIT) {
    freqInputIndex = 0;
    freqInputDotIndex = 0;
    gFInputTempFreq = 0;
    memset(freqInputArr, 0, FREQ_INPUT_LENGTH);
    return true;
  }
  if (state == KEY_RELEASED) {
    switch (key) {
    case KEY_0:
    case KEY_1:
    case KEY_2:
    case KEY_3:
    case KEY_4:
    case KEY_5:
    case KEY_6:
    case KEY_7:
    case KEY_8:
    case KEY_9:
    case KEY_STAR:
      input(key);
      if (gFInputTempFreq > 13000000) {
        input(KEY_STAR);
      }
      gRedrawScreen = true;
      return true;
    case KEY_EXIT:
      if (freqInputIndex == 0) {
        gFInputCallback = NULL;
        APPS_exit();
        return true;
      }
      input(key);
      gRedrawScreen = true;
      return true;
    case KEY_MENU:
    case KEY_F:
    case KEY_PTT:
      if (gFInputTempFreq <= BK4819_F_MAX && gFInputCallback) {
        APPS_exit();
        gFInputCallback(gFInputTempFreq);
        gFInputCallback = NULL;
        gFInputTempFreq = 0;
      }
      return true;
    default:
      break;
    }
  }
  return false;
}

void FINPUT_render(void) {
  uint8_t dotIndex = freqInputDotIndex ? freqInputDotIndex : freqInputIndex;

  const uint8_t charWidth = 10;
  const uint8_t BASE_X = 16;
  const uint8_t BASE_Y = 24;
  uint8_t i = 0;

  // MHz
  while (i < 4) {
    if (i >= 4 - dotIndex) {
      const unsigned int Digit = freqInputArr[i - (4 - dotIndex)];
      if (Digit < 10) {
        PrintBigDigits(BASE_X + i * charWidth, BASE_Y, "%c", '0' + Digit);
      }
    }
    i++;
  }

  // decimal point
  if (freqInputDotIndex || dotBlink) {
    PrintBigDigits(BASE_X + i * charWidth, BASE_Y, "%c", '.');
    i++;
  }

  // kHz
  if (freqInputDotIndex) {
    i = dotIndex + 1;
    while (i < freqInputIndex && i < dotIndex + 6) {
      const uint8_t Digit = freqInputArr[i];
      PrintBigDigits(BASE_X + (4 + i - dotIndex) * charWidth - 6, BASE_Y, "%c",
                     '0' + Digit);
      i++;
    }
  }
}
