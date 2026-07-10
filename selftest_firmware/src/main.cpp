#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>

namespace {

constexpr uint8_t PIN_BACKLIGHT = 5; // D1, inverted PWM on SD2 reference boards
constexpr uint8_t PIN_BUTTON = 4;    // D2, active low touch/button
constexpr uint8_t PIN_NEOPIXEL = 12; // D6
constexpr uint8_t PIN_BUZZER = 16;   // D0
constexpr uint16_t SCREEN_W = 240;
constexpr uint16_t SCREEN_H = 240;

TFT_eSPI tft;
Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

uint32_t bootMs = 0;
uint32_t lastScreenMs = 0;
uint32_t lastSerialMs = 0;
uint32_t buttonPresses = 0;
bool lastButton = true;
bool fsMounted = false;
uint8_t phase = 0;

void setBacklightPercent(uint8_t percent) {
  percent = constrain(percent, 0, 100);
  analogWrite(PIN_BACKLIGHT, 1023 - (percent * 1023 / 100));
}

void chirp(uint16_t frequency, uint16_t durationMs) {
  if (frequency == 0 || durationMs == 0) {
    return;
  }

  const uint32_t periodUs = 1000000UL / frequency;
  const uint32_t halfPeriodUs = periodUs / 2;
  const uint32_t endAt = millis() + durationMs;

  while (millis() < endAt) {
    digitalWrite(PIN_BUZZER, HIGH);
    delayMicroseconds(halfPeriodUs);
    digitalWrite(PIN_BUZZER, LOW);
    delayMicroseconds(halfPeriodUs);
    yield();
  }
}

void printChipInfo() {
  Serial.println();
  Serial.println(F("=== SD2 ESP8266 Small TV Self-Test ==="));
  Serial.printf("SDK: %s\n", ESP.getSdkVersion());
  Serial.printf("Chip ID: 0x%06X\n", ESP.getChipId());
  Serial.printf("CPU freq: %u MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("Flash chip ID: 0x%06X\n", ESP.getFlashChipId());
  Serial.printf("Flash real size: %u bytes\n", ESP.getFlashChipRealSize());
  Serial.printf("Flash SDK size: %u bytes\n", ESP.getFlashChipSize());
  Serial.printf("Sketch size/free: %u / %u bytes\n", ESP.getSketchSize(), ESP.getFreeSketchSpace());
  Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());
}

void drawHeader(const char* title, uint16_t color) {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, SCREEN_W, 28, color);
  tft.setTextColor(TFT_BLACK, color);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(title, SCREEN_W / 2, 14, 2);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
}

void drawStatusPage() {
  drawHeader("SD2 SELF TEST", TFT_CYAN);
  tft.setCursor(8, 40);
  tft.printf("Chip: ESP8266EX\n");
  tft.printf("Flash: %u KB\n", ESP.getFlashChipRealSize() / 1024);
  tft.printf("Heap: %u\n", ESP.getFreeHeap());
  tft.printf("MAC:\n%s\n", WiFi.macAddress().c_str());
  tft.printf("LittleFS: %s\n", fsMounted ? "OK" : "FAIL");
  tft.printf("Button GPIO4: %s\n", digitalRead(PIN_BUTTON) == LOW ? "DOWN" : "UP");
  tft.printf("Presses: %u\n", buttonPresses);
}

void drawColorBars() {
  static const uint16_t colors[] = {
    TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_BLACK, TFT_YELLOW, TFT_MAGENTA, TFT_CYAN,
  };

  const uint16_t barW = SCREEN_W / 4;
  const uint16_t barH = SCREEN_H / 2;
  for (uint8_t i = 0; i < 8; i++) {
    tft.fillRect((i % 4) * barW, (i / 4) * barH, barW, barH, colors[i]);
  }
}

void drawPinMap() {
  drawHeader("PIN MAP", TFT_GREEN);
  tft.setCursor(8, 38);
  tft.println("TFT SCK  D5 GPIO14");
  tft.println("TFT MOSI D7 GPIO13");
  tft.println("TFT CS   D8 GPIO15");
  tft.println("TFT DC   D3 GPIO0");
  tft.println("TFT RST  D4 GPIO2");
  tft.println("BL PWM   D1 GPIO5");
  tft.println("KEY      D2 GPIO4");
  tft.println("LED      D6 GPIO12");
  tft.println("BUZZER   D0 GPIO16");
}

void drawBacklightPage(uint8_t level) {
  drawHeader("BACKLIGHT", TFT_ORANGE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(String(level) + "%", SCREEN_W / 2, 100, 7);
  tft.fillRect(20, 160, 200, 18, TFT_DARKGREY);
  tft.fillRect(20, 160, map(level, 0, 100, 0, 200), 18, TFT_YELLOW);
  tft.setTextDatum(TL_DATUM);
}

void runPhase() {
  switch (phase) {
    case 0:
      setBacklightPercent(80);
      pixel.setPixelColor(0, pixel.Color(0, 0, 0));
      pixel.show();
      drawStatusPage();
      break;
    case 1:
      setBacklightPercent(100);
      pixel.setPixelColor(0, pixel.Color(32, 0, 0));
      pixel.show();
      drawColorBars();
      break;
    case 2:
      setBacklightPercent(20);
      pixel.setPixelColor(0, pixel.Color(0, 32, 0));
      pixel.show();
      drawBacklightPage(20);
      break;
    case 3:
      setBacklightPercent(60);
      pixel.setPixelColor(0, pixel.Color(0, 0, 32));
      pixel.show();
      drawBacklightPage(60);
      break;
    case 4:
      setBacklightPercent(100);
      pixel.setPixelColor(0, pixel.Color(24, 16, 0));
      pixel.show();
      drawBacklightPage(100);
      chirp(2200, 80);
      break;
    default:
      setBacklightPercent(80);
      pixel.setPixelColor(0, pixel.Color(8, 8, 8));
      pixel.show();
      drawPinMap();
      break;
  }

  phase = (phase + 1) % 6;
}

void updateButton() {
  const bool buttonNow = digitalRead(PIN_BUTTON);
  if (lastButton == HIGH && buttonNow == LOW) {
    buttonPresses++;
    Serial.printf("Button press #%u at %lu ms\n", buttonPresses, static_cast<unsigned long>(millis() - bootMs));
    chirp(1800, 35);
    drawStatusPage();
  }
  lastButton = buttonNow;
}

void printPeriodicStatus() {
  Serial.printf(
    "uptime=%lus heap=%u button=%s presses=%u phase=%u\n",
    static_cast<unsigned long>((millis() - bootMs) / 1000),
    ESP.getFreeHeap(),
    digitalRead(PIN_BUTTON) == LOW ? "DOWN" : "UP",
    buttonPresses,
    phase);
}

} // namespace

void setup() {
  bootMs = millis();
  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);

  pinMode(PIN_BACKLIGHT, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  analogWriteRange(1023);
  analogWriteFreq(1000);
  setBacklightPercent(80);

  pixel.begin();
  pixel.clear();
  pixel.show();

  fsMounted = LittleFS.begin();
  printChipInfo();
  Serial.printf("LittleFS mount: %s\n", fsMounted ? "OK" : "FAIL");
  if (fsMounted) {
    FSInfo info;
    LittleFS.info(info);
    Serial.printf("LittleFS used/total: %u / %u bytes\n", info.usedBytes, info.totalBytes);
    Serial.printf("/config.json exists: %s\n", LittleFS.exists("/config.json") ? "yes" : "no");
  }

  tft.init();
  tft.setRotation(0);
  tft.setTextFont(2);
  drawStatusPage();
  chirp(1600, 70);
  delay(120);
  chirp(2200, 70);

  lastScreenMs = millis();
  lastSerialMs = millis();
}

void loop() {
  updateButton();

  const uint32_t now = millis();
  if (now - lastScreenMs >= 3000) {
    lastScreenMs = now;
    runPhase();
  }

  if (now - lastSerialMs >= 5000) {
    lastSerialMs = now;
    printPeriodicStatus();
  }

  delay(10);
}
