#include "system.h"
#include "apps/apps.h"
#include "driver/audio.h"
#include "driver/bk4819.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "driver/uart.h"
#include "external/FreeRTOS/include/FreeRTOS.h"
#include "external/FreeRTOS/include/projdefs.h"
#include "external/FreeRTOS/include/queue.h"
#include "external/FreeRTOS/include/task.h"
#include "external/FreeRTOS/include/timers.h"
#include "external/FreeRTOS/portable/GCC/ARM_CM0/portmacro.h"
#include "helper/battery.h"
#include "helper/channels.h"
#include "helper/lootlist.h"
#include "helper/measurements.h"
#include "misc.h"
#include "radio.h"
#include "ui/graphics.h"
#include "ui/spectrum.h"
#include "ui/statusline.h"
#include <iso646.h>

#define queueLen 20
#define itemSize sizeof(SystemMessages)

typedef enum {
  MSG_TIMEOUT,
  MSG_BKCLIGHT,
  MSG_KEYPRESSED,
  MSG_PLAY_BEEP,
  MSG_RADIO_RX,
  MSG_RADIO_TX,
  MSG_APP_LOAD,
} SystemMSG;

typedef struct {
  SystemMSG message;
  uint32_t payload;
  KEY_Code_t key;
  Key_State_t state;
} SystemMessages;

static TimerHandle_t appTimer;
static StaticTimer_t appTimerBuffer;
static TimerHandle_t scanTimer;
static StaticTimer_t scanTimerBuffer;

static QueueHandle_t systemMessageQueue; // Message queue handle
static StaticQueue_t systemTasksQueue;   // Static queue storage area
static uint8_t systemQueueStorageArea[queueLen * itemSize];

StaticTask_t scanTaskBuffer;
StackType_t scanTaskStack[configMINIMAL_STACK_SIZE + 100];

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

static void appRender(void *arg) {
  BATTERY_UpdateBatteryInfo();

  UI_ClearScreen();
  STATUSLINE_render();
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

  ST7565_Blit();
  peakSnr = 0;
  lowSnr = 255;
}

static void selectFilter(Filter filter) {
  BK4819_ToggleGpioOut(BK4819_GPIO4_PIN32_VHF_LNA, filter == FILTER_VHF);
  BK4819_ToggleGpioOut(BK4819_GPIO3_PIN31_UHF_LNA, filter == FILTER_UHF);
}

static void appUpdate(void *arg) {
  for (;;) {
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
    vTaskDelay(2);
  }
}

void SYSTEM_Main(void *params) {
  KEYBOARD_Init();
  Log("Sys task OK");
  systemMessageQueue = xQueueCreateStatic(
      queueLen, itemSize, systemQueueStorageArea, &systemTasksQueue);
  /* scanTimer = xTimerCreateStatic("scan", pdMS_TO_TICKS(1), pdTRUE, NULL,
                                 scanUpdate, &scanTimerBuffer); */
  // xTimerStart(appTimer, 0);
  // xTimerStart(scanTimer, 0);
  // BK4819_TuneTo(43422500, true);
  BATTERY_UpdateBatteryInfo();
  BK4819_SetAGC(true, gain);
  BK4819_SetAFC(0);
  BK4819_SetFilterBandwidth(b.bw);
  // BK4819_SelectFilter(b.rxF);
  selectFilter(filter);
  BK4819_SetModulation(MOD_FM);
  BK4819_RX_TurnOn();
  m.f = b.rxF;
  SPECTRUM_Y = 6;
  SPECTRUM_H = 48;
  SP_Init(&b);
  xTaskCreateStatic(appUpdate, "scan", ARRAY_SIZE(scanTaskStack), NULL, 4,
                    scanTaskStack, &scanTaskBuffer);
  SystemMessages notification;

  for (;;) {
    if (xQueueReceive(systemMessageQueue, &notification, pdMS_TO_TICKS(5))) {
      // Process system notifications
      Log("MSG: m:%u, k:%u, st:%u", notification.message, notification.key,
          notification.state);
      uint8_t stp;
      uint8_t bw;

      if (notification.message == MSG_KEYPRESSED &&
          (notification.state == KEY_PRESSED ||
           notification.state == KEY_LONG_PRESSED_CONT)) {
        switch (notification.key) {
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
    }

    if (UART_IsCommandAvailable()) {
      UART_HandleCommand();
    }
  }
}

void SYSTEM_MsgKey(KEY_Code_t key, Key_State_t state) {
  SystemMessages appMSG = {MSG_KEYPRESSED, 0, key, state};
  BaseType_t xHigherPriorityTaskWoken = pdTRUE;
  xQueueSendFromISR(systemMessageQueue, (void *)&appMSG,
                    &xHigherPriorityTaskWoken);
}
