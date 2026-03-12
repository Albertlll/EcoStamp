#pragma once

#include <Arduino.h>

void printerModuleInit();
bool printerModulePrint(const char* qrText, const char* idText);
bool printerModulePrintSnapshot(
    const char* id,
    const char* ts,
    double lat,
    double lon,
    float tempC,
    bool hasFix,
    bool hasTemp);
bool printerModuleIsBusy();
void printerModuleTick();
