#pragma once
// ============================================================
//  SD2-OS: Network (WiFi, OTA, NTP DNS, Network Start/Stop)
// ============================================================

// ---- DNS Cache ----
namespace DNS {

struct Entry { String host; IPAddress ip; };
extern Entry cache[4];

String resolve(const String& host) {
  for (int i = 0; i < 4; i++) {
    if (cache[i].host == host) return cache[i].ip.toString();
  }
  IPAddress ip;
  if (!WiFi.hostByName(host.c_str(), ip)) return "ERR not resolved";
  for (int i = 3; i > 0; i--) cache[i] = cache[i - 1];
  cache[0].host = host;
  cache[0].ip = ip;
  return ip.toString();
}

String listJson() {
  String json = "{\"ok\":true,\"dns\":[";
  for (int i = 0; i < 4; i++) {
    if (!cache[i].host.length()) continue;
    if (i > 0 && cache[i-1].host.length()) json += ",";
    json += "{\"host\":\"" + cache[i].host + "\",\"ip\":\"" + cache[i].ip.toString() + "\"}";
  }
  json += "]}";
  return json;
}

} // namespace DNS

// ---- WiFi ----
inline void startWifiAp() {
  WiFi.mode(WIFI_AP_STA);
  apSsid = "SD2-TV-" + String(ESP.getChipId(), HEX);
  apSsid.toUpperCase();
  WiFi.softAP(apSsid.c_str());
}

inline bool saveWifiConfig(const String& ssid, const String& pass) {
  if (!fsMounted) return false;
  File file = LittleFS.open(WIFI_CONFIG_PATH, "w");
  if (!file) return false;
  file.println(ssid);
  file.println(pass);
  file.close();
  return true;
}

inline bool loadWifiConfig() {
  if (!fsMounted || !LittleFS.exists(WIFI_CONFIG_PATH)) return false;
  File file = LittleFS.open(WIFI_CONFIG_PATH, "r");
  if (!file) return false;
  staSsid = file.readStringUntil('\n');
  staPass = file.readStringUntil('\n');
  staSsid.trim();
  staPass.trim();
  file.close();
  return staSsid.length() > 0;
}

inline void requestStaConnect() {
  if (!staSsid.length()) return;
  WiFi.begin(staSsid.c_str(), staPass.c_str());
  staConnectRequested = true;
  staConnectStartedMs = millis();
}

inline void pollStaConnection() {
  if (!staConnectRequested) return;
  if (WiFi.status() == WL_CONNECTED) {
    staConnectRequested = false;
    Log::info("WIFI connected " + WiFi.localIP().toString());
    String event = String("{\"event\":\"wifi\",\"connected\":true,\"ip\":\"") + WiFi.localIP().toString() + "\"}";
    Serial.println(event);
    webSocket.broadcastTXT(event);
    if (currentScreen == "status") drawStatus();
    return;
  }
  if (millis() - staConnectStartedMs > 20000) {
    staConnectRequested = false;
    Log::warn("WIFI STA timeout");
    Serial.println(F("{\"event\":\"wifi\",\"connected\":false}"));
    webSocket.broadcastTXT("{\"event\":\"wifi\",\"connected\":false}");
  }
}

// ---- Network Start/Stop ----
inline String netStart() {
  if (netEnabled) return "OK network already running";
  startWifiAp();
  server.begin();
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
  shellServer.begin();
  shellServer.setNoDelay(true);
  netEnabled = true;
  Log::info("NET started");
  return "OK NET_START " + apSsid + " " + WiFi.softAPIP().toString();
}

inline String netStop() {
  if (!netEnabled) return "OK network already stopped";
  server.close();
  webSocket.close();
  if (shellClient) shellClient.stop();
  shellServer.close();
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  netEnabled = false;
  serialUploadActive = false;
  serialBinaryUploadActive = false;
  Log::info("NET stopped");
  return "OK NET_STOP (serial shell still active)";
}

// ---- LLM Start/Stop ----
inline String llmStart() {
  if (!Env::get("LLM_API_KEY").length()) return "ERR: SET LLM_API_KEY=sk-... first";
  llmEnabled = true;
  Log::info("LLM enabled");
  return "OK LLM_START provider=" + Env::get("LLM_PROVIDER") + " model=" + Env::get("LLM_MODEL");
}

inline String llmStop() {
  llmEnabled = false;
  Log::info("LLM disabled");
  return "OK LLM_STOP";
}

// ---- OTA ----
String otaUpdate(const String& url) {
  String host, path;
  int s = url.indexOf("://"); int hs = s >= 0 ? s+3 : 0;
  int ps = url.indexOf("/", hs);
  host = ps < 0 ? url.substring(hs) : url.substring(hs, ps);
  path = ps < 0 ? "/" : url.substring(ps);

  Log::info("OTA " + host + path);
  tft.fillScreen(TFT_BLACK);
  drawText5x7((SCREEN_W - String("OTA " + host).length()*12)/2, SCREEN_H/2-30, "OTA " + host, TFT_WHITE, TFT_BLACK, 2);

  WiFiClient client;
  if (!client.connect(host, 80)) return "ERR connect";
  client.print("GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\n\r\n");

  uint32_t st = millis();
  while (!client.available() && millis()-st < 5000) yield();
  String hl = client.readStringUntil('\n');
  if (hl.indexOf("200") < 0) { client.stop(); return "ERR HTTP " + hl; }

  int cl = 0;
  while (client.connected()) {
    String l = client.readStringUntil('\n'); l.trim();
    if (!l.length()) break;
    if (l.startsWith("Content-Length:") || l.startsWith("content-length:"))
      cl = l.substring(15).toInt();
  }
  if (cl <= 0 || cl > (int)(ESP.getFreeSketchSpace()-0x1000)) { client.stop(); return "ERR size " + String(cl); }

  drawText5x7((SCREEN_W - String("Flash " + String(cl/1024) + "KB").length()*12)/2, SCREEN_H/2+10, "Flash " + String(cl/1024) + "KB", TFT_WHITE, TFT_BLACK, 2);
  if (!Update.begin(cl)) { client.stop(); return "ERR begin"; }

  size_t w = 0; uint32_t lp = 0;
  while (client.connected() && w < (size_t)cl) {
    if (client.available()) {
      uint8_t b[1024]; size_t a = client.available();
      size_t r = a > sizeof(b) ? sizeof(b) : a;
      if (w+r > (size_t)cl) r = cl-w;
      int got = client.read(b, r);
      if (got > 0) { Update.write(b, got); w += got; }
      uint32_t p = w*100/cl;
      if (p != lp) { lp = p;
        tft.fillRect(0, SCREEN_H-35, SCREEN_W, 25, TFT_BLACK);
        drawText5x7((SCREEN_W - String(String(p)+"%").length()*12)/2, SCREEN_H-30, String(p)+"%", TFT_WHITE, TFT_BLACK, 2);
      }
    }
    yield();
  }
  client.stop();
  if (!Update.end() || w < (size_t)cl) return "ERR write " + String(w) + "/" + String(cl);
  drawText5x7((SCREEN_W - 14*6)/2, SCREEN_H-30, "Reboot...", TFT_WHITE, TFT_BLACK, 2);
  delay(800); ESP.restart(); return "OK";
}

// ---- Boot Info ----
inline void printHelp(Stream& out) {
  out.println(F("=== SD2-OS Shell ==="));
  out.println(F("SHOW logo|face|bars|status  SHOW_FILE <name>  TEXT <msg>  BRIGHT <n>"));
  out.println(F("ANIM_LIST/PLAY/STOP/INFO <name>"));
  out.println(F("SCRIPT_LIST/COMPILE/RUN  MODULE_LIST/LOAD/UNLOAD/STATUS"));
  out.println(F("IMG_BIN_BEGIN/IMG_BEGIN  (serial upload)"));
  out.println(F("WIFI <s> <p> / WIFI? / WIFI_CLEAR"));
  out.println(F("SPAWN PS KILL KILLALL SIGNAL TIMER CRON HOOK"));
  out.println(F("STATUS PING MEM TIME DATE TZ DMSG LOG_CLEAR"));
  out.println(F("SET GET ENV UNSET  NET_START/STOP  LLM_START/STOP/ASK"));
  out.println(F("OTA DNS INTR_LIST/ON/OFF  WDT_ENABLE/FEED/DISABLE"));
  out.println(F("BEEP GPIO FS REBOOT BT HELP"));
  out.println(F("Pipe: CMD1 | PARSE key  |  CMD1 | CMD2"));
  out.println(F("SIGNAL idle|working|attention|blocked  (WS2812 lamp)"));
}

inline void printBootInfo() {
  Serial.println();
  Serial.println(F("=== SD2 Host Firmware ==="));
  Serial.printf("AP SSID: %s\n", apSsid.c_str());
  Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("WebSocket: ws://%s:81/\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("TCP shell: %s:2323 password=%s\n", WiFi.softAPIP().toString().c_str(), SHELL_PASSWORD);
  Serial.printf("STA SSID: %s\n", staSsid.length() ? staSsid.c_str() : "(none)");
  Serial.printf("Serial: 115200 8N1\n");
  Serial.printf("Bluetooth: unsupported on ESP8266EX\n");
  Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());
  Serial.printf("Flash: %u bytes\n", ESP.getFlashChipRealSize());
  Serial.printf("LittleFS: %s\n", fsMounted ? "OK" : "FAIL");
  printHelp(Serial);
}
