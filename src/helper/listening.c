#include "listening.h"
#include "../driver/bk4819.h"
#include "../driver/system.h"
#include "../radio.h"
#include "../settings.h"
#include <stddef.h>

DW_t gDW;

static const uint8_t VFOS_COUNT = 2;
static const uint8_t DW_CHECK_DELAY = 20;
static const uint16_t DW_RSSI_RESET_CYCLE = 1000;
static uint32_t lastRssiResetTime = 0;
static uint32_t switchBackTimer = 0;

static bool checkActivityOnFreq(uint32_t freq) {
  uint32_t oldFreq = BK4819_GetFrequency();
  bool activity;

  BK4819_TuneTo(freq, false);
  SYSTEM_DelayMs(DW_CHECK_DELAY);
  activity = BK4819_IsSquelchOpen();
  BK4819_TuneTo(oldFreq, false);

  return activity;
}

static void sync(void) {
  static int8_t i = VFOS_COUNT - 1;

  // Do not sync if we are in TX
  if (gTxState != TX_OFF) {
      return;
  }

  if (checkActivityOnFreq(gVFO[i].rxF)) {
    gDW.activityOnVFO = i;
    gDW.lastActiveVFO = i;
    if (gSettings.dw == DW_SWITCH) {
      gSettings.activeVFO = i;
      RADIO_SaveCurrentVFO();
    }
    gDW.isSync = true;
    gDW.doSync = false;
    gDW.doSwitch = true;
    gDW.doSwitchBack = false;
  }

  i--;
  if (i < 0) {
    i = VFOS_COUNT - 1;
  }
}

void LISTENING_Init(void) {
  gDW.lastActiveVFO = -1;
  gDW.activityOnVFO = 0;
  gDW.isSync = false;
  gDW.doSync = gSettings.dw != DW_OFF;
  gDW.doSwitch = false;
  gDW.doSwitchBack = false;
}

void LISTENING_Update(void) {
  if (gSettings.dw != DW_OFF) {
    if (gDW.doSwitch) {
      gDW.doSwitch = false;
      gDW.doSync = false;
      gDW.doSwitchBack = true;

      radio = &gVFO[gDW.activityOnVFO];
      RADIO_SetupByCurrentVFO();
    }

    if (gDW.doSwitchBack) {
      if (!gIsListening && Now() - switchBackTimer >= 500) {
        switchBackTimer = Now();

        gDW.doSwitch = false;
        gDW.doSync = true;
        gDW.doSwitchBack = false;
        gDW.isSync = false;

        radio = &gVFO[gSettings.activeVFO];
        RADIO_SetupByCurrentVFO();
      }
    }
  }
  if (gTxState == TX_OFF && gDW.doSync) {
    if (gSettings.dw == DW_OFF) {
      gDW.doSync = false;
      return;
    }

    sync();

    if (Now() - lastRssiResetTime > DW_RSSI_RESET_CYCLE) {
      BK4819_ResetRSSI();
      lastRssiResetTime = Now();
    }
    return;
  }

  RADIO_CheckAndListen();
}

void LISTENING_Deinit(void) {
  gDW.doSync = false;
  gDW.doSwitch = false;
  gDW.doSwitchBack = false;
}
