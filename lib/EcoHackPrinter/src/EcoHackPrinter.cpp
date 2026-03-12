#include "EcoHackPrinter.h"

#include <cstring>
#include <vector>

#include "qrcode.h"

namespace ecohack {

namespace {

constexpr uint16_t kPaperWidth = 384;
constexpr uint16_t kLineBytes = kPaperWidth / 8;
constexpr uint8_t kFontWidth = 5;
constexpr uint8_t kFontHeight = 7;
constexpr uint8_t kCharSpacing = 2;

const uint8_t kDataFlowPause[] = {0x51, 0x78, 0xAE, 0x01, 0x01, 0x00, 0x10, 0x70, 0xFF};
const uint8_t kDataFlowResume[] = {0x51, 0x78, 0xAE, 0x01, 0x01, 0x00, 0x00, 0x00, 0xFF};

const uint8_t kLabelTypeBegin[] = {0xAA,0x55,0x17,0x38,0x44,0x5F,0x5F,0x5F,0x44,0x38,0x2C};
const uint8_t kLabelTypeEnd[] = {0xAA,0x55,0x17,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x17};

const uint8_t kFont5x7[][5] PROGMEM = {
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

const uint8_t kCrc8Table[256] PROGMEM = {
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

uint8_t crc8(const uint8_t* data, uint16_t length) {
  uint8_t crc = 0;
  for (uint16_t i = 0; i < length; ++i) {
    crc = pgm_read_byte(&kCrc8Table[(crc ^ data[i]) & 0xFF]);
  }
  return crc;
}

uint8_t reverseBits(uint8_t value) {
  value = ((value & 0xAA) >> 1) | ((value & 0x55) << 1);
  value = ((value & 0xCC) >> 2) | ((value & 0x33) << 2);
  return ((value & 0xF0) >> 4) | ((value & 0x0F) << 4);
}

void canvasSetPixel(uint8_t* rowBuffer, int16_t pixel) {
  if (pixel >= 0 && pixel < static_cast<int16_t>(kPaperWidth)) {
    rowBuffer[pixel >> 3] |= (0x80 >> (pixel & 7));
  }
}

}  // namespace

BleLabelPrinter* BleLabelPrinter::activeInstance_ = nullptr;
bool BleLabelPrinter::nimbleInitialized_ = false;

LabelContent::LabelContent() {
  clear();
}

void LabelContent::clear() {
  textCount_ = 0;
  qrCount_ = 0;
  qrSizeActual_ = 0;
  memset(qrBitBuffer_, 0, sizeof(qrBitBuffer_));
}

bool LabelContent::addText(int16_t x, int16_t y, const String& text, uint8_t scale) {
  if (textCount_ >= maxTextItems()) {
    return false;
  }
  textItems_[textCount_++] = {x, y, text, scale, false};
  return true;
}

bool LabelContent::addCenteredText(uint16_t y, const String& text, uint8_t scale) {
  const int16_t x = (static_cast<int16_t>(kPaperWidth) - static_cast<int16_t>(stringPixelWidth(text, scale))) / 2;
  return addText(x, static_cast<int16_t>(y), text, scale);
}

bool LabelContent::addRotatedText(int16_t x, int16_t y, const String& text, uint8_t scale) {
  if (textCount_ >= maxTextItems()) {
    return false;
  }
  textItems_[textCount_++] = {x, y, text, scale, true};
  return true;
}

bool LabelContent::setQr(int16_t x, int16_t y, const String& data, uint8_t pixelSize) {
  if (!generateQr(data)) {
    return false;
  }
  qrCount_ = 1;
  qrItems_[0] = {x, y, pixelSize};
  return true;
}

bool LabelContent::setQrCentered(uint16_t y, const String& data, uint8_t pixelSize) {
  if (!generateQr(data)) {
    return false;
  }
  const int16_t x = (static_cast<int16_t>(kPaperWidth) - static_cast<int16_t>(qrSizeActual_ * pixelSize)) / 2;
  qrCount_ = 1;
  qrItems_[0] = {x, static_cast<int16_t>(y), pixelSize};
  return true;
}

bool LabelContent::generateQr(const String& data) {
  QRCode qr;
  uint8_t qrBuffer[qrcode_getBufferSize(kQrVersion)];
  if (qrcode_initText(&qr, qrBuffer, kQrVersion, ECC_MEDIUM, data.c_str()) != 0) {
    return false;
  }

  qrSizeActual_ = qr.size;
  memset(qrBitBuffer_, 0, sizeof(qrBitBuffer_));

  for (uint8_t y = 0; y < qr.size; ++y) {
    for (uint8_t x = 0; x < qr.size; ++x) {
      if (qrcode_getModule(&qr, x, y)) {
        const uint16_t bit = y * qr.size + x;
        qrBitBuffer_[bit >> 3] |= (0x80 >> (bit & 7));
      }
    }
  }

  return true;
}

bool LabelContent::qrGet(uint8_t x, uint8_t y) const {
  const uint16_t bit = static_cast<uint16_t>(y) * qrSizeActual_ + x;
  return (qrBitBuffer_[bit >> 3] >> (7 - (bit & 7))) & 1;
}

uint16_t LabelContent::stringPixelWidth(const String& text, uint8_t scale) const {
  if (text.isEmpty()) {
    return 0;
  }
  return text.length() * (kFontWidth + kCharSpacing) * scale - kCharSpacing * scale;
}

BleLabelPrinter::BleLabelPrinter(const PrinterConfig& config) : config_(config) {}

BleLabelPrinter::~BleLabelPrinter() {
  disconnect();
}

bool BleLabelPrinter::begin() {
  if (!nimbleInitialized_) {
    NimBLEDevice::init("");
    NimBLEDevice::setMTU(185);
    nimbleInitialized_ = true;
  }
  return ensureConnected(true);
}

void BleLabelPrinter::tick() {
  if (isPrinting_ || connected()) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastReconnectMs_ < config_.reconnectIntervalMs) {
    return;
  }

  lastReconnectMs_ = now;
  ensureConnected(false);
}

bool BleLabelPrinter::connected() const {
  return client_ != nullptr && client_->isConnected() && txChar_ != nullptr;
}

bool BleLabelPrinter::busy() const {
  return isPrinting_;
}

bool BleLabelPrinter::calibrate(uint16_t feedSteps) {
  if (isPrinting_) {
    Serial.println("[CAL] Printer busy, calibration skipped");
    return false;
  }

  if (feedSteps == 0) {
    feedSteps = config_.feedAfterPrintSteps;
  }

  if (!ensureConnected(true)) {
    Serial.println("[CAL] BLE not connected, calibration skipped");
    return false;
  }

  Serial.printf("[CAL] Calibrating: feed %u steps (0x%04X)...\n", feedSteps, feedSteps);

  payload_.clear();
  sendCommand1(0xA8, 0x00);
  sendCommand(0xA6, kLabelTypeBegin, sizeof(kLabelTypeBegin));

  const uint8_t feed[] = {0x01, static_cast<uint8_t>(feedSteps & 0xFF), static_cast<uint8_t>(feedSteps >> 8)};
  sendCommand(0xF0, feed, sizeof(feed));
  if (!sendPayload()) {
    return false;
  }

  delay(config_.calibrationFeedDelayMs);

  payload_.clear();
  sendCommand(0xA6, kLabelTypeEnd, sizeof(kLabelTypeEnd));
  if (!sendPayload()) {
    return false;
  }

  delay(config_.calibrationSettleDelayMs);
  Serial.println("[CAL] Calibration done.");
  return true;
}

bool BleLabelPrinter::print(const LabelContent& content) {
  if (isPrinting_) {
    Serial.println("[PRINT] Printer busy, print skipped");
    return false;
  }

  if (!ensureConnected(true)) {
    Serial.println("[PRINT] BLE not connected, print skipped");
    return false;
  }

  if (!buildPayload(content)) {
    return false;
  }

  if (sendPayload()) {
    waitForPrintCompletion(lastPrintRows_);
    return true;
  }

  Serial.println("[PRINT] Print retry: reconnect");
  disconnect();
  delay(120);

  if (!ensureConnected(true)) {
    return false;
  }
  if (!sendPayload()) {
    return false;
  }

  waitForPrintCompletion(lastPrintRows_);
  return true;
}

void BleLabelPrinter::disconnect() {
  cleanupClient();
}

void BleLabelPrinter::onNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  if (activeInstance_ != nullptr) {
    activeInstance_->handleNotify(data, length);
  }
}

uint16_t BleLabelPrinter::labelHeightPx(uint16_t labelHeightMm) {
  return static_cast<uint16_t>(labelHeightMm * 203.0 / 25.4 + 0.5);
}

void BleLabelPrinter::handleNotify(uint8_t* data, size_t length) {
  if (length == sizeof(kDataFlowPause) && memcmp(data, kDataFlowPause, length) == 0) {
    paused_ = true;
    return;
  }
  if (length == sizeof(kDataFlowResume) && memcmp(data, kDataFlowResume, length) == 0) {
    paused_ = false;
  }
}

bool BleLabelPrinter::ensureConnected(bool verbose) {
  if (connected()) {
    return true;
  }

  if (config_.address == nullptr || config_.address[0] == '\0') {
    if (verbose) {
      Serial.println("[BLE] Printer address is not configured");
    }
    return false;
  }

  cleanupClient();

  if (verbose) {
    Serial.printf("[BLE] Connecting directly to %s\n", config_.address);
  }

  client_ = NimBLEDevice::createClient();
  client_->setConnectionParams(6, 12, 0, 200);

  if (verbose) {
    Serial.println("[BLE] trying public...");
  }

  const bool ok = client_->connect(NimBLEAddress(config_.address, BLE_ADDR_PUBLIC), 5000);
  if (!ok) {
    if (verbose) {
      Serial.println("[BLE] Direct connect failed");
    }
    cleanupClient();
    return false;
  }

  if (verbose) {
    Serial.printf("[BLE] Connected, MTU=%d\n", client_->getMTU());
  }

  txChar_ = nullptr;
  rxChar_ = nullptr;

  const std::vector<NimBLERemoteService*>& services = client_->getServices(true);
  for (NimBLERemoteService* service : services) {
    txChar_ = service->getCharacteristic(config_.txUuid);
    rxChar_ = service->getCharacteristic(config_.rxUuid);
    if (txChar_ != nullptr && rxChar_ != nullptr) {
      break;
    }
  }

  if (txChar_ == nullptr || rxChar_ == nullptr) {
    if (verbose) {
      Serial.println("[BLE] Characteristics not found");
    }
    cleanupClient();
    return false;
  }

  if (rxChar_->canNotify()) {
    activeInstance_ = this;
    rxChar_->subscribe(true, onNotify);
    if (verbose) {
      Serial.println("[BLE] Subscribed");
    }
  }

  if (verbose) {
    Serial.println("[BLE] Ready");
  }

  return true;
}

void BleLabelPrinter::cleanupClient() {
  if (client_ != nullptr) {
    if (client_->isConnected()) {
      client_->disconnect();
    }
    NimBLEDevice::deleteClient(client_);
    client_ = nullptr;
  }

  txChar_ = nullptr;
  rxChar_ = nullptr;
  paused_ = false;

  if (activeInstance_ == this) {
    activeInstance_ = nullptr;
  }
}

bool BleLabelPrinter::buildPayload(const LabelContent& content) {
  if (content.qrCount_ == 0 && content.textCount_ == 0) {
    Serial.println("[PRINT] Label content is empty");
    return false;
  }

  payload_.clear();
  payload_.reserve(16000);

  const uint16_t canvasHeight = labelHeightPx(config_.labelHeightMm);
  const uint16_t printHeight = autoLabelHeight(content, canvasHeight, config_.bottomMarginPx);

  Serial.printf("[PRINT] Auto print height: %u px (canvas=%u)\n", printHeight, canvasHeight);

  lastPrintRows_ = printHeight;
  printLabel(content, printHeight, config_.printEnergy);
  return true;
}

bool BleLabelPrinter::sendPayload() {
  if (!connected()) {
    return false;
  }

  isPrinting_ = true;
  paused_ = false;

  for (size_t offset = 0; offset < payload_.size(); offset += config_.chunkSize) {
    const uint32_t pauseStart = millis();
    while (paused_) {
      if (millis() - pauseStart > config_.flowPauseTimeoutMs) {
        Serial.println("[BLE] Flow pause timeout, continue");
        paused_ = false;
        break;
      }
      delay(20);
    }

    const size_t chunk = (payload_.size() - offset > config_.chunkSize) ? config_.chunkSize : (payload_.size() - offset);
    bool ok = txChar_->writeValue(&payload_[offset], chunk, false);
    if (!ok) {
      delay(12);
      ok = txChar_->writeValue(&payload_[offset], chunk, true);
    }

    if (!ok) {
      Serial.printf("[BLE] Write failed at offset %u\n", static_cast<unsigned>(offset));
      cleanupClient();
      isPrinting_ = false;
      return false;
    }

    delay(config_.chunkDelayMs);
  }

  delay(config_.postSendDelayMs);
  isPrinting_ = false;
  return true;
}

void BleLabelPrinter::waitForPrintCompletion(uint16_t totalRows) const {
  const float estimatedSec = (totalRows * 0.015f) + 0.5f;
  Serial.printf("[PRINT] Waiting %.1fs for hardware to finish...\n", estimatedSec);
  delay(static_cast<uint32_t>(estimatedSec * 1000.0f));
}

uint16_t BleLabelPrinter::autoLabelHeight(const LabelContent& content, uint16_t canvasHeight, uint16_t bottomMargin) const {
  uint8_t rowBuffer[kLineBytes];
  int16_t maxY = -1;

  for (int16_t y = static_cast<int16_t>(canvasHeight) - 1; y >= 0; --y) {
    renderRow(content, rowBuffer, static_cast<uint16_t>(y));
    bool hasBlack = false;
    for (uint16_t i = 0; i < kLineBytes; ++i) {
      if (rowBuffer[i] != 0) {
        hasBlack = true;
        break;
      }
    }

    if (hasBlack) {
      maxY = y;
      break;
    }
  }

  uint16_t height = (maxY >= 0) ? static_cast<uint16_t>(maxY + bottomMargin) : bottomMargin;
  if (height < 1) {
    height = 1;
  }
  if (height > canvasHeight) {
    height = canvasHeight;
  }
  return height;
}

void BleLabelPrinter::renderRow(const LabelContent& content, uint8_t* rowBuffer, uint16_t y) const {
  memset(rowBuffer, 0, kLineBytes);

  for (uint8_t textIndex = 0; textIndex < content.textCount_; ++textIndex) {
    const LabelContent::TextItem& item = content.textItems_[textIndex];

    if (!item.rotated) {
      const int16_t relativeY = static_cast<int16_t>(y) - item.y;
      if (relativeY < 0 || relativeY >= kFontHeight * static_cast<int16_t>(item.scale)) {
        continue;
      }

      const uint8_t fontRow = relativeY / item.scale;
      int16_t cursorX = item.x;

      for (size_t charIndex = 0; charIndex < item.text.length(); ++charIndex, cursorX += (kFontWidth + kCharSpacing) * item.scale) {
        const char symbol = item.text[charIndex];
        if (symbol < 32 || symbol > 127) {
          continue;
        }

        const uint8_t glyphIndex = symbol - 32;
        for (uint8_t column = 0; column < kFontWidth; ++column) {
          if (pgm_read_byte(&kFont5x7[glyphIndex][column]) & (1 << fontRow)) {
            for (uint8_t scaleX = 0; scaleX < item.scale; ++scaleX) {
              canvasSetPixel(rowBuffer, cursorX + column * item.scale + scaleX);
            }
          }
        }
      }
    } else {
      for (size_t charIndex = 0; charIndex < item.text.length(); ++charIndex) {
        const char symbol = item.text[charIndex];
        if (symbol < 32 || symbol > 127) {
          continue;
        }

        const int16_t charYStart = item.y + charIndex * (kFontWidth + kCharSpacing) * static_cast<int16_t>(item.scale);
        const int16_t relativeY = static_cast<int16_t>(y) - charYStart;
        if (relativeY < 0 || relativeY >= kFontWidth * static_cast<int16_t>(item.scale)) {
          continue;
        }

        const uint8_t fontColumn = relativeY / item.scale;
        const uint8_t columnData = pgm_read_byte(&kFont5x7[symbol - 32][fontColumn]);
        for (uint8_t row = 0; row < kFontHeight; ++row) {
          if (columnData & (1 << row)) {
            for (uint8_t scaleX = 0; scaleX < item.scale; ++scaleX) {
              canvasSetPixel(rowBuffer, item.x + (kFontHeight - 1 - row) * item.scale + scaleX);
            }
          }
        }
      }
    }
  }

  for (uint8_t qrIndex = 0; qrIndex < content.qrCount_; ++qrIndex) {
    const LabelContent::QrItem& item = content.qrItems_[qrIndex];
    const int16_t relativeY = static_cast<int16_t>(y) - item.y;
    if (relativeY < 0 || relativeY >= static_cast<int16_t>(content.qrSizeActual_ * item.pixelSize)) {
      continue;
    }

    const uint8_t qrRow = relativeY / item.pixelSize;
    for (uint8_t x = 0; x < content.qrSizeActual_; ++x) {
      if (content.qrGet(x, qrRow)) {
        for (uint8_t pixelScale = 0; pixelScale < item.pixelSize; ++pixelScale) {
          canvasSetPixel(rowBuffer, item.x + x * item.pixelSize + pixelScale);
        }
      }
    }
  }
}

void BleLabelPrinter::sendRow(uint8_t* rowBuffer) {
  uint8_t out[kLineBytes];
  for (uint8_t i = 0; i < kLineBytes; ++i) {
    out[i] = reverseBits(rowBuffer[i]);
  }
  sendCommand(0xA2, out, kLineBytes);
}

void BleLabelPrinter::printLabel(const LabelContent& content, uint16_t labelHeight, uint16_t energy) {
  uint8_t rowBuffer[kLineBytes];
  const uint8_t zero = 0x00;

  sendCommand1(0xA8, 0x00);
  sendCommand1(0xBB, 0x01);
  sendCommand(0xA3, &zero, 1);
  sendCommand(0xA6, kLabelTypeBegin, sizeof(kLabelTypeBegin));
  sendCommand2(0xAF, energy);
  sendCommand1(0xA4, config_.density);
  sendCommand1(0xAE, 0x00);

  for (uint16_t y = 0; y < labelHeight; ++y) {
    renderRow(content, rowBuffer, y);
    sendRow(rowBuffer);
  }

  sendCommand2(0xA1, 9);
  sendCommand1(0xAE, 0x00);
  sendCommand(0xA6, kLabelTypeEnd, sizeof(kLabelTypeEnd));
}

void BleLabelPrinter::sendCommand(uint8_t cmd, const uint8_t* payload, uint16_t payloadLength) {
  const uint8_t header[6] = {0x51, 0x78, cmd, 0x00, static_cast<uint8_t>(payloadLength & 0xFF), 0x00};
  payload_.insert(payload_.end(), header, header + sizeof(header));
  payload_.insert(payload_.end(), payload, payload + payloadLength);
  payload_.push_back(crc8(payload, payloadLength));
  payload_.push_back(0xFF);
}

void BleLabelPrinter::sendCommand1(uint8_t cmd, uint8_t value) {
  sendCommand(cmd, &value, 1);
}

void BleLabelPrinter::sendCommand2(uint8_t cmd, uint16_t value) {
  const uint8_t bytes[2] = {static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8)};
  sendCommand(cmd, bytes, sizeof(bytes));
}

}  // namespace ecohack
