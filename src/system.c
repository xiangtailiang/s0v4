#include "system.h"
#include "apps/apps.h"
#include "config/FreeRTOSConfig.h"
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
#include "misc.h"
#include "ui/graphics.h"
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

static TimerHandle_t sysTimer;
static StaticTimer_t sysTimerBuffer;

static QueueHandle_t systemMessageQueue; // Message queue handle
static StaticQueue_t systemTasksQueue;   // Static queue storage area
static uint8_t systemQueueStorageArea[queueLen * itemSize];

StaticTask_t scanTaskBuffer;
StackType_t scanTaskStack[configMINIMAL_STACK_SIZE + 100];

static void appRender(void *arg) {
  UI_ClearScreen();
  STATUSLINE_render();

  APPS_render();

  ST7565_Blit();
}

static void appUpdate(void *arg) {
  for (;;) {
    APPS_update();
    vTaskDelay(2);
  }
}

static void systemUpdate() { BATTERY_UpdateBatteryInfo(); }

void SYSTEM_Main(void *params) {
  KEYBOARD_Init();
  Log("Sys task OK");
  BATTERY_UpdateBatteryInfo();

  systemMessageQueue = xQueueCreateStatic(
      queueLen, itemSize, systemQueueStorageArea, &systemTasksQueue);
  sysTimer = xTimerCreateStatic("sysT", pdMS_TO_TICKS(2000), pdTRUE, NULL,
                                systemUpdate, &sysTimerBuffer);
  xTimerStart(systemTimer, 0);
  xTaskCreateStatic(appUpdate, "scan", ARRAY_SIZE(scanTaskStack), NULL, 4,
                    scanTaskStack, &scanTaskBuffer);
  SystemMessages notification;

  for (;;) {
    if (xQueueReceive(systemMessageQueue, &notification, pdMS_TO_TICKS(5))) {
      // Process system notifications
      Log("MSG: m:%u, k:%u, st:%u", notification.message, notification.key,
          notification.state);
      if (notification.message == MSG_KEYPRESSED &&
          (notification.state == KEY_PRESSED ||
           notification.state == KEY_LONG_PRESSED_CONT)) {
        if (APPS_key(notification.key, notification.state)) {
          APPS_render();
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
