#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>

namespace ecohack {

class LabelContent {
 public:
  static constexpr uint16_t kPaperWidthPx = 384;
  static constexpr uint8_t kMaxTextItems = 8;
  static constexpr uint8_t kMaxQrItems = 4;

  static constexpr uint16_t paperWidthPx() { return kPaperWidthPx; }
  static constexpr uint8_t maxTextItems() { return kMaxTextItems; }
  static constexpr uint8_t maxQrItems() { return kMaxQrItems; }

  LabelContent();

  void clear();
  bool addText(int16_t x, int16_t y, const String& text, uint8_t scale = 3);
  bool addCenteredText(uint16_t y, const String& text, uint8_t scale = 3);
  bool addRotatedText(int16_t x, int16_t y, const String& text, uint8_t scale = 3);
  bool setQr(int16_t x, int16_t y, const String& data, uint8_t pixelSize = 4);
  bool setQrCentered(uint16_t y, const String& data, uint8_t pixelSize = 4);

 private:
  friend class BleLabelPrinter;

  static constexpr uint8_t kQrVersion = 4;
  static constexpr uint8_t kQrMatrixSize = 33;
  static constexpr size_t kQrBufferBytes = 137;

  struct TextItem {
    int16_t x;
    int16_t y;
    String text;
    uint8_t scale;
    bool rotated;
  };

  struct QrItem {
    int16_t x;
    int16_t y;
    uint8_t pixelSize;
  };

  bool generateQr(const String& data);
  bool qrGet(uint8_t x, uint8_t y) const;
  uint16_t stringPixelWidth(const String& text, uint8_t scale) const;

  TextItem textItems_[kMaxTextItems];
  uint8_t textCount_ = 0;
  QrItem qrItems_[kMaxQrItems];
  uint8_t qrCount_ = 0;
  uint8_t qrBitBuffer_[kQrBufferBytes] = {0};
  uint8_t qrSizeActual_ = 0;
};

struct PrinterConfig {
  const char* address = nullptr;
  const char* serviceUuid = "0000ae30-0000-1000-8000-00805f9b34fb";
  const char* txUuid = "0000ae01-0000-1000-8000-00805f9b34fb";
  const char* rxUuid = "0000ae02-0000-1000-8000-00805f9b34fb";
  uint16_t labelHeightMm = 40;
  uint16_t printEnergy = 0x6000;
  uint16_t feedAfterPrintSteps = 0x2099;
  uint16_t bottomMarginPx = 20;
  uint8_t defaultFontScale = 3;
  uint8_t defaultQrPixelSize = 4;
  size_t chunkSize = 20;
  uint32_t reconnectIntervalMs = 3000;
  uint32_t flowPauseTimeoutMs = 8000;
  uint32_t chunkDelayMs = 8;
  uint32_t postSendDelayMs = 500;
  uint32_t calibrationFeedDelayMs = 5000;
  uint32_t calibrationSettleDelayMs = 500;
  uint8_t density = 50;
};

class BleLabelPrinter {
 public:
  explicit BleLabelPrinter(const PrinterConfig& config);
  ~BleLabelPrinter();

  bool begin();
  void tick();
  bool connected() const;
  bool busy() const;
  bool calibrate(uint16_t feedSteps = 0);
  bool print(const LabelContent& content);
  void disconnect();

 private:
  static void onNotify(NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify);

  static uint16_t labelHeightPx(uint16_t labelHeightMm);

  void handleNotify(uint8_t* data, size_t length);
  bool ensureConnected(bool verbose);
  void cleanupClient();
  bool buildPayload(const LabelContent& content);
  bool sendPayload();
  void waitForPrintCompletion(uint16_t totalRows) const;
  uint16_t autoLabelHeight(const LabelContent& content, uint16_t canvasHeight, uint16_t bottomMargin) const;
  void renderRow(const LabelContent& content, uint8_t* rowBuffer, uint16_t y) const;
  void sendRow(uint8_t* rowBuffer);
  void printLabel(const LabelContent& content, uint16_t labelHeight, uint16_t energy);
  void sendCommand(uint8_t cmd, const uint8_t* payload, uint16_t payloadLength);
  void sendCommand1(uint8_t cmd, uint8_t value);
  void sendCommand2(uint8_t cmd, uint16_t value);

  PrinterConfig config_;
  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* txChar_ = nullptr;
  NimBLERemoteCharacteristic* rxChar_ = nullptr;
  std::vector<uint8_t> payload_;
  volatile bool paused_ = false;
  bool isPrinting_ = false;
  uint32_t lastReconnectMs_ = 0;
  uint16_t lastPrintRows_ = 0;

  static BleLabelPrinter* activeInstance_;
  static bool nimbleInitialized_;
};

}  // namespace ecohack
