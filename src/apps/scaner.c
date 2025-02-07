#include "scaner.h"
#include "../driver/st7565.h"
#include "../driver/uart.h"
#include "../helper/bands.h"
#include "../helper/lootlist.h"
#include "../helper/measurements.h"
#include "../radio.h"
#include "../scheduler.h"
#include "../ui/components.h"
#include "../ui/spectrum.h"
#include "../ui/statusline.h"
#include "apps.h"
#include "finput.h"
#include <stdint.h>

static Band *b;
static Measurement *m;

static bool selStart = true;

static uint32_t delay = 1000;
static uint8_t afc = 0;

static uint16_t sqLevel;
static bool thinking = false;
static bool wasThinkingEarlier = false;

static uint16_t msmLow;
static uint16_t msmHigh;

typedef enum {
  SET_AGC,
  SET_BW,
  SET_AFC,
  SET_SQL_T,
  SET_SQL_V,
  SET_COUNT,
} Setting;

#define RANGES_STACK_SIZE 4
static Band rangesStack[RANGES_STACK_SIZE] = {0};
static int8_t rangesStackIndex = -1;

static void rangeClear() { rangesStackIndex = -1; }

static bool rangePush(Band r) {
  if (rangesStackIndex < RANGES_STACK_SIZE - 1) {
    rangesStack[++rangesStackIndex] = r;
  } else {
    for (uint8_t i = 1; i < RANGES_STACK_SIZE; ++i) {
      rangesStack[i - 1] = rangesStack[i];
    }
    rangesStack[rangesStackIndex] = r;
  }
  return true;
}

static Band rangePop(void) {
  if (rangesStackIndex > 0) {
    return rangesStack[rangesStackIndex--];
  }
  return rangesStack[rangesStackIndex];
}

static Band *rangePeek(void) {
  if (rangesStackIndex >= 0) {
    return &rangesStack[rangesStackIndex];
  }
  return NULL;
}

static Setting setting;

static uint16_t measure(uint32_t f) {
  BK4819_TuneTo(f, true);
  vTaskDelay(delay / 100);
  return BK4819_GetRSSI();
}

static void prepareSqLevel() {
  uint16_t msms[5];
  const uint32_t BW = b->txF - b->rxF;

  msms[0] = measure(b->rxF);
  msms[1] = measure(b->rxF + BW / 3);
  msms[2] = measure(b->rxF + BW / 2);
  msms[3] = measure(b->rxF - BW / 3);
  msms[4] = measure(b->txF);

  sqLevel = Mid(msms, ARRAY_SIZE(msms)) + 2;
}

static void setupRadio() {
  BK4819_SetAFC(afc);
  BK4819_SetFilterBandwidth(b->bw);
  BK4819_SetAGC(true, b->gainIndex);
  // BK4819_SetModulation(b->modulation);
  BK4819_Squelch(b->squelch.value, gSettings.sqlOpenTime,
                 gSettings.sqlCloseTime);
}

static void newScan() {
  radio->rxF = b->rxF;
  prepareSqLevel();
  setupRadio();
  SP_Init(b);
}

static void setStartF(uint32_t f) {
  b->rxF = f;
  newScan();
}

static void setEndF(uint32_t f) {
  b->txF = f;
  newScan();
}

void SCANER_init(void) {
  SPECTRUM_Y = 6;
  SPECTRUM_H = 46;

  RADIO_LoadCurrentVFO();

  m = &gLoot[gSettings.activeVFO];
  m->snr = 0;

  rangePush(gCurrentBand);

  b = rangePeek();
  b->meta.type = TYPE_BAND_DETACHED;

  b->rxF = 17200000;
  b->txF = 17300000;
  b->bw = BK4819_FILTER_BW_12k;
  b->gainIndex = AUTO_GAIN_INDEX;

  newScan();
}

uint32_t lastListenTime;

void SCANER_update(void) {
  if (m->open) {
    m->open = BK4819_IsSquelchOpen();
  } else {
    m->rssi = measure(m->f);
    m->open = m->rssi >= sqLevel;
    SP_AddPoint(m);
    if (m->rssi > msmHigh) {
      msmHigh = m->rssi;
    }
    if (m->rssi < msmLow) {
      msmLow = m->rssi;
    }
  }

  if (gSettings.skipGarbageFrequencies && (radio->rxF % 1300000 == 0)) {
    m->open = false;
  }

  // really good level?
  if (m->open && !gIsListening) {
    thinking = true;
    wasThinkingEarlier = true;
    gRedrawScreen = true;
    vTaskDelay(pdMS_TO_TICKS(100));
    m->open = BK4819_IsSquelchOpen();
    thinking = false;
    gRedrawScreen = true;
    if (!m->open) {
      sqLevel++;
    }
  }

  LOOT_Update(m);

  RADIO_ToggleRX(m->open);

  if (m->open) {
    gRedrawScreen = true;
    return;
  }

  static uint8_t stepsPassed;

  if (stepsPassed++ > 64) {
    stepsPassed = 0;
    gRedrawScreen = true;
    if (!wasThinkingEarlier) {
      sqLevel--;
    }
    wasThinkingEarlier = false;
  }

  if (RADIO_NextFScan(b, true) || radio->rxF < m->f) {
    setupRadio(); // NOTE somewhy makes first measurement artifact, maybe need
                  // delay
    gRedrawScreen = true;
  }
  m->f = radio->rxF;
}

bool SCANER_key(KEY_Code_t key, Key_State_t state) {
  uint8_t u8v;
  if (state == KEY_RELEASED || state == KEY_LONG_PRESSED_CONT) {
    switch (key) {
    case KEY_1:
    case KEY_7:
      IncDec32(&delay, 200, 10000, key == KEY_1 ? 100 : -100);
      return true;
    case KEY_2:
    case KEY_8:
      switch (setting) {
      case SET_AFC:
        IncDec8(&afc, 0, 8, key == KEY_2 ? 1 : -1);
        break;
      case SET_BW:
        u8v = b->bw;
        IncDec8(&u8v, BK4819_FILTER_BW_6k, BK4819_FILTER_BW_26k + 1,
                key == KEY_2 ? 1 : -1);
        b->bw = u8v;
        break;
      case SET_AGC:
        u8v = b->gainIndex;
        IncDec8(&u8v, 0, ARRAY_SIZE(gainTable), key == KEY_2 ? 1 : -1);
        b->gainIndex = u8v;
        break;
      case SET_SQL_T:
        u8v = b->squelch.type;
        IncDec8(&u8v, 0, ARRAY_SIZE(sqTypeNames), key == KEY_2 ? 1 : -1);
        b->squelch.type = u8v;
        break;
      case SET_SQL_V:
        u8v = b->squelch.value;
        IncDec8(&u8v, 0, 11, key == KEY_2 ? 1 : -1);
        b->squelch.value = u8v;
        break;
      default:
        break;
      }
      setupRadio();
      return true;
    case KEY_3:
    case KEY_9:
      u8v = b->step;
      IncDec8(&u8v, STEP_0_02kHz, STEP_500_0kHz + 1, key == KEY_3 ? 1 : -1);
      b->step = u8v;
      newScan();
      return true;
    case KEY_STAR:
      APPS_run(APP_LOOT_LIST);
      return true;
    case KEY_UP:
    case KEY_DOWN:
      CUR_Move(key == KEY_UP);
      return true;
    default:
      break;
    }
  }

  if (state == KEY_RELEASED) {
    switch (key) {
    case KEY_4:
      IncDec8(&setting, 0, SET_COUNT, 1);
      return true;
    case KEY_5:
      gFInputCallback = selStart ? setStartF : setEndF;
      APPS_run(APP_FINPUT);
      return true;
    case KEY_SIDE1:
      LOOT_BlacklistLast();
      return true;
    case KEY_SIDE2:
      LOOT_WhitelistLast();
      return true;
    case KEY_STAR:
      APPS_run(APP_LOOT_LIST);
      return true;

    case KEY_0:
      if (rangesStackIndex < RANGES_STACK_SIZE - 1) {
        rangePush(CUR_GetRange(rangePeek(), StepFrequencyTable[b->step]));
        newScan();
        return true;
      }
      break;
    case KEY_F:
      rangePop();
      newScan();
      return true;

    default:
      break;
    }
  }

  if (state == KEY_LONG_PRESSED && key == KEY_2) {
    selStart = !selStart;
    return true;
  }
  return false;
}

static void renderAnalyzerUI() {
  PrintSmallEx(0, 18, POS_L, C_FILL, "%u", msmHigh);
  PrintSmallEx(0, 24, POS_L, C_FILL, "%u", msmLow);
}

/*
MENU

ANALYZER
vMax
vMin

SCANER
gLastActiveLoot

BOTH
delay 1/7
step 3/9
msm->f
sql star/F?

*/

void SCANER_render(void) {
  const uint32_t step = StepFrequencyTable[b->step];

  if (thinking) {
    PrintSmallEx(LCD_XCENTER, 4, POS_C, C_FILL, "...");
  }

  STATUSLINE_SetText("");

  const uint8_t MUL = 22;

  PrintSmallEx(setting * MUL, 4, POS_L, C_FILL, ">");
  PrintSmallEx(4 + MUL * 0, 4, POS_L, C_FILL, "%+d",
               -gainTable[b->gainIndex].gainDb + 33);
  PrintSmallEx(4 + MUL * 1, 4, POS_L, C_FILL, "%s", bwNames[b->bw]);
  PrintSmallEx(4 + MUL * 2, 4, POS_L, C_FILL, "AFC%u", afc);
  PrintSmallEx(4 + MUL * 3, 4, POS_L, C_FILL, "%s",
               sqTypeNames[b->squelch.type]);
  PrintSmallEx(4 + MUL * 4, 4, POS_L, C_FILL, "%u", b->squelch.value);

  SP_Render(b);
  SP_RenderArrow(b, radio->rxF);
  CUR_Render(SPECTRUM_Y + 22);

  // top
  if (gLastActiveLoot) {
    UI_DrawLoot(gLastActiveLoot, LCD_XCENTER, 14, POS_C);
  }

  PrintSmallEx(0, 12, POS_L, C_FILL, "%uus", delay);

  PrintSmallEx(LCD_WIDTH, 12, POS_R, C_FILL, "%u.%02uk", step / 100,
               step % 100);

  // renderAnalyzerUI();

  // bottom
  FSmall(1, LCD_HEIGHT - 2, POS_L, b->rxF);
  FSmall(LCD_XCENTER, LCD_HEIGHT - 2, POS_C, radio->rxF);
  FSmall(LCD_WIDTH - 1, LCD_HEIGHT - 2, POS_R, b->txF);

  FillRect(selStart ? 0 : LCD_WIDTH - 42, LCD_HEIGHT - 7, 42, 7, C_INVERT);
}

void SCANER_deinit(void) {}
