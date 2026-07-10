#pragma once
// ============================================================
//  SD2-OS: Animation Engine (JPEG sequence + delta rects)
// ============================================================

inline String padFrameNum(int n) {
  if (n < 10) return String("0") + n;
  return String(n);
}

inline String animFramePath(const String& animName, int frame) {
  return String(ANIM_DIR) + "/" + animName + "/frame_" + padFrameNum(frame) + ".jpg";
}

inline String animFramePathBin(const String& animName, int frame) {
  return String(ANIM_DIR) + "/" + animName + "/frame_" + padFrameNum(frame) + ".bin";
}

inline String animManifestPath(const String& animName) {
  return String(ANIM_DIR) + "/" + animName + "/manifest.json";
}

bool loadAnimManifest(const String& animName, int& fps, bool& loop, int& frameCount) {
  String path = animManifestPath(animName);
  if (!LittleFS.exists(path)) return false;

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  String content = f.readString();
  f.close();

  auto findInt = [&](const char* key, int def) -> int {
    int p = content.indexOf(key);
    if (p < 0) return def;
    p += strlen(key) + 1;
    if (content[p] == ' ') p++;
    return content.substring(p).toInt();
  };
  auto findBool = [&](const char* key, bool def) -> bool {
    int p = content.indexOf(key);
    if (p < 0) return def;
    p += strlen(key) + 1;
    if (content[p] == ' ') p++;
    return content.substring(p).startsWith("true");
  };

  fps = findInt("\"fps\"", 8);
  if (fps < 1) fps = 1;
  if (fps > 30) fps = 30;
  loop = findBool("\"loop\"", true);
  frameCount = findInt("\"frames\"", 0);
  return frameCount > 0;
}

inline bool animIsDelta(const String& animName) {
  String path = animManifestPath(animName);
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  String content = f.readString();
  f.close();
  int idx = content.indexOf("\"type\"");
  if (idx < 0) return false;
  idx = content.indexOf("delta", idx);
  return idx > 0 && idx < (int)content.length();
}

bool animStart(const String& animName) {
  if (!fsMounted) return false;

  int fps, frameCount;
  bool loop;
  if (!loadAnimManifest(animName, fps, loop, frameCount)) return false;

  anim.deltaMode = animIsDelta(animName);

  if (!LittleFS.exists(animFramePath(animName, 0))) return false;
  if (anim.deltaMode && frameCount > 1) {
    if (!LittleFS.exists(animFramePathBin(animName, 1))) {
      anim.deltaMode = false;
    }
  }

  anim.name = animName;
  anim.fps = fps;
  anim.loop = loop;
  anim.frameCount = frameCount;
  anim.currentFrame = 0;
  anim.frameIntervalMs = 1000 / fps;
  anim.lastFrameMs = millis();
  anim.stoppedScreen = currentScreen;
  anim.playing = true;

  drawJpgFileAnim(animFramePath(animName, 0));
  return true;
}

inline void animStop() {
  if (!anim.playing) return;
  anim.playing = false;
  if (anim.stoppedScreen.length() && showImage(anim.stoppedScreen)) return;
  drawStatus();
}

inline void animPoll() {
  if (!anim.playing) return;

  const uint32_t now = millis();
  if (now - anim.lastFrameMs < anim.frameIntervalMs) return;

  anim.currentFrame++;
  if (anim.currentFrame >= anim.frameCount) {
    if (anim.loop) { anim.currentFrame = 0; }
    else { animStop(); return; }
  }

  if (anim.deltaMode && anim.currentFrame > 0) {
    String path = animFramePathBin(anim.name, anim.currentFrame);
    if (LittleFS.exists(path)) drawDeltaFrame(path);
  } else {
    String path = animFramePath(anim.name, anim.currentFrame);
    if (LittleFS.exists(path)) drawJpgFileAnim(path);
  }
  anim.lastFrameMs = now;
}

String animListJson() {
  String json = "{\"ok\":true,\"anims\":[";
  if (!fsMounted) { json += "]}"; return json; }
  Dir dir = LittleFS.openDir(ANIM_DIR);
  bool first = true;
  while (dir.next()) {
    if (!dir.isDirectory()) continue;
    String name = dir.fileName();
    name.replace(ANIM_DIR, "");
    name.replace("/", "");
    int fps, frameCount;
    bool loop;
    if (loadAnimManifest(name, fps, loop, frameCount)) {
      if (!first) json += ",";
      first = false;
      json += "{\"name\":\"" + jsonEscape(name) + "\"";
      json += ",\"fps\":" + String(fps);
      json += ",\"loop\":" + String(loop ? "true" : "false");
      json += ",\"frames\":" + String(frameCount);
      json += ",\"delta\":" + String(animIsDelta(name) ? "true" : "false");
      json += "}";
    }
  }
  json += "]}";
  return json;
}
