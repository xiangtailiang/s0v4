#include "analyzer.h"
#include "../apps/finput.h"
#include "../driver/st7565.h"
#include "../driver/system.h"
#include "../driver/uart.h"
#include "../helper/bands.h"
#include "../helper/lootlist.h"
#include "../helper/measurements.h"
#include "../radio.h"
#include "../scheduler.h"
#include "../ui/graphics.h"
#include "../ui/spectrum.h"
#include "../ui/statusline.h"
#include "apps.h"
#include "vfo1.h"
#include <stdint.h>

static Band b = {
    .rxF = 17200000,
    .txF = 17200000 + 2500 * 128,
    .step = STEP_25_0kHz,
    .bw = BK4819_FILTER_BW_12k,
};

static Measurement m;
static uint32_t delay = 1200;
static uint8_t filter = FILTER_VHF;

static uint32_t peakF = 0;
static uint8_t peakSnr = 0;
static uint8_t lowSnr = 255;
static const char *bwNames[10] = {
    "U6K",  //
    "U7K",  //
    "N9k",  //
    "N10k", //
    "W12k", //
    "W14k", //
    "W17k", //
    "W20k", //
    "W23k", //
    "W26k", //
};

static void selectFilter(Filter filter) {
  BK4819_ToggleGpioOut(BK4819_GPIO4_PIN32_VHF_LNA, filter == FILTER_VHF);
  BK4819_ToggleGpioOut(BK4819_GPIO3_PIN31_UHF_LNA, filter == FILTER_UHF);
}

static void updateSecondF(uint32_t f) {
  b.txF = f;
  SP_Init(&b);
}

static void updateFirstF(uint32_t f) { b.rxF = f; }

static const char *fltNames[3] = {"VHF", "UHF", "OFF"};

typedef enum {
  MSM_RSSI,
  MSM_SNR,
  MSM_NOISE,
  MSM_GLITCH,
  MSM_EXTRA,
} MsmBy;

static const char *msmByNames[5] = {
    "RSSI", "SNR", "NOISE", "GLITCH", "EXTRA",
};

static uint8_t msmBy = MSM_RSSI;
static uint8_t gain = 21;

void ANALYZER_init(void) {
  SPECTRUM_Y = 6;
  SPECTRUM_H = 48;

  BK4819_SetAGC(true, gain);
  BK4819_SetAFC(0);
  BK4819_SetFilterBandwidth(b.bw);
  selectFilter(filter);
  BK4819_SetModulation(MOD_FM);
  BK4819_RX_TurnOn();
  m.f = b.rxF;
  SP_Init(&b);
}

void ANALYZER_update(void) {
  m.snr = 0;
  m.rssi = 0;

  BK4819_SetFrequency(m.f);
  BK4819_WriteRegister(BK4819_REG_30, 0x0000);
  BK4819_WriteRegister(BK4819_REG_30, 0xBFF1);
  vTaskDelay(delay / 100);
  switch (msmBy) {
  case MSM_RSSI:
    m.rssi = BK4819_GetRSSI();
    break;
  case MSM_NOISE:
    m.rssi = BK4819_GetNoise();
    break;
  case MSM_GLITCH:
    m.rssi = BK4819_GetGlitch();
    break;
  case MSM_SNR:
    m.rssi = BK4819_GetSNR();
    break;
  case MSM_EXTRA:
    m.rssi = (BK4819_ReadRegister(0x62) >> 8) &
             0xff; // another snr ? размазывает сигнал на спектре, возможно
                   // уровень сигнала до фильтра, показывает и при 200мкс TEST
    break;
  }
  // m.rssi = BK4819_GetRSSI();
  /* m.noise = BK4819_GetNoise();
  m.glitch = BK4819_GetGlitch(); */

  // m.snr = BK4819_ReadRegister(0x66) & 0xff; // t>=4ms, interesting выше
  // m.snr = (BK4819_ReadRegister(0x66) >> 8) & 0xff; // noise? запаздывает на
  // шаг, показывает только правый канал TEST
  m.timeUs = delay;
  if (m.rssi > peakSnr) {
    peakSnr = m.rssi;
    peakF = m.f;
  }
  if (m.rssi < lowSnr) {
    lowSnr = m.rssi;
  }

  SP_AddPoint(&m);
  // Log("%u,%u,%u,%u,%u,%u", m.timeUs, m.f, m.rssi, m.noise, m.glitch,
  // m.snr);

  m.f += StepFrequencyTable[b.step];
  if (m.f > b.txF) {
    m.f = b.rxF;
    appRender(NULL);
    peakF = 0;
    peakSnr = 0;
  }
}

void ANALYZER_deinit(void) {}

bool ANALYZER_key(KEY_Code_t key, Key_State_t state) {
  uint8_t stp;
  uint8_t bw;

  switch (key) {
  case KEY_1:
    IncDec32(&delay, 200, 10000, 100);
    appRender(NULL);
    break;
  case KEY_7:
    IncDec32(&delay, 200, 10000, -100);
    appRender(NULL);
    break;
  case KEY_3:
    stp = b.step;
    IncDec8(&stp, STEP_0_02kHz, STEP_500_0kHz + 1, 1);
    b.step = stp;
    SP_Init(&b);
    appRender(NULL);
    break;
  case KEY_9:
    stp = b.step;
    IncDec8(&stp, STEP_0_02kHz, STEP_500_0kHz + 1, -1);
    b.step = stp;
    SP_Init(&b);
    break;
  case KEY_2:
    bw = b.bw;
    IncDec8(&bw, BK4819_FILTER_BW_6k, BK4819_FILTER_BW_26k + 1, 1);
    b.bw = bw;
    BK4819_SetFilterBandwidth(b.bw);
    appRender(NULL);
    break;
  case KEY_8:
    bw = b.bw;
    IncDec8(&bw, BK4819_FILTER_BW_6k, BK4819_FILTER_BW_26k + 1, -1);
    b.bw = bw;
    BK4819_SetFilterBandwidth(b.bw);
    appRender(NULL);
    break;
  case KEY_6:
    IncDec8(&filter, 0, 3, 1);
    selectFilter(filter);
    appRender(NULL);
    break;
  case KEY_STAR:
    IncDec8(&msmBy, MSM_RSSI, MSM_EXTRA + 1, 1);
    SP_Init(&b);
    m.f = b.rxF;
    appRender(NULL);
    break;
  case KEY_SIDE1:
    IncDec8(&gain, 0, ARRAY_SIZE(gainTable), 1);
    BK4819_SetAGC(true, gain);
    appRender(NULL);
    break;
  case KEY_SIDE2:
    IncDec8(&gain, 0, ARRAY_SIZE(gainTable), -1);
    BK4819_SetAGC(true, gain);
    appRender(NULL);
    break;

  default:
    break;
  }

  /* if (notification.key == KEY_5) {
    for (uint8_t r = 0; r < 255; ++r) {
      uint16_t reg = BK4819_ReadRegister(r);
      Log("0x%x, %u, %u", r, (reg >> 8) & 0xFF, reg & 0xFF);
    }
  } */
}

void ANALYZER_render(void) {
  SP_Render(&b);
  PrintSmallEx(0, 12 + 6 * 0, POS_L, C_FILL, "%uus", delay);
  PrintSmallEx(0, 12 + 6 * 1, POS_L, C_FILL, "%u", peakSnr);
  PrintSmallEx(0, 12 + 6 * 2, POS_L, C_FILL, "%u", lowSnr);

  PrintSmallEx(LCD_XCENTER, 16 + 6 * 1, POS_C, C_FILL, "%+d",
               -gainTable[gain].gainDb + 33);

  PrintSmallEx(LCD_WIDTH, 12 + 6 * 0, POS_R, C_FILL, "STP %u.%02uk",
               StepFrequencyTable[b.step] / 100,
               StepFrequencyTable[b.step] % 100);
  PrintSmallEx(LCD_WIDTH, 12 + 6 * 1, POS_R, C_FILL, "BW %s", bwNames[b.bw]);
  PrintSmallEx(LCD_WIDTH, 12 + 6 * 2, POS_R, C_FILL, "FLT %s",
               fltNames[filter]);
  PrintSmallEx(LCD_WIDTH, 12 + 6 * 3, POS_R, C_FILL, "%s", msmByNames[msmBy]);

  SP_RenderArrow(&b, peakF);
  PrintMediumEx(LCD_XCENTER, 16, POS_C, C_FILL, "%u.%05u", peakF / MHZ,
                peakF % MHZ);
  PrintSmallEx(0, LCD_HEIGHT - 1, POS_L, C_FILL, "%u.%05u", b.rxF / MHZ,
               b.rxF % MHZ);
  PrintSmallEx(LCD_WIDTH, LCD_HEIGHT - 1, POS_R, C_FILL, "%u.%05u", b.txF / MHZ,
               b.txF % MHZ);

  peakSnr = 0;
  lowSnr = 255;
}
