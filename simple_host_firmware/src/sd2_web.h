#pragma once
// ============================================================
//  SD2-OS: Web Server (HTTP Handlers + WebSocket)
// ============================================================

// ---- Status / Screen helpers ----
inline void drawStatus() {
  extern String currentScreen;
  currentScreen = "status";
  int bw = Signal::BORDER_W;

  tft.fillScreen(TFT_BLACK);
  tft.fillRect(bw, bw, SCREEN_W - 2*bw, 22, TFT_CYAN);
  drawText5x7((SCREEN_W - 11*12)/2, bw + 3, "HOST READY", TFT_BLACK, TFT_CYAN, 2);

  int ly = bw + 28;
  int lx = bw + 4;
  drawText5x7(lx, ly, "SD2-OS v1.0", TFT_WHITE, TFT_BLACK, 2); ly += 18;
  drawText5x7(lx, ly, "AP: " + apSsid, TFT_WHITE, TFT_BLACK, 2); ly += 18;
  drawText5x7(lx, ly, "IP: 192.168.4.1", TFT_WHITE, TFT_BLACK, 2); ly += 18;
  drawText5x7(lx, ly, "WS:81 TCP:2323", TFT_WHITE, TFT_BLACK, 2); ly += 18;
  drawText5x7(lx, ly, "Heap:" + String(ESP.getFreeHeap()), TFT_WHITE, TFT_BLACK, 2);

  if (Signal::curR || Signal::curG || Signal::curB) {
    Signal::drawTftBorder(Signal::curR, Signal::curG, Signal::curB);
  }
}

inline void drawBars() {
  extern String currentScreen;
  currentScreen = "bars";
  static const uint16_t colors[] = {
    TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_YELLOW, TFT_MAGENTA, TFT_CYAN, TFT_BLACK,
  };
  for (uint8_t i = 0; i < 8; i++) {
    const uint16_t x = (i % 4) * 60;
    const uint16_t y = (i / 4) * 120;
    tft.fillRect(x, y, 60, 120, colors[i]);
  }
}

inline void drawLogo() {
  extern String currentScreen;
  currentScreen = "logo";
  drawTitle("SD2 TV", TFT_SKYBLUE);

  tft.fillRoundRect(38, 58, 164, 118, 10, rgb565(30, 34, 48));
  tft.drawRoundRect(38, 58, 164, 118, 10, TFT_WHITE);

  for (int y = 70; y < 152; y++) {
    const uint8_t mix = map(y, 70, 151, 0, 255);
    tft.drawFastHLine(52, y, 136, rgb565(20 + mix / 8, 90 + mix / 5, 180 - mix / 8));
  }

  tft.fillCircle(78, 92, 18, TFT_YELLOW);
  tft.fillTriangle(52, 152, 108, 105, 150, 152, rgb565(30, 150, 80));
  tft.fillTriangle(95, 152, 150, 98, 190, 152, rgb565(20, 110, 95));
  drawText5x7(87, 182, "HELLO", TFT_WHITE, TFT_BLACK, 2);
}

inline void drawFace() {
  extern String currentScreen;
  currentScreen = "face";
  drawTitle("TOUCH ME", TFT_GREEN);
  tft.fillCircle(120, 120, 66, TFT_YELLOW);
  tft.fillCircle(96, 102, 8, TFT_BLACK);
  tft.fillCircle(144, 102, 8, TFT_BLACK);
  tft.drawWideLine(88, 146, 152, 146, 5, TFT_BLACK, TFT_BLACK);
  tft.fillCircle(88, 146, 3, TFT_BLACK);
  tft.fillCircle(152, 146, 3, TFT_BLACK);
  drawText5x7((SCREEN_W - 13*6)/2, 210, String("Presses ") + buttonPresses, TFT_WHITE, TFT_BLACK, 2);
}

inline bool showImage(const String& imageName) {
  String name = imageName;
  name.toLowerCase();
  if (name == "logo")   { drawLogo();   return true; }
  if (name == "face")   { drawFace();   return true; }
  if (name == "bars")   { drawBars();   return true; }
  if (name == "status") { drawStatus(); return true; }
  return false;
}

// ---- File display ----
inline String normalizePath(String name) {
  name.trim();
  name.replace("\\", "/");
  while (name.startsWith("/")) name.remove(0, 1);
  name.replace("..", "");
  if (!name.length()) name = "upload.bin";
  return "/" + name;
}

// ---- JSON helpers ----
inline String statusJson() {
  String json = "{";
  json += "\"ok\":true,";
  json += "\"chip\":\"ESP8266EX\",";
  json += "\"bluetooth\":\"unsupported\",";
  json += "\"ap_ssid\":\"" + jsonEscape(apSsid) + "\",";
  json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"sta_ssid\":\"" + jsonEscape(staSsid) + "\",";
  json += "\"sta_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"sta_ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("")) + "\",";
  json += "\"websocket\":\"ws://" + WiFi.softAPIP().toString() + ":81/\",";
  json += "\"screen\":\"" + jsonEscape(currentScreen) + "\",";
  json += "\"button_presses\":" + String(buttonPresses) + ",";
  json += "\"backlight\":" + String(backlightPercent) + ",";
  json += "\"shell_tcp\":\"" + WiFi.softAPIP().toString() + ":2323\",";
  json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"littlefs\":" + String(fsMounted ? "true" : "false") + ",";
  json += "\"heap_free\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"heap_max_block\":" + String(getMaxFreeBlock()) + ",";
  json += "\"anim_playing\":" + String(anim.playing ? "true" : "false");
  if (anim.playing) {
    json += ",\"anim_name\":\"" + jsonEscape(anim.name) + "\"";
    json += ",\"anim_frame\":" + String(anim.currentFrame);
    json += ",\"anim_fps\":" + String(anim.fps);
  }
  json += "}";
  return json;
}

inline void sendJson(int code, const String& body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", body);
}

inline String filesJson() {
  String json = "{\"ok\":true,\"files\":[";
  Dir dir = LittleFS.openDir("/");
  bool first = true;
  while (dir.next()) {
    if (!first) json += ",";
    first = false;
    json += "{\"name\":\"" + jsonEscape(dir.fileName()) + "\",\"size\":" + String(dir.fileSize()) + "}";
  }
  json += "]}";
  return json;
}

// ---- JPEG rendering ----
bool jpegDrawCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

bool drawJpgFile(const String& path) {
  if (!fsMounted || !LittleFS.exists(path)) return false;
  tft.fillScreen(TFT_BLACK);
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(jpegDrawCallback);
  const JRESULT rc = TJpgDec.drawFsJpg(0, 0, path.c_str(), LittleFS);
  if (rc != JDR_OK) return false;
  currentScreen = path;
  return true;
}

bool drawJpgFileAnim(const String& path) {
  if (!fsMounted || !LittleFS.exists(path)) return false;
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(jpegDrawCallback);
  tft.startWrite();
  const JRESULT rc = TJpgDec.drawFsJpg(0, 0, path.c_str(), LittleFS);
  tft.endWrite();
  if (rc != JDR_OK) return false;
  currentScreen = path;
  return true;
}

bool drawRaw565File(const String& path) {
  if (!fsMounted || !LittleFS.exists(path)) return false;
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  tft.startWrite();
  tft.setAddrWindow(0, 0, SCREEN_W, SCREEN_H);
  uint8_t buffer[480];
  uint32_t pixels = static_cast<uint32_t>(SCREEN_W) * SCREEN_H;
  while (pixels > 0 && file.available()) {
    const size_t wantPixels = pixels > 240 ? 240 : pixels;
    const size_t wantBytes = wantPixels * 2;
    const size_t got = file.read(buffer, wantBytes);
    if (got == 0) break;
    tft.pushPixels(reinterpret_cast<uint16_t*>(buffer), got / 2);
    pixels -= got / 2;
    yield();
  }
  tft.endWrite();
  file.close();
  currentScreen = path;
  return pixels == 0;
}

bool drawDeltaFrame(const String& binPath) {
  if (!LittleFS.exists(binPath)) return false;
  File f = LittleFS.open(binPath, "r");
  if (!f) return false;
  uint8_t header[2];
  if (f.read(header, 2) != 2) { f.close(); return false; }
  uint16_t rectCount = (header[0] << 8) | header[1];
  if (rectCount == 0 || rectCount > 64) { f.close(); return false; }
  uint16_t lineBuf[SCREEN_W];
  tft.startWrite();
  for (uint16_t i = 0; i < rectCount; i++) {
    uint8_t rh[8];
    if (f.read(rh, 8) != 8) break;
    uint16_t rx = (rh[0] << 8) | rh[1];
    uint16_t ry = (rh[2] << 8) | rh[3];
    uint16_t rw = (rh[4] << 8) | rh[5];
    uint16_t rh_h = (rh[6] << 8) | rh[7];
    if (rw == 0 || rh_h == 0 || rx + rw > SCREEN_W || ry + rh_h > SCREEN_H) continue;
    for (uint16_t yy = 0; yy < rh_h; yy++) {
      size_t toRead = rw * 2;
      size_t got = f.read(reinterpret_cast<uint8_t*>(lineBuf), toRead);
      if (got != toRead) break;
      tft.pushImage(rx, ry + yy, rw, 1, lineBuf);
    }
  }
  tft.endWrite();
  f.close();
  currentScreen = binPath;
  return true;
}

inline bool showStoredFile(String name) {
  name = normalizePath(name);
  String lower = name;
  lower.toLowerCase();
  if (lower.endsWith(".raw") || lower.endsWith(".rgb565")) return drawRaw565File(name);
  if (lower.endsWith(".jpg") || lower.endsWith(".jpeg"))    return drawJpgFile(name);
  return false;
}

// ---- HTTP Handler declarations ----
void handleRoot();
void handleStatus();
void handleShow();
void handleShowFile();
void handleAnimList();
void handleAnimPlay();
void handleAnimStop();
void handleScriptList();
void handleScriptCompile();
void handleScriptRun();
void handleModuleList();
void handleModuleLoad();
void handleModuleUnload();
void handleModuleStatus();
void handleSignal();
void handleCmd();
void handleStatusJson();
void handlePanel();
void handleFiles();
void handleUploadDone();
void handleUploadData();
void handleText();
void handleBeep();
void handleBluetooth();
void handleWifiPage();
void handleWifiConnect();
void handleWifiClear();

// ---- HTTP Handler implementations ----
inline void handleRoot() {
  const String html = R"HTML(
<!doctype html><meta name=viewport content="width=device-width,initial-scale=1"><title>SD2-OS</title>
<style>body{font:14px system-ui;margin:8px;background:#10131a;color:#eee}
button,input,select{font:inherit;margin:3px;padding:6px 8px;border-radius:5px;border:1px solid #556;background:#222;color:#fff;font-size:13px}
button:active{background:#3a3}</style>
<h3>SD2-OS</h3>
<input type=file id=F multiple><button onclick="U()">Upload</button><span id=S></span>
<p><button onclick="G('/show?img=logo')">Logo</button><button onclick="G('/show?img=face')">Face</button><button onclick="G('/show?img=bars')">Bars</button><button onclick="G('/beep')">Beep</button>
<input id=M value=hello size=8><button onclick="G('/text?msg='+M.value)">Text</button></p>
<p><select id=L></select><button onclick="G('/show_file?name='+L.value)">Show</button>
<select id=A></select><button onclick="G('/anim/play?name='+A.value)">Play</button><button onclick="G('/anim/stop')">Stop</button></p>
<p><input id=W placeholder=SSID size=10><input id=P placeholder=Pass type=password size=8><button onclick="G('/wifi/connect?ssid='+W.value+'&pass='+P.value)">Join</button></p>
<pre id=O>...</pre>
<script>let w;setInterval(function(){try{let x=new WebSocket('ws://'+location.hostname+':81/');x.onmessage=e=>O.textContent=e.data;w=x}catch(e){}R()},4000);R();
async function G(u){O.textContent=await(await fetch(u)).text()}
async function R(){try{let j=await(await fetch('/status')).json();O.textContent=j.screen+' '+j.heap_free}(e){}
try{let j=await(await fetch('/files')).json();L.innerHTML=j.files.filter(f=>!f.name.includes('/anim/')).map(f=>'<option>'+f.name+'</option>').join('')}(e){}
try{let j=await(await fetch('/anim/list')).json();A.innerHTML=j.anims.map(a=>'<option>'+a.name+'</option>').join('')}(e){}}
async function U(){let f=F.files;for(let x of f){let d=new FormData();d.append('file',x,'/'+x.name);S.textContent='Uploading...';let r=await fetch('/upload',{method:'POST',body:d});S.textContent=r.status==200?'OK':'ERR';if(x.name.endsWith('.sh')){await fetch('/script/compile?name='+x.name.replace('.sh',''));S.textContent+=' compiled'}await new Promise(r=>setTimeout(r,400));R()}}</script>
)HTML";
  server.send(200, "text/html", html);
}

inline void handleStatus()       { sendJson(200, statusJson()); }
inline void handleAnimList()     { sendJson(200, animListJson()); }
inline void handleScriptList()   { sendJson(200, Script::listJson()); }
inline void handleModuleList()   { sendJson(200, Module::listJson()); }
inline void handleFiles()        { sendJson(200, filesJson()); }

inline void handleShow() {
  const String imageName = server.hasArg("img") ? server.arg("img") : "logo";
  if (showImage(imageName) || showStoredFile(imageName)) {
    sendJson(200, String("{\"ok\":true,\"screen\":\"") + currentScreen + "\"}");
    return;
  }
  sendJson(404, "{\"ok\":false,\"error\":\"unknown image\"}");
}

inline void handleShowFile() {
  const String name = server.hasArg("name") ? server.arg("name") : "";
  if (!name.length() || !showStoredFile(name)) {
    sendJson(404, "{\"ok\":false,\"error\":\"file not found or unsupported\"}");
    return;
  }
  sendJson(200, String("{\"ok\":true,\"screen\":\"") + currentScreen + "\"}");
}

inline void handleAnimPlay() {
  const String name = server.hasArg("name") ? server.arg("name") : "";
  if (!name.length() || !animStart(name)) {
    sendJson(404, "{\"ok\":false,\"error\":\"anim not found\"}");
    return;
  }
  sendJson(200, String("{\"ok\":true,\"anim\":\"") + name + "\",\"frames\":" + anim.frameCount + ",\"fps\":" + anim.fps + "}");
}

inline void handleAnimStop() {
  animStop();
  sendJson(200, "{\"ok\":true,\"stopped\":true}");
}

inline void handleScriptCompile() {
  String name = server.hasArg("name") ? server.arg("name") : "";
  if (!name.length() || !name.endsWith(".sh")) name += ".sh";
  String src = name.startsWith("/") ? name : "/" + name;
  String dst = src;
  dst.replace(".sh", ".s2b");
  if (Script::compile(src, dst)) {
    sendJson(200, "{\"ok\":true,\"compiled\":\"" + jsonEscape(dst) + "\"}");
  } else {
    sendJson(400, "{\"ok\":false,\"error\":\"compile failed\"}");
  }
}

inline void handleScriptRun() {
  String name = server.hasArg("name") ? server.arg("name") : "";
  if (!name.length()) { sendJson(400, "{\"ok\":false}"); return; }
  if (!name.endsWith(".s2b")) name += ".s2b";
  String path = name.startsWith("/") ? name : "/" + name;
  String result = Script::run(path);
  bool ok = result.startsWith("OK");
  sendJson(ok ? 200 : 400, "{\"ok\":" + String(ok ? "true" : "false") + ",\"result\":\"" + jsonEscape(result) + "\"}");
}

inline void handleModuleLoad() {
  String name = server.hasArg("name") ? server.arg("name") : "";
  if (!name.length()) { sendJson(400, "{\"ok\":false}"); return; }
  String result = Module::load(name);
  bool ok = result.startsWith("OK");
  sendJson(ok ? 200 : 400, "{\"ok\":" + String(ok ? "true" : "false") + ",\"result\":\"" + jsonEscape(result) + "\"}");
}

inline void handleModuleUnload() {
  String result = Module::unload();
  sendJson(200, "{\"ok\":true,\"result\":\"" + jsonEscape(result) + "\"}");
}

inline void handleModuleStatus() {
  sendJson(200, "{\"ok\":true,\"status\":\"" + jsonEscape(Module::status()) + "\"}");
}

inline void handleSignal() {
  String mode = server.hasArg("mode") ? server.arg("mode") : "status";
  mode.toLowerCase();
  if (mode == "idle")             Signal::setMode(Signal::IDLE);
  else if (mode == "working")     Signal::setMode(Signal::WORKING);
  else if (mode == "attention")   Signal::setMode(Signal::ATTENTION);
  else if (mode == "blocked"||mode=="permission") Signal::setMode(Signal::BLOCKED);
  else if (mode == "off")         Signal::setMode(Signal::OFF);
  sendJson(200, "{\"ok\":true,\"signal\":\"" + Signal::status() + "\"}");
}

inline void handleCmd() {
  String c = server.arg("c");
  if (!c.length()) { sendJson(400, "{\"ok\":false}"); return; }
  c.trim();
  String result = runHostCommand(c, false);
  server.send(200, "text/plain", result.length() ? result : "OK");
}

inline void handleStatusJson() {
  const char* snames[] = {"off","idle","working","attention","blocked"};
  String json = "{\"signal\":\"" + String(snames[Signal::mode]) + "\"";
  json += ",\"heap\":" + String(ESP.getFreeHeap());
  json += ",\"uptime\":" + String((millis()-bootMs)/1000);
  json += ",\"ap\":\"" + apSsid + "\"";
  json += ",\"ip\":\"192.168.4.1\"";
  json += ",\"pet\":" + String(petShowActive?"true":"false");
  json += ",\"bright\":" + String(backlightPercent);
  json += "}";
  server.send(200, "application/json", json);
}

inline void handlePanel() {
  if (LittleFS.exists("/panel.html")) {
    File f = LittleFS.open("/panel.html", "r");
    server.streamFile(f, "text/html");
    f.close();
  } else {
    server.send(200, "text/html", "<html><body style='font-family:sans-serif;padding:20px;background:#0a0a1a;color:#fff'><h2>Panel not uploaded</h2><p>Run: <code>pio run -t uploadfs</code></p></body></html>");
  }
}

inline void handleUploadDone() {
  if (uploadName.length()) {
    sendJson(200, String("{\"ok\":true,\"name\":\"") + jsonEscape(uploadName) + "\",\"bytes\":" + uploadReceived + "}");
  } else {
    sendJson(400, "{\"ok\":false,\"error\":\"no upload\"}");
  }
}

inline void handleUploadData() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadName = normalizePath(upload.filename.length() ? upload.filename : server.arg("name"));
    uploadReceived = 0;
    if (uploadFile) uploadFile.close();
    uploadFile = LittleFS.open(uploadName, "w");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) { uploadFile.write(upload.buf, upload.currentSize); uploadReceived += upload.currentSize; }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
  }
}

inline void handleText() {
  const String message = server.hasArg("msg") ? server.arg("msg") : "";
  drawTextMessage(message.length() ? message : "(empty)");
  sendJson(200, "{\"ok\":true,\"screen\":\"text\"}");
}

inline void handleBeep() {
  beep();
  sendJson(200, "{\"ok\":true,\"beep\":true}");
}

inline void handleBluetooth() {
  sendJson(200, "{\"ok\":false,\"bluetooth\":\"unsupported\",\"reason\":\"ESP8266EX has no Bluetooth radio\"}");
}

inline void handleWifiPage() {
  String json = "{\"ap\":\"" + apSsid + "\",\"ip\":\"" + WiFi.softAPIP().toString() + "\"";
  json += ",\"sta\":" + String(netEnabled && WiFi.status()==WL_CONNECTED ? "true":"false");
  sendJson(200, json + "}");
}

inline void handleWifiConnect() {
  const String ssid = server.arg("ssid");
  const String pass = server.arg("pass");
  if (!ssid.length()) { sendJson(400, "{\"ok\":false,\"error\":\"missing ssid\"}"); return; }
  staSsid = ssid;
  staPass = pass;
  const bool saved = saveWifiConfig(staSsid, staPass);
  requestStaConnect();
  sendJson(200, String("{\"ok\":true,\"saved\":") + (saved ? "true" : "false") + ",\"sta_ssid\":\"" + jsonEscape(staSsid) + "\"}");
}

inline void handleWifiClear() {
  staSsid = "";
  staPass = "";
  if (fsMounted && LittleFS.exists(WIFI_CONFIG_PATH)) LittleFS.remove(WIFI_CONFIG_PATH);
  WiFi.disconnect();
  sendJson(200, "{\"ok\":true,\"cleared\":true}");
}

// ---- WebSocket ----
inline void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    String reply = statusJson();
    webSocket.sendTXT(num, reply);
    return;
  }
  if (type != WStype_TEXT) return;
  String line;
  line.reserve(length);
  for (size_t i = 0; i < length; i++) line += static_cast<char>(payload[i]);
  String reply = runHostCommand(line, false);
  if (reply.length()) webSocket.sendTXT(num, reply);
}

// ---- Server Setup ----
inline void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/show", handleShow);
  server.on("/show_file", handleShowFile);
  server.on("/files", handleFiles);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadData);
  server.on("/text", handleText);
  server.on("/beep", handleBeep);
  server.on("/bt", handleBluetooth);
  server.on("/wifi", handleWifiPage);
  server.on("/wifi/connect", handleWifiConnect);
  server.on("/wifi/clear", handleWifiClear);
  server.on("/anim/list", handleAnimList);
  server.on("/anim/play", handleAnimPlay);
  server.on("/anim/stop", handleAnimStop);
  server.on("/script/list", handleScriptList);
  server.on("/script/compile", handleScriptCompile);
  server.on("/script/run", handleScriptRun);
  server.on("/module/list", handleModuleList);
  server.on("/module/load", handleModuleLoad);
  server.on("/module/unload", handleModuleUnload);
  server.on("/module/status", handleModuleStatus);
  server.on("/signal", handleSignal);
  server.on("/cmd", handleCmd);
  server.on("/status.json", handleStatusJson);
  server.on("/panel", handlePanel);
  server.onNotFound([]() { sendJson(404, "{\"ok\":false,\"error\":\"not found\"}"); });
  server.begin();
}
