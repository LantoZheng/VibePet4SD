#pragma once
// ============================================================
//  SD2-OS: Pet System (Built-in Blob + JPEG Pet Rendering)
// ============================================================

inline void savePetCfg() {
  File f = LittleFS.open("/etc/pet.cfg", "w");
  if (f) {
    f.println(petActive);
    f.println(petShowActive ? "1" : "0");
    f.close();
  }
}

inline void loadPetCfg() {
  if (!LittleFS.exists("/etc/pet.cfg")) return;
  File f = LittleFS.open("/etc/pet.cfg", "r");
  if (f) {
    petActive = f.readStringUntil('\n'); petActive.trim();
    petShowActive = (f.readStringUntil('\n').indexOf('1') >= 0);
    f.close();
    petFrameIdx = 0;
    petLastMs = 0;
    petCurState = "idle";
    Log::info("PETFG load " + petActive + " show=" + String(petShowActive));
  }
}

inline void drawPet() {
  if (!petShowActive) return;

  int bw = Signal::BORDER_W;
  int cx = SCREEN_W / 2;
  int cy = SCREEN_H / 2 + 5;
  uint32_t t = millis() / 60;

  int bounce = (t % 30 < 15) ? (t % 15) : (15 - (t % 15));
  cy -= bounce / 4;

  uint16_t bodyColor;
  switch (Signal::mode) {
    case Signal::IDLE:      bodyColor = rgb565(80, 200, 80); break;
    case Signal::WORKING:   bodyColor = rgb565(60, 150, 220); break;
    case Signal::ATTENTION: bodyColor = rgb565(240, 180, 40); break;
    default:                bodyColor = rgb565(220, 50, 50); break;
  }

  tft.fillCircle(cx, cy + 44, 40, rgb565(20, 20, 20));
  tft.fillCircle(cx, cy, 40, bodyColor);
  tft.fillCircle(cx - 12, cy - 16, 9, TFT_WHITE);

  switch (Signal::mode) {
    case Signal::IDLE:
      tft.fillTriangle(cx-20, cy-10, cx-12, cy-20, cx-4, cy-10, TFT_WHITE);
      tft.fillTriangle(cx+4, cy-10, cx+12, cy-20, cx+20, cy-10, TFT_WHITE);
      tft.drawLine(cx-8, cy+16, cx, cy+22, TFT_BLACK);
      tft.drawLine(cx, cy+22, cx+8, cy+16, TFT_BLACK);
      break;
    case Signal::WORKING:
      tft.fillCircle(cx-12, cy-6, 6, TFT_WHITE);
      tft.fillCircle(cx+12, cy-6, 6, TFT_WHITE);
      tft.fillCircle(cx-12, cy-6, 3, TFT_BLACK);
      tft.fillCircle(cx+12, cy-6, 3, TFT_BLACK);
      tft.fillRect(cx-6, cy+16, 12, 2, TFT_BLACK);
      break;
    case Signal::ATTENTION:
      tft.fillCircle(cx-12, cy-6, 7, TFT_WHITE);
      tft.fillCircle(cx+12, cy-6, 7, TFT_WHITE);
      tft.fillCircle(cx-12, cy-6, 4, TFT_BLACK);
      tft.fillCircle(cx+12, cy-6, 4, TFT_BLACK);
      tft.fillCircle(cx, cy+14, 5, TFT_BLACK);
      break;
    default:
      tft.drawLine(cx-17, cy-12, cx-7, cy, TFT_WHITE); tft.drawLine(cx-17, cy, cx-7, cy-12, TFT_WHITE);
      tft.drawLine(cx+7, cy-12, cx+17, cy, TFT_WHITE); tft.drawLine(cx+7, cy, cx+17, cy-12, TFT_WHITE);
      tft.drawLine(cx-7, cy+18, cx, cy+14, TFT_BLACK);
      tft.drawLine(cx, cy+14, cx+7, cy+18, TFT_BLACK);
      break;
  }
}

inline void petPoll() {
  if (!petShowActive) return;
  static uint32_t lastDraw = 0;
  uint32_t now = millis();

  if (petActive.length() > 0) {
    if (now - petLastMs > 150) {
      petLastMs = now;
      petFrameIdx++;
      String testPath = "/pet/" + petActive + "/" + petCurState + "_" + String(petFrameIdx) + ".jpg";
      if (!LittleFS.exists(testPath)) petFrameIdx = 0;
    }
    String path = "/pet/" + petActive + "/" + petCurState + "_" + String(petFrameIdx) + ".jpg";
    if (LittleFS.exists(path)) {
      int bw = Signal::BORDER_W;
      uint16_t jw = 0, jh = 0;
      TJpgDec.getFsJpgSize(&jw, &jh, path.c_str(), LittleFS);
      int jx = (SCREEN_W - (int)jw) / 2;
      int jy = (SCREEN_H - (int)jh) / 2;
      if (jx < bw) jx = bw;
      if (jy < bw + 22) jy = bw + 22;

      yield();
      tft.startWrite();
      TJpgDec.setSwapBytes(true);
      TJpgDec.setCallback(jpegDrawCallback);
      TJpgDec.drawFsJpg(jx, jy, path.c_str(), LittleFS);
      tft.endWrite();
      return;
    }
  }

  if (now - lastDraw > 200) {
    lastDraw = now;
    drawPet();
  }
}
