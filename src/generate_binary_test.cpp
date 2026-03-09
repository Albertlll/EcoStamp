#include <Arduino.h>
#include <NimBLEDevice.h>
#include <TinyGPSPlus.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SPI.h>
#include <SD.h>
#include <cstring>
#include <vector>
#include "qrcode.h"

#define PAPER_WIDTH 384
#define LINE_BYTES (PAPER_WIDTH / 8)
#define FONT_W 5
#define FONT_H 7
#define FONT_SCALE 3
#define CHAR_SPACING 2
#define MAX_ITEMS 4
#define BUTTON_PIN 21
#define SAMPLE_MS 1
#define PRESS_STABLE_SAMPLES 12
#define RELEASE_STABLE_SAMPLES 12
#define GPS_RX 16
#define GPS_TX 17
#define WATER_TEMP_PIN 26
#define SD_CS_PIN 13

static const char* PRINTER_ADDRESS = "7D:11:28:1B:CF:6B";
static const char* PRINTER_SERVICE_UUID = "0000ae30-0000-1000-8000-00805f9b34fb";
static const char* PRINTER_TX_UUID = "0000ae01-0000-1000-8000-00805f9b34fb";
static const char* PRINTER_RX_UUID = "0000ae02-0000-1000-8000-00805f9b34fb";

static const uint8_t DATA_FLOW_PAUSE[] = {0x51, 0x78, 0xAE, 0x01, 0x01, 0x00, 0x10, 0x70, 0xFF};
static const uint8_t DATA_FLOW_RESUME[] = {0x51, 0x78, 0xAE, 0x01, 0x01, 0x00, 0x00, 0x00, 0xFF};

static std::vector<uint8_t> g_payload;
static volatile bool g_paused = false;
static bool g_isPrinting = false;
static bool g_sdReady = false;

static TinyGPSPlus gps;
static OneWire oneWire(WATER_TEMP_PIN);
static DallasTemperature waterTemp(&oneWire);

struct SampleSnapshot {
  char id[6];
  char ts[16];
  bool hasFix;
  double lat;
  double lon;
  bool hasTemp;
  float tempC;
  uint32_t capturedMs;
};

void generateId5(char* out, size_t outSize) {
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  if (outSize < 6) {
    return;
  }
  for (size_t i = 0; i < 5; i++) {
    out[i] = kAlphabet[esp_random() % (sizeof(kAlphabet) - 1)];
  }
  out[5] = '\0';
}

void readGpsStream() {
  while (Serial2.available()) {
    gps.encode((char)Serial2.read());
  }
}

void formatTimestamp(char* out, size_t outSize) {
  if (gps.date.isValid() && gps.time.isValid()) {
    snprintf(out, outSize, "%04d%02d%02d%02d%02d%02d",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());
  } else {
    snprintf(out, outSize, "MS%010lu", (unsigned long)millis());
  }
}

SampleSnapshot captureSnapshot() {
  SampleSnapshot s{};
  generateId5(s.id, sizeof(s.id));
  formatTimestamp(s.ts, sizeof(s.ts));
  s.capturedMs = millis();

  s.hasFix = gps.location.isValid() && gps.location.age() < 5000;
  if (s.hasFix) {
    s.lat = gps.location.lat();
    s.lon = gps.location.lng();
  } else {
    s.lat = 0.0;
    s.lon = 0.0;
  }

  waterTemp.requestTemperatures();
  const float t = waterTemp.getTempCByIndex(0);
  s.hasTemp = (t != DEVICE_DISCONNECTED_C && t > -55.0f && t < 125.0f);
  s.tempC = s.hasTemp ? t : 0.0f;

  return s;
}

void buildQrPayload(const SampleSnapshot& s, char* out, size_t outSize) {
  const char fixFlag = s.hasFix ? '1' : '0';
  const char tempFlag = s.hasTemp ? '1' : '0';
  snprintf(out, outSize, "ECO|%s|%s|%c|%.5f|%.5f|%c|%.1f",
           s.id, s.ts, fixFlag, s.lat, s.lon, tempFlag, s.tempC);
}

bool initSdIfNeeded() {
  if (g_sdReady) {
    return true;
  }
  g_sdReady = SD.begin(SD_CS_PIN);
  if (!g_sdReady) {
    Serial.println("SD init failed");
    return false;
  }
  Serial.println("SD ready");
  return true;
}

bool appendSnapshotToSd(const SampleSnapshot& s) {
  if (!initSdIfNeeded()) {
    return false;
  }

  if (!SD.exists("/samples.csv")) {
    File headerFile = SD.open("/samples.csv", FILE_WRITE);
    if (!headerFile) {
      Serial.println("SD header open failed");
      return false;
    }
    headerFile.println("id,timestamp,lat,lon,temp_c,fix_valid,temp_valid,captured_ms");
    headerFile.close();
  }

  File f = SD.open("/samples.csv", FILE_APPEND);
  if (!f) {
    Serial.println("SD append open failed");
    return false;
  }

  char line[192];
  snprintf(line, sizeof(line), "%s,%s,%.6f,%.6f,%.2f,%u,%u,%lu",
           s.id, s.ts, s.lat, s.lon, s.tempC,
           s.hasFix ? 1 : 0, s.hasTemp ? 1 : 0, (unsigned long)s.capturedMs);
  f.println(line);
  f.close();
  return true;
}

static const uint8_t FONT_5X7[][5] PROGMEM = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
  {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
  {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
  {0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
  {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
  {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
  {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
  {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
  {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
  {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
  {0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
  {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
  {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
  {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
  {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
  {0x00,0x41,0x36,0x08,0x00},{0x10,0x08,0x08,0x10,0x08},{0x00,0x00,0x00,0x00,0x00},
};

static const uint8_t CRC8_TABLE[256] PROGMEM = {
  0x00,0x07,0x0e,0x09,0x1c,0x1b,0x12,0x15,0x38,0x3f,0x36,0x31,
  0x24,0x23,0x2a,0x2d,0x70,0x77,0x7e,0x79,0x6c,0x6b,0x62,0x65,
  0x48,0x4f,0x46,0x41,0x54,0x53,0x5a,0x5d,0xe0,0xe7,0xee,0xe9,
  0xfc,0xfb,0xf2,0xf5,0xd8,0xdf,0xd6,0xd1,0xc4,0xc3,0xca,0xcd,
  0x90,0x97,0x9e,0x99,0x8c,0x8b,0x82,0x85,0xa8,0xaf,0xa6,0xa1,
  0xb4,0xb3,0xba,0xbd,0xc7,0xc0,0xc9,0xce,0xdb,0xdc,0xd5,0xd2,
  0xff,0xf8,0xf1,0xf6,0xe3,0xe4,0xed,0xea,0xb7,0xb0,0xb9,0xbe,
  0xab,0xac,0xa5,0xa2,0x8f,0x88,0x81,0x86,0x93,0x94,0x9d,0x9a,
  0x27,0x20,0x29,0x2e,0x3b,0x3c,0x35,0x32,0x1f,0x18,0x11,0x16,
  0x03,0x04,0x0d,0x0a,0x57,0x50,0x59,0x5e,0x4b,0x4c,0x45,0x42,
  0x6f,0x68,0x61,0x66,0x73,0x74,0x7d,0x7a,0x89,0x8e,0x87,0x80,
  0x95,0x92,0x9b,0x9c,0xb1,0xb6,0xbf,0xb8,0xad,0xaa,0xa3,0xa4,
  0xf9,0xfe,0xf7,0xf0,0xe5,0xe2,0xeb,0xec,0xc1,0xc6,0xcf,0xc8,
  0xdd,0xda,0xd3,0xd4,0x69,0x6e,0x67,0x60,0x75,0x72,0x7b,0x7c,
  0x51,0x56,0x5f,0x58,0x4d,0x4a,0x43,0x44,0x19,0x1e,0x17,0x10,
  0x05,0x02,0x0b,0x0c,0x21,0x26,0x2f,0x28,0x3d,0x3a,0x33,0x34,
  0x4e,0x49,0x40,0x47,0x52,0x55,0x5c,0x5b,0x76,0x71,0x78,0x7f,
  0x6a,0x6d,0x64,0x63,0x3e,0x39,0x30,0x37,0x22,0x25,0x2c,0x2b,
  0x06,0x01,0x08,0x0f,0x1a,0x1d,0x14,0x13,0xae,0xa9,0xa0,0xa7,
  0xb2,0xb5,0xbc,0xbb,0x96,0x91,0x98,0x9f,0x8a,0x8d,0x84,0x83,
  0xde,0xd9,0xd0,0xd7,0xc2,0xc5,0xcc,0xcb,0xe6,0xe1,0xe8,0xef,
  0xfa,0xfd,0xf4,0xf3
};

// QR хранится побитово: (size*size+7)/8 байт
// version 2 = 25x25 = 625 бит = 79 байт
#define QR_VERSION 4
#define QR_SIZE (17 + QR_VERSION * 4)
#define QR_BUF_BYTES ((QR_SIZE * QR_SIZE + 7) / 8)

static uint8_t qr_bitbuf[QR_BUF_BYTES];
static uint8_t qr_size_actual = 0;

bool generate_qr(const char* data) {
  QRCode qr;
  uint8_t qrbuf[qrcode_getBufferSize(QR_VERSION)];
  if (qrcode_initText(&qr, qrbuf, QR_VERSION, ECC_MEDIUM, data) != 0)
    return false;
  qr_size_actual = qr.size;
  memset(qr_bitbuf, 0, QR_BUF_BYTES);
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y)) {
        uint16_t bit = y * qr.size + x;
        qr_bitbuf[bit >> 3] |= (0x80 >> (bit & 7));
      }
  return true;
}

bool qr_get(uint8_t x, uint8_t y) {
  uint16_t bit = (uint16_t)y * qr_size_actual + x;
  return (qr_bitbuf[bit >> 3] >> (7 - (bit & 7))) & 1;
}

// ---- items ----

struct TextItem {
  int16_t x, y;
  const char* str;
  uint8_t scale;
  bool rotated;
};

struct QrItem {
  int16_t x, y;
  uint8_t pixel_size;
};

static TextItem text_items[MAX_ITEMS];
static uint8_t  text_count = 0;
static QrItem   qr_items[MAX_ITEMS];
static uint8_t  qr_count = 0;

void reset_items() { text_count = 0; qr_count = 0; }

uint16_t string_pixel_width(const char* str, uint8_t scale) {
  uint16_t len = strlen(str);
  if (!len) return 0;
  return len * (FONT_W + CHAR_SPACING) * scale - CHAR_SPACING * scale;
}

void add_text(int16_t x, int16_t y, const char* str, uint8_t scale, bool rotated) {
  if (text_count >= MAX_ITEMS) return;
  text_items[text_count++] = {x, y, str, scale, rotated};
}

void add_text_centered(uint16_t y, const char* str, uint8_t scale) {
  int16_t x = (PAPER_WIDTH - (int16_t)string_pixel_width(str, scale)) / 2;
  add_text(x, y, str, scale, false);
}

void add_text_rotated(int16_t x, int16_t y, const char* str, uint8_t scale) {
  add_text(x, y, str, scale, true);
}

void add_qr(int16_t x, int16_t y, uint8_t pixel_size) {
  if (qr_count >= MAX_ITEMS) return;
  qr_items[qr_count++] = {x, y, pixel_size};
}

void add_qr_centered(uint16_t y, uint8_t pixel_size) {
  int16_t x = (PAPER_WIDTH - (int16_t)(qr_size_actual * pixel_size)) / 2;
  add_qr(x, y, pixel_size);
}

// ---- protocol ----

uint8_t crc8(const uint8_t* data, uint16_t len) {
  uint8_t crc = 0;
  for (uint16_t i = 0; i < len; i++)
    crc = pgm_read_byte(&CRC8_TABLE[(crc ^ data[i]) & 0xFF]);
  return crc;
}

uint8_t reverse_bits(uint8_t b) {
  b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1);
  b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2);
  return ((b & 0xF0) >> 4) | ((b & 0x0F) << 4);
}

void send_command(uint8_t cmd, const uint8_t* payload, uint16_t plen) {
  uint8_t hdr[6] = {0x51,0x78,cmd,0x00,(uint8_t)(plen&0xFF),0x00};
  g_payload.insert(g_payload.end(), hdr, hdr + sizeof(hdr));
  g_payload.insert(g_payload.end(), payload, payload + plen);
  g_payload.push_back(crc8(payload, plen));
  g_payload.push_back(0xFF);
}

void send_command_1(uint8_t cmd, uint8_t val)    { send_command(cmd, &val, 1); }
void send_command_2(uint8_t cmd, uint16_t val)   {
  uint8_t b[2] = {(uint8_t)(val&0xFF),(uint8_t)(val>>8)};
  send_command(cmd, b, 2);
}

// ---- render ----

void canvas_set_px(uint8_t* row_buf, int16_t px) {
  if (px >= 0 && px < PAPER_WIDTH)
    row_buf[px >> 3] |= (0x80 >> (px & 7));
}

void render_row(uint8_t* row_buf, uint16_t y) {
  memset(row_buf, 0, LINE_BYTES);

  for (uint8_t t = 0; t < text_count; t++) {
    const TextItem& ti = text_items[t];
    if (!ti.rotated) {
      int16_t rel_y = (int16_t)y - ti.y;
      if (rel_y < 0 || rel_y >= FONT_H * (int16_t)ti.scale) continue;
      uint8_t font_row = rel_y / ti.scale;
      int16_t cx = ti.x;
      for (const char* p = ti.str; *p; p++, cx += (FONT_W + CHAR_SPACING) * ti.scale) {
        if (*p < 32 || *p > 127) continue;
        uint8_t idx = *p - 32;
        for (uint8_t col = 0; col < FONT_W; col++) {
          if (pgm_read_byte(&FONT_5X7[idx][col]) & (1 << font_row))
            for (uint8_t sx = 0; sx < ti.scale; sx++)
              canvas_set_px(row_buf, cx + col * ti.scale + sx);
        }
      }
    } else {
      for (uint16_t ci = 0; ci < strlen(ti.str); ci++) {
        char c = ti.str[ci];
        if (c < 32 || c > 127) continue;
        int16_t char_y_start = ti.y + ci * (FONT_W + CHAR_SPACING) * (int16_t)ti.scale;
        int16_t rel_y = (int16_t)y - char_y_start;
        if (rel_y < 0 || rel_y >= FONT_W * (int16_t)ti.scale) continue;
        uint8_t font_col = rel_y / ti.scale;
        uint8_t coldata = pgm_read_byte(&FONT_5X7[c - 32][font_col]);
        for (uint8_t row = 0; row < FONT_H; row++)
          if (coldata & (1 << row))
            for (uint8_t sx = 0; sx < ti.scale; sx++)
              canvas_set_px(row_buf, ti.x + (FONT_H - 1 - row) * ti.scale + sx);
      }
    }
  }

  for (uint8_t q = 0; q < qr_count; q++) {
    const QrItem& qi = qr_items[q];
    int16_t rel_y = (int16_t)y - qi.y;
    if (rel_y < 0 || rel_y >= (int16_t)(qr_size_actual * qi.pixel_size)) continue;
    uint8_t qr_row = rel_y / qi.pixel_size;
    for (uint8_t qx = 0; qx < qr_size_actual; qx++)
      if (qr_get(qx, qr_row))
        for (uint8_t ps = 0; ps < qi.pixel_size; ps++)
          canvas_set_px(row_buf, qi.x + qx * qi.pixel_size + ps);
  }
}

void send_row(uint8_t* row_buf) {
  uint8_t out[LINE_BYTES];
  for (uint8_t i = 0; i < LINE_BYTES; i++)
    out[i] = reverse_bits(row_buf[i]);
  send_command(0xA2, out, LINE_BYTES);
}

void print_label(uint16_t label_height, uint16_t energy) {
  uint8_t row_buf[LINE_BYTES];
  uint8_t p = 0x00;
  const uint8_t raw_init[] = {0x51,0x78,0xA3,0x00,0x01,0x00,0x00,0x00,0xFF};
  const uint8_t p_start[]  = {0xAA,0x55,0x17,0x38,0x44,0x5F,0x5F,0x5F,0x44,0x38,0x2C};
  const uint8_t p_end[]    = {0xAA,0x55,0x17,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x17};

  send_command(0xA3, &p, 1);
  g_payload.insert(g_payload.end(), raw_init, raw_init + sizeof(raw_init));
  send_command_1(0xA4, 50);
  send_command_2(0xAF, energy);
  send_command_1(0xBE, 0x01);
  send_command_1(0xA9, 0x01);
  send_command(0xA6, p_start, sizeof(p_start));

  for (uint16_t y = 0; y < label_height; y++) {
    render_row(row_buf, y);
    send_row(row_buf);
  }

  send_command(0xA6, p_end, sizeof(p_end));
  send_command(0xA3, &p, 1);
}

static void onNotify(
    NimBLERemoteCharacteristic*,
    uint8_t* data,
    size_t length,
    bool) {
  if (length == sizeof(DATA_FLOW_PAUSE) && memcmp(data, DATA_FLOW_PAUSE, length) == 0) {
    g_paused = true;
  } else if (length == sizeof(DATA_FLOW_RESUME) && memcmp(data, DATA_FLOW_RESUME, length) == 0) {
    g_paused = false;
  }
}

class BleLabelPrinter {
 public:
  BleLabelPrinter(const char* address, const char* service_uuid, const char* tx_uuid, const char* rx_uuid)
      : address_(address), service_uuid_(service_uuid), tx_uuid_(tx_uuid), rx_uuid_(rx_uuid) {}

  bool begin() {
    return ensureConnected(true);
  }

  void tick() {
    if (g_isPrinting) {
      return;
    }
    if (isConnected()) {
      return;
    }
    const uint32_t now = millis();
    if (now - lastReconnectMs_ < 3000) {
      return;
    }
    lastReconnectMs_ = now;
    ensureConnected(false);
  }

  bool printQrAndId(const char* qr_text, const char* id_text) {
    if (!buildPayload(qr_text, id_text)) {
      return false;
    }
    if (!ensureConnected(true)) {
      Serial.println("BLE not connected, print skipped");
      return false;
    }
    if (sendPayload()) {
      return true;
    }

    Serial.println("Print retry: reconnect");
    if (client_ != nullptr) {
      client_->disconnect();
    }
    txChar_ = nullptr;
    rxChar_ = nullptr;
    delay(120);

    if (!ensureConnected(true)) {
      return false;
    }
    return sendPayload();
  }

  void clearLocalQueue() {
    g_payload.clear();
  }

 private:
  bool isConnected() const {
    return client_ != nullptr && client_->isConnected() && txChar_ != nullptr;
  }

  bool ensureConnected(bool verbose) {
    if (isConnected()) {
      return true;
    }

    if (client_ == nullptr) {
      client_ = NimBLEDevice::getDisconnectedClient();
      if (client_ == nullptr) {
        client_ = NimBLEDevice::createClient();
      }
      if (client_ == nullptr) {
        if (verbose) {
          Serial.println("BLE client create failed");
        }
        return false;
      }
      client_->setConnectTimeout(10);
      client_->setConnectionParams(24, 48, 0, 200);
    }

    bool connected = false;
    if (verbose) {
      Serial.println("BLE connect try: public");
    }
    connected = client_->connect(NimBLEAddress(address_, BLE_ADDR_PUBLIC));
    if (!connected) {
      if (verbose) {
        Serial.println("BLE connect try: random");
      }
      connected = client_->connect(NimBLEAddress(address_, BLE_ADDR_RANDOM));
    }
    if (!connected) {
      if (verbose) {
        Serial.println("BLE connect failed (public + random)");
      }
      client_->disconnect();
      txChar_ = nullptr;
      rxChar_ = nullptr;
      return false;
    }

    NimBLERemoteService* service = client_->getService(service_uuid_);
    if (service == nullptr) {
      if (verbose) {
        Serial.println("BLE service not found");
      }
      client_->disconnect();
      txChar_ = nullptr;
      rxChar_ = nullptr;
      return false;
    }

    txChar_ = service->getCharacteristic(tx_uuid_);
    rxChar_ = service->getCharacteristic(rx_uuid_);
    if (txChar_ == nullptr || rxChar_ == nullptr) {
      if (verbose) {
        Serial.println("BLE characteristics not found");
      }
      client_->disconnect();
      txChar_ = nullptr;
      rxChar_ = nullptr;
      return false;
    }

    bool subscribed = false;
    if (rxChar_->canNotify()) {
      subscribed = rxChar_->subscribe(true, onNotify);
      if (verbose) {
        Serial.printf("BLE subscribe notify: %s\n", subscribed ? "ok" : "fail");
      }
    }
    if (!subscribed && rxChar_->canIndicate()) {
      subscribed = rxChar_->subscribe(false, onNotify);
      if (verbose) {
        Serial.printf("BLE subscribe indicate: %s\n", subscribed ? "ok" : "fail");
      }
    }
    if (!subscribed && verbose) {
      Serial.println("BLE subscribe skipped (no notify/indicate)");
    }

    const uint16_t mtu = client_->getMTU();
    chunkSize_ = 20;
    if (mtu > 3) {
      chunkSize_ = mtu - 3;
      if (chunkSize_ > 120) {
        chunkSize_ = 120;
      }
    }
    if (verbose) {
      Serial.printf("BLE connected, MTU: %u, chunk: %u\n", mtu, (unsigned)chunkSize_);
    }
    return true;
  }

  bool buildPayload(const char* qr_text, const char* id_text) {
    reset_items();
    if (!generate_qr(qr_text)) {
      Serial.println("QR generation failed");
      return false;
    }

    add_qr_centered(10, 4);
    add_text_rotated(4, 10, id_text, FONT_SCALE);

    g_payload.clear();
    g_payload.reserve(16000);
    print_label(240, 0x6000);
    return true;
  }

  bool sendPayload() {
    g_isPrinting = true;
    g_paused = false;
    for (size_t i = 0; i < g_payload.size(); i += chunkSize_) {
      uint32_t pauseStart = millis();
      while (g_paused) {
        if (millis() - pauseStart > 8000) {
          Serial.println("BLE flow pause timeout, continue");
          g_paused = false;
          break;
        }
        delay(20);
      }

      const size_t chunk = (g_payload.size() - i > chunkSize_) ? chunkSize_ : (g_payload.size() - i);
      bool ok = txChar_->writeValue(&g_payload[i], chunk, false);
      if (!ok) {
        delay(12);
        ok = txChar_->writeValue(&g_payload[i], chunk, true);
      }
      if (!ok) {
        Serial.printf("BLE write failed at offset %u\n", (unsigned)i);
        client_->disconnect();
        txChar_ = nullptr;
        rxChar_ = nullptr;
        delay(120);
        g_isPrinting = false;
        return false;
      }
      delay(8);
    }

    delay(500);
    g_isPrinting = false;
    return true;
  }

  const char* address_;
  const char* service_uuid_;
  const char* tx_uuid_;
  const char* rx_uuid_;
  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* txChar_ = nullptr;
  NimBLERemoteCharacteristic* rxChar_ = nullptr;
  size_t chunkSize_ = 20;
  uint32_t lastReconnectMs_ = 0;
};

static BleLabelPrinter g_printer(
    PRINTER_ADDRESS,
    PRINTER_SERVICE_UUID,
    PRINTER_TX_UUID,
    PRINTER_RX_UUID);

void printerModuleInit() {
  NimBLEDevice::init("");
  NimBLEDevice::setMTU(185);
  delay(200);
  Serial.println("Connecting printer...");
  const bool connected = g_printer.begin();
  g_printer.clearLocalQueue();
  Serial.printf("Printer %s.\n", connected ? "connected" : "not connected");
}

bool printerModulePrint(const char* qrText, const char* idText) {
  if (g_isPrinting) {
    Serial.println("Print busy, skip");
    return false;
  }
  const bool ok = g_printer.printQrAndId(qrText, idText);
  Serial.printf("Print %s\n", ok ? "ok" : "failed");
  return ok;
}

bool printerModuleIsBusy() {
  return g_isPrinting;
}

void printerModuleTick() {
  g_printer.tick();
}
