#pragma once
// ============================================================
//  SD2-OS: Shell — Command Dispatch, Serial Shell, TCP Shell
// ============================================================

// ---- Hex upload helpers ----
inline int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline void handleUploadHexLine(String line) {
  line.trim();
  if (!serialUploadActive || !uploadFile) { Serial.println(F("ERR no active upload")); return; }
  if (line == "ABORT") { uploadFile.close(); serialUploadActive = false; Serial.println(F("ERR upload aborted")); return; }

  uint8_t buffer[64];
  size_t count = 0;
  int high = -1;

  for (size_t i = 0; i < line.length(); i++) {
    const char c = line[i];
    if (c == ' ' || c == '\t') continue;
    const int value = hexValue(c);
    if (value < 0) { Serial.println(F("ERR bad hex")); return; }
    if (high < 0) { high = value; }
    else { buffer[count++] = static_cast<uint8_t>((high << 4) | value); high = -1;
      if (count == sizeof(buffer)) { uploadFile.write(buffer, count); uploadReceived += count; count = 0; }
    }
  }
  if (high >= 0) { Serial.println(F("ERR odd hex length")); return; }
  if (count) { uploadFile.write(buffer, count); uploadReceived += count; }

  if (uploadReceived >= uploadExpected) {
    uploadFile.close(); serialUploadActive = false;
    if (uploadReceived == uploadExpected)
      Serial.printf("OK IMG_DONE %s bytes=%lu\n", uploadName.c_str(), static_cast<unsigned long>(uploadReceived));
    else
      Serial.printf("ERR IMG_SIZE expected=%lu got=%lu\n", static_cast<unsigned long>(uploadExpected), static_cast<unsigned long>(uploadReceived));
  } else {
    Serial.printf("ACK %lu/%lu\n", static_cast<unsigned long>(uploadReceived), static_cast<unsigned long>(uploadExpected));
  }
}

// ---- Pet binary upload poll ----
inline void pollPetBinary() {
  if (!petBinaryActive) return;
  static uint8_t petBuf[4096];
  static uint32_t petBinaryStart = 0;
  if (petBinaryGot == 0) petBinaryStart = millis();
  while (Serial.available() && petBinaryGot < petBinarySize && petBinaryGot < 4096) {
    petBuf[petBinaryGot++] = Serial.read();
  }
  if (petBinaryGot > 0 && millis() - petBinaryStart > 5000 && petBinaryGot < petBinarySize) {
    petBinaryFile.close(); petBinaryActive = false;
    Serial.println("ERR PET_FRAME timeout"); return;
  }
  if (petBinaryGot >= petBinarySize) {
    const size_t CHUNK = 512;
    for (size_t off = 0; off < (size_t)petBinarySize; off += CHUNK) {
      size_t n = petBinarySize - off;
      if (n > CHUNK) n = CHUNK;
      ESP.wdtFeed();
      petBinaryFile.write(petBuf + off, n);
      ESP.wdtFeed();
    }
    petBinaryFile.close(); petBinaryActive = false;
    petUploadCurIdx++; petUploadDone++;
    Serial.println("OK PET_FRAME " + String(petBinaryIdx));
  }
}

// ---- Binary upload poll ----
inline void pollBinaryUpload() {
  if (!serialBinaryUploadActive || !uploadFile) return;
  uint8_t buffer[256];
  while (Serial.available() && uploadReceived < uploadExpected) {
    const uint32_t remaining = uploadExpected - uploadReceived;
    const size_t available = Serial.available();
    size_t want = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
    if (want > available) want = available;
    const int got = Serial.readBytes(buffer, want);
    if (got <= 0) break;
    uploadFile.write(buffer, got);
    uploadReceived += got;
    if (uploadReceived >= uploadNextAck && uploadReceived < uploadExpected) {
      Serial.printf("ACK_BIN %lu/%lu\n", static_cast<unsigned long>(uploadReceived), static_cast<unsigned long>(uploadExpected));
      uploadNextAck = uploadReceived + 256;
    }
    yield();
  }
  if (uploadReceived >= uploadExpected) {
    uploadFile.close(); serialBinaryUploadActive = false;
    Serial.printf("OK IMG_BIN_DONE %s bytes=%lu\n", uploadName.c_str(), static_cast<unsigned long>(uploadReceived));
  }
}

// ============================================================
//  Command Dispatch — the heart of SD2-OS
// ============================================================
String runHostCommand(String line, bool echoToSerial) {
  line.trim();
  if (!line.length()) return "";

  // Pipe support
  int pipePos = line.indexOf('|');
  if (pipePos > 0) {
    String left = line.substring(0, pipePos); left.trim();
    String right = line.substring(pipePos + 1); right.trim();
    String leftResult = runHostCommand(left, false);
    right.toUpperCase();
    if (right.startsWith("PARSE ")) {
      String key = right.substring(6); key.trim(); key.toLowerCase();
      String search = "\"" + key + "\"";
      int pos = leftResult.indexOf(search);
      if (pos >= 0) {
        pos += search.length();
        while (pos < (int)leftResult.length() && (leftResult[pos] == ':' || leftResult[pos] == ' ' || leftResult[pos] == '\"')) pos++;
        int end = pos;
        while (end < (int)leftResult.length() && leftResult[end] != '\"' && leftResult[end] != ',' && leftResult[end] != '}' && leftResult[end] != '\n') end++;
        return leftResult.substring(pos, end);
      }
      return "";
    }
    String piped = right + " " + leftResult;
    return runHostCommand(piped, echoToSerial);
  }

  const int firstSpace = line.indexOf(' ');
  String command = firstSpace >= 0 ? line.substring(0, firstSpace) : line;
  String arg = firstSpace >= 0 ? line.substring(firstSpace + 1) : "";
  command.toUpperCase();
  arg.trim();

  if (command == "PING")  return "OK PONG";

  if (command == "STATUS") return statusJson();

  if (command == "SHOW") {
    if (showImage(arg.length() ? arg : "logo")) return String("OK SHOW ") + currentScreen;
    return "ERR unknown image";
  }

  if (command == "TEXT") { drawTextMessage(arg); return "OK TEXT"; }

  if (command == "SHOW_FILE") {
    String name = arg; name.trim();
    if (!name.length()) return "ERR usage: SHOW_FILE <name.raw|.jpg>";
    if (showStoredFile(name)) return String("OK SHOW_FILE ") + normalizePath(name);
    return String("ERR SHOW_FILE ") + normalizePath(name);
  }

  if (command == "ANIM_LIST")  return animListJson();

  if (command == "ANIM_PLAY") {
    String name = arg; name.trim();
    if (!name.length()) return "ERR usage: ANIM_PLAY <name>";
    if (animStart(name)) return String("OK ANIM_PLAY ") + name + " frames=" + anim.frameCount + " fps=" + anim.fps;
    return "ERR anim not found";
  }

  if (command == "ANIM_STOP") { animStop(); return "OK ANIM_STOP"; }

  if (command == "ANIM_INFO") {
    String name = arg; name.trim();
    if (!name.length()) return "ERR usage: ANIM_INFO <name>";
    int fps, frameCount; bool loop;
    if (!loadAnimManifest(name, fps, loop, frameCount)) return "ERR anim not found";
    bool isDelta = animIsDelta(name);
    return "ANIM_INFO " + name + " fps=" + String(fps) + " frames=" + String(frameCount)
         + " loop=" + String(loop ? "yes" : "no") + " delta=" + String(isDelta ? "yes" : "no");
  }

  if (command == "SCRIPT_LIST")    return Script::listJson();

  if (command == "SCRIPT_COMPILE") {
    String name = arg; name.trim();
    if (!name.length()) return "ERR usage: SCRIPT_COMPILE <name.sh>";
    if (!name.endsWith(".sh")) name += ".sh";
    String src = name.startsWith("/") ? name : "/" + name;
    String dst = src; dst.replace(".sh", ".s2b");
    if (Script::compile(src, dst)) return String("OK COMPILED ") + dst + " (run with SCRIPT_RUN " + dst + ")";
    return "ERR compile failed";
  }

  if (command == "SCRIPT_RUN") {
    String name = arg; name.trim();
    if (!name.length()) return "ERR usage: SCRIPT_RUN <name.s2b>";
    if (!name.endsWith(".s2b")) name += ".s2b";
    String path = name.startsWith("/") ? name : "/" + name;
    return Script::run(path);
  }

  if (command == "SCRIPT_CLEAR") { scriptCache.clear(); animFileCache.close(); return "OK cache cleared"; }

  if (command == "MEM")       return heapStats();
  if (command == "TIME")      return Time::now();
  if (command == "DATE" || command == "DATETIME") return Time::dateTime();

  if (command == "TZ") {
    if (arg.length()) return Time::setTz(arg.toInt());
    return "TZ UTC" + String(Time::tzOffset >= 0 ? "+" : "") + Time::tzOffset;
  }

  if (command == "SET") {
    int eq = arg.indexOf('=');
    if (eq <= 0) return "ERR usage: SET key=value";
    String key = arg.substring(0, eq); key.trim();
    String val = arg.substring(eq + 1); val.trim();
    if (Env::set(key, val)) { Log::info("ENV set " + key); return "OK SET " + key + "=" + val; }
    return "ERR set failed";
  }

  if (command == "GET") {
    String key = arg; key.trim();
    if (!key.length()) return "ERR usage: GET <key>";
    String val = Env::get(key);
    return val.length() ? val : "(unset)";
  }

  if (command == "ENV")   return Env::listJson();
  if (command == "UNSET") { String key = arg; key.trim(); if (Env::unset(key)) { Log::info("ENV unset " + key); return "OK UNSET " + key; } return "ERR not found"; }

  if (command == "DMSG")      return Log::dump();
  if (command == "LOG_CLEAR") { Log::clear(); return "OK log cleared"; }

  if (command == "LLM_ASK") {
    if (!llmEnabled) return "ERR LLM not enabled. Use LLM_START first.";
    if (!arg.length()) return "ERR usage: LLM_ASK <prompt>";
    drawTitle("LLM thinking...", TFT_SKYBLUE);
    String result = LLM::ask(arg);
    drawTitle("LLM Response", TFT_ORANGE);
    int ly = 36;
    for (size_t i = 0; i < result.length() && ly < 220; i += 36) {
      drawText5x7(4, ly, result.substring(i, i+36), TFT_WHITE, TFT_BLACK, 1);
      ly += 10;
    }
    Log::info("LLM result: " + result.substring(0, 64));
    return result;
  }

  if (command == "LLM_LAST") { String cached = LLM::cache(); return cached.length() ? cached : "(no cached response)"; }

  if (command == "NET_START")  return netStart();
  if (command == "NET_STOP")   return netStop();
  if (command == "LLM_START")  return llmStart();
  if (command == "LLM_STOP")   return llmStop();

  if (command == "SPAWN" || command == "SCRIPT_SPAWN") {
    String name = arg; name.trim();
    if (!name.length()) return "ERR usage: SPAWN <name.s2b> [prio:0-3]";
    uint8_t prio = PRIO_NORM;
    int sp2 = name.lastIndexOf(' ');
    if (sp2 > 0) { prio = constrain(name.substring(sp2+1).toInt(), 0, 3); name = name.substring(0, sp2); }
    if (!name.endsWith(".s2b")) name += ".s2b";
    String path = name.startsWith("/") ? name : "/" + name;
    int pid = ProcMgr::spawn(name, path, prio);
    return pid > 0 ? String("OK SPAWN pid=") + pid : "ERR spawn failed";
  }

  if (command == "PS")         return ProcMgr::listJson();
  if (command == "KILL")       { int pid = arg.toInt(); if (pid <= 0) return "ERR usage: KILL <pid>"; return ProcMgr::kill(pid) ? String("OK KILL ") + pid : "ERR not found"; }
  if (command == "KILLALL")    { ProcMgr::killAll(); return "OK KILLALL"; }

  if (command == "SIGNAL") {
    String a = arg; a.toLowerCase();
    if (a == "idle")                { Signal::setMode(Signal::IDLE);      return "OK SIGNAL idle"; }
    if (a == "working"||a=="executing") { Signal::setMode(Signal::WORKING);   return "OK SIGNAL working"; }
    if (a == "thinking")            { Signal::setMode(Signal::THINKING);  return "OK SIGNAL thinking"; }
    if (a == "attention")           { Signal::setMode(Signal::ATTENTION); return "OK SIGNAL attention"; }
    if (a == "blocked"||a=="permission") { Signal::setMode(Signal::BLOCKED);  return "OK SIGNAL blocked"; }
    if (a == "off")                 { Signal::setMode(Signal::OFF);       return "OK SIGNAL off"; }
    if (a == "status")              { return Signal::status(); }
    int sp = arg.indexOf(' '); if (sp <= 0) return "ERR usage: SIGNAL <pid> <sig> or SIGNAL idle|working|attention|blocked";
    int pid = arg.substring(0, sp).toInt(); uint8_t sig = arg.substring(sp+1).toInt();
    return ProcMgr::signal(pid, sig) ? String("OK SIGNAL pid=")+pid+" sig="+sig : "ERR";
  }

  if (command == "TIMER") { int sp = arg.indexOf(' '); if (sp <= 0) return "ERR usage: TIMER <ms> <command>"; uint32_t ms = arg.substring(0, sp).toInt(); String cmd = arg.substring(sp+1); ProcMgr::timerAdd(ms, cmd, false); return "OK TIMER " + String(ms) + "ms"; }
  if (command == "CRON")  { int sp = arg.indexOf(' '); if (sp <= 0) return "ERR usage: CRON <ms> <command>";  uint32_t ms = arg.substring(0, sp).toInt(); String cmd = arg.substring(sp+1); ProcMgr::timerAdd(ms, cmd, true);  return "OK CRON every " + String(ms) + "ms"; }
  if (command == "TIMER_LIST") return ProcMgr::timerList();

  if (command == "HOOK") { int sp = arg.indexOf(' '); if (sp <= 0) return "ERR usage: HOOK <event> <command>"; String evt = arg.substring(0, sp); String cmd = arg.substring(sp+1); int id = ProcMgr::hookAdd(evt, cmd); return id >= 0 ? "OK HOOK #" + String(id) : "ERR hook table full"; }
  if (command == "HOOK_LIST") return ProcMgr::hookList();

  if (command == "INTR_LIST") return intrListJson();
  if (command == "INTR_OFF")  { hwIntrEnabled = false; detachInterrupt(digitalPinToInterrupt(PIN_BUTTON)); return "OK interrupts disabled (polling fallback)"; }
  if (command == "INTR_ON")   { hwIntrEnabled = true;  setupHardwareInterrupts(); return "OK interrupts enabled"; }

  if (command == "MODULE_LIST" || command == "MOD_LIST")       return Module::listJson();
  if (command == "MODULE_LOAD" || command == "MOD_LOAD")       { String name = arg; name.trim(); if (!name.length()) return "ERR usage: MODULE_LOAD <name>"; return Module::load(name); }
  if (command == "MODULE_UNLOAD" || command == "MOD_UNLOAD")   return Module::unload();
  if (command == "MODULE_STATUS" || command == "MOD_STATUS")   return Module::status();

  if (command == "WDT_ENABLE")  { ESP.wdtEnable(5000); Log::info("WDT enabled 5s"); return "OK WDT 5s timeout"; }
  if (command == "WDT_DISABLE") { ESP.wdtDisable(); Log::info("WDT disabled"); return "OK WDT disabled"; }
  if (command == "WDT_FEED")    { ESP.wdtFeed(); return "OK WDT fed"; }

  if (command == "OTA") { if (!arg.length()) return "ERR usage: OTA <http://host/firmware.bin>"; return otaUpdate(arg); }
  if (command == "DNS") { if (!arg.length()) return DNS::listJson(); return String("DNS ") + arg + " -> " + DNS::resolve(arg); }
  if (command == "DNS_LIST") return DNS::listJson();

  if (command == "IMG_BEGIN") {
    if (!fsMounted) return "ERR LittleFS not mounted";
    const int split = arg.lastIndexOf(' ');
    if (split <= 0) return "ERR usage: IMG_BEGIN <name.raw> <bytes>";
    uploadName = arg.substring(0, split); uploadName.trim();
    if (!uploadName.startsWith("/")) uploadName = "/" + uploadName;
    uploadExpected = arg.substring(split + 1).toInt();
    if (!uploadExpected || uploadExpected > 300000) return "ERR invalid byte count";
    if (uploadFile) uploadFile.close();
    uploadFile = LittleFS.open(uploadName, "w");
    if (!uploadFile) return "ERR cannot open file";
    uploadReceived = 0; serialUploadActive = true;
    return String("READY IMG_HEX ") + uploadName + " bytes=" + uploadExpected;
  }

  if (command == "IMG_BIN_BEGIN") {
    if (!fsMounted) return "ERR LittleFS not mounted";
    const int split = arg.lastIndexOf(' ');
    if (split <= 0) return "ERR usage: IMG_BIN_BEGIN <name.raw> <bytes>";
    uploadName = arg.substring(0, split); uploadName.trim();
    if (!uploadName.startsWith("/")) uploadName = "/" + uploadName;
    uploadExpected = arg.substring(split + 1).toInt();
    if (!uploadExpected || uploadExpected > 300000) return "ERR invalid byte count";
    if (uploadFile) uploadFile.close();
    uploadFile = LittleFS.open(uploadName, "w");
    if (!uploadFile) return "ERR cannot open file";
    uploadReceived = 0; uploadNextAck = 256; serialBinaryUploadActive = true;
    return String("READY IMG_BIN ") + uploadName + " bytes=" + uploadExpected;
  }

  if (command == "BEEP") {
    uint16_t freq = 2200, ms = 70;
    const int split = arg.indexOf(' ');
    if (arg.length()) {
      if (split > 0) { freq = arg.substring(0, split).toInt(); ms = arg.substring(split + 1).toInt(); }
      else { freq = arg.toInt(); }
    }
    if (freq < 100) freq = 2200;
    if (ms < 10 || ms > 1000) ms = 70;
    beep(freq, ms);
    return String("OK BEEP freq=") + freq + " ms=" + ms;
  }

  if (command == "BRIGHT" || command == "BACKLIGHT") {
    if (!arg.length()) return String("BRIGHT ") + backlightPercent;
    const int value = constrain(arg.toInt(), 0, 100);
    setBacklightPercent(value);
    return String("OK BRIGHT ") + value;
  }

  if (command == "WIFI") {
    const int split = arg.indexOf(' ');
    if (split <= 0) return "ERR usage: WIFI <ssid> <pass>";
    staSsid = arg.substring(0, split); staPass = arg.substring(split + 1);
    saveWifiConfig(staSsid, staPass); requestStaConnect();
    return String("OK WIFI ") + staSsid;
  }

  if (command == "WIFI?") return String("WIFI ap=") + apSsid + " sta=" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "not-connected");
  if (command == "WIFI_CLEAR") { staSsid = ""; staPass = ""; if (fsMounted && LittleFS.exists(WIFI_CONFIG_PATH)) LittleFS.remove(WIFI_CONFIG_PATH); WiFi.disconnect(); return "OK WIFI_CLEAR"; }

  if (command == "GPIO") {
    return "GPIO button=" + String(digitalRead(PIN_BUTTON) == LOW ? "DOWN" : "UP")
         + " backlight=" + String(backlightPercent) + " buzzer=GPIO16 tft=sck14,mosi13,cs15,dc0,rst2";
  }

  if (command == "FS") {
    if (!fsMounted) return "FS not-mounted";
    FSInfo info; LittleFS.info(info);
    return "FS used=" + String(info.usedBytes) + " total=" + String(info.totalBytes) + " wifi_cfg=" + String(LittleFS.exists(WIFI_CONFIG_PATH) ? "yes" : "no");
  }

  // ---- Pet commands ----
  if (command == "PET_UPLOAD") {
    int sp1 = arg.indexOf(' '); if (sp1 < 0) return "ERR PET_UPLOAD <slug> <num_states>";
    String slug = arg.substring(0, sp1); slug.trim();
    int nstates = arg.substring(sp1+1).toInt();
    if (!slug.length() || nstates <= 0) return "ERR invalid args";
    petUploadSlug = slug; petUploadStates = nstates; petUploadDone = 0;
    String dir = "/pet/" + slug;
    ESP.wdtFeed();
    if (!LittleFS.exists("/pet")) LittleFS.mkdir("/pet");
    ESP.wdtFeed();
    if (LittleFS.exists(dir)) {
      Dir d = LittleFS.openDir(dir);
      while (d.next()) { LittleFS.remove(dir + "/" + d.fileName()); ESP.wdtFeed(); }
      LittleFS.rmdir(dir);
    }
    ESP.wdtFeed(); LittleFS.mkdir(dir); ESP.wdtFeed();
    File m = LittleFS.open(dir + "/meta.txt", "w"); m.println(slug); m.println(nstates); m.close();
    Log::info("PET_UPLOAD start " + slug + " states=" + String(nstates));
    return "OK PET_UPLOAD " + slug;
  }

  if (command == "PET_STATE") {
    if (petUploadSlug.length() == 0) return "ERR no upload session";
    int sp1 = arg.indexOf(' '); if (sp1 < 0) return "ERR PET_STATE <name> <nframes>";
    petUploadCurState = arg.substring(0, sp1); petUploadCurState.trim();
    petUploadCurFrames = arg.substring(sp1+1).toInt(); petUploadCurIdx = 0;
    if (petUploadCurFrames <= 0 || petUploadCurFrames > 20) return "ERR frames 1-20";
    return "OK PET_STATE " + petUploadCurState;
  }

  if (command == "PET_FRAME") {
    if (petUploadCurState.length() == 0) return "ERR no state";
    int sp1 = arg.indexOf(' '); if (sp1 < 0) return "ERR PET_FRAME <idx> <size>";
    int idx = arg.substring(0, sp1).toInt(); int sz = arg.substring(sp1+1).toInt();
    if (sz <= 0 || sz > 8192) return "ERR size";
    String path = "/pet/" + petUploadSlug + "/" + petUploadCurState + "_" + String(idx) + ".jpg";
    petBinaryFile = LittleFS.open(path, "w");
    if (!petBinaryFile) return "ERR file create";
    petBinaryActive = true; petBinarySize = sz; petBinaryGot = 0; petBinaryIdx = idx;
    Serial.println("READY");
    return "";
  }

  if (command == "PET_SAVE") {
    if (petUploadSlug.length() == 0) return "ERR no upload session";
    String dir = "/pet/" + petUploadSlug;
    File m = LittleFS.open(dir + "/meta.txt", "a"); m.println("done"); m.close();
    Log::info("PET_SAVE " + petUploadSlug + " frames=" + String(petUploadDone));
    petUploadSlug = ""; petUploadCurState = "";
    return "OK PET_SAVE " + String(petUploadDone) + " frames";
  }

  if (command == "PET_SELECT") {
    arg.trim(); String dir = "/pet/" + arg;
    if (!LittleFS.exists(dir + "/meta.txt")) return "ERR pet not found";
    petActive = arg; petFrameIdx = 0; petLastMs = 0; petCurState = "idle";
    savePetCfg();
    Log::info("PET_SELECT " + arg);
    return "OK PET_SELECT " + arg;
  }

  if (command == "PET_LIST") {
    if (!LittleFS.exists("/pet")) return "OK 0 pets";
    Dir d = LittleFS.openDir("/pet"); String list; int n = 0;
    while (d.next()) { if (d.isDirectory()) { if (n++) list += ","; list += d.fileName(); } }
    return "OK " + String(n) + " pets:" + list;
  }

  if (command == "PET_DELETE") {
    arg.trim(); String dir = "/pet/" + arg;
    if (!LittleFS.exists(dir)) return "ERR not found";
    Dir d = LittleFS.openDir(dir);
    while (d.next()) LittleFS.remove(dir + "/" + d.fileName());
    LittleFS.rmdir(dir);
    if (petActive == arg) petActive = "";
    return "OK PET_DELETE " + arg;
  }

  if (command == "PET_SHOW") { petShowActive = true;  savePetCfg(); return "OK PET_SHOW"; }
  if (command == "PET_HIDE") { petShowActive = false; savePetCfg(); return "OK PET_HIDE"; }

  if (command == "REBOOT") { if (echoToSerial) Serial.println(F("OK REBOOT")); delay(100); ESP.restart(); return "OK REBOOT"; }
  if (command == "BT" || command == "BLUETOOTH") return "ERR Bluetooth unsupported: ESP8266EX has no Bluetooth radio";

  if (command == "HELP") { if (echoToSerial) printHelp(Serial); return "OK HELP"; }

  if (echoToSerial) printHelp(Serial);
  return "ERR unknown command";
}

// ---- Serial Shell ----
inline void pollSerial() {
  if (serialBinaryUploadActive) { pollBinaryUpload(); return; }
  if (petBinaryActive)          { pollPetBinary();    return; }

  while (Serial.available() && !petBinaryActive) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      if (serialUploadActive) handleUploadHexLine(serialLine);
      else { String reply = runHostCommand(serialLine, true); if (reply.length()) Serial.println(reply); }
      serialLine = "";
    } else if (serialLine.length() < 600) {
      serialLine += c;
    }
  }
}

// ---- TCP Shell ----
inline void pollTcpShell() {
  if (!shellClient || !shellClient.connected()) {
    WiFiClient incoming = shellServer.accept();
    if (incoming) {
      if (shellClient) shellClient.stop();
      shellClient = incoming;
      shellClient.setNoDelay(true);
      shellAuthed = false; shellLine = "";
      shellClient.println(F("SD2 on-chip shell"));
      shellClient.println(F("Transport: raw TCP, not SSH"));
      shellClient.print(F("password: "));
    }
    return;
  }

  while (shellClient.available()) {
    const char c = static_cast<char>(shellClient.read());
    if (c == '\r') continue;
    if (c == '\n') {
      shellLine.trim();
      if (!shellAuthed) {
        if (shellLine == SHELL_PASSWORD) {
          shellAuthed = true; shellClient.println(); shellClient.println(F("OK login"));
          printHelp(shellClient); shellClient.print(F("sd2> "));
        } else { shellClient.println(); shellClient.println(F("ERR bad password")); shellClient.stop(); }
      } else {
        String reply = runHostCommand(shellLine, false);
        if (reply.length()) shellClient.println(reply);
        shellClient.print(F("sd2> "));
      }
      shellLine = "";
    } else if (c == 8 || c == 127) {
      if (shellLine.length()) shellLine.remove(shellLine.length() - 1);
    } else if (shellLine.length() < 180 && c >= 32 && c <= 126) {
      shellLine += c;
    }
  }
}
