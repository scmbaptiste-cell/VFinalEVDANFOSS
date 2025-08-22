#pragma once
#include "Config.h"
#include <Adafruit_ADS1X15.h>
#include <Adafruit_PWMServoDriver.h>

struct CalAxis { int16_t minV, midV, maxV; };

struct AxisConfig { uint8_t adsIndex, channel; bool inverted; };
struct ADSRaw { int16_t X,Y,Z,LX,LY,LZ,R1,R2; };
struct Axes8  { int X,Y,Z,LX,LY,LZ,R1,R2; };

extern Adafruit_ADS1115 ads_gnd, ads_vdd;
extern Adafruit_PWMServoDriver pca;
extern CalAxis cal[8];
extern bool adsOK[2];
extern bool pcaOK;

extern int joyNeutralMin, joyNeutralMax;

void ioInitI2CAndPCA();
ADSRaw readADSRaw();
Axes8  mapADSAll(const ADSRaw& r);
int    mapADSWithCal(int16_t raw,const CalAxis& c);
void   applyAxisToPair(uint8_t pwmCh,int val);
void   neutralizeAllOutputs();
bool   waitNeutralAtBootWithBlink(uint32_t to_ms);
void   processADS();
void   onModeChanged(bool wiredNow);

// NEW: disponibilité d’un axe (true = ADS porteur présent)
bool isAxisAvailable(uint8_t axisIndex);
