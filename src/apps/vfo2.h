#ifndef VFO2_H
#define VFO2_H

#include "../driver/keyboard.h"
#include <stdbool.h>

void VFO2_init(void);
bool VFO2_key(KEY_Code_t key, Key_State_t state);
void VFO2_update(void);
void VFO2_render(void);
void VFO2_deinit(void);

#endif /* end of include guard: VFO2_H */
