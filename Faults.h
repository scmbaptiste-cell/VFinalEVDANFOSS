#pragma once
#include "Config.h"

extern bool missADSg, missADSd, missPCA;

void setFault(FaultCode c, const char* origin);
void clearFault();
void serviceFaultDisplay();
void faultsBootCheck();
void i2cRuntimeWatchdog();
bool isAnyControllerConnected();
String fmtUptime();
