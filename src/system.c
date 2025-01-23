#include "system.h"
#include "FreeRTOS.h"
#include "driver/audio.h"
#include "driver/bk4819.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "driver/systick.h"
#include "driver/uart.h"
#include "helper/battery.h"
#include "helper/channels.h"
#include "helper/lootlist.h"
#include "helper/measurements.h"
#include "queue.h"
#include "timers.h"
#include "ui/graphics.h"
#include "ui/spectrum.h"
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
};

static Measurement m;
static uint32_t delay = 2;

static void appUpdate(void *arg) {
  UI_ClearScreen();
  SP_Render(&b);
  PrintSmallEx(0, 12 + 6 * 0, POS_L, C_FILL, "%ums", delay);
  ST7565_Blit();
}

static void scanUpdate(void *arg) {
  for (;;) {
    BK4819_SetFrequency(m.f);
    BK4819_WriteRegister(BK4819_REG_30, 0x0200);
    BK4819_WriteRegister(BK4819_REG_30, 0xBFF1);
    SYSTICK_DelayUs(delay * 1000);
    m.rssi = BK4819_GetRSSI();
    m.snr = 0;

    SP_AddPoint(&m);

    m.f += 2500;
    if (m.f > b.txF) {
      m.f = b.rxF;
      appUpdate(NULL);
    }
    vTaskDelay(2 * delay);
  }
}

void SYSTEM_Main(void *params) {
  KEYBOARD_Init();
  Log("Sys task OK");
  systemMessageQueue = xQueueCreateStatic(
      queueLen, itemSize, systemQueueStorageArea, &systemTasksQueue);
  /* appTimer = xTimerCreateStatic("app", pdMS_TO_TICKS(250), pdTRUE, NULL,
                                appUpdate, &appTimerBuffer); */
  /* scanTimer = xTimerCreateStatic("scan", pdMS_TO_TICKS(1), pdTRUE, NULL,
                                 scanUpdate, &scanTimerBuffer); */
  // xTimerStart(appTimer, 0);
  // xTimerStart(scanTimer, 0);
  // BATTERY_UpdateBatteryInfo();
  BK4819_SetAGC(true, AUTO_GAIN_INDEX + 1);
  BK4819_SetAFC(0);
  BK4819_SetFilterBandwidth(BK4819_FILTER_BW_12k);
  BK4819_SelectFilter(b.rxF);
  BK4819_RX_TurnOn();
  m.f = b.rxF;
  SPECTRUM_Y = 6;
  SPECTRUM_H = 48;
  SP_Init(&b);
  xTaskCreateStatic(scanUpdate, "scan", ARRAY_SIZE(scanTaskStack), NULL, 4,
                    scanTaskStack, &scanTaskBuffer);
  /*
  BK4819_ToggleAFDAC(true);
  BK4819_ToggleAFBit(true);
  AUDIO_ToggleSpeaker(true); */
  SystemMessages notification;

  for (;;) {
    if (xQueueReceive(systemMessageQueue, &notification, pdMS_TO_TICKS(5)) ==
        pdTRUE) {
      // Process system notifications
      Log("MSG: m:%u, k:%u, st:%u", notification.message, notification.key,
          notification.state);

      if (notification.message == MSG_KEYPRESSED &&
          notification.state == KEY_PRESSED) {
        if (notification.key == KEY_1) {
          IncDec32(&delay, 1, 10, 1);
          appUpdate(NULL);
        } else if (notification.key == KEY_7) {
          IncDec32(&delay, 1, 10, -1);
          appUpdate(NULL);
        }
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
