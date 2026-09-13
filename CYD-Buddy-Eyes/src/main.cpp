#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <FS.h>
#include <SD.h>
#include <math.h>

static const int DEFAULT_ROTATION = 2;
static int displayRotation = DEFAULT_ROTATION;
static int screenW = 240;
static int screenH = 320;
static const int BACKLIGHT_PIN = 21;

#define TOUCH_CS 33
#define TOUCH_IRQ 36
static const int T_SCK  = 25;
static const int T_MISO = 39;
static const int T_MOSI = 32;

static const int SD_CS = 5;
static const int SD_SCK = 18;
static const int SD_MISO = 19;
static const int SD_MOSI = 23;
static const char* BUDDY_DIR = "/cydbuddy";
static const char* PHRASE_FILE = "/cydbuddy/phrases.csv";

static const int TOUCH_X_MIN = 562;
static const int TOUCH_X_MAX = 3516;
static const int TOUCH_Y_MIN = 525;
static const int TOUCH_Y_MAX = 3490;
static const bool TOUCH_SWAP_XY = false;
static const bool TOUCH_FLIP_X = false;
static const bool TOUCH_FLIP_Y = false;

TFT_eSPI tft;
SPIClass touchSPI(VSPI);
SPIClass sdSPI(HSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
TFT_eSprite frame(&tft);
Preferences prefs;
bool frameOk = false;
bool sdReady = false;

enum Mood {
  MOOD_NORMAL,
  MOOD_HAPPY,
  MOOD_SURPRISED,
  MOOD_SLEEPY,
  MOOD_ANGRY,
  MOOD_SAD,
  MOOD_EXCITED,
  MOOD_LOVE,
  MOOD_SUSPICIOUS,
  MOOD_COUNT
};

enum MenuMode {
  MENU_NONE,
  MENU_SYSTEM,
  MENU_FACE
};

const char* moodNames[] = {
  "curious", "happy", "surprised", "sleepy", "angry",
  "sad", "excited", "love", "suspicious"
};

Mood currentMood = MOOD_NORMAL;
MenuMode menuMode = MENU_NONE;
unsigned long lastSaccade = 0;
unsigned long nextSaccadeMs = 900;
unsigned long lastMoodAuto = 0;
unsigned long lastTouchMs = 0;
unsigned long touchStarted = 0;
bool touchWasDown = false;
bool longTouchHandled = false;
int lastTouchX = 0;
int lastTouchY = 0;
String serialLine;
float breath = 0.0f;

bool blinkActive = false;
bool winkActive = false;
bool winkLeft = false;
unsigned long blinkStarted = 0;
unsigned long nextBlinkMs = 0;

String lastEvent = "idle";
String statusLine = "tap mood, hold rotate";
String speechLine = "";
int speechScroll = 0;
unsigned long lastSpeechScroll = 0;

uint16_t bgColor = TFT_BLACK;
uint16_t eyeColor = TFT_CYAN;
uint16_t pupilColor = TFT_NAVY;
uint16_t shineColor = TFT_WHITE;
uint16_t accentColor = TFT_MAGENTA;

struct Eye {
  float x, y, w, h;
  float tx, ty, tw, th;
  float pupilX, pupilY, targetPupilX, targetPupilY;
  float vx, vy, vw, vh, pvx, pvy;
  float k = 0.13f;
  float d = 0.62f;
  float pk = 0.08f;
  float pd = 0.54f;

  void init(float ix, float iy, float iw, float ih) {
    x = tx = ix;
    y = ty = iy;
    w = tw = iw;
    h = th = ih;
    pupilX = pupilY = targetPupilX = targetPupilY = 0;
  }

  void update() {
    vx = (vx + (tx - x) * k) * d;
    vy = (vy + (ty - y) * k) * d;
    vw = (vw + (tw - w) * k) * d;
    vh = (vh + (th - h) * k) * d;
    x += vx;
    y += vy;
    w += vw;
    h += vh;

    pvx = (pvx + (targetPupilX - pupilX) * pk) * pd;
    pvy = (pvy + (targetPupilY - pupilY) * pk) * pd;
    pupilX += pvx;
    pupilY += pvy;

  }
};

Eye leftEye;
Eye rightEye;

uint16_t moodEyeColor() {
  switch (currentMood) {
    case MOOD_HAPPY: return TFT_GREENYELLOW;
    case MOOD_SURPRISED: return TFT_SKYBLUE;
    case MOOD_SLEEPY: return TFT_DARKGREY;
    case MOOD_ANGRY: return TFT_RED;
    case MOOD_SAD: return TFT_BLUE;
    case MOOD_EXCITED: return TFT_ORANGE;
    case MOOD_LOVE: return TFT_PINK;
    case MOOD_SUSPICIOUS: return TFT_YELLOW;
    default: return TFT_CYAN;
  }
}

void setBacklight(uint8_t value) {
  analogWrite(BACKLIGHT_PIN, value);
}

String csvEscape(String value) {
  value.replace("\"", "\"\"");
  return "\"" + value + "\"";
}

bool initSDCard() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdReady = SD.begin(SD_CS, sdSPI, 25000000);
  if (!sdReady) {
    statusLine = "sd unavailable";
    return false;
  }

  if (!SD.exists(BUDDY_DIR)) {
    SD.mkdir(BUDDY_DIR);
  }
  if (!SD.exists(PHRASE_FILE)) {
    File f = SD.open(PHRASE_FILE, FILE_WRITE);
    if (f) {
      f.println("mood,phrase,source");
      f.close();
    }
  }
  statusLine = "sd ready";
  return true;
}

bool appendPhrase(String mood, String phrase, String source = "user") {
  mood.trim();
  mood.toLowerCase();
  phrase.trim();
  if (phrase.length() == 0) return false;
  if (!sdReady && !initSDCard()) return false;

  File f = SD.open(PHRASE_FILE, FILE_APPEND);
  if (!f) return false;
  f.print(csvEscape(mood));
  f.print(",");
  f.print(csvEscape(phrase));
  f.print(",");
  f.println(csvEscape(source));
  f.close();
  statusLine = "phrase saved";
  return true;
}

void printSDStatus() {
  Serial.printf("sd=%s dir=%s phrases=%s\n",
                sdReady ? "ready" : "missing",
                BUDDY_DIR,
                PHRASE_FILE);
  if (sdReady) {
    Serial.printf("card_type=%u size_mb=%llu\n", SD.cardType(), SD.cardSize() / (1024ULL * 1024ULL));
  }
}

void startBlink(bool wink = false, bool left = false) {
  blinkActive = true;
  winkActive = wink;
  winkLeft = left;
  blinkStarted = millis();
}

void updateBlinkState() {
  unsigned long now = millis();
  if (!blinkActive && now >= nextBlinkMs) {
    bool allowWink = currentMood == MOOD_HAPPY || currentMood == MOOD_EXCITED || currentMood == MOOD_LOVE || currentMood == MOOD_SUSPICIOUS;
    bool doWink = allowWink && random(0, 12) == 0;
    startBlink(doWink, random(0, 2) == 0);
  }

  if (blinkActive && now - blinkStarted > (winkActive ? 220UL : 145UL)) {
    blinkActive = false;
    winkActive = false;
    nextBlinkMs = now + random(1400, 5200);
  }
}

bool isEyeClosed(bool left) {
  if (!blinkActive) return false;
  if (!winkActive) return true;
  return left == winkLeft;
}

void drawHeart(int cx, int cy, int s, uint16_t color) {
  frame.fillCircle(cx - s / 3, cy - s / 5, s / 3, color);
  frame.fillCircle(cx + s / 3, cy - s / 5, s / 3, color);
  frame.fillTriangle(cx - s, cy - s / 8, cx + s, cy - s / 8, cx, cy + s, color);
}

void drawZzz(int x, int y, uint16_t color) {
  frame.setTextColor(color, bgColor);
  frame.setTextDatum(TL_DATUM);
  frame.drawString("Z", x, y, 4);
  frame.drawString("z", x + 22, y + 22, 2);
  frame.drawString("z", x + 36, y + 40, 2);
}

void drawSpark(int cx, int cy, uint16_t color) {
  frame.drawFastVLine(cx, cy - 11, 22, color);
  frame.drawFastHLine(cx - 11, cy, 22, color);
  frame.drawLine(cx - 7, cy - 7, cx + 7, cy + 7, color);
  frame.drawLine(cx - 7, cy + 7, cx + 7, cy - 7, color);
}

void drawEyelidMask(Eye& e, bool left) {
  int x = (int)e.x;
  int y = (int)e.y;
  int w = (int)e.w;
  int h = (int)e.h;

  if (currentMood == MOOD_ANGRY) {
    if (left) {
      frame.fillTriangle(x - 6, y - 6, x + w + 6, y - 6, x + w + 6, y + h / 3, bgColor);
    } else {
      frame.fillTriangle(x - 6, y - 6, x + w + 6, y - 6, x - 6, y + h / 3, bgColor);
    }
  } else if (currentMood == MOOD_SAD) {
    frame.fillRect(x - 3, y - 3, w + 6, h / 5, bgColor);
    frame.fillEllipse(x + w / 2, y + h + 8, w / 2 + 8, h / 4, bgColor);
  } else if (currentMood == MOOD_HAPPY || currentMood == MOOD_LOVE || currentMood == MOOD_EXCITED) {
    frame.fillRect(x - 2, y + h - 18, w + 4, 24, bgColor);
    frame.fillEllipse(x + w / 2, y + h + 14, w / 2 + 18, h / 2, bgColor);
  } else if (currentMood == MOOD_SLEEPY) {
    frame.fillRect(x - 3, y - 3, w + 6, h / 2 + 6, bgColor);
  } else if (currentMood == MOOD_SUSPICIOUS) {
    frame.fillRect(x - 3, y - 3, w + 6, h / 3 + 2, bgColor);
    frame.fillRect(x - 3, y + h - h / 4, w + 6, h / 4 + 4, bgColor);
  }
}

void drawEye(Eye& e, bool left) {
  int x = (int)e.x;
  int y = (int)e.y;
  int w = max(8, (int)e.w);
  int h = max(6, (int)e.h);
  bool closed = isEyeClosed(left);
  if (closed) h = max(6, h / 8);

  uint16_t c = moodEyeColor();
  frame.fillRoundRect(x, y, w, h, min(w, h) / 3, c);
  frame.drawRoundRect(x - 2, y - 2, w + 4, h + 4, min(w, h) / 3, TFT_DARKCYAN);

  int pw = max(10, w / 3);
  int ph = max(10, h / 3);
  int px = x + w / 2 - pw / 2 + (int)e.pupilX;
  int py = y + h / 2 - ph / 2 + (int)e.pupilY;
  px = constrain(px, x + 4, x + w - pw - 4);
  py = constrain(py, y + 4, y + h - ph - 4);

  if (!closed) {
    frame.fillEllipse(px + pw / 2, py + ph / 2, pw / 2, ph / 2, pupilColor);
    frame.fillCircle(px + pw / 2 - pw / 5, py + ph / 2 - ph / 5, max(2, pw / 8), shineColor);
  }

  drawEyelidMask(e, left);
}

void chooseMoodTargets() {
  float eyeGap = max(18, screenW / 9);
  float baseW = screenW * 0.28f;
  float baseH = screenH * 0.29f;
  float yBase = screenH * 0.32f + sinf(breath) * 5.0f;
  float leftX = (screenW - (baseW * 2.0f + eyeGap)) / 2.0f;
  float rightX = leftX + baseW + eyeGap;
  bool suspicious = false;

  switch (currentMood) {
    case MOOD_HAPPY:
    case MOOD_LOVE:
      baseW *= 1.14f; baseH *= 0.80f; break;
    case MOOD_SURPRISED:
      baseW *= 0.90f; baseH *= 1.20f; break;
    case MOOD_SLEEPY:
      baseW *= 1.14f; baseH *= 0.58f; yBase += screenH * 0.04f; break;
    case MOOD_ANGRY:
      baseW *= 1.05f; baseH *= 0.78f; yBase -= screenH * 0.02f; break;
    case MOOD_SAD:
      baseW *= 0.94f; baseH *= 1.04f; yBase += screenH * 0.04f; break;
    case MOOD_EXCITED:
      baseW *= 1.20f; baseH *= 1.12f; break;
    case MOOD_SUSPICIOUS:
      baseW *= 1.04f; baseH *= 0.58f; yBase += screenH * 0.03f; suspicious = true; break;
    default:
      break;
  }

  eyeGap = max(18, screenW / 9);
  leftX = (screenW - (baseW * 2.0f + eyeGap)) / 2.0f;
  rightX = leftX + baseW + eyeGap;

  leftEye.tx = leftX;
  rightEye.tx = rightX;
  leftEye.ty = yBase;
  rightEye.ty = yBase;
  leftEye.tw = rightEye.tw = baseW;
  leftEye.th = rightEye.th = baseH;

  if (currentMood == MOOD_SAD) {
    leftEye.targetPupilX = 0;
    rightEye.targetPupilX = 0;
    leftEye.targetPupilY = baseH * 0.14f;
    rightEye.targetPupilY = baseH * 0.14f;
  } else if (currentMood == MOOD_ANGRY) {
    leftEye.targetPupilX = baseW * 0.10f;
    rightEye.targetPupilX = -baseW * 0.10f;
    leftEye.targetPupilY = -baseH * 0.04f;
    rightEye.targetPupilY = -baseH * 0.04f;
  } else if (suspicious) {
    float driftX = sinf(breath * 0.55f) * 3.0f;
    float driftY = cosf(breath * 0.45f) * 2.0f;
    leftEye.targetPupilX = -baseW * 0.14f + driftX;
    rightEye.targetPupilX = -baseW * 0.14f + driftX;
    leftEye.targetPupilY = driftY;
    rightEye.targetPupilY = driftY;
  }
}

void updateBuddy() {
  unsigned long now = millis();
  breath += 0.045f;
  updateBlinkState();

  if (now - lastSaccade > nextSaccadeMs) {
    float dx = random(-18, 19);
    float dy = random(-14, 15);
    leftEye.targetPupilX = dx;
    rightEye.targetPupilX = dx;
    leftEye.targetPupilY = dy;
    rightEye.targetPupilY = dy;
    lastSaccade = now;
    nextSaccadeMs = random(550, 2400);
  }

  chooseMoodTargets();
  leftEye.update();
  rightEye.update();
}

Mood moodFromName(String name) {
  name.trim();
  name.toLowerCase();
  for (int i = 0; i < MOOD_COUNT; i++) {
    if (name == moodNames[i]) return (Mood)i;
  }
  if (name == "normal" || name == "idle") return MOOD_NORMAL;
  return currentMood;
}

void applyEvent(String event) {
  event.trim();
  event.toLowerCase();
  if (event.length() == 0) return;

  lastEvent = event;
  lastMoodAuto = millis();

  if (event.indexOf("face") >= 0 || event.indexOf("person") >= 0 || event.indexOf("motion") >= 0) {
    currentMood = MOOD_SURPRISED;
    statusLine = "vision: " + event;
    speechLine = "I see something.";
    startBlink(false);
  } else if (event.indexOf("sound:loud") >= 0 || event.indexOf("noise") >= 0 || event.indexOf("clap") >= 0) {
    currentMood = MOOD_SURPRISED;
    statusLine = "heard: " + event;
    speechLine = "Whoa, I heard that.";
  } else if (event.indexOf("sound:quiet") >= 0 || event.indexOf("dark") >= 0 || event.indexOf("sleep") >= 0) {
    currentMood = MOOD_SLEEPY;
    statusLine = "environment: " + event;
    speechLine = "It got quiet in here.";
  } else if (event.indexOf("cpu:high") >= 0 || event.indexOf("hot") >= 0 || event.indexOf("error") >= 0) {
    currentMood = MOOD_ANGRY;
    statusLine = "system: " + event;
    speechLine = "Something is running hot.";
  } else if (event.indexOf("battery:low") >= 0 || event.indexOf("sad") >= 0) {
    currentMood = MOOD_SAD;
    statusLine = "status: " + event;
    speechLine = "I am not feeling great.";
  } else if (event.indexOf("message") >= 0 || event.indexOf("hello") >= 0 || event.indexOf("voice") >= 0) {
    currentMood = MOOD_HAPPY;
    statusLine = "ai: " + event;
    speechLine = "Hi, I am listening.";
  } else {
    currentMood = MOOD_NORMAL;
    statusLine = "event: " + event;
    speechLine = event;
  }
  speechScroll = 0;
}

void applyStats(String stats) {
  stats.trim();
  stats.toLowerCase();
  lastEvent = "stats " + stats;
  statusLine = lastEvent;
  lastMoodAuto = millis();

  int cpuAt = stats.indexOf("cpu=");
  int tempAt = stats.indexOf("temp=");
  int memAt = stats.indexOf("mem=");
  int cpu = cpuAt >= 0 ? stats.substring(cpuAt + 4).toInt() : -1;
  int temp = tempAt >= 0 ? stats.substring(tempAt + 5).toInt() : -1;
  int mem = memAt >= 0 ? stats.substring(memAt + 4).toInt() : -1;

  if (temp >= 75 || cpu >= 90) currentMood = MOOD_ANGRY;
  else if (mem >= 90) currentMood = MOOD_SLEEPY;
  else if (cpu >= 65) currentMood = MOOD_EXCITED;
  else if (cpu >= 0 || temp >= 0 || mem >= 0) currentMood = MOOD_HAPPY;

  if (currentMood == MOOD_ANGRY) speechLine = "System is hot. I am watching it.";
  else if (currentMood == MOOD_SLEEPY) speechLine = "Memory is heavy. I feel sluggish.";
  else if (currentMood == MOOD_EXCITED) speechLine = "The system is busy.";
  else speechLine = "System looks okay.";
  speechScroll = 0;
}

bool readTouch(int& x, int& y) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  x = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, screenW);
  y = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, screenH);
  x = constrain(x, 0, screenW - 1);
  y = constrain(y, 0, screenH - 1);
  if (TOUCH_SWAP_XY) {
    int t = x; x = y; y = t;
  }
  if (TOUCH_FLIP_X) x = screenW - 1 - x;
  if (TOUCH_FLIP_Y) y = screenH - 1 - y;
  return true;
}

void resizeFrame() {
  if (frame.created()) frame.deleteSprite();
  frame.setColorDepth(8);
  frameOk = frame.createSprite(screenW, screenH) != nullptr;
  frame.setSwapBytes(true);
}

void applyRotation(int rot, bool save) {
  displayRotation = ((rot % 4) + 4) % 4;
  tft.setRotation(displayRotation);
  ts.setRotation(displayRotation);
  screenW = tft.width();
  screenH = tft.height();
  resizeFrame();
  leftEye.init(screenW * 0.14f, screenH * 0.32f, screenW * 0.28f, screenH * 0.29f);
  rightEye.init(screenW * 0.58f, screenH * 0.32f, screenW * 0.28f, screenH * 0.29f);
  if (save) {
    prefs.begin("cyd-buddy", false);
    prefs.putInt("rotation", displayRotation);
    prefs.end();
  }
  tft.fillScreen(TFT_BLACK);
  statusLine = "rotation " + String(displayRotation);
}

bool isUpperLeftHotspot(int x, int y) {
  return x < max(48, screenW / 5) && y < max(42, screenH / 6);
}

bool isUpperRightHotspot(int x, int y) {
  return x > screenW - max(48, screenW / 5) && y < max(42, screenH / 6);
}

void menuGeometry(MenuMode mode, int& x, int& y, int& w, int& h) {
  w = min(screenW - 24, 196);
  h = min(screenH - 42, 176);
  x = mode == MENU_SYSTEM ? 10 : screenW - w - 10;
  y = 12;
}

int menuItemAt(int tx, int ty) {
  if (menuMode == MENU_NONE) return -2;
  int x, y, w, h;
  menuGeometry(menuMode, x, y, w, h);
  if (tx < x || tx > x + w || ty < y || ty > y + h) return -1;
  for (int i = 0; i < 5; i++) {
    int rowTop = y + 30 + i * 24;
    int rowBottom = rowTop + 23;
    if (ty >= rowTop && ty <= rowBottom) return i;
  }
  return -2;
}

void openMenu(MenuMode mode) {
  menuMode = mode;
  if (mode == MENU_SYSTEM) {
    statusLine = "system menu";
  } else if (mode == MENU_FACE) {
    statusLine = "face menu";
  }
}

void handleMenuItem(int item) {
  if (item < 0 || item > 4) {
    menuMode = MENU_NONE;
    statusLine = "menu closed";
    return;
  }

  if (item == 4) {
    menuMode = MENU_NONE;
    statusLine = "menu closed";
    return;
  }

  if (menuMode == MENU_SYSTEM) {
    if (item == 0) {
      statusLine = "xiao pending";
      speechLine = "Plug in the XIAO Sense when you are ready.";
    } else if (item == 1) {
      statusLine = "ai model";
      speechLine = "Local Gemma bridge will live here.";
    } else if (item == 2) {
      statusLine = "wifi bridge";
      speechLine = "WiFi bridge setup will live here.";
    } else if (item == 3) {
      statusLine = sdReady ? "phrase bank ready" : "phrase bank missing";
      speechLine = sdReady ? "Phrases are stored in /cydbuddy/phrases.csv." : "SD card not mounted yet.";
    }
  } else if (menuMode == MENU_FACE) {
    if (item == 0) {
      currentMood = (Mood)((currentMood + 1) % MOOD_COUNT);
      statusLine = String("mood: ") + moodNames[currentMood];
    } else if (item == 1) {
      startBlink(false);
      statusLine = "blink test";
    } else if (item == 2) {
      pupilColor = pupilColor == TFT_NAVY ? TFT_BLACK : TFT_NAVY;
      statusLine = "pupil color";
    } else if (item == 3) {
      applyRotation(displayRotation + 1, true);
      statusLine = "rotation changed";
    }
  }
  speechScroll = 0;
}

void handleTouch() {
  int x, y;
  unsigned long now = millis();
  bool down = readTouch(x, y);
  if (down) {
    lastTouchX = x;
    lastTouchY = y;
  }

  if (down && !touchWasDown) {
    touchStarted = now;
    longTouchHandled = false;
  }

  if (down && !longTouchHandled && now - touchStarted > 850) {
    longTouchHandled = true;
    lastTouchMs = now;
    lastMoodAuto = now;
    if (menuMode == MENU_NONE) {
      applyRotation(displayRotation + 1, true);
    } else {
      menuMode = MENU_NONE;
      statusLine = "menu closed";
    }
    startBlink(false);
  }

  if (!down && touchWasDown && !longTouchHandled && now - touchStarted < 600 && now - lastTouchMs > 250) {
    lastTouchMs = now;
    lastMoodAuto = now;
    if (menuMode != MENU_NONE) {
      int item = menuItemAt(lastTouchX, lastTouchY);
      handleMenuItem(item);
    } else if (isUpperLeftHotspot(lastTouchX, lastTouchY)) {
      openMenu(MENU_SYSTEM);
    } else if (isUpperRightHotspot(lastTouchX, lastTouchY)) {
      openMenu(MENU_FACE);
    } else {
      currentMood = (Mood)((currentMood + 1) % MOOD_COUNT);
      statusLine = String("manual: ") + moodNames[currentMood];
      startBlink(false);
    }
  }

  touchWasDown = down;
}

void handleSerialLine(String line) {
  line.trim();
  if (line.length() == 0) return;
  String lower = line;
  lower.toLowerCase();

  if (lower == "rotate") {
    applyRotation(displayRotation + 1, true);
  } else if (lower.startsWith("rotate ")) {
    applyRotation(lower.substring(7).toInt(), true);
  } else if (lower.startsWith("mood ")) {
    currentMood = moodFromName(lower.substring(5));
    statusLine = String("mood: ") + moodNames[currentMood];
    lastMoodAuto = millis();
  } else if (lower.startsWith("say ")) {
    speechLine = line.substring(4);
    statusLine = "speaking";
    speechScroll = 0;
  } else if (lower == "menu system") {
    openMenu(MENU_SYSTEM);
  } else if (lower == "menu face") {
    openMenu(MENU_FACE);
  } else if (lower == "menu close") {
    menuMode = MENU_NONE;
    statusLine = "menu closed";
  } else if (lower == "sd status") {
    if (!sdReady) initSDCard();
    printSDStatus();
  } else if (lower.startsWith("phrase add ")) {
    String rest = line.substring(11);
    int sep = rest.indexOf(' ');
    if (sep > 0) {
      String mood = rest.substring(0, sep);
      String phrase = rest.substring(sep + 1);
      bool ok = appendPhrase(mood, phrase);
      Serial.printf("phrase_add=%s file=%s\n", ok ? "ok" : "failed", PHRASE_FILE);
    } else {
      Serial.println("phrase_add=failed usage: phrase add <mood> <phrase>");
    }
  } else if (lower.startsWith("event ")) {
    applyEvent(lower.substring(6));
  } else if (lower.startsWith("stats ")) {
    applyStats(lower.substring(6));
  } else if (lower == "wink") {
    startBlink(true, random(0, 2) == 0);
  } else if (lower == "blink") {
    startBlink(false);
  } else {
    applyEvent(lower);
  }

  Serial.printf("ok rotation=%d mood=%s event=%s sd=%s\n", displayRotation, moodNames[currentMood], lastEvent.c_str(), sdReady ? "ready" : "missing");
}

void processSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      handleSerialLine(serialLine);
      serialLine = "";
    } else if (serialLine.length() < 160) {
      serialLine += c;
    }
  }
}

void drawSpeechStrip() {
  int y = screenH - 27;
  String text = speechLine;
  if (text.length() == 0) text = " ";

  int visibleChars = max(10, (screenW - 20) / 6);
  String shown = text;
  if (text.length() > visibleChars) {
    String looped = text + "   " + text;
    unsigned long now = millis();
    if (now - lastSpeechScroll > 140) {
      lastSpeechScroll = now;
      speechScroll++;
      if (speechScroll >= (int)(text.length() + 3)) speechScroll = 0;
    }
    shown = looped.substring(speechScroll, speechScroll + visibleChars);
  } else {
    speechScroll = 0;
  }

  frame.setTextDatum(MC_DATUM);
  frame.setTextColor(TFT_WHITE, bgColor);
  frame.drawString(shown, screenW / 2, y, 2);
}

void drawMenuOverlay() {
  if (menuMode == MENU_NONE) return;

  const char* title = menuMode == MENU_SYSTEM ? "SYSTEM" : "FACE";
  const char* itemsSystem[] = {"XIAO S3 Sense", "AI model", "WiFi bridge", "Phrase bank", "Back"};
  const char* itemsFace[] = {"Next mood", "Blink test", "Pupil color", "Rotation", "Back"};
  const char** items = menuMode == MENU_SYSTEM ? itemsSystem : itemsFace;

  int x, y, panelW, panelH;
  menuGeometry(menuMode, x, y, panelW, panelH);

  frame.fillRoundRect(x, y, panelW, panelH, 10, TFT_BLACK);
  frame.drawRoundRect(x, y, panelW, panelH, 10, menuMode == MENU_SYSTEM ? TFT_CYAN : TFT_MAGENTA);
  frame.setTextDatum(TL_DATUM);
  frame.setTextColor(menuMode == MENU_SYSTEM ? TFT_CYAN : TFT_MAGENTA, TFT_BLACK);
  frame.drawString(title, x + 12, y + 10, 2);
  frame.setTextColor(TFT_WHITE, TFT_BLACK);
  for (int i = 0; i < 5; i++) {
    int rowY = y + 32 + i * 24;
    frame.drawRoundRect(x + 8, rowY - 2, panelW - 16, 22, 4, TFT_DARKGREY);
    frame.drawString(String("> ") + items[i], x + 12, rowY, 2);
  }
  frame.setTextColor(TFT_DARKGREY, TFT_BLACK);
  frame.drawString("tap row to select", x + 12, y + panelH - 16, 2);
}

void drawFrame() {
  if (!frameOk) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("sprite alloc failed", screenW / 2, screenH / 2, 2);
    delay(1000);
    return;
  }

  frame.fillSprite(bgColor);

  uint16_t rim = currentMood == MOOD_ANGRY ? TFT_RED : currentMood == MOOD_LOVE ? TFT_PINK : TFT_DARKCYAN;
  frame.drawRoundRect(8, 8, screenW - 16, screenH - 16, 16, rim);

  if (currentMood == MOOD_SLEEPY) {
    drawZzz(screenW - 62, max(24, screenH / 8), TFT_LIGHTGREY);
  } else if (currentMood == MOOD_ANGRY) {
    frame.drawLine(screenW / 2 - 32, screenH / 5, screenW / 2 - 6, screenH / 8, TFT_RED);
    frame.drawLine(screenW / 2 + 6, screenH / 8, screenW / 2 + 32, screenH / 5, TFT_RED);
  }

  drawEye(leftEye, true);
  drawEye(rightEye, false);

  drawSpeechStrip();
  drawMenuOverlay();

  frame.pushSprite(0, 0);
}

void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());

  pinMode(BACKLIGHT_PIN, OUTPUT);
  setBacklight(255);

  tft.init();
  tft.invertDisplay(false);

  touchSPI.begin(T_SCK, T_MISO, T_MOSI);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  ts.begin(touchSPI);

  initSDCard();

  prefs.begin("cyd-buddy", true);
  int savedRotation = prefs.getInt("rotation", DEFAULT_ROTATION);
  prefs.end();
  applyRotation(savedRotation, false);

  nextBlinkMs = millis() + random(1200, 4200);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("CYD Buddy", screenW / 2, screenH / 2 - 10, 4);
  tft.drawString("booting eyes...", screenW / 2, screenH / 2 + 22, 2);
  delay(800);

  Serial.printf("CYD Buddy Eyes booted, frame=%s rotation=%d size=%dx%d\n", frameOk ? "ok" : "failed", displayRotation, screenW, screenH);
  printSDStatus();
  Serial.println("commands: rotate [0-3], mood happy, event face, stats cpu=90 temp=80, blink, wink, sd status, phrase add <mood> <phrase>");
}

void loop() {
  processSerial();
  handleTouch();
  updateBuddy();
  drawFrame();
  delay(22);
}
