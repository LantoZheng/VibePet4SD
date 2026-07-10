#pragma once
// ============================================================
//  SD2-OS: NTP Time & Timezone
// ============================================================

namespace Time {

extern int8_t   tzOffset;
extern bool     synced;
extern uint32_t lastSyncMs;
extern uint32_t lastNtpTryMs;
constexpr uint32_t NTP_RETRY = 600000;
constexpr const char* TZ_PATH = "/etc/tz.cfg";
const char* NTP_SERVER = "pool.ntp.org";

void loadTz() {
  if (!LittleFS.exists(TZ_PATH)) return;
  File f = LittleFS.open(TZ_PATH, "r");
  if (!f) return;
  tzOffset = f.readString().toInt();
  f.close();
  if (tzOffset < -12 || tzOffset > 14) tzOffset = 8;
}

void saveTz() {
  File f = LittleFS.open(TZ_PATH, "w");
  if (f) { f.print(tzOffset); f.close(); }
}

void begin() {
  loadTz();
  configTime(tzOffset * 3600, 0, NTP_SERVER, "time.google.com", "ntp.aliyun.com");
}

void poll() {
  if (!synced) {
    time_t now;
    time(&now);
    if (now > 1609459200) {
      synced = true;
      lastSyncMs = millis();
    } else if (WiFi.status() == WL_CONNECTED && millis() - lastNtpTryMs > NTP_RETRY) {
      lastNtpTryMs = millis();
      configTime(tzOffset * 3600, 0, NTP_SERVER, "time.google.com");
    }
  }
}

String now() {
  if (!synced) return "NTP not synced";
  time_t t;
  time(&t);
  struct tm* tm = localtime(&t);
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
    tm->tm_hour, tm->tm_min, tm->tm_sec);
  return String(buf);
}

String dateTime() {
  if (!synced) return "NTP not synced";
  time_t t;
  time(&t);
  struct tm* tm = localtime(&t);
  char buf[48];
  snprintf(buf, sizeof(buf),
    "%04d-%02d-%02d %02d:%02d:%02d UTC%+d  day=%d",
    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
    tm->tm_hour, tm->tm_min, tm->tm_sec,
    tzOffset, tm->tm_wday);
  return String(buf);
}

String setTz(int8_t offset) {
  tzOffset = constrain(offset, -12, 14);
  saveTz();
  configTime(tzOffset * 3600, 0, NTP_SERVER, "time.google.com");
  synced = false;
  lastNtpTryMs = 0;
  return "OK TZ UTC" + String(tzOffset >= 0 ? "+" : "") + tzOffset;
}

} // namespace Time
