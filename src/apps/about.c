#include "about.h"
#include "../ui/graphics.h"
#include "apps.h"

void ABOUT_Render() {
  PrintMediumEx(LCD_XCENTER, LCD_YCENTER - 8, POS_C, C_FILL, "s0v4");
  PrintSmallEx(LCD_XCENTER, LCD_YCENTER, POS_C, C_FILL, "FAGCI & Tiger");
  PrintSmallEx(LCD_XCENTER, LCD_YCENTER + 8, POS_C, C_FILL, TIME_STAMP);
}

#pragma GCC diagnostic ignored "-Wunused-parameter"
bool ABOUT_key(KEY_Code_t k, Key_State_t state) {
  switch (k) {
  case KEY_EXIT:
    APPS_exit();
    return true;
  default:
    return false;
  }
}
