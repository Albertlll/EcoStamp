#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <TinyGPSPlus.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
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
  server.onNotFound(handleStaticFiles);
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
    onSampleButtonPressed(nowMs);
    return;
  }
  if (buttonStable == LOW && raw == HIGH && buttonSameCount >= RELEASE_STABLE_SAMPLES) {
    buttonStable = HIGH;
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
  waterTemp.requestTemperatures();
  float t = waterTemp.getTempCByIndex(0);
  if (t != DEVICE_DISCONNECTED_C && t > -55.0f && t < 125.0f) {
    lastTempC = t;
    tempSensorOk = true;
    tempReadFailStreak = 0;
  } else {
    if (tempReadFailStreak < 255) {
      tempReadFailStreak++;
    }
    if (tempReadFailStreak >= 3) {
      tempSensorOk = false;
    }
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
