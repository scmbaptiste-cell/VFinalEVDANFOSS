#include "Controllers.h"
#include "Led.h"
#include "Faults.h"
#include "IOMap.h"
#include "Calibration.h"

ControllerPtr myControllers[BP32_MAX_GAMEPADS];
uint32_t psHoldStartMs[BP32_MAX_GAMEPADS] = {0}, rlHoldStartMs[BP32_MAX_GAMEPADS] = {0};
bool psLastPressed[BP32_MAX_GAMEPADS] = {false}, psLongActionDone[BP32_MAX_GAMEPADS] = {false};
bool psSeenReleasedSinceConnect[BP32_MAX_GAMEPADS] = {false}, rlBothLastPressed[BP32_MAX_GAMEPADS] = {false};
bool optLastPressed[BP32_MAX_GAMEPADS] = {false};

bool safetyReady=false;
bool softRadioOverride=false;

const uint32_t HOLD_BLINK_MS = 300;
const uint32_t DEFAULT_PULSE_MS = 500;

enum CtrlLedMode : uint8_t { LEDMODE_NORMAL=0, LEDMODE_HOLD_R1R1, LEDMODE_PULSES };
struct CtrlLedState { CtrlLedMode mode=LEDMODE_NORMAL; uint32_t lastToggleMs=0; bool on=false; uint8_t pulsesDone=0,pulseTarget=0; uint8_t pr=0,pg=0,pb=0; uint32_t pulseMs=500; } ctrlLed[BP32_MAX_GAMEPADS];

static void setControllerColor(ControllerPtr ctl,uint8_t r,uint8_t g,uint8_t b){ if(ctl&&ctl->isConnected()) ctl->setColorLED(r,g,b); }

bool isAnyControllerConnected(){ for(auto c:myControllers) if(c&&c->isConnected()) return true; return false; }
bool isWiredMode(){ return digitalRead(MODE_SEL_PIN)==LOW; }

void onConnectedController(ControllerPtr ctl){
  for(int i=0;i<BP32_MAX_GAMEPADS;i++) if(!myControllers[i]){ myControllers[i]=ctl; setControllerColor(ctl,255,0,0); return; }
  Serial.println("[PAD] Manette connectée.");
}
void onDisconnectedController(ControllerPtr ctl){
  for(int i=0;i<BP32_MAX_GAMEPADS;i++) if(myControllers[i]==ctl){ myControllers[i]=nullptr; break; }
  if(!isAnyControllerConnected()){ safetyReady=false; neutralizeAllOutputs(); }
  Serial.println("[PAD] Manette déconnectée.");
}

void controllersSetup(){ BP32.setup(&onConnectedController,&onDisconnectedController); BP32.forgetBluetoothKeys(); }
void controllersUpdate(){ BP32.update(); }

void refreshControllersColor(){
  for(int i=0;i<BP32_MAX_GAMEPADS;i++){
    auto ctl=myControllers[i];
    if(!ctl||!ctl->isConnected()||ctrlLed[i].mode!=LEDMODE_NORMAL) continue;
    if(faultCode!=FC_NONE) { setControllerColor(ctl,255,0,0); continue; }
    if(safetyReady) setControllerColor(ctl,0,255,0); else setControllerColor(ctl,0,0,255);
  }
}

void triggerControllerPulses(int idx, uint8_t count, uint32_t pulseMs, uint8_t r, uint8_t g, uint8_t b){
  if(idx<0 || idx>=BP32_MAX_GAMEPADS) return;
  ctrlLed[idx].mode = LEDMODE_PULSES;
  ctrlLed[idx].lastToggleMs = 0; ctrlLed[idx].on = false; ctrlLed[idx].pulsesDone = 0;
  ctrlLed[idx].pulseTarget = count; ctrlLed[idx].pulseMs = pulseMs; ctrlLed[idx].pr=r; ctrlLed[idx].pg=g; ctrlLed[idx].pb=b;
}

void serviceControllerLEDs(){
  uint32_t now=millis();
  for(int i=0;i<BP32_MAX_GAMEPADS;i++){
    auto ctl=myControllers[i]; if(!ctl||!ctl->isConnected()) continue;
    switch(ctrlLed[i].mode){
      case LEDMODE_NORMAL: break;
      case LEDMODE_HOLD_R1R1:
        if(now-ctrlLed[i].lastToggleMs>=HOLD_BLINK_MS){
          ctrlLed[i].lastToggleMs=now; ctrlLed[i].on=!ctrlLed[i].on;
          setControllerColor(ctl, ctrlLed[i].on?0:255, ctrlLed[i].on?255:0, 0);
        } break;
      case LEDMODE_PULSES:
        if(now-ctrlLed[i].lastToggleMs>=ctrlLed[i].pulseMs){
          ctrlLed[i].lastToggleMs=now; ctrlLed[i].on=!ctrlLed[i].on;
          if(ctrlLed[i].on) setControllerColor(ctl, ctrlLed[i].pr, ctrlLed[i].pg, ctrlLed[i].pb);
          else { setControllerColor(ctl, 0,0,0); if(++ctrlLed[i].pulsesDone>=ctrlLed[i].pulseTarget){ ctrlLed[i].mode=LEDMODE_NORMAL; refreshControllersColor(); } }
        } break;
    }
  }
}

static bool lxInverted=false;

bool controllerAxesNeutral(ControllerPtr ctl){
  if(!ctl) return false;
  int vals[AX_COUNT];
  if(!getPadValues(vals, ctl)) return false;
  auto inN=[&](int v,int i){ return v>=padNeutralMin[i] && v<=padNeutralMax[i]; };
  for(int i=0;i<AX_COUNT;i++) if(!inN(vals[i],i)) return false;
  return true;
}

static bool canSoftOverrideToPad(){
  if(!pcaOK) return false;
  if(faultCode==FC_NONE) return true;
  if(faultCode==FC_ADS_DROIT || faultCode==FC_ADS_GAUCHE) return true;
  if(faultCode==FC_I2C_GENERAL && !missPCA && (missADSg || missADSd)) return true;
  return false;
}

void processR1L1Override(){
  if(!isWiredMode() || softRadioOverride || !isAnyControllerConnected() || safetyReady) return;

  bool okToOverride = canSoftOverrideToPad();

  for(int i=0;i<BP32_MAX_GAMEPADS;i++){
    auto ctl=myControllers[i]; if(!ctl||!ctl->isConnected()) continue;
    uint16_t btn=ctl->buttons(); bool r1=(btn & BTN_R1), l1=(btn & BTN_L1), both=r1&&l1;

    if (both){
      if (!rlBothLastPressed[i]){
        rlBothLastPressed[i]=true; rlHoldStartMs[i]=millis();
        ctrlLed[i].mode=LEDMODE_HOLD_R1R1; ctrlLed[i].lastToggleMs=0; ctrlLed[i].on=false;
        Serial.println("[PAD] L1+R1 maintenus : tentative de passage en mode manette…");
        startBlink(LEDP_GREEN, 0xFFFFFFFF, 100);
      } else if (millis()-rlHoldStartMs[i]>=10000){
        if (okToOverride){
          softRadioOverride=true; safetyReady=false; neutralizeAllOutputs();
          onModeChanged(false); // basculer totalement en mode manette
          modeChangeBlockUntil=millis()+MODE_CHANGE_BLOCK_MS;
          triggerControllerPulses(i, 3, DEFAULT_PULSE_MS, 0,255,0);
          Serial.println("[PAD] Passage logiciel en mode manette (GPIO33=0). PS 5s pour armer.");
          stopBlink(); startBlink(LEDP_GREEN, 3000, 500);
        } else {
          ctrlLed[i].mode=LEDMODE_NORMAL;
        }
        rlBothLastPressed[i]=false; rlHoldStartMs[i]=0; return;
      }
    } else {
      if (rlBothLastPressed[i]){
        rlBothLastPressed[i]=false; rlHoldStartMs[i]=0;
        ctrlLed[i].mode=LEDMODE_NORMAL;
        refreshControllersColor();
        stopBlink();
      }
    }
  }
}

void processControllers() {
  if(isEffectiveWiredMode()) return; // ignore pad axes in joystick mode

  ControllerPtr ctl=nullptr; int idx=-1;
  for (int i = 0; i < BP32_MAX_GAMEPADS; ++i) {
    if (myControllers[i] && myControllers[i]->isConnected()) { ctl = myControllers[i]; idx = i; break; }
  }
  if (!ctl) { neutralizeAllOutputs(); return; }

  const uint8_t MISC_BUTTON_SYSTEM = 0x01;
  bool psPressed = (ctl->miscButtons() & MISC_BUTTON_SYSTEM);
  uint32_t now = millis();

  if (psPressed && !psLastPressed[idx]) {
    psHoldStartMs[idx] = now; psLastPressed[idx] = true; psLongActionDone[idx] = false; psSeenReleasedSinceConnect[idx] = true;
  } else if (psPressed && psLastPressed[idx]) {
    if (!psLongActionDone[idx] && (now - psHoldStartMs[idx] >= 5000)) {
      if (!safetyReady) { if (controllerAxesNeutral(ctl)) { safetyReady = true; digitalWrite(GPIO_MANETTE_CONNECTEE, true); Serial.println("[PAD] ARMÉ."); } else Serial.println("[PAD] REFUS ARMEMENT : axes non neutres."); }
      else { safetyReady = false; digitalWrite(GPIO_MANETTE_CONNECTEE, false); neutralizeAllOutputs(); Serial.println("[PAD] DÉSARMÉ."); }
      psLongActionDone[idx] = true; refreshControllersColor();
    }
  } else if (!psPressed && psLastPressed[idx]) {
    uint32_t held = now - psHoldStartMs[idx];
    if (held >= 50 && held < 5000) {
      if (safetyReady) { safetyReady=false; digitalWrite(GPIO_MANETTE_CONNECTEE,false); neutralizeAllOutputs(); refreshControllersColor(); Serial.println("[PAD] DÉSARMÉ (PS court)."); }
    }
    psLastPressed[idx] = false; psHoldStartMs[idx] = 0;
  }

  const uint8_t MISC_BUTTON_START = 0x04;
  bool optPressed = (ctl->miscButtons() & MISC_BUTTON_START);
  if (optPressed && !optLastPressed[idx]) {
    if (!safetyReady) { lxInverted = !lxInverted; Serial.printf("[PAD] Inversion LX = %s\n", lxInverted?"ACTIVE":"NORMALE"); triggerControllerPulses(idx, lxInverted?3:2, DEFAULT_PULSE_MS, 0,255,0); }
    else Serial.println("[PAD] Inversion LX ignorée (système armé).");
  }
  optLastPressed[idx] = optPressed;

  int rawLX = ctl->axisX(); if (lxInverted) rawLX = -rawLX;
  int X  = constrain(map(ctl->axisRX(), -512, 512, padMapMin[AX_X],  padMapMax[AX_X]), padMapMin[AX_X], padMapMax[AX_X]);
  int Y  = constrain(map(ctl->axisRY(), -512, 512, padMapMin[AX_Y],  padMapMax[AX_Y]), padMapMin[AX_Y], padMapMax[AX_Y]);
  int LX = constrain(map(rawLX,         -512, 512, padMapMin[AX_LX], padMapMax[AX_LX]), padMapMin[AX_LX], padMapMax[AX_LX]);
  int LY = constrain(map(ctl->axisY(),  -512, 512, padMapMin[AX_LY], padMapMax[AX_LY]), padMapMin[AX_LY], padMapMax[AX_LY]);
  int Z  = padNeutral[AX_Z];
  if (ctl->throttle() > 0)
    Z = constrain(map(ctl->throttle(), 0, 1023, padNeutral[AX_Z], padMapMin[AX_Z]), padMapMin[AX_Z], padMapMax[AX_Z]);
  else if (ctl->brake() > 0)
    Z = constrain(map(ctl->brake(),    0, 1023, padNeutral[AX_Z], padMapMax[AX_Z]), padMapMin[AX_Z], padMapMax[AX_Z]);
  int LZ = padNeutral[AX_LZ]; uint8_t d = ctl->dpad();
  if (d == 0x01) LZ = padMapMax[AX_LZ];
  else if (d == 0x02) LZ = padMapMin[AX_LZ];
  int R1 = padNeutral[AX_R1], R2 = padNeutral[AX_R2];
  uint16_t b = ctl->buttons();
  if (b & 0x0001) R1 = padMapMin[AX_R1]; else if (b & 0x0002) R1 = padMapMax[AX_R1];
  if (b & 0x0004) R2 = padMapMin[AX_R2]; else if (b & 0x0008) R2 = padMapMax[AX_R2];

  if (pcaOK && safetyReady) {
    applyAxisToPair(0, X); applyAxisToPair(1, Y); applyAxisToPair(2, Z); applyAxisToPair(3, LX);
    applyAxisToPair(4, LY); applyAxisToPair(5, LZ); applyAxisToPair(6, R1); applyAxisToPair(7, R2);
  } else {
    neutralizeAllOutputs();
  }
}


bool getPadValues(int out[AX_COUNT], ControllerPtr ctl){
  if(!ctl){
    for(int i=0;i<BP32_MAX_GAMEPADS;i++){
      if(myControllers[i] && myControllers[i]->isConnected()){ ctl=myControllers[i]; break; }
    }

  }
  if(!ctl){ for(int i=0;i<AX_COUNT;i++) out[i]=0; return false; }
  int rawLX = ctl->axisX(); if(lxInverted) rawLX = -rawLX;
  out[AX_X]  = constrain(map(ctl->axisRX(), -512, 512, padMapMin[AX_X],  padMapMax[AX_X]), padMapMin[AX_X], padMapMax[AX_X]);
  out[AX_Y]  = constrain(map(ctl->axisRY(), -512, 512, padMapMin[AX_Y],  padMapMax[AX_Y]), padMapMin[AX_Y], padMapMax[AX_Y]);
  out[AX_LX] = constrain(map(rawLX,         -512, 512, padMapMin[AX_LX], padMapMax[AX_LX]), padMapMin[AX_LX], padMapMax[AX_LX]);
  out[AX_LY] = constrain(map(ctl->axisY(),  -512, 512, padMapMin[AX_LY], padMapMax[AX_LY]), padMapMin[AX_LY], padMapMax[AX_LY]);
  out[AX_Z]  = padNeutral[AX_Z];
  if(ctl->throttle()>0)      out[AX_Z]=constrain(map(ctl->throttle(),0,1023,padNeutral[AX_Z],padMapMin[AX_Z]), padMapMin[AX_Z], padMapMax[AX_Z]);
  else if(ctl->brake()>0)    out[AX_Z]=constrain(map(ctl->brake(),   0,1023,padNeutral[AX_Z],padMapMax[AX_Z]), padMapMin[AX_Z], padMapMax[AX_Z]);
  out[AX_LZ]=padNeutral[AX_LZ]; uint8_t d=ctl->dpad(); if(d==0x01) out[AX_LZ]=padMapMax[AX_LZ]; else if(d==0x02) out[AX_LZ]=padMapMin[AX_LZ];
  out[AX_R1]=padNeutral[AX_R1]; out[AX_R2]=padNeutral[AX_R2]; uint16_t b=ctl->buttons();
  if(b&0x0001) out[AX_R1]=padMapMin[AX_R1]; else if(b&0x0002) out[AX_R1]=padMapMax[AX_R1];
  if(b&0x0004) out[AX_R2]=padMapMin[AX_R2]; else if(b&0x0008) out[AX_R2]=padMapMax[AX_R2];
  return true;
}

void updateStatusLEDs(){
  // ⬇️ Respecte la calibration/les clignotements en cours
  if (calibMode || isBlinking()) { serviceBlink(); return; }

  if( faultCode != FC_NONE ){ serviceFaultDisplay(); return; }

  bool wired = isEffectiveWiredMode();
  bool connected = isAnyControllerConnected();

  if (wired){
    digitalWrite(GPIO_MANETTE_CONNECTEE, wiredNeutralOK);
    digitalWrite(LED_ROUGE_PIN, LOW);
    digitalWrite(LED_VERTE_PIN, HIGH);
  } else {
    digitalWrite(GPIO_MANETTE_CONNECTEE, safetyReady);
    if (connected && safetyReady){
      digitalWrite(LED_VERTE_PIN, HIGH);
      digitalWrite(LED_ROUGE_PIN, LOW);
    } else if (connected){
      static uint32_t t0=0; static bool on=false;
      if(millis()-t0>=600){ t0=millis(); on=!on; }
      digitalWrite(LED_VERTE_PIN,on); digitalWrite(LED_ROUGE_PIN,LOW);
    } else {
      digitalWrite(LED_VERTE_PIN,LOW); digitalWrite(LED_ROUGE_PIN,HIGH);
    }
    refreshControllersColor();
  }
}
