#include "scaner.h"
#include "../driver/st7565.h"
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

static uint32_t delay = 2000;
// static uint8_t gain = 20;

static bool selStart = true;

static uint16_t sqLevel = UINT16_MAX;

static bool thinking = false;

static uint16_t closeLevel = 43;

static bool wasThinkingEarlier = false;

static inline void measure() {
  m->rssi = 0;
  m->rssi = 0;

  BK4819_SetFrequency(m->f);
  BK4819_WriteRegister(BK4819_REG_30, 0x0200);
  BK4819_WriteRegister(BK4819_REG_30, 0xBFF1);
  vTaskDelay(delay / 100);
  m->rssi = BK4819_GetRSSI();
}

static void prepareSqLevel() {
  uint16_t msms[5];
  const uint32_t BW = b->txF - b->rxF;

  m->f = b->rxF;
  measure();
  msms[0] = m->rssi;

  m->f = b->rxF + BW / 3;
  measure();
  msms[1] = m->rssi;

  m->f = b->rxF + BW / 2;
  measure();
  msms[2] = m->rssi;

  m->f = b->txF - BW / 3;
  measure();
  msms[3] = m->rssi;

  m->f = b->txF;
  measure();
  msms[4] = m->rssi;

  sqLevel = Mid(msms, ARRAY_SIZE(msms)) + 2;
}

static void newScan() {
  prepareSqLevel();

  RADIO_TuneTo(b->rxF);
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
  SPECTRUM_H = 48;

  RADIO_LoadCurrentVFO();

  m = &gLoot[gSettings.activeVFO];
  b = &gCurrentBand;

  b->meta.type = TYPE_BAND_DETACHED;

  newScan();
}

void SCANER_update(void) {
  if (m->open) {
    m->open = BK4819_IsSquelchOpen();
  } else {
    measure();
    m->open = m->rssi >= sqLevel;
    SP_AddPoint(m);
  }

  if (gSettings.skipGarbageFrequencies && (radio->rxF % 1300000 == 0)) {
    m->open = false;
  }

  // really good level?
  if (m->open && !gIsListening) {
    thinking = true;
    wasThinkingEarlier = true;
    gRedrawScreen = true;
    vTaskDelay(pdMS_TO_TICKS(150));
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
      IncDec32(&delay, 200, 10000, 100);
      return true;
    case KEY_7:
      IncDec32(&delay, 200, 10000, -100);
      return true;
    /* case KEY_STAR:
      IncDec8(&gain, 0, ARRAY_SIZE(gainTable), 1);
      BK4819_SetAGC(true, gain);
      newScan();
      return true;
    case KEY_0:
      IncDec8(&gain, 0, ARRAY_SIZE(gainTable), -1);
      BK4819_SetAGC(true, gain);
      newScan();
      return true; */
    case KEY_3:
      stp = b->step;
      IncDec8(&stp, STEP_0_02kHz, STEP_500_0kHz + 1, 1);
      b->step = stp;
      newScan();
      return true;
    case KEY_9:
      stp = b->step;
      IncDec8(&stp, STEP_0_02kHz, STEP_500_0kHz + 1, -1);
      b->step = stp;
      newScan();
      return true;
    case KEY_2:
      RADIO_UpdateSquelchLevel(true);
      return true;
    case KEY_8:
      RADIO_UpdateSquelchLevel(false);
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

void SCANER_render(void) {
  const uint32_t step = StepFrequencyTable[b->step];

  if (thinking) {
    PrintSmallEx(LCD_XCENTER, 4, POS_C, C_FILL, "...");
  }

  SP_Render(b);
  SP_RenderLine(sqLevel);

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

  /* PrintSmallEx(LCD_XCENTER, 16 + 6 * 1, POS_C, C_FILL, "%+d",
               -gainTable[gain].gainDb + 33); */

  // bottom
  PrintSmallEx(LCD_XCENTER, LCD_HEIGHT - 2, POS_C, C_FILL, "%u.%05u",
               radio->rxF / MHZ, radio->rxF % MHZ);

  PrintSmallEx(1, LCD_HEIGHT - 2, POS_L, C_FILL, "%u.%05u", b->rxF / MHZ,
               b->rxF % MHZ);
  PrintSmallEx(LCD_WIDTH - 1, LCD_HEIGHT - 2, POS_R, C_FILL, "%u.%05u",
               b->txF / MHZ, b->txF % MHZ);

  FillRect(selStart ? 0 : LCD_WIDTH - 42, LCD_HEIGHT - 7, 42, 7, C_INVERT);
}

void SCANER_deinit(void) {}
