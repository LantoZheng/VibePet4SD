#pragma once
// ============================================================
//  SD2-OS: Environment Variables (persisted to /etc/env.cfg)
// ============================================================

namespace Env {

constexpr const char* ENV_PATH = "/etc/env.cfg";

String get(const String& key) {
  if (!LittleFS.exists(ENV_PATH)) return "";
  File f = LittleFS.open(ENV_PATH, "r");
  if (!f) return "";
  String line;
  while (f.available()) {
    line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq > 0 && line.substring(0, eq) == key) {
      f.close();
      return line.substring(eq + 1);
    }
  }
  f.close();
  return "";
}

bool set(const String& key, const String& value) {
  String buf;
  bool found = false;
  if (LittleFS.exists(ENV_PATH)) {
    File f = LittleFS.open(ENV_PATH, "r");
    if (f) {
      String line;
      while (f.available()) {
        line = f.readStringUntil('\n');
        line.trim();
        int eq = line.indexOf('=');
        if (eq > 0 && line.substring(0, eq) == key) {
          buf += key + "=" + value + "\n";
          found = true;
        } else if (line.length()) {
          buf += line + "\n";
        }
      }
      f.close();
    }
  }
  if (!found) buf += key + "=" + value + "\n";

  File f = LittleFS.open(ENV_PATH, "w");
  if (!f) return false;
  f.print(buf);
  f.close();
  return true;
}

bool unset(const String& key) {
  String buf;
  bool found = false;
  if (LittleFS.exists(ENV_PATH)) {
    File f = LittleFS.open(ENV_PATH, "r");
    if (f) {
      String line;
      while (f.available()) {
        line = f.readStringUntil('\n');
        line.trim();
        int eq = line.indexOf('=');
        if (eq > 0 && line.substring(0, eq) == key) {
          found = true;
        } else if (line.length()) {
          buf += line + "\n";
        }
      }
      f.close();
    }
  }
  if (!found) return false;
  File f = LittleFS.open(ENV_PATH, "w");
  if (!f) return false;
  f.print(buf);
  f.close();
  return true;
}

String listJson() {
  String json = "{\"ok\":true,\"env\":{";
  if (!LittleFS.exists(ENV_PATH)) { json += "}}"; return json; }
  File f = LittleFS.open(ENV_PATH, "r");
  if (!f) { json += "}}"; return json; }
  bool first = true;
  String line;
  while (f.available()) {
    line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq > 0) {
      if (!first) json += ",";
      first = false;
      String key = line.substring(0, eq);
      String val = line.substring(eq + 1);
      json += "\"" + key + "\":\"" + val + "\"";
    }
  }
  f.close();
  json += "}}";
  return json;
}

} // namespace Env
