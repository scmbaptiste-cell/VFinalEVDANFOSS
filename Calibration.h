#pragma once
#include "Config.h"
#include <EEPROM.h>
#include <WebServer.h>
#include <WiFi.h>
#include <DNSServer.h>

// -------- Types/extern --------
struct CalAxis;
extern CalAxis cal[8];
extern bool haveMin[8], haveMax[8];

extern bool calibMode;
extern bool wiredNeutralOK;

// Calibration valide présente en EEPROM
extern bool calDataValid;

// Machine d’états de calibration
enum CalPhase : uint8_t {
  CAL_PHASE_IDLE = 0,
  CAL_PHASE_NEUTRAL_INIT,
  CAL_PHASE_NEUTRAL_VALIDATE,
  CAL_PHASE_NEUTRAL_DONE,
  CAL_PHASE_EXTREMES,
  CAL_PHASE_FINISH
};
extern volatile CalPhase calPhase;

// Timers exposés (lecture/debug)
extern uint32_t calPhaseEndMs;

// Seuil de détection d’axe en MAP (255..768)
extern const int CAL_MOVE_THR_MAP;  // = 20

// -------- API --------
void calLoadOrDefault();
void saveCalToEEPROM();
bool readCalButton();
void processCalibration();

void startCalibration();
void finishCalibration();

// Portail (laissé actif, mais affichage série prioritaire)
void calibWifiStart();
void calibWifiStop();
