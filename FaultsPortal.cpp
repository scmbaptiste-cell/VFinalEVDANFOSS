// FaultsPortal.cpp — Page défaut servie via le portail Wi-Fi unique
#include "FaultsPortal.h"
#include "Portal.h"
#include "Faults.h"        // faultCode, missADSg/missADSd/missPCA, fmtUptime()
#include "Calibration.h"   // calibMode
#include "Bridage.h"       // isBridageActive()

// Indique si la page défaut est enregistrée dans le serveur partagé
static bool active = false;

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

// -----------------------------------------------------------------------------
// Texte détaillé par code défaut
// -----------------------------------------------------------------------------
static String faultDetailText(uint8_t fc){
  switch (fc){
    case FC_ADS_DROIT:   return "N2 : ADS1115 DROIT (0x49) absent/KO.";
    case FC_ADS_GAUCHE:  return "N3 : ADS1115 GAUCHE (0x48) absent/KO.";
    case FC_PCA:         return "N4 : PCA9685 (0x40) absent/KO.";
    case FC_I2C_GENERAL: {
      String s="N5 : D\u00E9faut g\u00E9n\u00E9ral I2C.";
      if (missADSd) s += " [N2: ADS1115 DROIT KO]";
      if (missADSg) s += " [N3: ADS1115 GAUCHE KO]";
      if (missPCA)  s += " [N4: PCA9685 KO]";
      return s;
    }
    case FC_NEUTRAL_TO:  return "N7 : Temps d\u00E9pass\u00E9 pour neutre joystick au d\u00E9marrage.";
    case FC_NO_GAMEPAD:  return "Alternance Rouge/Vert = Mode manette sans manette connect\u00E9e.";
    default:             return "Aucun d\u00E9faut.";
  }
}

// -----------------------------------------------------------------------------
// Liste de TOUS les modules HS (séparés par virgules) ou "-" si aucun
// -----------------------------------------------------------------------------
static String modulesHS(){
  String s="";
  if (faultCode==FC_PCA        || missPCA)   { if (s.length()) s += ", "; s += "PCA9685 (0x40)"; }
  if (faultCode==FC_ADS_DROIT  || missADSd)  { if (s.length()) s += ", "; s += "ADS1115 DROIT (0x49)"; }
  if (faultCode==FC_ADS_GAUCHE || missADSg)  { if (s.length()) s += ", "; s += "ADS1115 GAUCHE (0x48)"; }
  if (s.length()==0) s = "-";
  return s;
}

// -----------------------------------------------------------------------------
// Page HTML (auto-refresh JS via /status.json)
// -----------------------------------------------------------------------------
static String htmlPage(){
  String html;
  html.reserve(16000);

  html += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Defaut syst\u00E8me</title>";
  html += "<style>";
  html += "body{font-family:system-ui,Arial;margin:16px;background:#0f172a;color:#e5e7eb}";
  html += ".muted{color:#9ca3af}.ok{color:#10b981;font-weight:700}.bad{color:#ef4444;font-weight:700}";
  html += "table{border-collapse:collapse;width:100%;max-width:900px}";
  html += "th,td{border-bottom:1px solid #1f2937;padding:10px 8px;text-align:left}";
  html += "th{font-size:12px;color:#9ca3af;text-transform:uppercase;letter-spacing:.08em}";
  html += "tbody tr:hover{background:rgba(255,255,255,.04)}";
  html += "code{background:#111827;border:1px solid #1f2937;border-radius:6px;padding:2px 6px}";
  html += "</style></head><body>";

  html += "<h2>D\u00E9faut syst\u00E8me</h2>";
  html += navBar();
  html += "<p class='muted'>Point d'acc\u00E8s <b>ESP32-CONTROLE</b> &middot; Uptime: <code id='upt'>";
  html += fmtUptime();
  html += "</code></p>";

  html += "<table><thead><tr><th>Champ</th><th>Valeur</th></tr></thead><tbody>";
  html += "<tr><td>Code</td><td><b>N = <span id='ncode'>";
  html += String((int)faultCode);
  html += "</span></b></td></tr>";

  html += "<tr><td>D\u00E9tail</td><td id='detail'>";
  html += faultDetailText(faultCode);
  html += "</td></tr>";

  html += "<tr><td>Module(s) HS</td><td><b id='mods'>";
  html += modulesHS();
  html += "</b></td></tr>";

  html += "<tr><td>ADS gauche (0x48)</td><td id='ads_g'>";
  html += missADSg ? "<span class='bad'>ABSENT/KO</span>" : "<span class='ok'>OK</span>";
  html += "</td></tr>";

  html += "<tr><td>ADS droit (0x49)</td><td id='ads_d'>";
  html += missADSd ? "<span class='bad'>ABSENT/KO</span>" : "<span class='ok'>OK</span>";
  html += "</td></tr>";

  html += "<tr><td>PCA9685 (0x40)</td><td id='pca'>";
  html += missPCA ? "<span class='bad'>ABSENT/KO</span>" : "<span class='ok'>OK</span>";
  html += "</td></tr>";
  html += "</tbody></table>";

  // ---- JS : polling JSON toutes les 1 s
  html += "<script>";
  html += "function badge(ok){return ok?\"<span class='ok'>OK</span>\":\"<span class='bad'>ABSENT/KO</span>\";}";
  html += "async function poll(){";
  html += " try{";
  html += "  const r=await fetch('/status.json',{cache:'no-store'});";
  html += "  const j=await r.json();";
  html += "  document.getElementById('ncode').textContent = String(j.N);";
  html += "  document.getElementById('detail').textContent = j.detail || '';";
  html += "  var list = (j.modules && j.modules.length) ? j.modules.join(', ') : (j.module || '-');";
  html += "  document.getElementById('mods').textContent = list;";
  html += "  document.getElementById('ads_g').innerHTML = badge(!j.missADSg);";
  html += "  document.getElementById('ads_d').innerHTML = badge(!j.missADSd);";
  html += "  document.getElementById('pca').innerHTML   = badge(!j.missPCA);";
  html += "  document.getElementById('upt').textContent = j.uptime || '';";
  html += " }catch(e){}";
  html += " setTimeout(poll,1000);";
  html += "}";
  html += "window.addEventListener('load', poll);";
  html += "document.addEventListener('visibilitychange',()=>{ if(!document.hidden) poll(); });";
  html += "</script>";

  html += "</body></html>";
  return html;
}

// -----------------------------------------------------------------------------
// Handlers
// -----------------------------------------------------------------------------
static void onRoot(){ auto& server = portalServer(); server.send(200, "text/html", htmlPage()); }

static void onStatus(){
  String d = faultDetailText(faultCode); d.replace("\"","\\\"");
  String j = "{";
  j += "\"N\":" + String((int)faultCode);
  j += ",\"detail\":\"" + d + "\"";
  j += ",\"modules\":["; bool first = true;
  if (faultCode==FC_PCA || missPCA){ j += "\"PCA9685 (0x40)\""; first=false; }
  if (faultCode==FC_ADS_DROIT || missADSd){ if(!first) j+=','; j += "\"ADS1115 DROIT (0x49)\""; first=false; }
  if (faultCode==FC_ADS_GAUCHE || missADSg){ if(!first) j+=','; j += "\"ADS1115 GAUCHE (0x48)\""; }
  j += "]";
  j += ",\"module\":\"" + modulesHS() + "\"";
  j += ",\"missADSg\":" + String(missADSg ? "true" : "false");
  j += ",\"missADSd\":" + String(missADSd ? "true" : "false");
  j += ",\"missPCA\":"  + String(missPCA  ? "true" : "false");
  j += ",\"uptime\":\"" + fmtUptime() + "\"";
  j += "}";
  auto& server = portalServer();
  server.send(200, "application/json", j);
}

// -----------------------------------------------------------------------------
// AP Start/Stop + loop
// -----------------------------------------------------------------------------
void faultsPortalStartAP(){
  if (active) return;
  if (calibMode) return;
  if (isBridageActive()) return;

  portalStart();

  auto& server = portalServer();
  server.on("/defaut", HTTP_GET, onRoot);
  server.on("/status.json", HTTP_GET, onStatus);

  active = true;
  Serial.println("[DEFAUT] Portail d\u00E9faut actif (AP partag\u00E9).");
}

void faultsPortalStopAP(){
  if (!active) return;
  portalStop();
  active = false;
  Serial.println("[DEFAUT] Portail d\u00E9faut stopp\u00E9.");
}

void faultsPortalHandle(){ if (active) portalHandle(); }
bool isFaultsPortalActive(){ return active; }
