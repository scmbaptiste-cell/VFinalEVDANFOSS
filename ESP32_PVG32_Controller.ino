// ESP32_PVG32_Controller.ino — point d'entrée Arduino
// Carte: ESP32 Dev Module (Espressif 3.3.0)

#include <Arduino.h>
#include "Config.h"
#include "IOMap.h"
#include "Controllers.h"
#include "Calibration.h"
#include "Bridage.h"
#include "Faults.h"
#include "FaultsPortal.h"
#include "Portal.h"
#include "Led.h"

static bool lastWired = false;

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(LED_VERTE_PIN, OUTPUT);
  pinMode(LED_ROUGE_PIN, OUTPUT);
  pinMode(GPIO_MANETTE_CONNECTEE, OUTPUT);
  pinMode(MODE_SEL_PIN, INPUT);
  pinMode(CAL_BTN_PIN, INPUT);

  ledSelfTest();

  ioInitI2CAndPCA();       // I2C + ADS + PCA (neutralise les sorties au passage)
  faultsBootCheck();       // Auto-test I2C (N2/N3/N4/N5 si besoin)
  calLoadOrDefault();      // Calibration joysticks (EEPROM)
  bridageLoadOrDefault();  // Bridage manette (EEPROM)
  loadNeutralOffset();
  updateNeutralWindow();
  controllersSetup();      // Bluepad32 (callbacks connect/disconnect)

  // Mode initial + neutralisation
  lastWired = isWiredMode();
  onModeChanged(lastWired);

  Serial.println("=== ESP32 PVG32 Controller prêt ===");
}

void loop() {
  // 1) Manette
  controllersUpdate();
  processR1L1Override();
  processControllers();

  // 2) Joystick (et calibration intégrée)
  processADS();

  // 3) Bridage: séquence 5 appuis + portail
  bridageHandleButtonSequence(false, 50, 5000);
  bridageHandlePortal();

  // 4) Défauts / Portail défaut
  faultsPortalHandle();
  portalHandle();

  // 5) Watchdog + LEDs
  i2cRuntimeWatchdog();
  serviceControllerLEDs();
  updateStatusLEDs();

  // 6) Changement de mode via sectionneur
  bool wiredNow = isWiredMode();
  if (wiredNow != lastWired && millis() > modeChangeBlockUntil) {
    onModeChanged(wiredNow);
    lastWired = wiredNow;
  }

  delay(5);
}
