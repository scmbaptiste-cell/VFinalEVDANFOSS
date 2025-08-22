#pragma once
#include <Arduino.h>

// ----------- Brochage ----------
#define I2C_SDA 21
#define I2C_SCL 22
#define MODE_SEL_PIN 33        // LOW = filaire ; HIGH = manette
#define CAL_BTN_PIN 32
#define LED_VERTE_PIN 25
#define LED_ROUGE_PIN 26
#define GPIO_MANETTE_CONNECTEE 27

// ----------- Boutons (Bluepad32) ----------
#define BTN_R1 0x0010
#define BTN_L1 0x0020

// ----------- Fenêtre neutre base ----------
extern int joyNeutralMin; // 482
extern int joyNeutralMax; // 542
extern int neutralOffset; // 512
void updateNeutralWindow();
void loadNeutralOffset();
void saveNeutralOffset();

// ----------- États globaux / timers ----------
enum FaultCode : uint8_t {
  FC_NONE        = 0,
  FC_ADS_DROIT   = 2,
  FC_ADS_GAUCHE  = 3,
  FC_PCA         = 4,
  FC_I2C_GENERAL = 5,
  FC_NEUTRAL_TO  = 7,
  FC_NO_GAMEPAD  = 101
};

extern volatile uint8_t faultCode;
extern bool pcaOK;
extern bool adsOK[2];
extern bool wiredNeutralOK;
extern bool calibMode;
extern bool safetyReady;
extern bool softRadioOverride;

extern unsigned long modeChangeBlockUntil;
extern const unsigned long MODE_CHANGE_BLOCK_MS;

// ----------- Utilitaires ----------
bool isWiredMode();
inline bool isEffectiveWiredMode() { return isWiredMode() && !softRadioOverride; }

// LED carte helpers
void updateStatusLEDs();
void ledSelfTest();

// ----------- Config bouton calibration -----------
// 0 = ACTIF-BAS (avec INPUT_PULLUP) — c'est ton câblage
// 1 = actif-haut (si tu changes plus tard)
#ifndef CAL_BTN_ACTIVE_HIGH
#define CAL_BTN_ACTIVE_HIGH 0
#endif
