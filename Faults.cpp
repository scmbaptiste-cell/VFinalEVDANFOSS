#include "Faults.h"
#include <Wire.h>
#include "Led.h"
#include "IOMap.h"
#include "Controllers.h"
#include "Calibration.h"
#include "FaultsPortal.h"

volatile uint8_t faultCode = FC_NONE;
bool missADSg=false, missADSd=false, missPCA=false;

const unsigned long MODE_CHANGE_BLOCK_MS = 500;
unsigned long modeChangeBlockUntil = 0;

static const uint32_t FAULT_BLINK_ON_MS=1000, FAULT_BLINK_OFF_MS=1000, FAULT_PAUSE_MS=4000;
struct FaultDisplay { bool active=false; uint32_t t0=0; uint8_t state=0, blinkCount=0, blinkTarget=0; } fdisp;

String fmtUptime(){ unsigned long s=millis()/1000UL; char b[16]; snprintf(b,sizeof(b),"%02lu:%02lu:%02lu",(s/3600UL)%100,(s%3600UL)/60UL,(s%60UL)); return String(b); }

static void faultLEDOff(){ digitalWrite(LED_VERTE_PIN,false); digitalWrite(LED_ROUGE_PIN,false); }
static void startFaultSeries(uint8_t n){ fdisp.active=true; fdisp.t0=millis(); fdisp.state=0; fdisp.blinkCount=0; fdisp.blinkTarget=n; digitalWrite(LED_VERTE_PIN,false); digitalWrite(LED_ROUGE_PIN, true); }
static bool i2cExists(uint8_t addr){ Wire.beginTransmission(addr); return (Wire.endTransmission()==0); }

// Séquence détaillée après N=5
static uint8_t detailCycleIdx = 0;
static uint8_t pickNextI2CDetail(){
  for(uint8_t k=0;k<3;k++){
    uint8_t idx=(detailCycleIdx + k)%3;
    if(idx==0 && missADSd) { detailCycleIdx=(idx+1)%3; return FC_ADS_DROIT; }
    if(idx==1 && missADSg) { detailCycleIdx=(idx+1)%3; return FC_ADS_GAUCHE; }
    if(idx==2 && missPCA ) { detailCycleIdx=(idx+1)%3; return FC_PCA; }
  }
  return FC_I2C_GENERAL;
}

void setFault(FaultCode c, const char* origin){
  if (faultCode == c) return;
  faultCode = c; fdisp.active = false;
  Serial.printf("[DEFAUT %s] -> %u\n", origin, c);
  if (c == FC_I2C_GENERAL){
    Serial.print("N5 : Défaut général I2C. Détails :");
    if(missADSd) Serial.print(" N2(ADS droit KO)");
    if(missADSg) Serial.print(" N3(ADS gauche KO)");
    if(missPCA)  Serial.print(" N4(PCA KO)");
    Serial.println();
  } else if (c == FC_ADS_DROIT)   Serial.println("N2 : ADS1115 DROIT (0x49) absent/KO.");
  else if (c == FC_ADS_GAUCHE)    Serial.println("N3 : ADS1115 GAUCHE (0x48) absent/KO.");
  else if (c == FC_PCA)           Serial.println("N4 : PCA9685 (0x40) absent/KO.");
  else if (c == FC_NEUTRAL_TO) {
    Serial.println("N7 : Temps dépassé pour neutre joystick au démarrage.");
    // Afficher immédiatement les valeurs axes via le portail de calibration
    calibWifiStart();
  }
  else if (c == FC_NO_GAMEPAD)    Serial.println("Alternance Rouge/Vert = Mode manette sans manette connectée.");

  if (faultCode != FC_NONE){
    faultsPortalStartAP();
  }
}

void clearFault(){
  if(faultCode!=FC_NONE){ Serial.println("[DEFAUT] Effacement des défauts."); }
  faultCode=FC_NONE; fdisp.active=false;
  faultsPortalStopAP();
}

void serviceFaultDisplay(){
  if(faultCode==FC_NONE){ fdisp.active=false; return; }

  faultsPortalHandle();

  // Autoriser la calibration par appui long même en N7
  static bool last=false; static uint32_t t0=0;
  if (faultCode == FC_NEUTRAL_TO){
    bool now = readCalButton();   // <<< même logique que la calibration
    if(now && !last){ t0 = millis(); }
    if(now && last){
      if(millis()-t0 >= 5000){
        Serial.println("[DEFAUT] Appui long détecté en N7 -> démarrage de la calibration.");
        startCalibration();
        return;
      }
    }
    if(!now && last){ t0=0; }
    last = now;
  }

  if(faultCode==FC_NO_GAMEPAD){
    static uint32_t t=0; static bool flip=false;
    if(millis()-t>=FAULT_BLINK_ON_MS){ t=millis(); flip=!flip; }
    digitalWrite(LED_VERTE_PIN,flip); digitalWrite(LED_ROUGE_PIN,!flip);
    return;
  }
  if(!fdisp.active){ startFaultSeries((uint8_t)faultCode); }
  uint32_t now=millis();
  switch(fdisp.state){
    case 0: if(now-fdisp.t0>=FAULT_BLINK_ON_MS){ faultLEDOff(); fdisp.t0=now; fdisp.state=1; } break;
    case 1: if(now-fdisp.t0>=FAULT_BLINK_OFF_MS){
      if(++fdisp.blinkCount>=fdisp.blinkTarget){ fdisp.state=2; fdisp.t0=now; faultLEDOff(); }
      else { digitalWrite(LED_ROUGE_PIN,true); digitalWrite(LED_VERTE_PIN,false); fdisp.state=0; fdisp.t0=now; }
    } break;
    case 2:
      if(now - fdisp.t0 >= FAULT_PAUSE_MS){
        if(faultCode==FC_I2C_GENERAL){ startFaultSeries(pickNextI2CDetail()); }
        else { if(missADSd || missADSg || missPCA) startFaultSeries(FC_I2C_GENERAL); else startFaultSeries((uint8_t)faultCode); }
      } break;
  }
}

void faultsBootCheck(){
  auto exists=[&](uint8_t a){ Wire.beginTransmission(a); return Wire.endTransmission()==0; };
  adsOK[0]=exists(0x48);
  adsOK[1]=exists(0x49);
  pcaOK   =exists(0x40);

  missADSg=!adsOK[0]; missADSd=!adsOK[1]; missPCA=!pcaOK;

  Serial.print("ADS GAUCHE @0x48 : "); Serial.println(adsOK[0]?"OK":"ERREUR");
  Serial.print("ADS DROIT  @0x49 : "); Serial.println(adsOK[1]?"OK":"ERREUR");
  Serial.print("PCA9685    @0x40 : "); Serial.println(pcaOK?"OK":"ABSENT");

  if(!adsOK[0] || !adsOK[1] || !pcaOK) setFault(FC_I2C_GENERAL,"boot_i2c_check");
}

void i2cRuntimeWatchdog(){
  static uint32_t last=0; if(millis()-last<2500) return; last=millis();
  auto exists=[&](uint8_t a){ Wire.beginTransmission(a); return Wire.endTransmission()==0; };
  bool newADSg=exists(0x48), newADSd=exists(0x49), newPCA=exists(0x40);
  adsOK[0]=newADSg; adsOK[1]=newADSd; pcaOK=newPCA;

  missADSg=!adsOK[0]; missADSd=!adsOK[1]; missPCA=!pcaOK;

  if(!adsOK[0] || !adsOK[1] || !pcaOK){
    if(faultCode != FC_I2C_GENERAL) setFault(FC_I2C_GENERAL,"i2c_watchdog");
  } else {
    if(faultCode==FC_I2C_GENERAL || faultCode==FC_ADS_DROIT || faultCode==FC_ADS_GAUCHE || faultCode==FC_PCA){
      clearFault();
    }
  }
}
