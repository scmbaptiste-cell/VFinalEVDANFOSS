#include "Portal.h"
#include <WiFi.h>

static WebServer server(80);
static DNSServer dns;
static bool active = false;
static IPAddress apIP(192,168,4,1), apGW(192,168,4,1), apMask(255,255,255,0);

WebServer& portalServer(){ return server; }
DNSServer& portalDNS(){ return dns; }
bool portalActive(){ return active; }

static String homePage(){
  return String(
    "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32 Contr\u00F4le</title><style>"
    "body{font-family:system-ui,Arial;margin:16px;background:#0f172a;color:#e5e7eb}"
    "a{display:block;margin:8px 0;padding:12px;background:#334155;color:#e5e7eb;border:1px solid #1f2937;border-radius:8px;text-decoration:none}"
    "a:hover{background:#475569}"
    "</style></head><body>"
    "<h2>ESP32 Contr\u00F4le</h2>"
    "<a href='/defaut'>D\u00E9faut ESP32</a>"
    "<a href='/calib'>Calibration</a>"
    "<a href='/bridage'>Bridage axes</a>"
    "</body></html>"
  );
}

void portalStart(const char* ssid){
  if (active) return;

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apGW, apMask);
  WiFi.softAP(ssid);

  // DNS wildcard pour portail captif
  dns.start(53, "*", apIP);

  // Endpoints "captive" qui redirigent vers "/"
  server.on("/generate_204", HTTP_GET, [](){ server.sendHeader("Location","/",true); server.send(302,"text/plain",""); });
  server.on("/hotspot-detect.html", HTTP_GET, [](){ server.send(200,"text/html","<html><head><meta http-equiv='refresh' content='0;url=/'></head><body>...</body></html>"); });
  server.on("/ncsi.txt", HTTP_GET, [](){ server.sendHeader("Location","/",true); server.send(302,"text/plain",""); });
  server.on("/connecttest.txt", HTTP_GET, [](){ server.sendHeader("Location","/",true); server.send(302,"text/plain",""); });

  server.on("/", HTTP_GET, [](){ server.send(200,"text/html",homePage()); });

  server.begin();
  active = true;
  Serial.println("[PORTAL] AP unique actif : SSID=ESP32-CONTROLE");
}

void portalStop(){
  if (!active) return;
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  // Ne pas couper le Bluetooth en arrêtant uniquement le Wi-Fi
  WiFi.mode(WIFI_MODE_NULL);
  active = false;
  Serial.println("[PORTAL] AP stoppé.");
}

void portalHandle(){
  if (!active) return;
  dns.processNextRequest();
  server.handleClient();
}
