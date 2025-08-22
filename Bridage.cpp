#include <Arduino.h>
#include <EEPROM.h>
#include "Portal.h"
#include "Bridage.h"
#include "Calibration.h"
#include "Led.h"  // pour readCalButton()
#include "Config.h"
#include "Controllers.h"

#define AX_COUNT 8
#define NEUTRAL_HALF_WINDOW 30
#define BRIDAGE_BASE_SPAN 513

#define BRDG_EE_MAGIC      0xB1D6
#define BRDG_EE_VER        0x0001
#define BRDG_EE_SIZE       512
#define BRDG_EE_MAGIC_ADDR 256
#define BRDG_EE_VER_ADDR   258
#define BRDG_EE_DATA_ADDR  260

int padMapMin[AX_COUNT]     = {255,255,255,255,255,255,255,255};
int padMapMax[AX_COUNT]     = {768,768,768,768,768,768,768,768};
int padNeutral[AX_COUNT]    = {512,512,512,512,512,512,512,512};
int padNeutralMin[AX_COUNT] = {
  512-NEUTRAL_HALF_WINDOW,512-NEUTRAL_HALF_WINDOW,512-NEUTRAL_HALF_WINDOW,512-NEUTRAL_HALF_WINDOW,
  512-NEUTRAL_HALF_WINDOW,512-NEUTRAL_HALF_WINDOW,512-NEUTRAL_HALF_WINDOW,512-NEUTRAL_HALF_WINDOW
};
int padNeutralMax[AX_COUNT] = {
  512+NEUTRAL_HALF_WINDOW,512+NEUTRAL_HALF_WINDOW,512+NEUTRAL_HALF_WINDOW,512+NEUTRAL_HALF_WINDOW,
  512+NEUTRAL_HALF_WINDOW,512+NEUTRAL_HALF_WINDOW,512+NEUTRAL_HALF_WINDOW,512+NEUTRAL_HALF_WINDOW
};

static inline int clampInt(int v,int lo,int hi){ if(v<lo) return lo; if(v>hi) return hi; return v; }
static inline bool isPadMode(){ return digitalRead(MODE_SEL_PIN)==HIGH; } // HIGH = mode manette

void bridageRecalcNeutralForAxis(int i){
  int n=(padMapMin[i]+padMapMax[i])/2;
  padNeutral[i]=n;
  padNeutralMin[i]=n-NEUTRAL_HALF_WINDOW;
  padNeutralMax[i]=n+NEUTRAL_HALF_WINDOW;
}
static void bridageRecalcAll(){ for(int i=0;i<AX_COUNT;i++) bridageRecalcNeutralForAxis(i); }

void bridageSaveToEEPROM(){
#if defined(ARDUINO_ARCH_ESP32)
  EEPROM.begin(BRDG_EE_SIZE);
#endif
  EEPROM.put(BRDG_EE_MAGIC_ADDR,(uint16_t)BRDG_EE_MAGIC);
  EEPROM.put(BRDG_EE_VER_ADDR,(uint16_t)BRDG_EE_VER);
  int addr=BRDG_EE_DATA_ADDR;
  for(int i=0;i<AX_COUNT;i++){ int16_t v=(int16_t)padMapMin[i]; EEPROM.put(addr,v); addr+=2; }
  for(int i=0;i<AX_COUNT;i++){ int16_t v=(int16_t)padMapMax[i]; EEPROM.put(addr,v); addr+=2; }
#if defined(ARDUINO_ARCH_ESP32)
  EEPROM.commit();
#endif
  Serial.println("[BRIDAGE] Sauvegarde EEPROM : OK");
}

static bool bridageLoadFromEEPROM(){
#if defined(ARDUINO_ARCH_ESP32)
  EEPROM.begin(BRDG_EE_SIZE);
#endif
  uint16_t magic=0,ver=0;
  EEPROM.get(BRDG_EE_MAGIC_ADDR,magic);
  EEPROM.get(BRDG_EE_VER_ADDR,ver);
  if(magic!=BRDG_EE_MAGIC){
    Serial.println("[BRIDAGE] EEPROM absente -> valeurs par défaut.");
    return false;
  }
  int addr=BRDG_EE_DATA_ADDR;
  for(int i=0;i<AX_COUNT;i++){ int16_t v; EEPROM.get(addr,v); addr+=2; padMapMin[i]=(int)v; }
  for(int i=0;i<AX_COUNT;i++){ int16_t v; EEPROM.get(addr,v); addr+=2; padMapMax[i]=(int)v; }
  Serial.printf("[BRIDAGE] Configuration chargée (ver=0x%04X).\n",ver);
  return true;
}

void bridageLoadOrDefault(){
  bool ok=bridageLoadFromEEPROM();
  if(!ok){ for(int i=0;i<AX_COUNT;i++){ padMapMin[i]=255; padMapMax[i]=768; } }
  bridageRecalcAll();
}

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

static String htmlPage(){
  auto csv=[&](int *a){ String s=""; for(int i=0;i<AX_COUNT;i++){ s+=String(a[i]); if(i<AX_COUNT-1) s+=","; } return s; };
  int deltaFrom512 = neutralOffset - 512;
  int dispMin[AX_COUNT], dispMax[AX_COUNT];
  for(int i=0;i<AX_COUNT;i++){ dispMin[i]=padMapMin[i]-deltaFrom512; dispMax[i]=padMapMax[i]-deltaFrom512; }
  String sMin=csv(dispMin), sMax=csv(dispMax);

  String html;
  html.reserve(18000);

  html += "<!DOCTYPE html><html><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  html += "<title>Bridage axes (mode manette)</title>";
  html += "<style>";
  html += ":root{--bg:#0f172a;--fg:#e5e7eb;--muted:#9ca3af;--line:#1f2937;--btn:#334155;--btnh:#475569;}";
  html += "html,body{margin:0;padding:0;background:var(--bg);color:var(--fg);font-family:system-ui,Arial;}";
  html += ".wrap{padding:16px; max-width:1100px; margin:0 auto;}";
  html += "h2{margin:8px 0 14px 0;}";
  html += ".bar{display:flex; gap:8px; align-items:center; margin:10px 0 16px;}";
  html += "button{background:var(--btn); color:var(--fg); border:1px solid var(--line);padding:8px 12px; border-radius:8px; cursor:pointer}";
  html += "button:hover{background:var(--btnh)}";
  html += "#msg{color:var(--muted)}";
  html += "table{border-collapse:collapse; width:100%;}";
  html += "th,td{border-bottom:1px solid var(--line); padding:10px 8px; text-align:center;}";
  html += "th{font-size:12px; color:var(--muted); text-transform:uppercase; letter-spacing:.08em;}";
  html += "tbody tr:hover{background:rgba(255,255,255,.04)}";
  html += "select{background:#111827; color:#e5e7eb; border:1px solid #1f2937; border-radius:6px; padding:4px 6px; min-width:90px;}";
  html += ".muted{color:#9ca3af} .ok{color:#10b981;font-weight:600} .bad{color:#ef4444;font-weight:600}";
  html += "</style></head><body><div class=\"wrap\">";
  html += "<h2>Bridage des axes (mode manette)</h2>";
  html += navBar();
  html += "<div class=\"bar\">";
  html += "<button id=\"btnSend\">Envoyer les valeurs</button>";
  html += "<button id=\"btnDefaults\">Valeurs par défaut</button>";
  html += "<button id=\"btnFinish\">Fin de Bridage</button>";
  html += "<span id=\"msg\" class=\"muted\"></span>";
  html += "</div>";
  html += "<div style='margin:10px 0'>D\u00E9calage neutre : <select id=\"off\"></select> <button id=\"saveOff\">Sauvegarde EEPROM</button></div>";

  html += "<table id=\"t\"><thead><tr>";
  html += "<th>Axe</th><th>Valeur mini</th><th>Valeur maxi</th><th>Plage neutre</th><th>Conseil</th><th>Valeurs enregistrées</th><th>Manette</th>";
  html += "</tr></thead><tbody id=\"tb\"></tbody></table>";

  html += "<script>";
  html += "var AX=['X','Y','Z','LX','LY','LZ','R1','R2'];";
  html += "var Smin=[" + sMin + "];";
  html += "var Smax=[" + sMax + "];";
  html += "var OFFSET=" + String(neutralOffset) + ";";
  html += "var ACTMIN=Smin.slice(), ACTMAX=Smax.slice();";
  html += "var PAD=[0,0,0,0,0,0,0,0];";
  html += "var touchedMin=[false,false,false,false,false,false,false,false];";
  html += "var touchedMax=[false,false,false,false,false,false,false,false];";
  html += "var lastChanged=['','','','','','','',''];";
  html += "function buildOffset(){ var o=''; for(var v=0; v<=1023; v++){ o+='<option value=\"'+v+'\"'+(v===OFFSET?' selected':'')+'>'+v+'</option>'; } document.getElementById('off').innerHTML=o; }";
  html += "function applyOffset(save){ var v=document.getElementById('off').value; var u='/offset?val='+v; if(save) u+='&save=1'; fetch(u,{cache:'no-store'}).then(function(){ if(save) location.reload(); }); }";
  html += "function clamp(v,lo,hi){ return Math.min(hi, Math.max(lo, v)); }";
  html += "function mkSelect(id,val){ var o=''; for(var v=0; v<=1023; v++){ o+='<option value=\"'+v+'\"'+(v===val?' selected':'')+'>'+v+'</option>'; } return '<select id=\"'+id+'\">'+o+'</select>'; }";
  html += "function neutralText(mn,mx){ var n=Math.round((mn+mx)/2); var nmin=clamp(n-30,0,1023); var nmax=clamp(n+30,0,1023); return nmin+' / '+nmax; }";
  html += "function recommendedText(i,mn,mx){ var span=mx-mn; var changedMin=touchedMin[i] && !touchedMax[i]; var changedMax=touchedMax[i] && !touchedMin[i]; if(changedMin){ var rec=mn+513; if(rec>1023) rec=1023; return 'conseil: maxi = '+rec; } if(changedMax){ var rec=mx-513; if(rec<0) rec=0; return 'conseil: mini = '+rec; } if(touchedMin[i] && touchedMax[i]){ if(span<513) return '<span class=\\\"bad\\\">axe bridé</span>'; } return '<span class=\\\"ok\\\">mini = '+mn+' &middot; maxi = '+mx+'</span>'; } function setActCell(i){ var el=document.getElementById('act_'+i); el.textContent=ACTMIN[i]+' / '+ACTMAX[i]; } function setPadCell(i){ var el=document.getElementById('pad_'+i); el.textContent=PAD[i]; }";
  html += "function recalcRow(i){ var mn=parseInt(document.getElementById('min_'+i).value,10); var mx=parseInt(document.getElementById('max_'+i).value,10); if(isNaN(mn))mn=0; if(isNaN(mx))mx=0; document.getElementById('neutral_'+i).textContent=neutralText(mn,mx); document.getElementById('rec_'+i).innerHTML=recommendedText(i,mn,mx); }";
  html += "function draw(){ var t=''; for(var i=0;i<8;i++){ t+='<tr>'+'<td>'+AX[i]+'</td>'+'<td>'+mkSelect('min_'+i,Smin[i])+'</td>'+'<td>'+mkSelect('max_'+i,Smax[i])+'</td>'+'<td class=\"muted\" id=\"neutral_'+i+'\">—</td>'+'<td class=\"muted\" id=\"rec_'+i+'\">—</td>'+'<td class=\"muted\" id=\"act_'+i+'\">—</td>'+'<td class=\"muted\" id=\"pad_'+i+'\">—</td>'+'</tr>'; } document.getElementById('tb').innerHTML=t; for(var i=0;i<8;i++){ (function(ii){ var mnSel=document.getElementById('min_'+ii); var mxSel=document.getElementById('max_'+ii); mnSel.addEventListener('change',function(){touchedMin[ii]=true; lastChanged[ii]='min'; recalcRow(ii);}); mxSel.addEventListener('change',function(){touchedMax[ii]=true; lastChanged[ii]='max'; recalcRow(ii);}); recalcRow(ii); setActCell(ii); setPadCell(ii); })(i); } document.getElementById('btnSend').onclick=sendValues; document.getElementById('btnDefaults').onclick=resetDefaults; document.getElementById('btnFinish').onclick=finishBridage; }";
  html += "function collectCSV(){ var mins=[],maxs=[]; for(var i=0;i<8;i++){ mins.push(document.getElementById('min_'+i).value); maxs.push(document.getElementById('max_'+i).value);} return {min:mins.join(','), max:maxs.join(',')}; }";
  html += "function afterSavedReflectCurrent(){ for(var i=0;i<8;i++){ ACTMIN[i]=parseInt(document.getElementById('min_'+i).value,10); ACTMAX[i]=parseInt(document.getElementById('max_'+i).value,10); setActCell(i); touchedMin[i]=false; touchedMax[i]=false; lastChanged[i]=''; } }";
  html += "function refreshPads(){ fetch('/pad',{cache:'no-store'}).then(function(r){return r.text();}).then(function(t){ var v=t.trim().split(','); for(var i=0;i<8;i++){ PAD[i]=parseInt(v[i]||'0',10); setPadCell(i);} setTimeout(refreshPads,200); }).catch(function(){ setTimeout(refreshPads,1000); }); }";
  html += "function sendValues(){ var d=collectCSV(); var msg=document.getElementById('msg'); msg.textContent='Envoi...'; fetch('/apply?min='+encodeURIComponent(d.min)+'&max='+encodeURIComponent(d.max),{cache:'no-store'}).then(function(r){if(!r.ok) throw new Error('bad'); return r.text();}).then(function(){ msg.textContent='Valeurs enregistr\u00E9es.'; afterSavedReflectCurrent(); setTimeout(function(){msg.textContent='';},1500); }).catch(function(){ msg.textContent='Erreur d’envoi'; }); }";
  html += "function resetDefaults(){ for(var i=0;i<8;i++){ document.getElementById('min_'+i).value=255; document.getElementById('max_'+i).value=768; recalcRow(i);} sendValues(); }";
  html += "function finishBridage(){ var msg=document.getElementById('msg'); fetch('/finish',{cache:'no-store'}).then(function(){ msg.textContent='Bridage termin\u00E9.'; setTimeout(function(){ document.body.innerHTML='<div class=\"wrap\"><h3>Bridage termin\u00E9</h3><p>Vous pouvez fermer cette page.</p></div>'; },400); }).catch(function(){ msg.textContent='Erreur'; }); }";

  html += "draw(); refreshPads();";
  html += "buildOffset(); document.getElementById('off').addEventListener('change',function(){applyOffset(false);}); document.getElementById('saveOff').addEventListener('click',function(){applyOffset(true);});";
  html += "</script></div></body></html>";
  return html;
}

static bool bActive=false;
static bool bStopPending=false;
static uint32_t bStopAtMs=0;

void bridageStartAP(){
  if(bActive) return;

  // sécurité: forcer GPIO27 LOW pendant bridage
  pinMode(GPIO_MANETTE_CONNECTEE, OUTPUT);
  digitalWrite(GPIO_MANETTE_CONNECTEE, LOW);

  portalStart("ESP32-CONTROLE");
  Serial.println("[BRIDAGE] D\u00E9marrage AP + page /bridage");
  auto& server = portalServer();
  server.on("/bridage", HTTP_GET, [&](){ server.send(200,"text/html",htmlPage()); });
  server.on("/apply", HTTP_GET, [&](){
    if(!server.hasArg("min") || !server.hasArg("max")){ server.send(400,"text/plain","missing args"); return; }
    auto parseCSV8=[&](const String& s,int out[AX_COUNT]){
      int idx=0; String tok="";
      for(uint16_t i=0;i<=s.length();++i){
        if(i==s.length() || s[i]==','){ if(idx<AX_COUNT) out[idx++] = tok.toInt(); tok=""; }
        else tok+=s[i];
      }
      while(idx<AX_COUNT){ out[idx++]=0; }
    };
    int mins[AX_COUNT], maxs[AX_COUNT];
    parseCSV8(server.arg("min"), mins);
    parseCSV8(server.arg("max"), maxs);
    int delta = neutralOffset - 512;
    for(int i=0;i<AX_COUNT;i++){
      int mn = clampInt(mins[i], 0, 1023);
      int mx = clampInt(maxs[i], 0, 1023);
      if(mx < mn){ int t=mn; mn=mx; mx=t; }
      padMapMin[i]=clampInt(mn + delta, 0, 1023);
      padMapMax[i]=clampInt(mx + delta, 0, 1023);
      bridageRecalcNeutralForAxis(i);
    }
    bridageSaveToEEPROM();
    server.send(200,"text/plain","OK");
  });
  server.on("/finish", HTTP_GET, [&](){
    server.send(200,"text/plain","OK");
    bStopPending=true; bStopAtMs=millis()+800;
  });
  server.on("/offset", HTTP_GET, [&](){
    if(!server.hasArg("val")){ server.send(400,"text/plain","missing"); return; }
    int v = clampInt(server.arg("val").toInt(), 0, 1023);
    neutralOffset = v;
    updateNeutralWindow();
    if(server.hasArg("save")) { saveNeutralOffset(); bridageSaveToEEPROM(); }
    server.send(200,"text/plain","OK");
  });

  server.on("/pad", HTTP_GET, [&](){
    int vals[AX_COUNT];
    getPadValues(vals);
    String s="";
    for(int i=0;i<AX_COUNT;i++){ if(i) s+=","; s+=String(vals[i]); }
    server.send(200,"text/plain",s);
  });

  bActive=true;
  Serial.println("[BRIDAGE] Routes bridage montées (portail unique).");
}

void bridageStopAP(){ bActive=false; portalStop(); Serial.println("[BRIDAGE] Inactif."); }
bool isBridageActive(){ return bActive; }

void bridageHandlePortal(){
  portalHandle();
  if(bStopPending && millis()>=bStopAtMs){
    bStopPending=false;
    bridageStopAP();
  }
}

// Séquence 5 appuis courts pour ouvrir le bridage
void bridageHandleButtonSequence(bool /*unused*/, uint32_t /*shortPressMinMs*/, uint32_t /*longPressMs*/){
  static bool last=false; static uint32_t tPress=0; static uint8_t count=0; static uint32_t windowStart=0;
  bool now = readCalButton();  // <<< cohérent avec la calibration

  // Signal visuel: pendant l'appui court, LED verte fixe
  if (isPadMode() && now) { setLED(true,false); }

  if(!isPadMode()){ last=false; tPress=0; count=0; windowStart=0; return; }

  if(now && !last){ tPress = millis(); if(count==0) windowStart = tPress; }
  if(!now && last){
    uint32_t dt = millis() - tPress;
    if(dt >= 50 && dt <= 800){
      count++; Serial.printf("[BRIDAGE] Appui court %u/5\n", count);
      if(count>=5){ bridageStartAP(); count=0; windowStart=0; }
    }
  }
  if(windowStart && (millis()-windowStart > 6000)){ count=0; windowStart=0; }
  last = now;
}
