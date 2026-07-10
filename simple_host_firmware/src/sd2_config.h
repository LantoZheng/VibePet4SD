#pragma once
// ============================================================
//  SD2-OS: Global Configuration, Pin Definitions & Constants
// ============================================================

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <TJpg_Decoder.h>
#include <TFT_eSPI.h>
#include <WebSocketsServer.h>
#include <WiFiClientSecureBearSSL.h>
#include <Adafruit_NeoPixel.h>

// ---- Pin Map ----
constexpr uint8_t PIN_BACKLIGHT = 5;   // D1, inverted PWM
constexpr uint8_t PIN_BUTTON    = 4;   // D2, active low
constexpr uint8_t PIN_BUZZER    = 16;  // D0
constexpr uint16_t SCREEN_W     = 240;
constexpr uint16_t SCREEN_H     = 240;

// ---- Constants ----
constexpr const char* WIFI_CONFIG_PATH = "/wifi.cfg";
constexpr const char* SHELL_PASSWORD   = "sd2";
constexpr const char* ANIM_DIR         = "/anim";
constexpr size_t HTTP_UPLOAD_CHUNK     = 1024;

// ---- Multi-process limits ----
constexpr int MAX_PROCS  = 3;
constexpr int MAX_TIMERS = 4;
constexpr int MAX_HOOKS  = 4;

// ---- Process State Enums ----
enum ProcState : uint8_t { READY = 0, WAITING = 1, BLOCKED_SIG = 2 };
enum ProcPrio  : uint8_t { PRIO_HIGH = 0, PRIO_NORM = 1, PRIO_LOW = 2, PRIO_IDLE = 3 };

// ---- Script Opcodes ----
namespace ScriptOp {
  enum : uint8_t {
    NOP = 0, SHOW_BUILTIN = 1, SHOW_FILE = 2, TEXT = 3,
    BEEP_OP = 4, BRIGHT_OP = 5, WAIT = 6,
    ANIM_PLAY_OP = 7, ANIM_STOP_OP = 8, LOOP = 9, END = 10,
    SIG_WAIT = 11, SIG_SEND = 12, YIELD = 13, PRIORITY = 14,
  };
}

// ---- Global Objects (defined in main.cpp) ----
extern TFT_eSPI tft;
extern ESP8266WebServer server;
extern WebSocketsServer webSocket;
extern WiFiServer shellServer;
extern WiFiClient shellClient;
extern Adafruit_NeoPixel ws2812;

// ---- Global State (defined in main.cpp) ----
extern String apSsid;
extern String staSsid;
extern String staPass;
extern String currentScreen;
extern String serialLine;
extern String shellLine;
extern File   uploadFile;
extern String uploadName;
extern uint32_t bootMs;
extern uint32_t buttonPresses;
extern uint32_t lastButtonChangeMs;
extern uint32_t uploadExpected;
extern uint32_t uploadReceived;
extern uint32_t uploadNextAck;
extern bool lastButtonLevel;
extern bool stableButtonLevel;
extern bool fsMounted;
extern bool staConnectRequested;
extern bool shellAuthed;
extern bool serialUploadActive;
extern bool serialBinaryUploadActive;
extern uint32_t staConnectStartedMs;
extern uint8_t  backlightPercent;

// Hardware interrupt flags
extern volatile bool     intrButtonFired;
extern volatile uint32_t intrButtonLastMs;
extern bool hwIntrEnabled;

// Module gates
extern bool netEnabled;
extern bool llmEnabled;

// ---- Pet System State (defined in main.cpp) ----
extern String   petActive;
extern String   petCurState;
extern int      petFrameIdx;
extern uint32_t petLastMs;
extern bool     petShowActive;
extern String   petUploadSlug;
extern String   petUploadCurState;
extern int      petUploadStates;
extern int      petUploadCurFrames;
extern int      petUploadCurIdx;
extern int      petUploadDone;
extern bool     petBinaryActive;
extern int      petBinarySize;
extern int      petBinaryGot;
extern int      petBinaryIdx;
extern File     petBinaryFile;

// ---- Animation State ----
struct AnimState {
  bool     playing       = false;
  bool     loop          = true;
  bool     deltaMode     = false;
  int      fps           = 8;
  int      frameCount    = 0;
  int      currentFrame  = 0;
  uint32_t lastFrameMs   = 0;
  uint32_t frameIntervalMs = 125;
  String   name;
  String   stoppedScreen;
};
extern AnimState anim;

// ---- Scheduler Structures ----
struct Proc {
  uint8_t  id;
  uint8_t* code       = nullptr;
  uint16_t codeSize   = 0;
  uint16_t ip         = 0;
  uint16_t loopTarget = 0;
  uint16_t loopCount  = 0;
  int      loopDepth  = -1;
  bool     running    = false;
  uint32_t waitUntil  = 0;
  uint8_t  state      = READY;
  uint8_t  priority   = PRIO_NORM;
  uint32_t sigPending = 0;
  uint16_t sigHandler = 0;
  uint16_t savedIp    = 0;
  String   name;
};
extern Proc procs[MAX_PROCS];
extern int  procCount;

struct Timer {
  uint32_t nextFire;
  uint32_t period;
  String   command;
  bool     active = false;
};
extern Timer timers[MAX_TIMERS];

struct Hook {
  String event;
  String command;
  bool   active = true;
};
extern Hook hooks[MAX_HOOKS];

// ---- Script Cache ----
struct ScriptCache {
  uint8_t* code = nullptr;
  uint16_t size = 0;
  String   name;
  void clear();
  bool load(const String& path);
};
extern ScriptCache scriptCache;

struct FileCache {
  File   file;
  String path;
  void   close();
  File*  get(const String& p);
};
extern FileCache animFileCache;

// ---- Forward Declarations (utility functions) ----
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);
void     setBacklightPercent(uint8_t percent);
void     beep(uint16_t frequency = 2200, uint16_t durationMs = 70);
String   jsonEscape(const String& input);
String   normalizePath(String name);
String   heapStats();
uint32_t getMaxFreeBlock();

void drawStatus();
void drawTitle(const String& title, uint16_t color);
void drawText5x7(int x, int y, const String& s, uint16_t fg, uint16_t bg, int scale);
void drawTextMessage(const String& message);
bool showImage(const String& imageName);
bool showStoredFile(String name);
bool drawRaw565File(const String& path);
bool drawJpgFile(const String& path);
bool drawJpgFileAnim(const String& path);
bool drawDeltaFrame(const String& binPath);

String statusJson();
void   sendJson(int code, const String& body);
void   startWifiAp();
String runHostCommand(String line, bool echoToSerial);
String intrListJson();
void   setupHardwareInterrupts();
String otaUpdate(const String& url);
String netStart();
String netStop();
String llmStart();
String llmStop();
void   printHelp(Stream& out);
void   printBootInfo();
void   pollStaConnection();
void   pollSerial();
void   pollTcpShell();
void   pollButton();
void   pollPetBinary();
void   pollBinaryUpload();
bool   saveWifiConfig(const String& ssid, const String& pass);
bool   loadWifiConfig();
void   requestStaConnect();
void   setupWebServer();
void   onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);

// JPEG callback
bool jpegDrawCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
