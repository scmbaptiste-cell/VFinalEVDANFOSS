#pragma once
#include "Config.h"

enum LedPattern { LEDP_NONE=0, LEDP_BOTH, LEDP_GREEN, LEDP_RED, LEDP_ALT, LEDP_SOLID_GREEN, LEDP_SOLID_RED };

void startBlink(LedPattern p, uint32_t duration_ms, uint32_t interval_ms=250);
void stopBlink();
void serviceBlink();
void setLED(bool g,bool r);
void solidGreenFor(uint32_t ms);
void solidRedFor(uint32_t ms);
void pulseGreen2();
void ledSelfTest();

// ⬇️ NOUVEAU : savoir si un clignotement est en cours pour laisser la main
bool isBlinking();
