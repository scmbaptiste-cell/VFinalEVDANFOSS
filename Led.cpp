#include "Led.h"

struct BlinkState { LedPattern pattern=LEDP_NONE; bool active=false; uint32_t until=0, interval=250, lastToggle=0; bool state=false; } blink;

void setLED(bool g,bool r){
  digitalWrite(LED_VERTE_PIN,g);
  digitalWrite(LED_ROUGE_PIN,r);
}

void startBlink(LedPattern p, uint32_t duration_ms, uint32_t interval_ms){
  blink.pattern = p; blink.active = true;
  blink.until = (duration_ms==0xFFFFFFFF)? 0xFFFFFFFF : (millis()+duration_ms);
  blink.interval = interval_ms; blink.lastToggle = 0; blink.state = false;
}

void stopBlink(){
  blink.active=false; blink.pattern=LEDP_NONE;
}

void serviceBlink(){
  if(!blink.active) return;
  uint32_t now=millis();
  if(blink.until!=0xFFFFFFFF && now>=blink.until){ stopBlink(); setLED(false,false); return; }

  switch(blink.pattern){
    case LEDP_SOLID_GREEN: setLED(true,false); return;
    case LEDP_SOLID_RED:   setLED(false,true); return;
    default: break;
  }
  if(now-blink.lastToggle>=blink.interval){
    blink.lastToggle=now; blink.state=!blink.state;
    if(blink.pattern==LEDP_BOTH) setLED(blink.state,blink.state);
    else if(blink.pattern==LEDP_GREEN) setLED(blink.state,false);
    else if(blink.pattern==LEDP_RED) setLED(false,blink.state);
    else if(blink.pattern==LEDP_ALT) setLED(blink.state, !blink.state);
  }
}

void solidGreenFor(uint32_t ms){ startBlink(LEDP_SOLID_GREEN, ms, 0); }
void solidRedFor(uint32_t ms){ startBlink(LEDP_SOLID_RED, ms, 0); }
void pulseGreen2(){ startBlink(LEDP_GREEN, 800, 200); }

void ledSelfTest(){
  // Rouge 300ms puis Vert 300ms (spécification)
  digitalWrite(LED_VERTE_PIN,false); digitalWrite(LED_ROUGE_PIN,false);
  delay(100);
  digitalWrite(LED_ROUGE_PIN,true);  delay(300); digitalWrite(LED_ROUGE_PIN,false);
  delay(300);
  digitalWrite(LED_VERTE_PIN,true);  delay(300); digitalWrite(LED_VERTE_PIN,false);
  delay(400);
}

// NOUVEAU
bool isBlinking(){ return blink.active; }
