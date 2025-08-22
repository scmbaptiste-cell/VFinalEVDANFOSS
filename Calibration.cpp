#include "Calibration.h"
#include "Led.h"
#include "IOMap.h"
#include "Faults.h"
#include "Portal.h"   // portail unique (optionnel pour la calib)
#include "Bridage.h"

// ======================== États & constantes ========================
bool haveMin[8]={false,false,false,false,false,false,false,false};
bool haveMax[8]={false,false,false,false,false,false,false,false};

bool calibMode=false, wiredNeutralOK=false;

// Calibration valide chargée depuis l’EEPROM ?
bool calDataValid = false;

// Seuil de détection d’axe en MAP pré‑calibration (défini ici, déclaré extern dans .h)
const int CAL_MOVE_THR_MAP = 20;

// Définis ici (déclarés extern dans .h)
volatile CalPhase calPhase = CAL_PHASE_IDLE;
uint32_t calPhaseEndMs = 0;

// Bouton & timings
static uint32_t calBtnStart=0;
static bool calBtnLast=false;
static const uint32_t CAL_HOLD_MS=5000, CAL_SHORT_MIN_MS=50;

// Timings par phase
static const uint16_t NEUTRAL_AVG_MS      = 1000; // 1s de moyenne
static const uint16_t NEUTRAL_VALIDATE_MS = 800;  // 0.8s de stabilité
static const uint16_t READY_HOLD_MS       = 2000; // LED verte fixe avant relâche

// Fenêtre neutre finale (provenant d'IOMap.cpp)
extern int joyNeutralMin, joyNeutralMax;

// ======================== EEPROM ========================
#define EE_MAGIC 0xC0DE
#define EE_VER   0x0002
#define EE_MAGIC_ADDR 0
#define EE_VER_ADDR   2
#define EE_DATA_ADDR  4

static void setDefaultCal(){
  for(int i=0;i<8;i++){
    cal[i].minV=0; cal[i].midV=16384; cal[i].maxV=32767;
    haveMin[i]=haveMax[i]=false;
  }
  calDataValid = false;
}

static bool loadCalFromEEPROM(){
#if defined(ARDUINO_ARCH_ESP32)
  EEPROM.begin(512);
#endif
  uint16_t magic=0,ver=0;
  EEPROM.get(EE_MAGIC_ADDR,magic);
  EEPROM.get(EE_VER_ADDR,ver);
  if(magic!=EE_MAGIC){
    Serial.println("[CAL] EEPROM invalide -> valeurs par défaut.");
    return false;
  }
  int addr=EE_DATA_ADDR;
  for(int i=0;i<8;i++){
    EEPROM.get(addr,cal[i].minV); addr+=2;
    EEPROM.get(addr,cal[i].midV); addr+=2;
    EEPROM.get(addr,cal[i].maxV); addr+=2;
  }
  Serial.printf("[CAL] Chargement OK (ver 0x%04X).\n", ver);
  return true;
}

void calLoadOrDefault(){
  calDataValid = loadCalFromEEPROM();
  if(!calDataValid) setDefaultCal();
}

void saveCalToEEPROM(){
#if defined(ARDUINO_ARCH_ESP32)
  EEPROM.begin(512);
#endif
  EEPROM.put(EE_MAGIC_ADDR,(uint16_t)EE_MAGIC);
  EEPROM.put(EE_VER_ADDR,(uint16_t)EE_VER);
  int addr=EE_DATA_ADDR;
  for(int i=0;i<8;i++){
    EEPROM.put(addr,cal[i].minV); addr+=2;
    EEPROM.put(addr,cal[i].midV); addr+=2;
    EEPROM.put(addr,cal[i].maxV); addr+=2;
  }
#if defined(ARDUINO_ARCH_ESP32)
  EEPROM.commit();
#endif
  calDataValid = true;
  Serial.println("[CAL] Sauvegarde OK.");
}

// ======================== Helpers mapping & bouton ========================

// map "pré-calibration": RAW (0..32767) -> 255..768, mid=16384
static inline int mapRawPreCal(int16_t raw){
  if (raw < 0) raw = 0;
  if (raw > 32767) raw = 32767;
  const int16_t mn = 0, md = 16384, mx = 32767;
  if (raw <= md) return map(raw, mn, md, 255, 512);
  else           return map(raw, md, mx, 512, 768);
}

// Détection axe le plus écarté du neutre (en MAP pré-calibration)
static int detectMovedAxisMAP(const ADSRaw& r, int16_t thr_map){
  int16_t raw[8]={r.X,r.Y,r.Z,r.LX,r.LY,r.LZ,r.R1,r.R2};
  int idx=-1; long best=0;
  for(int i=0;i<8;i++){
    int mid_map = mapRawPreCal(cal[i].midV);
    int v_map   = mapRawPreCal(raw[i]);
    long d = labs((long)v_map - (long)mid_map);
    if(d>best){ best=d; idx=i; }
  }
  return (best<thr_map)?-1:idx;
}

// Bouton calibration (dans ce projet : HIGH = appui)
bool readCalButton(){ return digitalRead(CAL_BTN_PIN)==HIGH; }

// ======================== Portail web (optionnel) ========================
static String navBar(){
  return String(
    "<div class='bar'>"
      "<a class='btn' href='/defaut'>D\u00E9faut ESP32</a>"
      "<a class='btn' href='/calib'>Calibration</a>"
      "<a class='btn' href='/bridage'>Bridage axes</a>"
    "</div>"
    "<style>.bar{display:flex;gap:8px;margin:10px 0 16px}"
    ".btn{background:#334155;color:#e5e7eb;border:1px solid #1f2937;padding:8px 12px;border-radius:8px;text-decoration:none}"
    ".btn:hover{background:#475569}</style>"
  );
}

void calibWifiStart(){
  portalStart("ESP32-CONTROLE");
  auto& server = portalServer();

  server.on("/calib", HTTP_GET, [&](){
    String html =
      "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Calibration 8 axes</title><style>"
      "body{font-family:system-ui,Arial;margin:16px;background:#0f172a;color:#e5e7eb}"
      "table{border-collapse:collapse;width:100%;max-width:820px}"
      "th,td{border-bottom:1px solid #1f2937;padding:10px 8px;text-align:center}"
      "th{font-size:12px;color:#9ca3af;text-transform:uppercase;letter-spacing:.08em}"
      "tbody tr:hover{background:rgba(255,255,255,.04)}"
      ".ok{color:#10b981;font-weight:700}.bad{color:#ef4444;font-weight:700}.muted{color:#9ca3af}"
      "</style></head><body>"
      "<h2>Calibration (mode filaire)</h2>" + navBar() +
      "<p class='muted'>Suivi d\u00E9taill\u00E9 dans le Moniteur s\u00E9rie (MAP). Min/Max s’affichent quand enregistr\u00E9s.</p>"
      "<table><thead><tr><th>Axe</th><th>Min MAP</th><th>Max MAP</th><th>Enregistr\u00E9</th></tr></thead>"
      "<tbody id='rows'></tbody></table>"
      "<script>"
      "const AX=['X','Y','Z','LX','LY','LZ','R1','R2'];"
      "const OFFSET=" + String(neutralOffset) + ";"
      "(function(){let sel=document.createElement('select');sel.id='off';for(let v=0;v<=1023;v++){let o=document.createElement('option');o.value=v;o.text=v;if(v===OFFSET)o.selected=true;sel.appendChild(o);}let btn=document.createElement('button');btn.id='saveOff';btn.textContent='Sauvegarde EEPROM';let div=document.createElement('div');div.style.margin='10px 0';div.appendChild(document.createTextNode('D\u00E9calage neutre : '));div.appendChild(sel);div.appendChild(document.createTextNode(' '));div.appendChild(btn);let tbl=document.querySelector('table');document.body.insertBefore(div,tbl);sel.addEventListener('change',()=>fetch('/offset?val='+sel.value,{cache:'no-store'}));btn.addEventListener('click',()=>fetch('/offset?val='+sel.value+'&save=1',{cache:'no-store'}).then(()=>alert('Offset sauvegard\u00E9')));})();"
      "function r(j){let t='';for(let i=0;i<8;i++){const s=!!j.saved[i];"
      "t+=`<tr><td>${AX[i]}</td><td>${Number(j.min_map[i])}</td><td>${Number(j.max_map[i])}</td><td class='${s?'ok':'bad'}'>${s?'\\u2713':'\\u2717'}</td></tr>`;}"
      "document.getElementById('rows').innerHTML=t;}"
      "async function p(){try{let x=await fetch('/axes.json',{cache:'no-store'});r(await x.json())}catch(e){}setTimeout(p,800);}p();"
      "</script></body></html>";
    server.send(200,"text/html",html);
  });

  server.on("/axes.json", HTTP_GET, [&](){
    String json="{\"min_map\":[";
    for(int i=0;i<8;i++){ json += String(mapRawPreCal(cal[i].minV)); if(i<7) json+=','; }
    json += "],\"max_map\":[";
    for(int i=0;i<8;i++){ json += String(mapRawPreCal(cal[i].maxV)); if(i<7) json+=','; }
    json += "],\"saved\":[";
    for(int i=0;i<8;i++){ json += (haveMin[i]&&haveMax[i])? "true":"false"; if(i<7) json+=','; }
    json += "]}";
    server.send(200,"application/json",json);
  });

  server.on("/offset", HTTP_GET, [&](){
    if(!server.hasArg("val")){ server.send(400,"text/plain","missing"); return; }
    int v = constrain(server.arg("val").toInt(), 0, 1023);
    neutralOffset = v;
    updateNeutralWindow();
    if(server.hasArg("save")) { saveNeutralOffset(); bridageSaveToEEPROM(); }
    server.send(200,"text/plain","OK");
  });
}

void calibWifiStop(){ portalStop(); }

// ======================== Démarrage / Fin ========================
void startCalibration(){
  clearFault();
  calibMode=true;

  setDefaultCal();
  calPhase = CAL_PHASE_NEUTRAL_INIT;
  calPhaseEndMs = millis() + NEUTRAL_AVG_MS;
  stopBlink();
  startBlink(LEDP_GREEN, 0xFFFFFFFF, 700); // clignotement lent phase neutre
  neutralizeAllOutputs();

  // Sécurité : forcer GPIO27 LOW pendant toute la calibration
  pinMode(GPIO_MANETTE_CONNECTEE, OUTPUT);
  digitalWrite(GPIO_MANETTE_CONNECTEE, LOW);

  calibWifiStart();
  Serial.println("=== CALIBRATION DEMARREE ===");
}

void finishCalibration(){
  // Sanity check sur min/mid/max
  for(int i=0;i<8;i++){
    if(cal[i].minV>=cal[i].midV) cal[i].minV=cal[i].midV-1;
    if(cal[i].maxV<=cal[i].midV) cal[i].maxV=cal[i].midV+1;
  }
  saveCalToEEPROM();
  calibWifiStop();
  calibMode=false;
  calPhase = CAL_PHASE_IDLE;
  stopBlink();
  setLED(true,false);
  Serial.println("=== CALIBRATION TERMINEE ===");
}

// ======================== Boucle Calibration (FSM) ========================
void processCalibration(){
  if(!isWiredMode() && !calibMode) return; // only joystick calibration

  // Laisser vivre le portail (si ouvert) + sécurité
  portalHandle();
  if (calibMode) digitalWrite(GPIO_MANETTE_CONNECTEE, LOW);

  // --- Pré-état : attente appui long + relâchement ---
  bool pressed = readCalButton();
  static uint8_t preState = 0; // 0=idle,1=hold,2=ready
  static uint32_t preT0=0, preReadyEnd=0;
  if(!calibMode){
    switch(preState){
      case 0: // idle
        if(pressed){ preState=1; preT0=millis(); startBlink(LEDP_ALT,0xFFFFFFFF,180); }
        break;
      case 1: // holding
        if(!pressed){ preState=0; stopBlink(); setLED(false,false); }
        else if(millis()-preT0>=CAL_HOLD_MS){
          preState=2; solidGreenFor(READY_HOLD_MS); preReadyEnd=millis()+READY_HOLD_MS;
        } else {
          serviceBlink();
        }
        break;
      case 2: // ready, waiting release after 2s
        serviceBlink();
        if(!pressed){
          if(millis()>=preReadyEnd){ stopBlink(); setLED(false,false); preState=0; startCalibration(); }
          else { stopBlink(); setLED(false,false); preState=0; }
        }
        break;
    }
    return;
  }

  // Affichage MAP uniquement (10 Hz) -- seulement PENDANT la calibration
  static uint32_t lastPrint=0;
  if (calibMode && millis()-lastPrint >= 100){
    ADSRaw r = readADSRaw();
    Axes8 a = mapADSAll(r);
    Serial.printf("[MAP] X=%3d Y=%3d Z=%3d LX=%3d LY=%3d LZ=%3d R1=%3d R2=%3d\n", a.X,a.Y,a.Z,a.LX,a.LY,a.LZ,a.R1,a.R2);
    lastPrint = millis();
  }

  // Gestion bouton (détection appui court / long)
  pressed = readCalButton();
  if(pressed && !calBtnLast){ calBtnStart=millis(); }
  bool shortRelease = (!pressed && calBtnLast && (millis()-calBtnStart>=CAL_SHORT_MIN_MS) && (millis()-calBtnStart<CAL_HOLD_MS));
  calBtnLast=pressed;

  // FSM
  switch(calPhase){

    case CAL_PHASE_NEUTRAL_INIT: {
      // Moyenne sur 1s pour fixer mid ~ neutre => 512 en MAP
      static uint32_t t0=0; static long acc[8]={0}; static int n=0;
      if(t0==0){ t0=millis(); for(int i=0;i<8;i++) acc[i]=0; n=0; }

      ADSRaw rr=readADSRaw(); int16_t vv[8]={rr.X,rr.Y,rr.Z,rr.LX,rr.LY,rr.LZ,rr.R1,rr.R2};
      for(int i=0;i<8;i++) acc[i]+=vv[i]; n++;

      neutralizeAllOutputs();

      if(millis() >= calPhaseEndMs){
        for(int i=0;i<8;i++){
          long m=acc[i]/n; cal[i].midV=(int16_t)constrain(m,0,32767);
          cal[i].minV=max(0,cal[i].midV-8000); cal[i].maxV=min(32767,cal[i].midV+8000);
          haveMin[i]=haveMax[i]=false;
        }
        Serial.println("[CAL] neutres enregistrés !");
        t0=0;
        calPhase = CAL_PHASE_NEUTRAL_VALIDATE;
        calPhaseEndMs = millis() + NEUTRAL_VALIDATE_MS;
      }
      return;
    }

    case CAL_PHASE_NEUTRAL_VALIDATE: {
      // Courte stabilité (aucun axe ne doit s’éloigner franchement du mid)
      ADSRaw rNow = readADSRaw();
      bool stable = (detectMovedAxisMAP(rNow, CAL_MOVE_THR_MAP) < 0);

      neutralizeAllOutputs();

      if(millis() >= calPhaseEndMs && stable){
        solidGreenFor(3000); // validation neutre
        calPhase = CAL_PHASE_NEUTRAL_DONE;
        calPhaseEndMs = millis() + 3000;
      }
      return;
    }

    case CAL_PHASE_NEUTRAL_DONE: {
      serviceBlink();
      neutralizeAllOutputs();
      if(millis() >= calPhaseEndMs){
        startBlink(LEDP_GREEN, 0xFFFFFFFF, 120); // passage phase extrêmes (clignotement rapide)
        calPhase = CAL_PHASE_EXTREMES;
      }
      return;
    }

    case CAL_PHASE_EXTREMES: {
      // Appui court -> enregistre MIN ou MAX de l’axe le plus éloigné
      if(shortRelease){
        ADSRaw rr=readADSRaw();
        int ax = detectMovedAxisMAP(rr, CAL_MOVE_THR_MAP);
        if(ax<0){
          Serial.println("[ERREUR] Mouvement insuffisant (< seuil 20).");
          solidRedFor(1000);
        } else {
          int16_t v_raw = ((int16_t*)&rr)[ax];
          int v_map   = mapRawPreCal(v_raw);
          int mid_map = mapRawPreCal(cal[ax].midV);
          bool isMin = (v_map < mid_map);

          if(isMin){ cal[ax].minV=v_raw; haveMin[ax]=true; }
          else     { cal[ax].maxV=v_raw; haveMax[ax]=true; }

          Axes8 aNow = mapADSAll(rr);
          int v_mapped_arr[8]={aNow.X,aNow.Y,aNow.Z,aNow.LX,aNow.LY,aNow.LZ,aNow.R1,aNow.R2};
          int v_mapped = v_mapped_arr[ax];
          Serial.printf("[OK] Axe %d enregistré: %s = MAP %d\n", ax, isMin?"MIN":"MAX", v_mapped);
          pulseGreen2();

          bool done=true; for(int i=0;i<8;i++) if(!haveMin[i]||!haveMax[i]){done=false;break;}
          if(done){
            startBlink(LEDP_GREEN, 0xFFFFFFFF, 150);
            calPhase = CAL_PHASE_FINISH;
          }
        }
        neutralizeAllOutputs();
        return;
      }

      ADSRaw rr=readADSRaw();
      if(detectMovedAxisMAP(rr, CAL_MOVE_THR_MAP) >= 0){
        stopBlink(); setLED(true,false);
      } else {
        startBlink(LEDP_GREEN, 0xFFFFFFFF, 120);
      }

      neutralizeAllOutputs();
      return;
    }

    case CAL_PHASE_FINISH: {
      serviceBlink();

      // ✅ Fin SEULEMENT si toutes les MAP sont dans la fenêtre [joyNeutralMin..joyNeutralMax]
      ADSRaw rNow = readADSRaw();
      Axes8 aNow  = mapADSAll(rNow);
      int v[8]={aNow.X,aNow.Y,aNow.Z,aNow.LX,aNow.LY,aNow.LZ,aNow.R1,aNow.R2};
      bool neutral = true;
      for(int i=0;i<8;i++){ if(v[i] < joyNeutralMin || v[i] > joyNeutralMax){ neutral=false; break; } }

      if(neutral){
        finishCalibration();
      } else {
        neutralizeAllOutputs(); // sécurité tant que non validé
      }
      return;
    }

    case CAL_PHASE_IDLE:
    default:
      // ne devrait pas arriver pendant calibMode==true
      return;
  }
}