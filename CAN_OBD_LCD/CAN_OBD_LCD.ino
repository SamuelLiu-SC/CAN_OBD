#include <Arduino_CAN.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <Arduino_GFX_Library.h>

hd44780_I2Cexp lcd;

// GC9A01 240x240 round TFT, driven over software SPI (D2-D7 are not the
// board's hardware SPI pins).
//   SCL (SCK)  -> D7     RES (RST) -> D5     CS  -> D3
//   SDA (MOSI) -> D6     DC        -> D4     BLK -> D2
constexpr uint8_t TFT_SCLK = 7;
constexpr uint8_t TFT_MOSI = 6;
constexpr uint8_t TFT_RST = 5;
constexpr uint8_t TFT_DC = 4;
constexpr uint8_t TFT_CS = 3;
constexpr uint8_t TFT_BLK = 2;

Arduino_DataBus *tftBus = new Arduino_SWSPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI);
Arduino_GFX *tft = new Arduino_GC9A01(tftBus, TFT_RST, /*rotation=*/0, /*ips=*/true);
bool tftReady = false;

// Gauge layout: a square region centered on the round screen, sized to stay
// clear of the circular bezel (240x240 panel, usable square ~170x170).
constexpr int16_t GAUGE_CENTER_X = 120;
constexpr int16_t GAUGE_RECT_X = 30;
constexpr int16_t GAUGE_RECT_Y = 50;
constexpr int16_t GAUGE_RECT_W = 180;
constexpr int16_t GAUGE_RECT_H = 140;
constexpr int16_t GAUGE_LABEL_Y = 65;
constexpr int16_t GAUGE_VALUE_Y = 105;
constexpr int16_t GAUGE_UNIT_Y = 165;

constexpr uint32_t REQUEST_ID = 0x7DF;
constexpr uint8_t PID_RPM = 0x0C;
constexpr uint32_t POLL_INTERVAL_MS = 1000;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 750;
constexpr uint32_t VALUE_TIMEOUT_MS = 15000;
constexpr uint32_t SCROLL_INTERVAL_MS = 2000;

enum class ValueFormat {
  Rpm,
  Speed,
  Temperature,
  Percent
};

struct Reading {
  uint8_t pid;
  const char *label;
  const char *unit;
  ValueFormat format;
  int value;
  uint32_t updatedAt;
  bool valid;
};

Reading readings[] = {
  {PID_RPM, "RPM", "", ValueFormat::Rpm, 0, 0, false},
  {0x0D, "Speed", " km/h", ValueFormat::Speed, 0, 0, false},
  {0x05, "Coolant", " C", ValueFormat::Temperature, 0, 0, false},
  {0x04, "Load", "%", ValueFormat::Percent, 0, 0, false},
  {0x11, "Throttle", "%", ValueFormat::Percent, 0, 0, false},
  {0x0F, "Intake", " C", ValueFormat::Temperature, 0, 0, false},
  {0x2F, "Fuel", "%", ValueFormat::Percent, 0, 0, false}
};
constexpr size_t READING_COUNT = sizeof(readings) / sizeof(readings[0]);
size_t nextReading = 0;
size_t pendingReading = 0;
size_t topRow = 0;
uint32_t requestAt = 0;
uint32_t lastPollAt = 0;
bool awaitingResponse = false;
bool txError = false;

// Tracks what the gauge last rendered so it only redraws when something
// actually changes, since a full-rect TFT redraw is much slower than an LCD
// row update.
size_t lastGaugeIndex = READING_COUNT;
int lastGaugeValue = 0;
bool lastGaugeValid = false;
bool lastGaugeError = false;

void printLine(uint8_t row, const char *text) {
  lcd.setCursor(0, row);
  uint8_t count = 0;
  while (*text && count < 16) {
    lcd.print(*text++);
    ++count;
  }
  while (count++ < 16) {
    lcd.print(' ');
  }
}

void refreshDisplay(uint32_t now) {
  for (uint8_t row = 0; row < 2; ++row) {
    if (row == 1 && txError) {
      printLine(row, "CAN TX error");
      continue;
    }
    const Reading &reading = readings[(topRow + row) % READING_COUNT];
    char line[17];
    if (reading.valid && now - reading.updatedAt <= VALUE_TIMEOUT_MS) {
      snprintf(line, sizeof(line), "%s: %d%s", reading.label, reading.value,
               reading.unit);
    } else {
      snprintf(line, sizeof(line), "%s: --%s", reading.label, reading.unit);
    }
    printLine(row, line);
  }
}

void gaugeCenterPrint(const char *text, int16_t y) {
  int16_t x1, y1;
  uint16_t w, h;
  tft->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  tft->setCursor(GAUGE_CENTER_X - w / 2, y);
  tft->print(text);
}

void drawGauge(const Reading &reading, bool valid, bool error) {
  tft->fillRect(GAUGE_RECT_X, GAUGE_RECT_Y, GAUGE_RECT_W, GAUGE_RECT_H, RGB565_BLACK);

  if (error) {
    tft->setTextColor(RGB565_RED);
    tft->setTextSize(2);
    gaugeCenterPrint("CAN TX", GAUGE_LABEL_Y);
    gaugeCenterPrint("ERROR", GAUGE_VALUE_Y);
    return;
  }

  tft->setTextColor(RGB565_WHITE);
  tft->setTextSize(2);
  gaugeCenterPrint(reading.label, GAUGE_LABEL_Y);

  char valueText[12];
  if (valid) {
    snprintf(valueText, sizeof(valueText), "%d", reading.value);
  } else {
    snprintf(valueText, sizeof(valueText), "--");
  }
  tft->setTextSize(5);
  gaugeCenterPrint(valueText, GAUGE_VALUE_Y);

  if (reading.unit[0] != '\0') {
    tft->setTextSize(2);
    gaugeCenterPrint(reading.unit, GAUGE_UNIT_Y);
  }
}

void updateGauge(uint32_t now) {
  if (!tftReady) {
    return;
  }

  const Reading &reading = readings[topRow];
  const bool valid = reading.valid && now - reading.updatedAt <= VALUE_TIMEOUT_MS;

  if (topRow == lastGaugeIndex && valid == lastGaugeValid &&
      (!valid || reading.value == lastGaugeValue) && txError == lastGaugeError) {
    return;
  }

  drawGauge(reading, valid, txError);
  lastGaugeIndex = topRow;
  lastGaugeValid = valid;
  lastGaugeValue = reading.value;
  lastGaugeError = txError;
}

void requestPid(size_t readingIndex, uint32_t now) {
  const uint8_t data[8] = {0x02, 0x01, readings[readingIndex].pid, 0, 0, 0, 0, 0};
  const CanMsg message(CanStandardId(REQUEST_ID), sizeof(data), data);
  const int result = CAN.write(message);
  if (result < 0) {
    txError = true;
    Serial.print("CAN write failed: ");
    Serial.println(result);
    return;
  }
  txError = false;
  pendingReading = readingIndex;
  requestAt = now;
  awaitingResponse = true;
}

void processResponse(const CanMsg &message, uint32_t now) {
  if (!awaitingResponse || !message.isStandardId() ||
      message.getStandardId() < 0x7E8 || message.getStandardId() > 0x7EF ||
      message.data_length < 4) {
    return;
  }

  const uint8_t payloadLength = message.data[0];
  if (payloadLength < 3 || payloadLength > 7 ||
      message.data_length < payloadLength + 1 ||
      message.data[1] != 0x41 ||
      message.data[2] != readings[pendingReading].pid) {
    return;
  }

  Reading &reading = readings[pendingReading];
  switch (reading.format) {
    case ValueFormat::Rpm:
      if (payloadLength < 4) {
        return;
      }
      reading.value = (static_cast<unsigned int>(message.data[3]) * 256U +
                       message.data[4]) / 4U;
      break;
    case ValueFormat::Speed:
      reading.value = message.data[3];
      break;
    case ValueFormat::Temperature:
      reading.value = static_cast<int>(message.data[3]) - 40;
      break;
    case ValueFormat::Percent:
      reading.value = static_cast<unsigned int>(message.data[3]) * 100U / 255U;
      break;
  }
  reading.updatedAt = now;
  reading.valid = true;
  awaitingResponse = false;
}

void setup() {
  Serial.begin(115200);
  const int lcdStatus = lcd.begin(16, 2);
  if (lcdStatus != 0) {
    Serial.print("LCD initialization failed: ");
    Serial.println(lcdStatus);
    while (true) {
      delay(1000);
    }
  }
  lcd.backlight();
  printLine(0, "OBD-II starting");
  printLine(1, "");

  // GC9A01 is a supplemental gauge display; failure here doesn't stop OBD
  // polling or the primary LCD, so it's non-fatal.
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH);
  tftReady = tft->begin();
  if (!tftReady) {
    Serial.println("GC9A01 initialization failed");
  } else {
    tft->fillScreen(RGB565_BLACK);
  }

  if (!CAN.begin(CanBitRate::BR_500k)) {
    printLine(0, "CAN init failed");
    printLine(1, "Check setup");
    Serial.println("CAN initialization failed");
    while (true) {
      delay(1000);
    }
  }

  // Send the first request immediately, including when millis() is near zero.
  lastPollAt = millis() - POLL_INTERVAL_MS;
}

void loop() {
  const uint32_t now = millis();

  // Bound the work per loop even on a busy vehicle network.
  for (uint8_t count = 0; count < 32 && CAN.available(); ++count) {
    const CanMsg message = CAN.read();
    if (awaitingResponse && now - requestAt <= RESPONSE_TIMEOUT_MS) {
      processResponse(message, now);
    }
  }

  if (awaitingResponse && now - requestAt > RESPONSE_TIMEOUT_MS) {
    Serial.print("OBD response timeout, PID 0x");
    Serial.println(readings[pendingReading].pid, HEX);
    awaitingResponse = false;
  }

  if (!awaitingResponse && now - lastPollAt >= POLL_INTERVAL_MS) {
    lastPollAt = now;
    requestPid(nextReading, now);
    nextReading = (nextReading + 1) % READING_COUNT;
  }

  static uint32_t lastScrollAt = 0;
  if (now - lastScrollAt >= SCROLL_INTERVAL_MS) {
    lastScrollAt = now;
    topRow = (topRow + 1) % READING_COUNT;
  }

  static uint32_t lastDisplayAt = 0;
  if (now - lastDisplayAt >= 250) {
    lastDisplayAt = now;
    refreshDisplay(now);
    updateGauge(now);
  }
}
