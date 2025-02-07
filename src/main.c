#include "board.h"
#include "config/FreeRTOSConfig.h"
#include "driver/crc.h"
#include "driver/system.h"
#include "driver/systick.h"
#include "driver/uart.h"
#include "external/FreeRTOS/include/FreeRTOS.h"
#include "external/FreeRTOS/portable/GCC/ARM_CM0/portmacro.h"
#include "misc.h"
#include "system.h"

StaticTask_t systemTaskBuffer;
StackType_t systemTaskStack[configMINIMAL_STACK_SIZE + 300];

void Main(void) {
  SYSTICK_Init();
  SYSTEM_ConfigureSysCon();
  BOARD_GPIO_Init();
  BOARD_PORTCON_Init();
  BOARD_ADC_Init();
  CRC_Init();
  UART_Init();

  Log("s0v4");

  xTaskCreateStatic(SYSTEM_Main, "sys", ARRAY_SIZE(systemTaskStack), NULL, 1,
                    systemTaskStack, &systemTaskBuffer);

  vTaskStartScheduler();

  Log("Scheduler exit =(");

  for (;;) {
  }
}
