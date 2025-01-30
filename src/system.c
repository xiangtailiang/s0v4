#include "system.h"
#include "../external/CMSIS_5/Device/ARM/ARMCM0/Include/ARMCM0.h"
#include "apps/apps.h"
#include "config/FreeRTOSConfig.h"
#include "driver/eeprom.h"
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
#include "settings.h"
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

uint32_t gAppUpdateInterval = pdMS_TO_TICKS(1);

static TimerHandle_t sysTimer;
static StaticTimer_t sysTimerBuffer;

static QueueHandle_t systemMessageQueue; // Message queue handle
static StaticQueue_t systemTasksQueue;   // Static queue storage area
static uint8_t systemQueueStorageArea[queueLen * itemSize];

StaticTask_t appUpdateTaskBuffer;
StackType_t appUpdateTaskStack[configMINIMAL_STACK_SIZE + 100];

StaticTask_t appRenderTaskBuffer;
StackType_t appRenderTaskStack[configMINIMAL_STACK_SIZE + 100];

static void appUpdate(void *arg) {
  for (;;) {
    APPS_update();
    vTaskDelay(gAppUpdateInterval);
  }
}

static void appRender(void *arg) {
  for (;;) {
    if (gRedrawScreen) {
      UI_ClearScreen();
      STATUSLINE_render();

      APPS_render();

      ST7565_Blit();
      gRedrawScreen = false;
    }
    vTaskDelay(pdMS_TO_TICKS(40)); // 25 fps
  }
}

static void systemUpdate() { BATTERY_UpdateBatteryInfo(); }

void SYSTEM_Main(void *params) {
  KEYBOARD_Init();
  Log("Sys task OK");

  uint8_t buf[2];
  uint8_t deadBuf[] = {0xDE, 0xAD};
  EEPROM_ReadBuffer(0, buf, 2);

  if (memcmp(buf, deadBuf, 2) == 0) {
    gSettings.batteryCalibration = 2000;
    gSettings.backlight = 5;
    APPS_run(APP_RESET);
  } else {
    SETTINGS_Load();
    if (gSettings.batteryCalibration > 2154 ||
        gSettings.batteryCalibration < 1900) {
      gSettings.batteryCalibration = 0;
      EEPROM_WriteBuffer(0, deadBuf, 2);
      NVIC_SystemReset();
    }

    ST7565_Init();
    // BACKLIGHT_Init();
    APPS_run(gSettings.mainApp);
  }

  BATTERY_UpdateBatteryInfo();

  systemMessageQueue = xQueueCreateStatic(
      queueLen, itemSize, systemQueueStorageArea, &systemTasksQueue);
  sysTimer = xTimerCreateStatic("sysT", pdMS_TO_TICKS(2000), pdTRUE, NULL,
                                systemUpdate, &sysTimerBuffer);
  xTimerStart(sysTimer, 0);

  xTaskCreateStatic(appUpdate, "appU", ARRAY_SIZE(appUpdateTaskStack), NULL, 4,
                    appUpdateTaskStack, &appUpdateTaskBuffer);
  xTaskCreateStatic(appRender, "appR", ARRAY_SIZE(appRenderTaskStack), NULL, 4,
                    appRenderTaskStack, &appRenderTaskBuffer);

  SystemMessages n;

  for (;;) {
    if (xQueueReceive(systemMessageQueue, &n, pdMS_TO_TICKS(5))) {
      // Process system notifications
      Log("MSG: m:%u, k:%u, st:%u", n.message, n.key, n.state);
      if (n.message == MSG_KEYPRESSED) {
        if (APPS_key(n.key, n.state)) {
          gRedrawScreen = true;
        } else {
          Log("Process keys external");
          if (n.key == KEY_MENU) {
            if (n.state == KEY_LONG_PRESSED) {
              APPS_run(APP_SETTINGS);
            } else if (n.state == KEY_RELEASED) {
              APPS_run(APP_APPS_LIST);
            }
          }
          if (n.key == KEY_EXIT) {
            if (n.state == KEY_PRESSED) {
              APPS_exit();
            }
          }
        }
      }
    }

    if (UART_IsCommandAvailable()) {
      UART_HandleCommand();
    }
    STATUSLINE_update();
  }
}

void SYSTEM_MsgKey(KEY_Code_t key, Key_State_t state) {
  SystemMessages appMSG = {MSG_KEYPRESSED, 0, key, state};
  BaseType_t xHigherPriorityTaskWoken = pdTRUE;
  xQueueSendFromISR(systemMessageQueue, (void *)&appMSG,
                    &xHigherPriorityTaskWoken);
}
