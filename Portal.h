#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

// Portail HTTP/DNS unique pour tout le projet.
// - Démarre un seul AP (SSID: ESP32-CONTROLE)
// - Fournit WebServer & DNSServer partagés
// - Doit être "handled" souvent via portalHandle()

WebServer& portalServer();      // serveur HTTP global (port 80)
DNSServer& portalDNS();         // DNS pour portail captif
bool portalActive();

void portalStart(const char* ssid = "ESP32-CONTROLE");  // idempotent
void portalStop();                                      // coupe AP + serveurs
void portalHandle();                                    // à appeler souvent
