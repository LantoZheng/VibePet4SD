// ============================================================
//  VibePet4SD — AI Pet + Signal Light for SD2 Small TV
//  Tribute to the SD2 open-source hardware project.
//  https://github.com/Lanto/VibePet4SD
// ============================================================
//
//  Module structure:
//    sd2_config.h   — Pin map, globals, forward declarations
//    sd2_log.h      — Ring log buffer
//    sd2_env.h      — Environment variables
//    sd2_time.h     — NTP time sync
//    sd2_font.h     — Custom 5×7 pixel font
//    sd2_signal.h   — WS2812 + TFT border signal light
//    sd2_pet.h      — Built-in blob + JPEG pet rendering
//    sd2_anim.h     — JPEG animation engine
//    sd2_script.h   — Script compiler + VM
//    sd2_proc.h     — Cooperative multi-process scheduler
//    sd2_module.h   — Dynamic module loader
//    sd2_llm.h      — OpenAI / Claude API bridge
//    sd2_network.h  — WiFi, OTA, DNS, network management
//    sd2_web.h      — HTTP handlers + WebSocket
//    sd2_shell.h    — Command dispatch + serial/TCP shell
//
//  This file: global definitions, setup(), loop()

#include "sd2_config.h"
#include "sd2_log.h"
#include "sd2_env.h"
#include "sd2_time.h"
#include "sd2_font.h"
#include "sd2_signal.h"
#include "sd2_pet.h"
#include "sd2_anim.h"
#include "sd2_script.h"
#include "sd2_proc.h"
#include "sd2_module.h"
#include "sd2_llm.h"
#include "sd2_network.h"
#include "sd2_web.h"
#include "sd2_shell.h"

// ============================================================
//  Global Object Definitions
// ============================================================
TFT_eSPI tft;
ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
WiFiServer shellServer(2323);
WiFiClient shellClient;
Adafruit_NeoPixel ws2812(1, 12, NEO_GRB + NEO_KHZ800);

// ---- Global State ----
String apSsid;
String staSsid;
String staPass;
String currentScreen = "boot";
String serialLine;
String shellLine;
File   uploadFile;
String uploadName;
uint32_t bootMs              = 0;
uint32_t buttonPresses        = 0;
uint32_t lastButtonChangeMs  = 0;
uint32_t uploadExpected      = 0;
uint32_t uploadReceived      = 0;
uint32_t uploadNextAck       = 0;
bool lastButtonLevel         = HIGH;
bool stableButtonLevel       = HIGH;
bool fsMounted               = false;
bool staConnectRequested     = false;
bool shellAuthed             = false;
bool serialUploadActive      = false;
bool serialBinaryUploadActive = false;
uint32_t staConnectStartedMs = 0;
uint8_t  backlightPercent    = 85;

volatile bool     intrButtonFired  = false;
volatile uint32_t intrButtonLastMs = 0;
bool hwIntrEnabled = true;
bool netEnabled = true;
bool llmEnabled = false;

// ---- Pet System ----
String   petActive;
String   petCurState    = "idle";
int      petFrameIdx    = 0;
uint32_t petLastMs      = 0;
bool     petShowActive  = false;
String   petUploadSlug;
String   petUploadCurState;
int      petUploadStates    = 0;
int      petUploadCurFrames = 0;
int      petUploadCurIdx    = 0;
int      petUploadDone      = 0;
bool     petBinaryActive    = false;
int      petBinarySize      = 0;
int      petBinaryGot       = 0;
int      petBinaryIdx       = 0;
File     petBinaryFile;

// ---- Animation State ----
AnimState anim;

// ---- Namespace Data ----
namespace Log   { char ring[16][64] = {}; int head = 0, count = 0; }
namespace Time  { int8_t tzOffset = 8; bool synced = false; uint32_t lastSyncMs = 0, lastNtpTryMs = 0; }
namespace Signal { Mode mode = IDLE; uint32_t lastMs = 0; uint8_t cycleStep = 0; bool flashOn = false; uint32_t flashInterval = 500; uint8_t curR = 0, curG = 40, curB = 0; }
namespace DNS    { Entry cache[4]; }
namespace LLM    { String lastResponse; uint32_t lastCallMs = 0; }
namespace Module { String loadedName; }

Proc  procs[MAX_PROCS];
int   procCount = 0;
Timer timers[MAX_TIMERS];
Hook  hooks[MAX_HOOKS];

ScriptCache scriptCache;
FileCache   animFileCache;

struct HwIntr { uint8_t pin; String event; bool active; };
HwIntr hwIntrs[4];
int    hwIntrCount = 0;

// ---- Cache methods ----
void ScriptCache::clear() { delete[] code; code = nullptr; size = 0; name = ""; }
bool ScriptCache::load(const String& path) {
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  uint8_t hdr[10];
  if (f.read(hdr, 10) != 10) { f.close(); return false; }
  size = (hdr[8] << 8) | hdr[9];
  if (size > 2048) { f.close(); return false; }
  delete[] code;
  code = new uint8_t[size];
  f.seek(0);
  if (f.read(code, size) != size) { delete[] code; code = nullptr; f.close(); return false; }
  f.close(); name = path;
  return true;
}
void FileCache::close() { if (file) file.close(); path = ""; }
File* FileCache::get(const String& p) {
  if (path == p && file) { file.seek(0); return &file; }
  close();
  if (!LittleFS.exists(p)) return nullptr;
  file = LittleFS.open(p, "r");
  if (!file) return nullptr;
  path = p;
  return &file;
}

// ============================================================
//  Utility Functions
// ============================================================
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void setBacklightPercent(uint8_t percent) {
  percent = constrain(percent, 0, 100);
  backlightPercent = percent;
  analogWrite(PIN_BACKLIGHT, 1023 - (percent * 1023 / 100));
}

void beep(uint16_t frequency, uint16_t durationMs) {
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

String jsonEscape(const String& input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); i++) {
    const char c = input[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') { out += "\\n"; }
    else if (c == '\r') { out += "\\r"; }
    else { out += c; }
  }
  return out;
}

uint32_t getMaxFreeBlock() {
  uint32_t low = 0, high = 65536;
  while (low + 64 < high) {
    uint32_t mid = (low + high) / 2;
    void* p = malloc(mid);
    if (p) { free(p); low = mid; }
    else { high = mid; }
  }
  return low;
}

String heapStats() {
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t maxBlock = getMaxFreeBlock();
  uint32_t frag = freeHeap > 0 ? ((freeHeap - maxBlock) * 100 / freeHeap) : 0;
  return "HEAP free=" + String(freeHeap)
       + " maxBlock=" + String(maxBlock)
       + " frag=" + String(frag) + "%"
       + " scriptCache=" + String(scriptCache.code ? "loaded" : "empty")
       + " animFile=" + String(animFileCache.file ? "open" : "closed");
}

// ============================================================
//  Hardware Interrupts
// ============================================================
IRAM_ATTR void buttonISR() { intrButtonFired = true; }

void checkButtonIntr() {
  if (!intrButtonFired) return;
  intrButtonFired = false;
  uint32_t now = millis();
  if (now - intrButtonLastMs < 50) return;
  intrButtonLastMs = now;
  if (digitalRead(PIN_BUTTON) == LOW) {
    buttonPresses++;
    beep(2400, 55);
    Serial.printf("EVENT button press=%u\n", buttonPresses);
    ProcMgr::hookFire("button");
    if (netEnabled) { String evt = String("{\"event\":\"button\",\"presses\":") + buttonPresses + "}"; webSocket.broadcastTXT(evt); }
    if (currentScreen == "face" || currentScreen == "status") showImage(currentScreen);
  }
}

void setupHardwareInterrupts() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), buttonISR, FALLING);
  hwIntrs[0] = {PIN_BUTTON, "button", true};
  hwIntrCount = 1;
}

String intrListJson() {
  String json = "{\"ok\":true,\"interrupts\":[";
  for (int i = 0; i < hwIntrCount; i++) {
    if (i > 0) json += ",";
    json += "{\"pin\":" + String(hwIntrs[i].pin) + ",\"event\":\"" + hwIntrs[i].event + "\",\"active\":" + String(hwIntrs[i].active ? "true" : "false") + "}";
  }
  return json + "],\"global\":" + String(hwIntrEnabled ? "true" : "false") + "}";
}

void pollButton() {
  if (hwIntrEnabled) { checkButtonIntr(); return; }
  const bool raw = digitalRead(PIN_BUTTON);
  if (raw != lastButtonLevel) { lastButtonLevel = raw; lastButtonChangeMs = millis(); }
  if (millis() - lastButtonChangeMs > 35 && raw != stableButtonLevel) {
    stableButtonLevel = raw;
    if (stableButtonLevel == LOW) { buttonPresses++; beep(2400, 55); ProcMgr::hookFire("button"); }
  }
}

// ============================================================
//  Setup & Loop
// ============================================================
void setup() {
  bootMs = millis();
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_BACKLIGHT, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  setupHardwareInterrupts();

  analogWriteRange(1023);
  analogWriteFreq(1000);
  setBacklightPercent(85);

  tft.init();
  tft.setRotation(0);
  tft.setTextFont(2);

  fsMounted = LittleFS.begin();
  Time::begin();
  Log::info("BOOT fs=" + String(fsMounted ? "OK" : "FAIL") + " flash=" + String(ESP.getFlashChipRealSize()));
  ProcMgr::hookFire("boot");
  startWifiAp();
  loadWifiConfig();
  if (staSsid.length()) requestStaConnect();

  netEnabled = true;
  setupWebServer();
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
  shellServer.begin();
  shellServer.setNoDelay(true);

  if (Env::get("LLM_API_KEY").length()) llmEnabled = true;

  drawStatus();
  loadPetCfg();
  Signal::begin();

  beep(1700, 50); delay(80); beep(2300, 50);
  printBootInfo();

  if (LittleFS.exists("/auto.s2b")) {
    Log::info("AUTO running /auto.s2b");
    Script::run("/auto.s2b");
  }
}

void loop() {
  if (petBinaryActive) {
    while (petBinaryActive) { pollPetBinary(); if (Serial.available()) continue; delay(1); }
  }

  if (netEnabled) { server.handleClient(); webSocket.loop(); pollTcpShell(); }
  pollStaConnection();
  pollSerial();
  pollButton();

  Signal::poll();
  petPoll();
  animPoll();
  ProcMgr::schedule();
}
