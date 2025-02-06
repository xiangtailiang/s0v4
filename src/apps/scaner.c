#include "scaner.h"
#include "../driver/st7565.h"
#include "../driver/uart.h"
#include "../helper/bands.h"
#include "../helper/lootlist.h"
#include "../helper/measurements.h"
#include "../radio.h"
#include "../ui/graphics.h"
#include "../ui/spectrum.h"
#include "apps.h"
#include "finput.h"
#include <stdint.h>

static Band *b;
static Measurement *m;

static bool selStart = true;

static uint32_t delay = 1000;

static uint16_t sqLevel = UINT16_MAX;
static bool thinking = false;
static bool wasThinkingEarlier = false;

static uint16_t msmLow;
static uint16_t msmHigh;

static uint16_t measure(uint32_t f) {
  m->snr = 0;
  BK4819_SetFrequency(m->f);
  BK4819_WriteRegister(BK4819_REG_30, 0x0200);
  BK4819_WriteRegister(BK4819_REG_30, 0xBFF1);
  vTaskDelay(delay / 1000);
  m->rssi = BK4819_GetRSSI();
  if (m->rssi > msmHigh) {
    msmHigh = m->rssi;
  }
  if (m->rssi < msmLow) {
    msmLow = m->rssi;
  }

  return m->rssi;
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

static void newScan() {
  prepareSqLevel();

  RADIO_TuneTo(b->rxF);

  BK4819_SetFilterBandwidth(b->bw);
  BK4819_SetAGC(true, b->gainIndex);
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

  b = &gCurrentBand;
  b->meta.type = TYPE_BAND_DETACHED;

  b->rxF = 17200000;
  b->txF = 17300000;
  b->bw = BK4819_FILTER_BW_12k;
  b->gainIndex = AUTO_GAIN_INDEX;

  newScan();
}

void SCANER_update(void) {
  if (m->open) {
    m->open = BK4819_IsSquelchOpen();
  } else {
    m->open = measure(m->f) >= sqLevel;
    SP_AddPoint(m);
  }

  if (gSettings.skipGarbageFrequencies && (radio->rxF % 1300000 == 0)) {
    m->open = false;
  }
  m->open = false; // FIXME: DELETE AFTER TEST

  // really good level?
  /* if (m->open && !gIsListening) {
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
  } */

  LOOT_Update(m);

  RADIO_ToggleRX(m->open);

  if (m->open) {
    gRedrawScreen = true;
    return;
  }

  static uint8_t stepsPassed;
  stepsPassed++;

  if (stepsPassed > 64) {
    gRedrawScreen = true;
    stepsPassed = 0;
    if (!wasThinkingEarlier) {
      sqLevel--;
    }
    wasThinkingEarlier = false;
  }

  if (RADIO_NextFScan(true)) {
    RADIO_SetupBandParams();
  }
  if (radio->rxF < m->f) {
    gRedrawScreen = true;
  }
  m->f = radio->rxF;
}

bool SCANER_key(KEY_Code_t key, Key_State_t state) {
  uint8_t stp;
  uint8_t bw;
  if (state == KEY_RELEASED || state == KEY_LONG_PRESSED_CONT) {
    switch (key) {
    case KEY_1:
    case KEY_7:
      IncDec32(&delay, 200, 10000, key == KEY_1 ? 100 : -100);
      return true;
    case KEY_2:
    case KEY_8:
      bw = b->bw;
      IncDec8(&bw, BK4819_FILTER_BW_6k, BK4819_FILTER_BW_26k + 1,
              key == KEY_2 ? 1 : -1);
      b->bw = bw;
      BK4819_SetFilterBandwidth(b->bw);
      return true;
    case KEY_3:
    case KEY_9:
      stp = b->step;
      IncDec8(&stp, STEP_0_02kHz, STEP_500_0kHz + 1, key == KEY_3 ? 1 : -1);
      b->step = stp;
      newScan();
      return true;
    case KEY_STAR:
    case KEY_F:
      RADIO_UpdateSquelchLevel(key == KEY_STAR);
      return true;
    default:
      break;
    }
  }

  if (state == KEY_RELEASED) {
    switch (key) {
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
    case KEY_UP:
      RADIO_ToggleRX(false);
      if (RADIO_NextFScan(true)) {
        RADIO_SetupBandParams();
      }
      m->f = radio->rxF;
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
  PrintSmallEx(0, 12 + 6 * 0, POS_L, C_FILL, "%uus", delay);
  PrintSmallEx(0, 12 + 6 * 1, POS_L, C_FILL, "%u", msmHigh);
  PrintSmallEx(0, 12 + 6 * 2, POS_L, C_FILL, "%u", msmLow);
  PrintSmallEx(LCD_XCENTER, 16 + 6 * 1, POS_C, C_FILL, "%+d",
               -gainTable[b->gainIndex].gainDb + 33);
}

void SCANER_render(void) {
  const uint32_t step = StepFrequencyTable[b->step];

  if (thinking) {
    PrintSmallEx(LCD_XCENTER, 4, POS_C, C_FILL, "...");
  }

  SP_Render(b);
  SP_RenderArrow(b, radio->rxF);

  // top
  if (gLastActiveLoot) {
    PrintMediumEx(LCD_XCENTER, 14, POS_C, C_FILL, "%u.%05u",
                  gLastActiveLoot->f / MHZ, gLastActiveLoot->f % MHZ);
  }

  PrintSmallEx(0, 12 + 6 * 0, POS_L, C_FILL, "%uus", delay);

  PrintSmallEx(LCD_WIDTH, 12, POS_R, C_FILL, "%u.%02uk", step / 100,
               step % 100);

  PrintSmallEx(LCD_WIDTH - 1, 18, POS_R, C_FILL, "%s%u",
               sqTypeNames[radio->squelch.type], radio->squelch.value);
  PrintSmallEx(LCD_WIDTH, 12 + 6 * 2, POS_R, C_FILL, "%s", bwNames[b->bw]);

  /* PrintSmallEx(LCD_XCENTER, 16 + 6 * 1, POS_C, C_FILL, "%+d",
               -gainTable[gain].gainDb + 33); */

  // bottom
  FSmall(1, LCD_HEIGHT - 2, POS_L, b->rxF);
  FSmall(LCD_XCENTER, LCD_HEIGHT - 2, POS_C, radio->rxF);
  FSmall(LCD_WIDTH - 1, LCD_HEIGHT - 2, POS_R, b->txF);

  FillRect(selStart ? 0 : LCD_WIDTH - 42, LCD_HEIGHT - 7, 42, 7, C_INVERT);
}

void SCANER_deinit(void) {}
