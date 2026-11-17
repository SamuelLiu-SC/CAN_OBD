/*
 * GC9A01 240x240 round TFT display - "hello" test
 * Board: Arduino UNO R4 WiFi
 *
 * Wiring (software SPI, since D7/D6 are not the board's hardware SPI pins):
 *   Display SCL (SCK)  -> D7
 *   Display SDA (MOSI) -> D6
 *   Display RES (RST)  -> D5
 *   Display DC         -> D4
 *   Display CS         -> D3
 *   Display BLK (LED)  -> D2
 *   Display VCC        -> 3.3V
 *   Display GND        -> GND
 *
 * Required library (install with arduino-cli):
 *   arduino-cli lib install "GFX Library for Arduino"
 *
 * Note: the Adafruit_GC9A01A library was not used here because it pulls in
 * wiring_private.h, which the Arduino UNO R4 (renesas_uno core) does not
 * provide.
 */

#include <Arduino_GFX_Library.h>

constexpr uint8_t TFT_SCLK = 7;  // SCL
constexpr uint8_t TFT_MOSI = 6;  // SDA
constexpr uint8_t TFT_RST = 5;   // RES
constexpr uint8_t TFT_DC = 4;
constexpr uint8_t TFT_CS = 3;
constexpr uint8_t TFT_BLK = 2;   // Backlight

Arduino_DataBus *bus = new Arduino_SWSPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI);
// ips=true matches the panel's default color polarity (matches the
// inverted-display mode most round GC9A01 modules ship with).
Arduino_GFX *gfx = new Arduino_GC9A01(bus, TFT_RST, /*rotation=*/0, /*ips=*/true);

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH);

  gfx->begin();
  gfx->fillScreen(RGB565_BLACK);

  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(3);

  // Center "hello" on the 240x240 round display
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds("hello", 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((gfx->width() - w) / 2, (gfx->height() - h) / 2);
  gfx->print("hello");

  Serial.println(F("GC9A01 initialized, displaying 'hello'"));
}

void loop() {
  // Display is static; nothing to do here.
}
