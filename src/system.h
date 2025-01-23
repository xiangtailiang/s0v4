#ifndef SYSTEM_H
#define SYSTEM_H
#include "driver/keyboard.h"

void SYSTEM_Main(void *params);
void SYSTEM_MsgKey(KEY_Code_t key, Key_State_t state);

#endif /* end of include guard: SYSTEM_H */
