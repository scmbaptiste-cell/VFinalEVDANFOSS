#include "IOMap.h"
#include "Led.h"
#include "Faults.h"
#include "Calibration.h"
#include "Bridage.h"
#include <Wire.h>
#include <EEPROM.h>

/* Mapping from PWM input channels to TOR channels (per user wiring)
   PWM indexes 0 to 7 map to TOR indexes 9,8,10,11,12,13,14,15. */
static const uint8_t PWM_TO_TOR[8] = {9,8,10,11,12,13,14,15};


Adafruit_ADS1115 ads_gnd; // 0x48
Adafruit_ADS1115 ads_vdd; // 0x49
Adafruit_PWMServoDriver pca = Adafruit_PWMServoDriver(0x40);

bool pcaOK=true;
bool adsOK[2]={false,false};
int neutralOffset=512;
int joyNeutralMin=0, joyNeutralMax=0;
static int mapMin=255, mapMax=768;

static const int NEUTRAL_HALF_WINDOW = 30;
static int lastOffset = 512;
#define EE_NEUTRAL_OFFSET_ADDR 200

void updateNeutralWindow(){
  joyNeutralMin = neutralOffset - NEUTRAL_HALF_WINDOW;
  joyNeutralMax = neutralOffset + NEUTRAL_HALF_WINDOW;

  int delta = neutralOffset - lastOffset;
  if(delta != 0){
    for(int i=0;i<AX_COUNT;i++){
      padMapMin[i] += delta;
      padMapMax[i] += delta;
      padNeutral[i] += delta;
      padNeutralMin[i] += delta;
      padNeutralMax[i] += delta;
    }
    lastOffset = neutralOffset;
  }
}

void loadNeutralOffset(){
#if defined(ARDUINO_ARCH_ESP32)
  EEPROM.begin(512);
#endif
  uint16_t v=512;
  EEPROM.get(EE_NEUTRAL_OFFSET_ADDR, v);
  if(v>1023) v=512;
  neutralOffset = (int)v;
  lastOffset = neutralOffset;
}

void saveNeutralOffset(){
#if defined(ARDUINO_ARCH_ESP32)
  EEPROM.begin(512);
#endif
  uint16_t v = (uint16_t)neutralOffset;
  EEPROM.put(EE_NEUTRAL_OFFSET_ADDR, v);
#if defined(ARDUINO_ARCH_ESP32)
  EEPROM.commit();
#endif
}

CalAxis cal[8];

static bool i2cExists(uint8_t addr){ Wire.beginTransmission(addr); return (Wire.endTransmission()==0); }

static const AxisConfig AXIS_MAP[8] = {
  {1,2,false}, {1,1,false}, {1,0,false}, {0,1,false},
  {0,2,true }, {0,0,true }, {1,3,true }, {0,3,true }
};

bool isAxisAvailable(uint8_t axisIndex){
  if(axisIndex>=8) return false;
  uint8_t adsIdx = AXIS_MAP[axisIndex].adsIndex;
  return adsOK[adsIdx];
}

static inline int16_t readAxisRawOne(const AxisConfig& cfg){
  int16_t v=16384;
  if(cfg.adsIndex==0){ if(adsOK[0]) v=ads_gnd.readADC_SingleEnded(cfg.channel); }
  else { if(adsOK[1]) v=ads_vdd.readADC_SingleEnded(cfg.channel); }
  return cfg.inverted? (int16_t)(32767 - v) : v;
}

ADSRaw readADSRaw(){
  ADSRaw r{}; int16_t* p=(int16_t*)&r;
  for(int i=0;i<8;i++) p[i]=readAxisRawOne(AXIS_MAP[i]);
  return r;
}

int mapADSWithCal(int16_t raw,const CalAxis& c){
  int16_t mn=c.minV, md=c.midV, mx=c.maxV; if(mx<=md) mx=md+1; if(mn>=md) mn=md-1;
  if(raw<=md){ if(raw<mn) raw=mn; return map(raw,mn,md,255,512); }
  else       { if(raw>mx) raw=mx; return map(raw,md,mx,512,768); }
}

Axes8 mapADSAll(const ADSRaw& r){
  Axes8 a{};
  a.X = mapADSWithCal(r.X,cal[0]); a.Y = mapADSWithCal(r.Y,cal[1]); a.Z = mapADSWithCal(r.Z,cal[2]);
  a.LX= mapADSWithCal(r.LX,cal[3]); a.LY= mapADSWithCal(r.LY,cal[4]); a.LZ= mapADSWithCal(r.LZ,cal[5]);
  a.R1= mapADSWithCal(r.R1,cal[6]); a.R2= mapADSWithCal(r.R2,cal[7]);

  int *v = (int*)&a;
  for(int i=0;i<8;i++){
    if(v[i] < mapMin) v[i] = mapMin;
    if(v[i] > mapMax) v[i] = mapMax;
  }
  return a;
}

static inline float mapf(float x,float in_min,float in_max,float out_min,float out_max){
  if(in_max==in_min) return out_min;
  float r=(x-in_min)/(in_max-in_min);
  if(r<0) r=0;
  if(r>1) r=1;
  return out_min + r*(out_max-out_min);
}

static inline uint16_t dutyToCount(float duty){
  if(duty < 0) duty = 0;
  if(duty > 1) duty = 1;
  return (uint16_t)(duty*4095.0f + 0.5f);
}

static inline void setPWMpercent(uint8_t ch,float duty){
  if(pcaOK) pca.setPWM(ch, 0, dutyToCount(duty));
}

static inline void setTOR(uint8_t ch,bool on){
  if(!pcaOK) return;
  if(on) pca.setPWM(ch, 4096, 0);  // full ON
  else   pca.setPWM(ch, 0, 4096);  // full OFF
}



void applyAxisToPair(uint8_t pwmCh, int val){
  // Duty targets: 25% (min), 50% (neutral), 75% (max)
  const float DUTY_MIN = 0.25f;
  const float DUTY_MID = 0.50f;
  const float DUTY_MAX = 0.75f;

  // Offset duty so neutralOffset shifts the entire PWM range.
  float offsetDuty = -((float)neutralOffset - 512.0f) / 512.0f * (DUTY_MID - DUTY_MIN);

  float duty = DUTY_MID + offsetDuty;
  bool active = false; // true when axis is outside neutral window

  if (val < joyNeutralMin){
    duty = mapf((float)val, (float)mapMin, (float)joyNeutralMin, DUTY_MIN, DUTY_MID) + offsetDuty;
    active = true;
  } else if (val > joyNeutralMax){
    duty = mapf((float)val, (float)joyNeutralMax, (float)mapMax, DUTY_MID, DUTY_MAX) + offsetDuty;
    active = true;
  }

  if(duty < 0) duty = 0; else if(duty > 1) duty = 1;

  if (pwmCh < 8){
    setPWMpercent(pwmCh, duty);
    uint8_t torCh = PWM_TO_TOR[pwmCh];
    setTOR(torCh, active);
  }
}
void neutralizeAllOutputs(){ for(uint8_t i=0;i<8;++i) setPWMpercent(i,0.5f); for(uint8_t i=8;i<16;++i) setTOR(i,false); }

static bool isAllAxesNeutral(){
  if(!adsOK[0] || !adsOK[1] || !pcaOK) return true;
  ADSRaw rr=readADSRaw(); Axes8 a=mapADSAll(rr);
  auto inN=[&](int v){ return v>=joyNeutralMin && v<=joyNeutralMax; };
  int v[8]={a.X,a.Y,a.Z,a.LX,a.LY,a.LZ,a.R1,a.R2};
  for(int i=0;i<8;i++) if(!inN(v[i])) return false;
  return true;
}

bool waitNeutralAtBootWithBlink(uint32_t to_ms){
  if(!adsOK[0] || !adsOK[1] || !pcaOK) return false;
  Serial.println("[BOOT FILAIRE] Attente du neutre");
  uint32_t start=millis(), t0=0; bool on=false;
  while(!isAllAxesNeutral()){
    if(millis()-t0>=300){ t0=millis(); on=!on; }
    digitalWrite(LED_VERTE_PIN,on); digitalWrite(LED_ROUGE_PIN,false);
    neutralizeAllOutputs();
    if(to_ms && (millis()-start>to_ms)){ setFault(FC_NEUTRAL_TO,"boot_neutral"); return false; }
    delay(20);
  }
  digitalWrite(LED_VERTE_PIN,true); digitalWrite(LED_ROUGE_PIN,false);
  Serial.println("[BOOT FILAIRE] Neutre OK."); return true;
}

// Important : calibration prioritaire
void processADS(){
  processCalibration();
  if(calibMode){
    // Sorties et GPIO27 à l’arrêt pendant calibration
    neutralizeAllOutputs();
    digitalWrite(GPIO_MANETTE_CONNECTEE, LOW);
    return;
  }

  if(!isEffectiveWiredMode()){
    return; // laisser la manette piloter les sorties
  }

  if(!adsOK[0] || !adsOK[1] || !pcaOK){
    neutralizeAllOutputs();
    return;
  }

  ADSRaw rr=readADSRaw(); Axes8 a=mapADSAll(rr);
  applyAxisToPair(0,a.X); applyAxisToPair(1,a.Y); applyAxisToPair(2,a.Z); applyAxisToPair(3,a.LX);
  applyAxisToPair(4,a.LY); applyAxisToPair(5,a.LZ); applyAxisToPair(6,a.R1); applyAxisToPair(7,a.R2);
}

void ioInitI2CAndPCA(){
#if defined(ARDUINO_ARCH_ESP32)
  Wire.begin(I2C_SDA,I2C_SCL);
#else
  Wire.begin();
#endif
  Wire.setClock(400000); delay(20);

  adsOK[0]=i2cExists(0x48);
  adsOK[1]=i2cExists(0x49);
  pcaOK   =i2cExists(0x40);

  if(adsOK[0]) { if(!ads_gnd.begin(0x48)) adsOK[0]=false; else ads_gnd.setGain(GAIN_TWOTHIRDS); }
  if(adsOK[1]) { if(!ads_vdd.begin(0x49)) adsOK[1]=false; else ads_vdd.setGain(GAIN_TWOTHIRDS); }
  if(pcaOK)    { pca.begin(); pca.setPWMFreq(1000); neutralizeAllOutputs(); }
}

void onModeChanged(bool wiredNow){
  if (wiredNow && softRadioOverride){
    softRadioOverride = false;
    Serial.println("[MODE] Sélecteur FILAIRE → override radio annulé.");
  }

  neutralizeAllOutputs(); modeChangeBlockUntil=millis()+MODE_CHANGE_BLOCK_MS;
  safetyReady=false;
  stopBlink(); setLED(false,false);
  if(wiredNow){
    wiredNeutralOK=false; digitalWrite(GPIO_MANETTE_CONNECTEE,false);
    if(faultCode==FC_NONE){ bool ok=waitNeutralAtBootWithBlink(10000); if(ok){ wiredNeutralOK=true; digitalWrite(GPIO_MANETTE_CONNECTEE,true); } }
  } else {
    digitalWrite(GPIO_MANETTE_CONNECTEE, safetyReady);
  }
}