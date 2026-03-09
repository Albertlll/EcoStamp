#pragma once

#include <Arduino.h>

void printerModuleInit();
bool printerModulePrint(const char* qrText, const char* idText);
bool printerModuleIsBusy();
void printerModuleTick();
