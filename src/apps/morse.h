#ifndef MORSE_H
#define MORSE_H

#include "../scheduler.h"
#include "../driver/keyboard.h"
#include <stdbool.h>
#include <stdint.h>


void MORSE_init(void);
void MORSE_update(void);
void MORSE_render(void);
bool MORSE_key(KEY_Code_t key, Key_State_t state);
void MORSE_deinit(void);

#endif
