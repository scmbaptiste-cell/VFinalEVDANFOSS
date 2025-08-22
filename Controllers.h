#pragma once
#include "Config.h"
#include <Bluepad32.h>
#include "Bridage.h"

extern ControllerPtr myControllers[BP32_MAX_GAMEPADS];
extern bool safetyReady;
extern bool softRadioOverride;

void controllersSetup();
void controllersUpdate();
void processControllers();
void processR1L1Override();
void serviceControllerLEDs();
void refreshControllersColor();

bool controllerAxesNeutral(ControllerPtr ctl);
bool isAnyControllerConnected();
void updateStatusLEDs();

extern const uint32_t DEFAULT_PULSE_MS;

bool getPadValues(int out[AX_COUNT], ControllerPtr ctl=nullptr);

