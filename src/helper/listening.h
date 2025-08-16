#ifndef LISTENING_H
#define LISTENING_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  int8_t activityOnVFO;
  int8_t lastActiveVFO;
  bool isSync;
  bool doSync;
  bool doSwitch;
  bool doSwitchBack;
} DW_t;

extern DW_t gDW;

void LISTENING_Init(void);
void LISTENING_Update(void);
void LISTENING_Deinit(void);

#endif /* LISTENING_H */
