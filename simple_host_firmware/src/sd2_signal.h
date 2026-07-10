#pragma once
// ============================================================
//  SD2-OS: Signal Light System (WS2812 NeoPixel + TFT Border)
// ============================================================

namespace Signal {

enum Mode { OFF = 0, IDLE = 1, WORKING = 2, THINKING = 5, ATTENTION = 3, BLOCKED = 4 };
extern Mode     mode;
extern uint32_t lastMs;
extern uint8_t  cycleStep;
extern bool     flashOn;
extern uint32_t flashInterval;

// Color state for TFT sync
extern uint8_t curR, curG, curB;
constexpr int BORDER_W = 24;

inline void setColor(uint8_t r, uint8_t g, uint8_t b) {
  ws2812.setPixelColor(0, ws2812.Color(r, g, b));
  noInterrupts();
  ws2812.show();
  interrupts();
}

inline void drawTftBorder(uint8_t r, uint8_t g, uint8_t b) {
  r = (r > 0) ? min(255, r * 6) : 0;
  g = (g > 0) ? min(255, g * 6) : 0;
  b = (b > 0) ? min(255, b * 6) : 0;
  constexpr int LAYERS = 6;
  for (int i = 0; i < LAYERS; i++) {
    int inset = i * 4;
    uint8_t alpha = 255 - (i * 255 / LAYERS);
    uint8_t pr = (r * alpha) / 255;
    uint8_t pg = (g * alpha) / 255;
    uint8_t pb = (b * alpha) / 255;
    uint16_t c = ((pr & 0xF8) << 8) | ((pg & 0xFC) << 3) | (pb >> 3);

    int x0 = inset, y0 = inset;
    int x1 = SCREEN_W - inset - 1, y1 = SCREEN_H - inset - 1;
    int bw = x1 - x0 + 1, bh = y1 - y0 + 1;

    tft.fillRect(x0, y0, bw, 4, c);
    tft.fillRect(x0, y1 - 3, bw, 4, c);
    tft.fillRect(x0, y0 + 4, 4, bh - 8, c);
    tft.fillRect(x1 - 3, y0 + 4, 4, bh - 8, c);
  }
  curR = r; curG = g; curB = b;
}

inline void clearTftBorder() {
  tft.fillRect(0, 0, SCREEN_W, BORDER_W, TFT_BLACK);
  tft.fillRect(0, SCREEN_H - BORDER_W, SCREEN_W, BORDER_W, TFT_BLACK);
  tft.fillRect(0, BORDER_W, BORDER_W, SCREEN_H - 2*BORDER_W, TFT_BLACK);
  tft.fillRect(SCREEN_W - BORDER_W, BORDER_W, BORDER_W, SCREEN_H - 2*BORDER_W, TFT_BLACK);
  curR = curG = curB = 0;
}

void setMode(Mode m) {
  Mode prev = mode;
  mode = m; lastMs = millis(); cycleStep = 0; flashOn = false;

  // Sound notifications on transition
  if (m != prev) {
    if (m == IDLE && (prev == WORKING || prev == ATTENTION)) {
      beep(2000, 60); delay(80); beep(2600, 80);
    } else if (m == BLOCKED && prev != BLOCKED) {
      beep(400, 200);
    } else if (m == ATTENTION && prev != ATTENTION && prev != BLOCKED) {
      beep(1500, 50);
    }
  }

  // Sync pet animation state
  if (petShowActive && petActive.length()) {
    switch (m) {
      case IDLE:      petCurState = "idle"; break;
      case WORKING:   petCurState = "run"; break;
      case THINKING:  petCurState = "wave"; break;
      case ATTENTION: petCurState = "wave"; break;
      case BLOCKED:   petCurState = "failed"; break;
      default:        petCurState = "idle"; break;
    }
    petFrameIdx = 0;
  }
  switch (mode) {
    case IDLE:      setColor(0, 40, 0);  drawTftBorder(0, 40, 0);  break;
    case WORKING:   setColor(0, 10, 40); drawTftBorder(0, 10, 40); break;
    case THINKING:  setColor(30, 0, 30); drawTftBorder(30, 0, 30); break;
    case ATTENTION: setColor(40, 30, 0); flashInterval = 500; drawTftBorder(40, 30, 0); break;
    case BLOCKED:   setColor(40, 0, 0);  flashInterval = 300; drawTftBorder(40, 0, 0);  break;
    default:        setColor(0, 0, 0);   clearTftBorder();     break;
  }
}

void poll() {
  uint32_t now = millis();
  switch (mode) {
    case IDLE: break;
    case WORKING: {
      uint32_t cycle = now % 3000;
      uint8_t v;
      if (cycle < 1500) v = 5 + (cycle * 35 / 1500);
      else              v = 40 - ((cycle-1500) * 35 / 1500);
      setColor(0, v/4, v);
      drawTftBorder(0, v/4, v);
      break;
    }
    case THINKING: {
      uint32_t cycle = now % 5000;
      uint8_t v;
      if (cycle < 2500) v = 5 + (cycle * 30 / 2500);
      else              v = 35 - ((cycle-2500) * 30 / 2500);
      setColor(v, 0, v);
      drawTftBorder(v, 0, v);
      break;
    }
    case ATTENTION:
    case BLOCKED:
      if (now - lastMs > flashInterval) { lastMs = now; flashOn = !flashOn;
        if (flashOn) {
          uint8_t rr = 40, gg = (mode == ATTENTION ? 30 : 0);
          setColor(rr, gg, 0); drawTftBorder(rr, gg, 0);
        } else {
          setColor(0, 0, 0); clearTftBorder();
        }
      }
      break;
    default: break;
  }
}

String status() {
  const char* names[] = {"OFF","IDLE","WORKING","ATTENTION","BLOCKED","","THINKING"};
  return String("SIGNAL ") + names[mode];
}

inline void begin() {
  ws2812.begin();
  setMode(IDLE);
  Log::info("SIGNAL idle (green)");
}

} // namespace Signal
