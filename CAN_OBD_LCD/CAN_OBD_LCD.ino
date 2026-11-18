#include <Arduino_CAN.h>
#include <Wire.h>
#include <math.h>
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
constexpr int16_t GAUGE_CENTER_Y = 120;
constexpr int16_t GAUGE_RECT_X = 30;
constexpr int16_t GAUGE_RECT_Y = 50;
constexpr int16_t GAUGE_RECT_W = 180;
constexpr int16_t GAUGE_RECT_H = 140;
constexpr int16_t GAUGE_LABEL_Y = 65;
constexpr int16_t GAUGE_VALUE_Y = 105;
constexpr int16_t GAUGE_UNIT_Y = 165;
constexpr uint8_t GAUGE_TICK_COUNT = 13;
constexpr int16_t GAUGE_TICK_INNER_RADIUS = 114;
constexpr int16_t GAUGE_TICK_OUTER_RADIUS = 119;
constexpr int16_t GAUGE_INDICATOR_RADIUS = 113;
constexpr int16_t GAUGE_INDICATOR_DOT_RADIUS = 5;
constexpr float GAUGE_SWEEP_START_DEG = 135.0f;
constexpr float GAUGE_SWEEP_DEG = 270.0f;
constexpr float PI_F = 3.14159265f;
constexpr float KMH_TO_MPH = 0.621371f;
constexpr uint8_t GAUGE_BUTTON_PIN = 8;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;

constexpr uint32_t REQUEST_ID = 0x7DF;
constexpr uint8_t PID_RPM = 0x0C;
constexpr uint8_t PID_RUNTIME = 0x1F;
constexpr uint32_t POLL_INTERVAL_MS = 1000;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 750;
constexpr uint32_t VALUE_TIMEOUT_MS = 15000;
constexpr uint32_t SCROLL_INTERVAL_MS = 2000;

enum class ValueFormat {
  Rpm,
  AirFlow,
  Runtime,
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
  {0x10, "MAF", " g/s", ValueFormat::AirFlow, 0, 0, false},
  {PID_RUNTIME, "Runtime", " s", ValueFormat::Runtime, 0, 0, false},
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
size_t gaugeReadingIndex = 3;
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
int16_t lastIndicatorX = 0;
int16_t lastIndicatorY = 0;
bool gaugeIndicatorDrawn = false;

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
      if (reading.format == ValueFormat::AirFlow) {
        snprintf(line, sizeof(line), "%s: %d.%02d%s", reading.label,
                 reading.value / 100, reading.value % 100, reading.unit);
      } else {
        snprintf(line, sizeof(line), "%s: %d%s", reading.label, reading.value,
                 reading.unit);
      }
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

void drawGaugeScale() {
  for (uint8_t tick = 0; tick < GAUGE_TICK_COUNT; ++tick) {
    const float fraction = static_cast<float>(tick) / (GAUGE_TICK_COUNT - 1);
    const float angle =
        (GAUGE_SWEEP_START_DEG + fraction * GAUGE_SWEEP_DEG) * PI_F / 180.0f;
    const int16_t innerX = GAUGE_CENTER_X +
        static_cast<int16_t>(lroundf(cosf(angle) * GAUGE_TICK_INNER_RADIUS));
    const int16_t innerY = GAUGE_CENTER_Y +
        static_cast<int16_t>(lroundf(sinf(angle) * GAUGE_TICK_INNER_RADIUS));
    const int16_t outerX = GAUGE_CENTER_X +
        static_cast<int16_t>(lroundf(cosf(angle) * GAUGE_TICK_OUTER_RADIUS));
    const int16_t outerY = GAUGE_CENTER_Y +
        static_cast<int16_t>(lroundf(sinf(angle) * GAUGE_TICK_OUTER_RADIUS));
    tft->drawLine(innerX, innerY, outerX, outerY, 0x7BEF);
  }
}

float gaugeFraction(const Reading &reading) {
  float minimum;
  float maximum;
  switch (reading.format) {
    case ValueFormat::Rpm:
      minimum = 0.0f;
      maximum = 8000.0f;
      break;
    case ValueFormat::AirFlow:
      minimum = 0.0f;
      maximum = 65535.0f;
      break;
    case ValueFormat::Runtime:
      minimum = 0.0f;
      maximum = 65535.0f;
      break;
    case ValueFormat::Speed:
      minimum = 0.0f;
      maximum = 255.0f;
      break;
    case ValueFormat::Temperature:
      minimum = -40.0f;
      maximum = 215.0f;
      break;
    case ValueFormat::Percent:
      minimum = 0.0f;
      maximum = 100.0f;
      break;
  }
  const float fraction = (reading.value - minimum) / (maximum - minimum);
  return fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
}

void updateGaugeIndicator(const Reading &reading, bool valid) {
  if (gaugeIndicatorDrawn) {
    tft->fillCircle(lastIndicatorX, lastIndicatorY,
                    GAUGE_INDICATOR_DOT_RADIUS, RGB565_BLACK);
    gaugeIndicatorDrawn = false;
  }
  if (!valid) {
    return;
  }

  const float angle =
      (GAUGE_SWEEP_START_DEG + gaugeFraction(reading) * GAUGE_SWEEP_DEG) *
      PI_F / 180.0f;
  lastIndicatorX = GAUGE_CENTER_X +
      static_cast<int16_t>(lroundf(cosf(angle) * GAUGE_INDICATOR_RADIUS));
  lastIndicatorY = GAUGE_CENTER_Y +
      static_cast<int16_t>(lroundf(sinf(angle) * GAUGE_INDICATOR_RADIUS));
  tft->fillCircle(lastIndicatorX, lastIndicatorY,
                  GAUGE_INDICATOR_DOT_RADIUS, RGB565_RED);
  gaugeIndicatorDrawn = true;
}

void drawGauge(const Reading &reading, bool valid) {
  tft->fillRect(GAUGE_RECT_X, GAUGE_RECT_Y, GAUGE_RECT_W, GAUGE_RECT_H, RGB565_BLACK);
  updateGaugeIndicator(reading, valid);

  int displayValue = reading.value;
  const char *displayUnit = reading.unit;
  if (reading.format == ValueFormat::Speed) {
    displayValue = static_cast<int>(lroundf(reading.value * KMH_TO_MPH));
    displayUnit = " mph";
  }

  tft->setTextColor(RGB565_WHITE);
  tft->setTextSize(2);
  gaugeCenterPrint(reading.label, GAUGE_LABEL_Y);

  char valueText[12];
  if (valid) {
    if (reading.format == ValueFormat::AirFlow) {
      snprintf(valueText, sizeof(valueText), "%d.%02d", reading.value / 100,
               reading.value % 100);
    } else {
      snprintf(valueText, sizeof(valueText), "%d", displayValue);
    }
  } else {
    snprintf(valueText, sizeof(valueText), "--");
  }
  tft->setTextSize(5);
  gaugeCenterPrint(valueText, GAUGE_VALUE_Y);

  if (displayUnit[0] != '\0') {
    tft->setTextSize(2);
    gaugeCenterPrint(displayUnit, GAUGE_UNIT_Y);
  }
}

void updateGauge(uint32_t now) {
  if (!tftReady) {
    return;
  }

  const Reading &reading = readings[gaugeReadingIndex];
  const bool valid = reading.valid && now - reading.updatedAt <= VALUE_TIMEOUT_MS;

  if (gaugeReadingIndex == lastGaugeIndex && valid == lastGaugeValid &&
      (!valid || reading.value == lastGaugeValue)) {
    return;
  }

  drawGauge(reading, valid);
  lastGaugeIndex = gaugeReadingIndex;
  lastGaugeValid = valid;
  lastGaugeValue = reading.value;
}

void updateGaugeButton(uint32_t now) {
  static bool lastReading = HIGH;
  static bool buttonState = HIGH;
  static uint32_t lastChangeAt = 0;
  const bool reading = digitalRead(GAUGE_BUTTON_PIN);

  if (reading != lastReading) {
    lastChangeAt = now;
    lastReading = reading;
  }

  if (now - lastChangeAt >= BUTTON_DEBOUNCE_MS && reading != buttonState) {
    buttonState = reading;
    if (buttonState == LOW) {
      gaugeReadingIndex = (gaugeReadingIndex + 1) % READING_COUNT;
    }
  }
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
    case ValueFormat::AirFlow:
      if (payloadLength < 4) {
        return;
      }
      reading.value = static_cast<unsigned int>(message.data[3]) * 256U +
                      message.data[4];
      break;
    case ValueFormat::Runtime:
      if (payloadLength < 4) {
        return;
      }
      reading.value = static_cast<unsigned int>(message.data[3]) * 256U +
                      message.data[4];
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
  pinMode(GAUGE_BUTTON_PIN, INPUT_PULLUP);
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
    drawGaugeScale();
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
  updateGaugeButton(now);

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
