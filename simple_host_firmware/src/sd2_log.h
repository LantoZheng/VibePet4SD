#pragma once
// ============================================================
//  SD2-OS: Ring Log Buffer (DMSG)
// ============================================================

namespace Log {

constexpr int LINES    = 16;
constexpr int LINE_LEN = 64;
extern char ring[LINES][LINE_LEN];
extern int  head;
extern int  count;

void write(const char* level, const String& msg) {
  snprintf(ring[head], LINE_LEN, "[%s] %s", level, msg.c_str());
  head = (head + 1) % LINES;
  if (count < LINES) count++;
  Serial.print(ring[(head - 1 + LINES) % LINES]);
  Serial.println();
}

inline void info(const String& m)  { write("INFO ", m); }
inline void warn(const String& m)  { write("WARN ", m); }
inline void error(const String& m) { write("ERROR", m); }

String dump() {
  String out;
  out.reserve(LINES * (LINE_LEN + 2));
  int start = count < LINES ? 0 : head;
  for (int i = 0; i < count; i++) {
    int idx = (start + i) % LINES;
    if (ring[idx][0]) {
      out += String(i) + " " + String(ring[idx]) + "\n";
    }
  }
  if (!out.length()) out = "(log empty)\n";
  return out;
}

inline void clear() {
  memset(ring, 0, sizeof(ring));
  head = count = 0;
}

} // namespace Log
