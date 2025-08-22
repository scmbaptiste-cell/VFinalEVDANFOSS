#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

enum AxisIdx { AX_X=0, AX_Y, AX_Z, AX_LX, AX_LY, AX_LZ, AX_R1, AX_R2, AX_COUNT=8 };

extern int padMapMin[AX_COUNT];
extern int padMapMax[AX_COUNT];

extern int padNeutral[AX_COUNT];
extern int padNeutralMin[AX_COUNT];
extern int padNeutralMax[AX_COUNT];

bool isBridageActive();
void bridageStartAP();
void bridageStopAP();
void bridageHandlePortal();
void bridageHandleButtonSequence(bool btnPressed, uint32_t shortPressMinMs=50, uint32_t longPressMs=5000);

void bridageRecalcNeutralForAxis(int i);
void bridageClampAndRecommend(int &minV, int &maxV, int changed);
void bridageLoadOrDefault();
void bridageSaveToEEPROM();
