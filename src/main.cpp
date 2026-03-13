#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <TinyGPSPlus.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <vector>
#include <qrcode.h>
#include "printer_module.h"

LV_FONT_DECLARE(lv_font_ru_12);
LV_FONT_DECLARE(lv_font_ru_14);

static constexpr int GPS_RX = 16;
static constexpr int GPS_TX = 17;
static constexpr int WATER_TEMP_PIN = 26;
static constexpr int SD_CS_PIN = 13;
static constexpr int BUTTON_PIN = 21;
static constexpr int SAMPLE_MS = 1;
static constexpr int PRESS_STABLE_SAMPLES = 12;
static constexpr int RELEASE_STABLE_SAMPLES = 12;
static constexpr uint32_t DOUBLE_CLICK_MS = 350;
static constexpr const char* AP_SSID = "EcoLog-Device";
static constexpr const char* AP_PASS = "ecolog123";

TFT_eSPI tft;
TinyGPSPlus gps;
OneWire oneWire(WATER_TEMP_PIN);
DallasTemperature waterTemp(&oneWire);
WebServer server(80);
static bool sdReady = false;

static lv_disp_draw_buf_t drawBuf;
static lv_color_t buf[128 * 20];
static lv_obj_t* lblTime;
static lv_obj_t* lblCoords;
static lv_obj_t* lblTemp;
static lv_obj_t* panel;

static uint32_t lastTickMs = 0;
static uint32_t lastUiMs = 0;
static uint32_t lastTempMs = 0;

static bool tempSensorOk = false;
static uint8_t tempReadFailStreak = 0;
static float lastTempC = NAN;
static uint32_t lastButtonSampleMs = 0;
static uint8_t buttonStable = HIGH;
static uint8_t buttonLastRaw = HIGH;
static uint16_t buttonSameCount = 0;
static uint8_t buttonClickCount = 0;
static uint32_t firstClickMs = 0;
static String webExpedition = "ECOHAK-2026";
static String webPrefix = "EXP";

struct WebSample {
  String id;
  String timestamp;
  String date;
  String time;
  String temp;
  String lat;
  String lon;
  String note;
  bool fixValid = false;
  bool tempValid = false;
  uint32_t capturedMs = 0;
};

static bool initSd() {
  if (sdReady) {
    return true;
  }
  sdReady = SD.begin(SD_CS_PIN);
  Serial.printf("SD %s\n", sdReady ? "ready" : "init failed");
  return sdReady;
}

static bool appendSnapshotToSd(
    const char* id,
    const char* ts,
    double lat,
    double lon,
    float tempC,
    bool hasFix,
    bool hasTemp,
    uint32_t capturedMs) {
  if (!initSd()) {
    return false;
  }

  if (!SD.exists("/samples.csv")) {
    File header = SD.open("/samples.csv", FILE_WRITE);
    if (!header) {
      return false;
    }
    header.println("id,timestamp,lat,lon,temp_c,fix_valid,temp_valid,captured_ms,note");
    header.close();
  }

  File f = SD.open("/samples.csv", FILE_APPEND);
  if (!f) {
    return false;
  }

  char line[220];
  snprintf(line, sizeof(line), "%s,%s,%.6f,%.6f,%.2f,%u,%u,%lu,",
           id, ts, lat, lon, tempC, hasFix ? 1 : 0, hasTemp ? 1 : 0, (unsigned long)capturedMs);
  f.println(line);
  f.close();
  return true;
}

static String contentTypeFor(const String& path) {
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".js")) return "application/javascript";
  if (path.endsWith(".csv")) return "text/csv; charset=utf-8";
  if (path.endsWith(".json")) return "application/json";
  return "text/plain; charset=utf-8";
}

static String jsonEscape(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '\"': out += "\\\""; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) {
          out += ' ';
        } else {
          out += c;
        }
        break;
    }
  }
  return out;
}

static bool csvColumn(const String& line, int index, String& out) {
  int start = 0;
  int current = 0;
  while (current < index) {
    const int comma = line.indexOf(',', start);
    if (comma < 0) {
      out = "";
      return false;
    }
    start = comma + 1;
    current++;
  }

  if (index == 8) {
    out = line.substring(start);
  } else {
    const int comma = line.indexOf(',', start);
    out = (comma < 0) ? line.substring(start) : line.substring(start, comma);
  }
  out.trim();
  return true;
}

static void splitTimestamp(const String& timestamp, String& date, String& time) {
  if (timestamp.length() == 14) {
    date = timestamp.substring(0, 4) + "-" + timestamp.substring(4, 6) + "-" + timestamp.substring(6, 8);
    time = timestamp.substring(8, 10) + ":" + timestamp.substring(10, 12) + ":" + timestamp.substring(12, 14);
    return;
  }
  date = "";
  time = timestamp;
}

static bool loadSamples(std::vector<WebSample>& samples) {
  samples.clear();
  if (!initSd()) {
    return false;
  }
  if (!SD.exists("/samples.csv")) {
    return true;
  }

  File f = SD.open("/samples.csv", FILE_READ);
  if (!f) {
    return false;
  }

  bool headerSkipped = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) {
      continue;
    }
    if (!headerSkipped) {
      headerSkipped = true;
      if (line.startsWith("id,")) {
        continue;
      }
    }

    WebSample sample;
    String fixValid;
    String tempValid;
    String capturedMs;
    csvColumn(line, 0, sample.id);
    csvColumn(line, 1, sample.timestamp);
    csvColumn(line, 2, sample.lat);
    csvColumn(line, 3, sample.lon);
    csvColumn(line, 4, sample.temp);
    csvColumn(line, 5, fixValid);
    csvColumn(line, 6, tempValid);
    csvColumn(line, 7, capturedMs);
    csvColumn(line, 8, sample.note);
    splitTimestamp(sample.timestamp, sample.date, sample.time);
    sample.fixValid = fixValid.toInt() != 0;
    sample.tempValid = tempValid.toInt() != 0;
    sample.capturedMs = static_cast<uint32_t>(capturedMs.toInt());
    samples.push_back(sample);
  }

  f.close();
  return true;
}

static bool writeSamples(const std::vector<WebSample>& samples) {
  if (!initSd()) {
    return false;
  }

  if (SD.exists("/samples.csv")) {
    SD.remove("/samples.csv");
  }

  File f = SD.open("/samples.csv", FILE_WRITE);
  if (!f) {
    return false;
  }

  f.println("id,timestamp,lat,lon,temp_c,fix_valid,temp_valid,captured_ms,note");
  for (const WebSample& sample : samples) {
    char line[320];
    snprintf(line, sizeof(line), "%s,%s,%s,%s,%s,%u,%u,%lu,%s",
             sample.id.c_str(),
             sample.timestamp.c_str(),
             sample.lat.length() ? sample.lat.c_str() : "0",
             sample.lon.length() ? sample.lon.c_str() : "0",
             sample.temp.length() ? sample.temp.c_str() : "0",
             sample.fixValid ? 1 : 0,
             sample.tempValid ? 1 : 0,
             static_cast<unsigned long>(sample.capturedMs),
             sample.note.c_str());
    f.println(line);
  }
  f.close();
  return true;
}

static void sendSampleJson(WiFiClient& client, const WebSample& sample) {
  client.print("{\"id\":\"");
  client.print(jsonEscape(sample.id));
  client.print("\",\"date\":\"");
  client.print(jsonEscape(sample.date));
  client.print("\",\"time\":\"");
  client.print(jsonEscape(sample.time));
  client.print("\",\"temp\":\"");
  client.print(jsonEscape(sample.tempValid ? sample.temp : String("")));
  client.print("\",\"lat\":\"");
  client.print(jsonEscape(sample.fixValid ? sample.lat : String("")));
  client.print("\",\"lon\":\"");
  client.print(jsonEscape(sample.fixValid ? sample.lon : String("")));
  client.print("\",\"note\":\"");
  client.print(jsonEscape(sample.note));
  client.print("\",\"timestamp\":\"");
  client.print(jsonEscape(sample.timestamp));
  client.print("\",\"temp_valid\":");
  client.print(sample.tempValid ? "true" : "false");
  client.print(",\"fix_valid\":");
  client.print(sample.fixValid ? "true" : "false");
  client.print(",\"captured_ms\":");
  client.print(sample.capturedMs);
  client.print("}");
}

static int findSampleIndex(const std::vector<WebSample>& samples, const String& id) {
  for (size_t i = 0; i < samples.size(); ++i) {
    if (samples[i].id == id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

static bool streamFromSd(const String& path) {
  if (!initSd()) {
    server.send(500, "text/plain", "SD init failed");
    return false;
  }
  if (!SD.exists(path)) {
    return false;
  }
  File f = SD.open(path, FILE_READ);
  if (!f) {
    server.send(500, "text/plain", "Open failed");
    return true;
  }
  server.streamFile(f, contentTypeFor(path));
  f.close();
  return true;
}

static void handleRoot() {
  if (!streamFromSd("/www/index.html")) {
    server.send(404, "text/plain", "Missing /www/index.html on SD");
  }
}

static void handleStaticFiles() {
  String path = server.uri();
  if (path == "/") {
    handleRoot();
    return;
  }
  if (!path.startsWith("/www/")) {
    path = String("/www") + path;
  }
  if (!streamFromSd(path)) {
    server.send(404, "text/plain", "Not found");
  }
}

static void handleGetSamplesCsv() {
  if (!initSd()) {
    server.send(500, "text/plain", "SD init failed");
    return;
  }
  if (!SD.exists("/samples.csv")) {
    server.send(200, "text/csv; charset=utf-8",
                "id,timestamp,lat,lon,temp_c,fix_valid,temp_valid,captured_ms,note\n");
    return;
  }
  File f = SD.open("/samples.csv", FILE_READ);
  if (!f) {
    server.send(500, "text/plain", "Read failed");
    return;
  }
  server.streamFile(f, "text/csv; charset=utf-8");
  f.close();
}

static void handlePostSamplesCsv() {
  if (!initSd()) {
    server.send(500, "text/plain", "SD init failed");
    return;
  }
  const String body = server.arg("plain");
  if (body.isEmpty()) {
    server.send(400, "text/plain", "Body is empty");
    return;
  }
  if (SD.exists("/samples.csv")) {
    SD.remove("/samples.csv");
  }
  File f = SD.open("/samples.csv", FILE_WRITE);
  if (!f) {
    server.send(500, "text/plain", "Write failed");
    return;
  }
  f.print(body);
  f.close();
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleStatus() {
  String json = "{\"ok\":true,\"ssid\":\"";
  json += jsonEscape(AP_SSID);
  json += "\",\"ip\":\"";
  json += WiFi.softAPIP().toString();
  json += "\",\"sd_ready\":";
  json += initSd() ? "true" : "false";
  json += ",\"samples\":";
  std::vector<WebSample> samples;
  json += loadSamples(samples) ? String(samples.size()) : "0";
  json += ",\"heap\":";
  json += ESP.getFreeHeap();
  json += "}";
  server.send(200, "application/json", json);
}

static void handleGetRecords() {
  if (!initSd()) {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"SD init failed\"}");
    return;
  }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  WiFiClient client = server.client();

  client.print("{\"ok\":true,\"records\":[");
  if (SD.exists("/samples.csv")) {
    File f = SD.open("/samples.csv", FILE_READ);
    if (!f) {
      client.print("],\"error\":\"Read failed\"}");
      return;
    }

    bool first = true;
    bool headerSkipped = false;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.isEmpty()) {
        continue;
      }
      if (!headerSkipped) {
        headerSkipped = true;
        if (line.startsWith("id,")) {
          continue;
        }
      }

      String id;
      String timestamp;
      String lat;
      String lon;
      String tempC;
      String fixValid;
      String tempValid;
      String capturedMs;
      String note;
      csvColumn(line, 0, id);
      csvColumn(line, 1, timestamp);
      csvColumn(line, 2, lat);
      csvColumn(line, 3, lon);
      csvColumn(line, 4, tempC);
      csvColumn(line, 5, fixValid);
      csvColumn(line, 6, tempValid);
      csvColumn(line, 7, capturedMs);
      csvColumn(line, 8, note);

      if (!first) {
        client.print(',');
      }
      first = false;

      client.print("{\"id\":\"");
      client.print(jsonEscape(id));
      client.print("\",\"timestamp\":\"");
      client.print(jsonEscape(timestamp));
      client.print("\",\"lat\":");
      client.print(lat.length() ? lat : "0");
      client.print(",\"lon\":");
      client.print(lon.length() ? lon : "0");
      client.print(",\"temp_c\":");
      client.print(tempC.length() ? tempC : "0");
      client.print(",\"fix_valid\":");
      client.print(fixValid.length() ? fixValid : "0");
      client.print(",\"temp_valid\":");
      client.print(tempValid.length() ? tempValid : "0");
      client.print(",\"captured_ms\":");
      client.print(capturedMs.length() ? capturedMs : "0");
      client.print(",\"note\":\"");
      client.print(jsonEscape(note));
      client.print("\"}");
    }
    f.close();
  }

  client.print("]}");
}

static void handleGetSamples() {
  std::vector<WebSample> samples;
  if (!loadSamples(samples)) {
    server.send(500, "application/json", "{\"error\":\"SD init failed\"}");
    return;
  }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  WiFiClient client = server.client();
  client.print('[');
  for (size_t i = 0; i < samples.size(); ++i) {
    if (i) {
      client.print(',');
    }
    sendSampleJson(client, samples[i]);
  }
  client.print(']');
}

static void handleDeleteAllSamples() {
  if (!initSd()) {
    server.send(500, "application/json", "{\"error\":\"SD init failed\"}");
    return;
  }
  std::vector<WebSample> empty;
  if (!writeSamples(empty)) {
    server.send(500, "application/json", "{\"error\":\"Write failed\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleExportCsv() {
  std::vector<WebSample> samples;
  if (!loadSamples(samples)) {
    server.send(500, "application/json", "{\"error\":\"SD init failed\"}");
    return;
  }

  String csv = "\xEF\xBB\xBFID,Дата,Время,Температура,Широта,Долгота,Заметка\r\n";
  for (const WebSample& sample : samples) {
    String note = sample.note;
    note.replace("\"", "\"\"");
    csv += sample.id + "," + sample.date + "," + sample.time + "," +
           (sample.tempValid ? sample.temp : "") + "," +
           (sample.fixValid ? sample.lat : "") + "," +
           (sample.fixValid ? sample.lon : "") + ",\"" + note + "\"\r\n";
  }
  server.send(200, "text/csv; charset=utf-8", csv);
}

static void handleExportJson() {
  std::vector<WebSample> samples;
  if (!loadSamples(samples)) {
    server.send(500, "application/json", "{\"error\":\"SD init failed\"}");
    return;
  }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  WiFiClient client = server.client();
  client.print("{\"samples\":[");
  for (size_t i = 0; i < samples.size(); ++i) {
    if (i) {
      client.print(',');
    }
    sendSampleJson(client, samples[i]);
  }
  client.print("]}");
}

static void handleGetSettings() {
  String json = "{\"expedition\":\"";
  json += jsonEscape(webExpedition);
  json += "\",\"prefix\":\"";
  json += jsonEscape(webPrefix);
  json += "\",\"printerOk\":true}";
  server.send(200, "application/json", json);
}

static void handlePostSettings() {
  if (server.hasArg("plain")) {
    const String body = server.arg("plain");
    const int expKey = body.indexOf("\"expedition\"");
    const int prefixKey = body.indexOf("\"prefix\"");
    if (expKey >= 0) {
      const int firstQuote = body.indexOf('\"', body.indexOf(':', expKey) + 1);
      const int secondQuote = body.indexOf('\"', firstQuote + 1);
      if (firstQuote >= 0 && secondQuote > firstQuote) {
        webExpedition = body.substring(firstQuote + 1, secondQuote);
      }
    }
    if (prefixKey >= 0) {
      const int firstQuote = body.indexOf('\"', body.indexOf(':', prefixKey) + 1);
      const int secondQuote = body.indexOf('\"', firstQuote + 1);
      if (firstQuote >= 0 && secondQuote > firstQuote) {
        webPrefix = body.substring(firstQuote + 1, secondQuote);
      }
    }
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handlePrinterTest() {
  const bool ok = printerModulePrint("TEST", "TEST");
  server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"print failed\"}");
}

static void handlePrinterPrint() {
  const String body = server.arg("plain");
  const int idKey = body.indexOf("\"id\"");
  if (idKey < 0) {
    server.send(400, "application/json", "{\"error\":\"id required\"}");
    return;
  }

  const int firstQuote = body.indexOf('\"', body.indexOf(':', idKey) + 1);
  const int secondQuote = body.indexOf('\"', firstQuote + 1);
  if (firstQuote < 0 || secondQuote <= firstQuote) {
    server.send(400, "application/json", "{\"error\":\"id required\"}");
    return;
  }

  const String id = body.substring(firstQuote + 1, secondQuote);
  std::vector<WebSample> samples;
  if (!loadSamples(samples)) {
    server.send(500, "application/json", "{\"error\":\"SD init failed\"}");
    return;
  }

  const int index = findSampleIndex(samples, id);
  if (index < 0) {
    server.send(404, "application/json", "{\"error\":\"not found\"}");
    return;
  }

  const WebSample& sample = samples[index];
  const bool ok = printerModulePrintSnapshot(
      sample.id.c_str(),
      sample.timestamp.c_str(),
      sample.lat.toDouble(),
      sample.lon.toDouble(),
      sample.temp.toFloat(),
      sample.fixValid,
      sample.tempValid);
  server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"print failed\"}");
}

static void handleUpdateSample() {
  String id = server.uri();
  const String prefix = "/api/samples/";
  if (!id.startsWith(prefix)) {
    server.send(404, "application/json", "{\"error\":\"not found\"}");
    return;
  }
  id.remove(0, prefix.length());

  std::vector<WebSample> samples;
  if (!loadSamples(samples)) {
    server.send(500, "application/json", "{\"error\":\"SD init failed\"}");
    return;
  }

  const int index = findSampleIndex(samples, id);
  if (index < 0) {
    server.send(404, "application/json", "{\"error\":\"not found\"}");
    return;
  }

  WebSample& sample = samples[index];
  const String body = server.arg("plain");
  auto extractString = [&](const char* key, String& target) {
    const String pattern = String("\"") + key + "\"";
    const int keyPos = body.indexOf(pattern);
    if (keyPos < 0) {
      return;
    }
    const int valueStart = body.indexOf('\"', body.indexOf(':', keyPos) + 1);
    const int valueEnd = body.indexOf('\"', valueStart + 1);
    if (valueStart >= 0 && valueEnd > valueStart) {
      target = body.substring(valueStart + 1, valueEnd);
    }
  };

  extractString("id", sample.id);
  extractString("date", sample.date);
  extractString("time", sample.time);
  extractString("temp", sample.temp);
  extractString("lat", sample.lat);
  extractString("lon", sample.lon);
  extractString("note", sample.note);
  sample.timestamp = sample.date;
  sample.timestamp.replace("-", "");
  String compactTime = sample.time;
  compactTime.replace(":", "");
  sample.timestamp += compactTime;
  sample.fixValid = sample.lat.length() && sample.lon.length();
  sample.tempValid = sample.temp.length();

  if (!writeSamples(samples)) {
    server.send(500, "application/json", "{\"error\":\"Write failed\"}");
    return;
  }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  WiFiClient client = server.client();
  sendSampleJson(client, sample);
}

static void handleQrSvg() {
  String data = server.hasArg("data") ? server.arg("data") : "?";
  if (data.length() > 50) {
    data = data.substring(0, 50);
  }

  const int version = 3;
  const int cell = 6;
  const int quiet = 4;
  QRCode qr;
  uint8_t qrBuffer[qrcode_getBufferSize(version)];
  qrcode_initText(&qr, qrBuffer, version, ECC_MEDIUM, data.c_str());

  const int size = (qr.size + quiet * 2) * cell;
  String svg = "<svg xmlns='http://www.w3.org/2000/svg' width='" + String(size) +
               "' height='" + String(size) + "'><rect width='" + String(size) +
               "' height='" + String(size) + "' fill='white'/>";
  for (int y = 0; y < qr.size; ++y) {
    for (int x = 0; x < qr.size; ++x) {
      if (qrcode_getModule(&qr, x, y)) {
        svg += "<rect x='" + String((x + quiet) * cell) + "' y='" + String((y + quiet) * cell) +
               "' width='" + String(cell) + "' height='" + String(cell) + "' fill='black'/>";
      }
    }
  }
  svg += "</svg>";
  server.send(200, "image/svg+xml", svg);
}

static void handleNotFound() {
  if (server.method() == HTTP_PUT && server.uri().startsWith("/api/samples/")) {
    handleUpdateSample();
    return;
  }
  if (server.uri().startsWith("/api/")) {
    server.send(404, "application/json", "{\"error\":\"not found\"}");
    return;
  }
  handleStaticFiles();
}

static void startWebServer() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Web AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/index.html", HTTP_GET, []() {
    if (!streamFromSd("/www/index.html")) server.send(404, "text/plain", "index missing");
  });
  server.on("/styles.css", HTTP_GET, []() {
    if (!streamFromSd("/www/styles.css")) server.send(404, "text/plain", "styles missing");
  });
  server.on("/app.js", HTTP_GET, []() {
    if (!streamFromSd("/www/app.js")) server.send(404, "text/plain", "app missing");
  });
  server.on("/samples.csv", HTTP_GET, handleGetSamplesCsv);
  server.on("/samples.csv", HTTP_POST, handlePostSamplesCsv);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/records", HTTP_GET, handleGetRecords);
  server.on("/api/samples", HTTP_GET, handleGetSamples);
  server.on("/api/samples/all", HTTP_DELETE, handleDeleteAllSamples);
  server.on("/api/export/csv", HTTP_GET, handleExportCsv);
  server.on("/api/export/json", HTTP_GET, handleExportJson);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/printer/test", HTTP_POST, handlePrinterTest);
  server.on("/api/printer/print", HTTP_POST, handlePrinterPrint);
  server.on("/api/qr", HTTP_GET, handleQrSvg);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server started");
}

static void generateId5(char* out, size_t outSize) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  if (outSize < 6) {
    return;
  }
  for (size_t i = 0; i < 5; i++) {
    out[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
  }
  out[5] = '\0';
}

static void formatSnapshotTs(char* out, size_t outSize, uint32_t nowMs) {
  if (gps.date.isValid() && gps.time.isValid()) {
    snprintf(out, outSize, "%04d%02d%02d%02d%02d%02d",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());
  } else {
    snprintf(out, outSize, "MS%010lu", (unsigned long)nowMs);
  }
}

static void onSampleButtonPressed(uint32_t nowMs) {
  char id[6];
  char ts[16];
  char qr[192];
  generateId5(id, sizeof(id));
  formatSnapshotTs(ts, sizeof(ts), nowMs);

  const bool hasFix = gps.location.isValid() && gps.location.age() < 5000;
  const double lat = hasFix ? gps.location.lat() : 0.0;
  const double lon = hasFix ? gps.location.lng() : 0.0;
  const bool hasTemp = tempSensorOk && !isnan(lastTempC);
  const float tempC = hasTemp ? lastTempC : 0.0f;

  const bool saved = appendSnapshotToSd(id, ts, lat, lon, tempC, hasFix, hasTemp, nowMs);
  Serial.printf("Snapshot SD %s: %s\n", saved ? "saved" : "not_saved", id);

  Serial.printf("Sample press: %s\n", id);
  printerModulePrintSnapshot(id, ts, lat, lon, tempC, hasFix, hasTemp);
}

static void triggerButtonAction(uint32_t nowMs) {
  if (buttonClickCount == 1) {
    Serial.println("Button single click: capture sample");
    onSampleButtonPressed(nowMs);
  } else if (buttonClickCount >= 2) {
    Serial.println("Button double click: calibrate printer");
    printerModuleCalibrate();
  }
  buttonClickCount = 0;
  firstClickMs = 0;
}

static void handleButton(uint32_t nowMs) {
  if (nowMs - lastButtonSampleMs < SAMPLE_MS) {
    return;
  }
  lastButtonSampleMs = nowMs;

  const uint8_t raw = digitalRead(BUTTON_PIN);
  if (raw == buttonLastRaw) {
    if (buttonSameCount < 65535) {
      buttonSameCount++;
    }
  } else {
    buttonLastRaw = raw;
    buttonSameCount = 1;
  }

  if (buttonStable == HIGH && raw == LOW && buttonSameCount >= PRESS_STABLE_SAMPLES) {
    buttonStable = LOW;
    return;
  }
  if (buttonStable == LOW && raw == HIGH && buttonSameCount >= RELEASE_STABLE_SAMPLES) {
    buttonStable = HIGH;
    if (buttonClickCount == 0 || nowMs - firstClickMs > DOUBLE_CLICK_MS) {
      buttonClickCount = 1;
      firstClickMs = nowMs;
    } else {
      buttonClickCount++;
      triggerButtonAction(nowMs);
    }
    return;
  }

  if (buttonStable == HIGH && buttonClickCount == 1 && nowMs - firstClickMs > DOUBLE_CLICK_MS) {
    triggerButtonAction(nowMs);
  }
}

static void dispFlush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorP) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t*)&colorP->full, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

static void makeUi() {
  lv_obj_t* screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x0F141A), LV_PART_MAIN);

  panel = lv_obj_create(screen);
  lv_obj_set_size(panel, 120, 146);
  lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_text_font(panel, &lv_font_ru_14, LV_PART_MAIN);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x151B22), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(panel, LV_OPA_90, LV_PART_MAIN);
  lv_obj_set_style_border_color(panel, lv_color_hex(0x2B3948), LV_PART_MAIN);
  lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(panel, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_top(panel, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_row(panel, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_left(panel, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_right(panel, 6, LV_PART_MAIN);
  lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lblTime = lv_label_create(panel);
  lv_label_set_text(lblTime, "Дата --.--.--\nВремя --:--:--");
  lv_obj_set_width(lblTime, 108);
  lv_label_set_long_mode(lblTime, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(lblTime, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_font(lblTime, &lv_font_ru_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(lblTime, lv_color_hex(0xBCE7FF), LV_PART_MAIN);

  lblCoords = lv_label_create(panel);
  lv_label_set_text(lblCoords, "Шир --\nДолг --");
  lv_obj_set_width(lblCoords, 108);
  lv_label_set_long_mode(lblCoords, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(lblCoords, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(lblCoords, lv_color_hex(0xEAF2F7), LV_PART_MAIN);

  lblTemp = lv_label_create(panel);
  lv_label_set_text(lblTemp, "Темп --.-C");
  lv_obj_set_width(lblTemp, 108);
  lv_label_set_long_mode(lblTemp, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(lblTemp, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(lblTemp, lv_color_hex(0x8BE9A9), LV_PART_MAIN);
}

static void readGps() {
  while (Serial2.available()) {
    const char c = (char)Serial2.read();
    gps.encode(c);
  }
}

static void readWaterTemp(uint32_t nowMs) {
  if (nowMs - lastTempMs < 1500) {
    return;
  }

  lastTempMs = nowMs;
  const int deviceCount = waterTemp.getDeviceCount();
  Serial.printf("[TEMP] devices=%d sensorOk=%s failStreak=%u\n",
                deviceCount,
                tempSensorOk ? "true" : "false",
                tempReadFailStreak);

  waterTemp.requestTemperatures();
  float t = waterTemp.getTempCByIndex(0);
  Serial.printf("[TEMP] raw=%.4f disconnectedConst=%.4f\n", t, DEVICE_DISCONNECTED_C);
  if (t != DEVICE_DISCONNECTED_C && t > -55.0f && t < 125.0f) {
    lastTempC = t;
    tempSensorOk = true;
    tempReadFailStreak = 0;
    Serial.printf("[TEMP] valid=true value=%.2fC\n", lastTempC);
  } else {
    if (tempReadFailStreak < 255) {
      tempReadFailStreak++;
    }
    if (tempReadFailStreak >= 3) {
      tempSensorOk = false;
    }
    Serial.printf("[TEMP] valid=false reason=%s failStreak=%u\n",
                  (t == DEVICE_DISCONNECTED_C) ? "device_disconnected" :
                  (t <= -55.0f) ? "too_low" :
                  (t >= 125.0f) ? "too_high" : "unknown",
                  tempReadFailStreak);
  }
}

static void updateUi(uint32_t nowMs) {
  if (nowMs - lastUiMs < 250) {
    return;
  }
  lastUiMs = nowMs;

  const bool hasFix = gps.location.isValid() && gps.location.age() < 3000;

  if (hasFix) {
    char coordBuf[64];
    snprintf(coordBuf, sizeof(coordBuf), "Шир %.5f\nДолг %.5f",
             gps.location.lat(), gps.location.lng());
    lv_label_set_text(lblCoords, coordBuf);

  } else {
    lv_label_set_text(lblCoords, "Шир --\nДолг --");
  }

  if (gps.time.isValid()) {
    const int localHour = gps.time.hour();
    const int localMinute = gps.time.minute();
    const int localSecond = gps.time.second();
    const char* label = "Время";
    if (gps.date.isValid()) {
      char timeBuf[48];
      snprintf(timeBuf, sizeof(timeBuf), "Дата %02d.%02d.%02d\n%s %02d:%02d:%02d",
               gps.date.day(), gps.date.month(), gps.date.year() % 100,
               label, localHour, localMinute, localSecond);
      lv_label_set_text(lblTime, timeBuf);
    } else {
      char timeBuf[32];
      snprintf(timeBuf, sizeof(timeBuf), "%s %02d:%02d:%02d",
               label, localHour, localMinute, localSecond);
      lv_label_set_text(lblTime, timeBuf);
    }
  } else {
    lv_label_set_text(lblTime, "Дата --.--.--\nВремя --:--:--");
  }

  if (!tempSensorOk) {
    lv_label_set_text(lblTemp, "Темп Н/Д");
  } else if (isnan(lastTempC)) {
    lv_label_set_text(lblTemp, "Темп --.-C");
  } else {
    char tempBuf[32];
    snprintf(tempBuf, sizeof(tempBuf), "Темп %.1fC", lastTempC);
    lv_label_set_text(lblTemp, tempBuf);
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Keep SD deselected on shared SPI bus so TFT can initialize reliably.
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);

  tft.init();
  tft.setRotation(0);
  tft.setSwapBytes(true);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  #ifdef TFT_BACKLIGHT_ON
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
  #else
  digitalWrite(TFT_BL, HIGH);
  #endif
#endif

  lv_init();
  lv_disp_draw_buf_init(&drawBuf, buf, nullptr, 128 * 20);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = 128;
  dispDrv.ver_res = 160;
  dispDrv.flush_cb = dispFlush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  waterTemp.begin();
  tempSensorOk = (waterTemp.getDeviceCount() > 0);
  Serial.printf("[TEMP] startup devices=%d sensorOk=%s pin=%d\n",
                waterTemp.getDeviceCount(),
                tempSensorOk ? "true" : "false",
                WATER_TEMP_PIN);
  printerModuleInit();
  initSd();
  startWebServer();

  makeUi();
  lastTickMs = millis();
  lastUiMs = 0;
  lastTempMs = 0;
}

void loop() {
  const uint32_t now = millis();
  readGps();
  readWaterTemp(now);
  updateUi(now);
  printerModuleTick();
  handleButton(now);
  server.handleClient();

  lv_tick_inc(now - lastTickMs);
  lastTickMs = now;
  lv_timer_handler();
  delay(5);
}
