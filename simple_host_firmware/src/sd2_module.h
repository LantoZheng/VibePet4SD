#pragma once
// ============================================================
//  SD2-OS: Module Manager (dynamic load/unload of .s2b scripts)
// ============================================================

namespace Module {

constexpr const char* MOD_DIR = "/mod";
extern String loadedName;

String listJson() {
  String json = "{\"ok\":true,\"modules\":[";
  if (!fsMounted) { json += "]}"; return json; }
  if (!LittleFS.exists(MOD_DIR)) { json += "]}"; return json; }

  Dir dir = LittleFS.openDir(MOD_DIR);
  bool first = true;
  while (dir.next()) {
    if (!dir.isDirectory()) continue;
    String name = dir.fileName();
    name.replace(String(MOD_DIR) + "/", "");

    String s2bPath = String(MOD_DIR) + "/" + name + "/module.s2b";
    if (!LittleFS.exists(s2bPath)) continue;

    String desc = "", version = "1.0";
    String metaPath = String(MOD_DIR) + "/" + name + "/module.json";
    if (LittleFS.exists(metaPath)) {
      File f = LittleFS.open(metaPath, "r");
      if (f) {
        String content = f.readString();
        f.close();
        auto strVal = [&](const char* key) -> String {
          int p = content.indexOf(String("\"") + key + "\"");
          if (p < 0) return "";
          p = content.indexOf("\"", p + strlen(key) + 2) + 1;
          int e = content.indexOf("\"", p);
          return (p > 0 && e > p) ? content.substring(p, e) : "";
        };
        desc = strVal("desc");
        version = strVal("version");
        if (!version.length()) version = "1.0";
      }
    }

    if (!first) json += ",";
    first = false;
    json += "{\"name\":\"" + name + "\",\"desc\":\"" + desc + "\",\"version\":\"" + version + "\"";
    json += ",\"loaded\":" + String(loadedName == name ? "true" : "false");
    json += ",\"size\":" + String(LittleFS.exists(s2bPath) ? (int)LittleFS.open(s2bPath,"r").size() : 0);
    json += "}";
  }
  json += "]}";
  return json;
}

String load(const String& name) {
  if (!fsMounted) return "ERR fs not mounted";

  String srcPath = String(MOD_DIR) + "/" + name + "/module.sh";
  String binPath = String(MOD_DIR) + "/" + name + "/module.s2b";

  if (!LittleFS.exists(binPath) && LittleFS.exists(srcPath)) {
    if (!Script::compile(srcPath, binPath)) return "ERR compile failed: " + name;
    Log::info("MOD compiled " + name);
  }
  if (!LittleFS.exists(binPath)) return "ERR module not found: " + name;

  if (scriptCache.code && loadedName.length()) Log::info("MOD unload " + loadedName);
  scriptCache.clear();
  animFileCache.close();

  if (!scriptCache.load(binPath)) return "ERR load failed: " + name;
  loadedName = name;
  Log::info("MOD loaded " + name + " (" + String(scriptCache.size) + " bytes)");

  String result = Script::run(binPath);
  return "OK MOD_LOAD " + name + " -> " + result;
}

String unload() {
  if (!loadedName.length()) return "OK no module loaded";
  String wasName = loadedName;
  if (anim.playing) animStop();
  scriptCache.clear();
  animFileCache.close();
  loadedName = "";
  Log::info("MOD unloaded " + wasName);
  drawStatus();
  return "OK MOD_UNLOAD " + wasName;
}

String status() {
  if (!loadedName.length()) return "MOD no module loaded. " + heapStats();
  return "MOD loaded=" + loadedName + " size=" + String(scriptCache.size) + " " + heapStats();
}

} // namespace Module
