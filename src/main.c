#include "board.h"
#include "driver/crc.h"
#include "driver/system.h"
#include "driver/systick.h"
#include "driver/uart.h"
#include "FreeRTOS.h"
#include "misc.h"
#include "system.h"

StaticTask_t systemTaskBuffer;
StackType_t systemTaskStack[configMINIMAL_STACK_SIZE + 200];

void Main(void) {
  SYSTICK_Init();
  SYSTEM_ConfigureSysCon();
  BOARD_GPIO_Init();
  BOARD_PORTCON_Init();
  BOARD_ADC_Init();
  CRC_Init();
  UART_Init();

  Log("s0v4");

  BOARD_Init();
  Log("BOARD OK");

  xTaskCreateStatic(SYSTEM_Main, "sys", ARRAY_SIZE(systemTaskStack), NULL, 0,
                    systemTaskStack, &systemTaskBuffer);

  vTaskStartScheduler();

  Log("Scheduler exit =(");

  for (;;) {
  }
}
