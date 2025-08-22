// FaultPortal.h — Page défaut servie via le portail Wi-Fi unique
#pragma once
#include <Arduino.h>

// Enregistre la page défaut et démarre l'AP partagé au besoin
void faultsPortalStartAP();

// Coupe la page défaut et le Wi-Fi
void faultsPortalStopAP();

// À appeler régulièrement dans la loop() quand un défaut est actif
void faultsPortalHandle();

// Indique si le portail défaut est actif
bool isFaultsPortalActive();
