#include "printer_module.h"

#include <Arduino.h>

#include <EcoHackPrinter.h>

namespace {

constexpr const char* kPrinterAddress = "7D:11:28:1B:CF:6B";

String makeTemperatureLine(float tempC, bool hasTemp) {
  if (!hasTemp) {
    return "TEMP N/A";
  }

  char buffer[24];
  snprintf(buffer, sizeof(buffer), "TEMP %.1fC", tempC);
  return String(buffer);
}

String makeLocationLine(double lat, double lon, bool hasFix) {
  if (!hasFix) {
    return "GPS NO FIX";
  }

  char buffer[48];
  snprintf(buffer, sizeof(buffer), "%.5f %.5f", lat, lon);
  return String(buffer);
}

ecohack::PrinterConfig makePrinterConfig() {
  ecohack::PrinterConfig config;
  config.address = kPrinterAddress;
  config.labelHeightMm = 40;
  return config;
}

ecohack::BleLabelPrinter g_printer(makePrinterConfig());

}  // namespace

void printerModuleInit() {
  delay(200);
  Serial.println("Connecting printer...");
  const bool connected = g_printer.begin();
  Serial.printf("Printer %s\n", connected ? "connected" : "not connected");
}

bool printerModulePrint(const char* qrText, const char* idText) {
  if (g_printer.busy()) {
    Serial.println("Print busy, skip");
    return false;
  }

  ecohack::LabelContent label;
  if (!label.setQrCentered(10, qrText, 4)) {
    Serial.println("QR generation failed");
    return false;
  }
  if (!label.addRotatedText(4, 10, idText, 3)) {
    Serial.println("Label composition failed");
    return false;
  }

  const bool ok = g_printer.print(label);
  Serial.printf("Print %s\n", ok ? "ok" : "failed");
  return ok;
}

bool printerModulePrintSnapshot(
    const char* id,
    const char* ts,
    double lat,
    double lon,
    float tempC,
    bool hasFix,
    bool hasTemp) {
  if (g_printer.busy()) {
    Serial.println("Print busy, skip");
    return false;
  }

  char qrPayload[160];
  snprintf(qrPayload, sizeof(qrPayload), "ECO|%s|%s|%c|%.5f|%.5f|%c|%.1f",
           id,
           ts,
           hasFix ? '1' : '0',
           lat,
           lon,
           hasTemp ? '1' : '0',
           tempC);

  ecohack::LabelContent label;
  const bool qrOk = label.setQrCentered(10, qrPayload, 4);
  const bool idOk = label.addRotatedText(4, 10, id, 3);
  const bool tempOk = label.addCenteredText(165, makeTemperatureLine(tempC, hasTemp), 2);
  const bool gpsOk = label.addCenteredText(195, makeLocationLine(lat, lon, hasFix), 1);
  const bool tsOk = label.addCenteredText(215, String(ts), 1);
  if (!(qrOk && idOk && tempOk && gpsOk && tsOk)) {
    Serial.println("Label composition failed");
    return false;
  }

  const bool ok = g_printer.print(label);
  Serial.printf("Print %s\n", ok ? "ok" : "failed");
  return ok;
}

bool printerModuleCalibrate() {
  if (g_printer.busy()) {
    Serial.println("Calibration skipped: printer busy");
    return false;
  }
  const bool ok = g_printer.calibrate();
  Serial.printf("Calibration %s\n", ok ? "ok" : "failed");
  return ok;
}

bool printerModuleIsBusy() {
  return g_printer.busy();
}

void printerModuleTick() {
  g_printer.tick();
}
