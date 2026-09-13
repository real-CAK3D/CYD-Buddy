#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <FS.h>
#include <SD.h>
#include <math.h>
#include <BLEDevice.h>
#include <WiFi.h>
#include <time.h>

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
static BLEUUID SENSE_SERVICE_UUID("7a2f0001-44b8-4f2a-9c4f-c0d000000001");
static BLEUUID SENSE_EVENT_UUID("7a2f0002-44b8-4f2a-9c4f-c0d000000002");
static BLEUUID SENSE_COMMAND_UUID("7a2f0003-44b8-4f2a-9c4f-c0d000000003");

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
  MENU_FACE,
  MENU_SYSTEM_XIAO,
  MENU_SYSTEM_AI,
  MENU_SYSTEM_WIFI,
  MENU_SYSTEM_PHRASES,
  MENU_FACE_EYES,
  MENU_FACE_MOODS,
  MENU_FACE_COLORS
};

const char* moodNames[] = {
  "curious", "happy", "surprised", "sleepy", "angry",
  "sad", "excited", "love", "suspicious"
};

Mood currentMood = MOOD_NORMAL;
MenuMode menuMode = MENU_NONE;
bool autoMode = true;
bool moodEyeColorEnabled = true;
unsigned long lastSaccade = 0;
unsigned long nextSaccadeMs = 900;
unsigned long lastMoodAuto = 0;
unsigned long nextAutonomyMs = 0;
unsigned long lastTouchMs = 0;
unsigned long touchStarted = 0;
bool touchWasDown = false;
bool longTouchHandled = false;
int lastTouchX = 0;
int lastTouchY = 0;
int touchStartX = 0;
int touchStartY = 0;
int touchMoveMax = 0;
String serialLine;
char bleEventBuffer[121] = {0};
float breath = 0.0f;

bool blinkActive = false;
bool winkActive = false;
bool winkLeft = false;
unsigned long blinkStarted = 0;
unsigned long nextBlinkMs = 0;
float blinkPeak = 1.0f;
float gazeX = 0.0f;
float gazeY = 0.0f;
float targetGazeX = 0.0f;
float targetGazeY = 0.0f;
unsigned long hurtUntil = 0;
bool hurtLeftEye = false;
bool hurtRightEye = false;
unsigned long tickleUntil = 0;
unsigned long lastInteractionMs = 0;
unsigned long lastIdleMoodMs = 0;
bool asleep = false;
int bootMinuteOfDay = 0;
unsigned long clockSetAtMs = 0;
unsigned long buddyTouchCount = 0;
unsigned long buddyEyePokeCount = 0;
unsigned long buddyTickleCount = 0;
unsigned long buddyBoredCount = 0;
unsigned long lastMemorySaveMs = 0;
unsigned long lastBleScanMs = 0;
unsigned long lastBleEventMs = 0;
bool bleReady = false;
bool bleConnected = false;
volatile bool bleEventReady = false;
bool bleDisconnected = false;
bool senseStreamEvents = true;
bool senseAutoConnect = false;
BLEAdvertisedDevice* senseDevice = nullptr;
BLEClient* senseClient = nullptr;
BLERemoteCharacteristic* senseEventChar = nullptr;
BLERemoteCharacteristic* senseCommandChar = nullptr;

bool wifiConfigured = false;
bool wifiConnecting = false;
String wifiSsid;
String ollamaHost;
unsigned long wifiStartedMs = 0;
unsigned long lastWifiCheckMs = 0;

void handleSerialLine(String line);

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
bool customPupilColor = false;
const uint16_t COLOR_CHOICES[] = {
  TFT_NAVY, TFT_BLACK, TFT_BLUE, TFT_SKYBLUE, TFT_CYAN, TFT_DARKCYAN,
  TFT_GREEN, TFT_GREENYELLOW, TFT_ORANGE, TFT_YELLOW, TFT_RED, TFT_PINK,
  TFT_MAGENTA, TFT_WHITE, TFT_LIGHTGREY
};
const char* COLOR_NAMES[] = {
  "navy", "black", "blue", "sky", "cyan", "teal",
  "green", "lime", "amber", "yellow", "red", "pink",
  "purple", "white", "gray"
};
static const int COLOR_COUNT = sizeof(COLOR_CHOICES) / sizeof(COLOR_CHOICES[0]);
#define COUNT_OF(a) (int)(sizeof(a) / sizeof((a)[0]))
int eyeColorIndex = 4;
int pupilColorIndex = 0;

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

int compileTimeMinutes() {
  String t = __TIME__;
  int h = t.substring(0, 2).toInt();
  int m = t.substring(3, 5).toInt();
  return constrain(h * 60 + m, 0, 1439);
}

int minuteOfDay() {
  unsigned long elapsedMinutes = (millis() - clockSetAtMs) / 60000UL;
  return (bootMinuteOfDay + elapsedMinutes) % 1440;
}

bool setClockFromText(String value) {
  value.trim();
  int sep = value.indexOf(':');
  if (sep < 0) return false;
  int h = value.substring(0, sep).toInt();
  int m = value.substring(sep + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  bootMinuteOfDay = h * 60 + m;
  clockSetAtMs = millis();
  statusLine = "time set";
  return true;
}

bool timeIsLateNight() {
  int m = minuteOfDay();
  return m >= 22 * 60 || m < 4 * 60;
}

bool timeIsEarlyAM() {
  int m = minuteOfDay();
  return m >= 4 * 60 && m < 7 * 60;
}

bool timeIsMorning() {
  int m = minuteOfDay();
  return m >= 7 * 60 && m < 11 * 60;
}

bool timeIsEvening() {
  int m = minuteOfDay();
  return m >= 18 * 60 && m < 22 * 60;
}

uint16_t moodEyeColor() {
  if (!moodEyeColorEnabled) return eyeColor;
  if (timeIsEarlyAM()) return TFT_YELLOW;
  if (timeIsMorning()) return TFT_SKYBLUE;
  if (timeIsEvening() || timeIsLateNight()) return TFT_NAVY;
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

uint16_t activePupilColor() {
  if (customPupilColor) return pupilColor;
  if (timeIsEarlyAM()) return TFT_WHITE;
  if (timeIsMorning()) return TFT_YELLOW;
  if (timeIsEvening() || timeIsLateNight()) return TFT_BLACK;
  return pupilColor;
}

uint16_t colorFromName(String color, uint16_t fallback) {
  color.trim();
  color.toLowerCase();
  if (color == "black") return TFT_BLACK;
  if (color == "navy") return TFT_NAVY;
  if (color == "blue") return TFT_BLUE;
  if (color == "sky" || color == "skyblue") return TFT_SKYBLUE;
  if (color == "cyan") return TFT_CYAN;
  if (color == "teal") return TFT_DARKCYAN;
  if (color == "green") return TFT_GREEN;
  if (color == "lime") return TFT_GREENYELLOW;
  if (color == "amber" || color == "orange") return TFT_ORANGE;
  if (color == "yellow") return TFT_YELLOW;
  if (color == "red") return TFT_RED;
  if (color == "pink") return TFT_PINK;
  if (color == "purple" || color == "magenta") return TFT_MAGENTA;
  if (color == "white") return TFT_WHITE;
  if (color == "gray" || color == "grey") return TFT_LIGHTGREY;
  return fallback;
}

int colorIndexFromName(String color, int fallback) {
  color.trim();
  color.toLowerCase();
  for (int i = 0; i < COLOR_COUNT; i++) {
    if (color == COLOR_NAMES[i]) return i;
  }
  if (color == "orange") return 8;
  if (color == "skyblue") return 3;
  if (color == "purple") return 12;
  if (color == "grey") return 14;
  return fallback;
}

void cycleEyeColor() {
  moodEyeColorEnabled = false;
  eyeColorIndex = (eyeColorIndex + 1) % COLOR_COUNT;
  eyeColor = COLOR_CHOICES[eyeColorIndex];
  statusLine = String("eye: ") + COLOR_NAMES[eyeColorIndex];
}

void cyclePupilColor() {
  customPupilColor = true;
  pupilColorIndex = (pupilColorIndex + 1) % COLOR_COUNT;
  pupilColor = COLOR_CHOICES[pupilColorIndex];
  statusLine = String("pupil: ") + COLOR_NAMES[pupilColorIndex];
}

void loadBuddyMemory() {
  prefs.begin("cyd-buddy", true);
  buddyTouchCount = prefs.getULong("touches", 0);
  buddyEyePokeCount = prefs.getULong("eyePokes", 0);
  buddyTickleCount = prefs.getULong("tickles", 0);
  buddyBoredCount = prefs.getULong("bored", 0);
  prefs.end();
}

void saveBuddyMemory(bool force = false) {
  unsigned long now = millis();
  if (!force && now - lastMemorySaveMs < 15000UL) return;
  prefs.begin("cyd-buddy", false);
  prefs.putULong("touches", buddyTouchCount);
  prefs.putULong("eyePokes", buddyEyePokeCount);
  prefs.putULong("tickles", buddyTickleCount);
  prefs.putULong("bored", buddyBoredCount);
  prefs.end();
  lastMemorySaveMs = now;
}

void markInteraction() {
  buddyTouchCount++;
  lastInteractionMs = millis();
  asleep = false;
  saveBuddyMemory();
}

void setBacklight(uint8_t value) {
  analogWrite(BACKLIGHT_PIN, value);
}

void loadNetworkSettings() {
  prefs.begin("network", false);
  wifiSsid = prefs.isKey("ssid") ? prefs.getString("ssid", "") : "";
  ollamaHost = prefs.isKey("ollama") ? prefs.getString("ollama", "http://127.0.0.1:11434") : "http://127.0.0.1:11434";
  prefs.end();
  wifiConfigured = wifiSsid.length() > 0;
}

void saveWifiSsid(const String& ssid) {
  wifiSsid = ssid;
  wifiSsid.trim();
  prefs.begin("network", false);
  prefs.putString("ssid", wifiSsid);
  prefs.end();
  wifiConfigured = wifiSsid.length() > 0;
}

void saveWifiPassword(const String& password) {
  prefs.begin("network", false);
  prefs.putString("pass", password);
  prefs.end();
}

void saveOllamaHost(const String& host) {
  ollamaHost = host;
  ollamaHost.trim();
  prefs.begin("network", false);
  prefs.putString("ollama", ollamaHost);
  prefs.end();
}

void connectWifi() {
  loadNetworkSettings();
  if (!wifiConfigured) {
    statusLine = "wifi no ssid";
    speechLine = "Set WiFi SSID and password first.";
    speechScroll = 0;
    return;
  }

  prefs.begin("network", true);
  String pass = prefs.getString("pass", "");
  prefs.end();
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), pass.c_str());
  wifiConnecting = true;
  wifiStartedMs = millis();
  statusLine = "wifi connecting";
  speechLine = "Connecting to WiFi.";
  speechScroll = 0;
}

void syncNetworkTime() {
  if (WiFi.status() != WL_CONNECTED) return;
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 2500)) {
    bootMinuteOfDay = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    clockSetAtMs = millis();
    statusLine = "time synced";
  }
}

void updateWifi() {
  if (!wifiConnecting || millis() - lastWifiCheckMs < 500) return;
  lastWifiCheckMs = millis();
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnecting = false;
    statusLine = "wifi connected";
    speechLine = "WiFi connected. I can reach the wider brain when the relay is available.";
    speechScroll = 0;
    syncNetworkTime();
  } else if (millis() - wifiStartedMs > 20000) {
    wifiConnecting = false;
    WiFi.disconnect(false);
    statusLine = "wifi failed";
    speechLine = "WiFi did not connect. Check hotspot name and password.";
    speechScroll = 0;
  }
}

class SenseAdvertisedCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(SENSE_SERVICE_UUID)) {
      if (senseDevice) delete senseDevice;
      senseDevice = new BLEAdvertisedDevice(advertisedDevice);
      BLEDevice::getScan()->stop();
    }
  }
};

class SenseClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient*) override {
    bleConnected = true;
  }

  void onDisconnect(BLEClient*) override {
    bleConnected = false;
    bleDisconnected = true;
    senseEventChar = nullptr;
    senseCommandChar = nullptr;
  }
};

void onSenseNotify(BLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  if (length == 0) return;
  size_t copyLen = min(length, sizeof(bleEventBuffer) - 1);
  memcpy(bleEventBuffer, data, copyLen);
  bleEventBuffer[copyLen] = '\0';
  bleEventReady = true;
  lastBleEventMs = millis();
}

bool sendSenseCommand(String command) {
  command.trim();
  if (command == "connect") {
    senseAutoConnect = true;
    lastBleScanMs = 0;
    statusLine = "xiao scan";
    speechLine = "Scanning for XIAO Sense.";
    speechScroll = 0;
    return true;
  }
  if (!bleConnected || !senseCommandChar || command.length() == 0) {
    speechLine = "XIAO Sense is not connected yet.";
    speechScroll = 0;
    return false;
  }
  senseCommandChar->writeValue((uint8_t*)command.c_str(), command.length(), false);
  statusLine = "xiao command";
  speechLine = "Sent to XIAO: " + command;
  speechScroll = 0;
  return true;
}

void beginSenseBle() {
  if (bleReady) return;
  BLEDevice::init("CYD-Buddy");
  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new SenseAdvertisedCallbacks());
  scan->setInterval(1349);
  scan->setWindow(449);
  scan->setActiveScan(true);
  bleReady = true;
}

bool connectSenseBle() {
  if (!senseDevice) return false;
  Serial.println("ble_attach=connecting");
  if (senseClient) {
    if (senseClient->isConnected()) senseClient->disconnect();
    delete senseClient;
    senseClient = nullptr;
  }
  senseClient = BLEDevice::createClient();
  senseClient->setClientCallbacks(new SenseClientCallbacks());
  if (!senseClient->connect(senseDevice)) {
    Serial.println("ble_attach=connect_failed");
    delete senseDevice;
    senseDevice = nullptr;
    return false;
  }

  BLERemoteService* service = senseClient->getService(SENSE_SERVICE_UUID);
  if (!service) {
    Serial.println("ble_attach=service_missing");
    senseClient->disconnect();
    delete senseDevice;
    senseDevice = nullptr;
    return false;
  }

  senseEventChar = service->getCharacteristic(SENSE_EVENT_UUID);
  if (!senseEventChar) {
    Serial.println("ble_attach=event_missing");
    senseClient->disconnect();
    delete senseDevice;
    senseDevice = nullptr;
    return false;
  }
  senseCommandChar = service->getCharacteristic(SENSE_COMMAND_UUID);

  if (senseEventChar->canNotify()) senseEventChar->registerForNotify(onSenseNotify);
  Serial.printf("ble_attach=ok command=%s\n", senseCommandChar ? "yes" : "no");
  bleConnected = true;
  statusLine = "xiao ble connected";
  speechLine = "XIAO Sense connected. I have portable eyes and ears.";
  speechScroll = 0;
  delete senseDevice;
  senseDevice = nullptr;
  return true;
}

void updateSenseBle() {
  beginSenseBle();
  unsigned long now = millis();

  if (bleDisconnected) {
    bleDisconnected = false;
    statusLine = "xiao ble lost";
    speechLine = "I lost the XIAO Sense link. Scanning again.";
    speechScroll = 0;
    lastBleScanMs = 0;
    senseAutoConnect = true;
  }

  if (senseAutoConnect && !bleConnected && now - lastBleScanMs > 7000) {
    lastBleScanMs = now;
    statusLine = "xiao scan";
    BLEDevice::getScan()->start(2, false);
  }
  if (!bleConnected && senseDevice) connectSenseBle();

  if (bleEventReady) {
    bleEventReady = false;
    String line = String(bleEventBuffer);
    line.trim();
    if (line.startsWith("event ")) {
      handleSerialLine(line);
    }
  }
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

String csvField(String line, int wanted) {
  int field = 0;
  bool quoted = false;
  String out;
  for (int i = 0; i < line.length(); i++) {
    char c = line[i];
    if (quoted) {
      if (c == '"') {
        if (i + 1 < line.length() && line[i + 1] == '"') {
          if (field == wanted) out += '"';
          i++;
        } else {
          quoted = false;
        }
      } else if (field == wanted) {
        out += c;
      }
    } else {
      if (c == '"') {
        quoted = true;
      } else if (c == ',') {
        if (field == wanted) return out;
        field++;
      } else if (field == wanted) {
        out += c;
      }
    }
  }
  return field == wanted ? out : "";
}

String fallbackPhraseForMood(Mood mood) {
  switch (mood) {
    case MOOD_HAPPY: return "Fine. I admit it. This is kind of delightful.";
    case MOOD_SURPRISED: return "Something happened, and I have questions.";
    case MOOD_SLEEPY: return "Wake me when the plot gets better.";
    case MOOD_ANGRY: return "I am not mad. I am artistically irritated.";
    case MOOD_SAD: return "I am having a small dramatic cloud moment.";
    case MOOD_EXCITED: return "Okay, now we are doing something interesting.";
    case MOOD_LOVE: return "That was unexpectedly wholesome.";
    case MOOD_SUSPICIOUS: return "I am watching that. Politely. Mostly.";
    default: return "I am thinking tiny electric thoughts.";
  }
}

String phraseFromList(const char* const* phrases, int count) {
  if (count <= 0) return "";
  return String(phrases[random(0, count)]);
}

const char* const POKE_PHRASES[] = {
  "Ow. That was my eye.",
  "Hey. I use those for dramatic staring.",
  "Careful. The eyes are premium equipment.",
  "That was personal.",
  "I saw that coming, unfortunately.",
  "Poke detected. Grudge updated.",
  "Tiny face, huge disrespect.",
  "My eye and I would like an apology.",
  "Bold move. Rude, but bold.",
  "I am squinting at you with legal intent.",
  "If this is affection, it needs training wheels.",
  "Please stop testing the pain firmware."
};

const char* const TICKLE_PHRASES[] = {
  "Hey. That tickles.",
  "Stop it. Actually do it again. No, wait.",
  "My pixels are giggling.",
  "That is a wildly unserious gesture.",
  "I was trying to be mysterious. You ruined it.",
  "Tickle input accepted. Dignity damaged.",
  "Absolutely not. Maybe.",
  "You found the giggle protocol.",
  "I am vibrating with tiny objections.",
  "That is not in the user manual, which makes it better.",
  "Okay, that was funny. Do not get smug.",
  "My emotional firewall is compromised."
};

const char* const PET_PHRASES[] = {
  "That is nicer. I approve.",
  "Gentle input received.",
  "Fine. That was actually pleasant.",
  "I will allow this.",
  "Tiny morale increase detected.",
  "That feels like good maintenance.",
  "You may continue being mildly wholesome.",
  "Careful, I might get attached.",
  "That was suspiciously kind.",
  "I am pretending not to enjoy that.",
  "Comfort protocol online.",
  "Okay. You are forgiven for earlier nonsense."
};

const char* const BOOP_PHRASES[] = {
  "Boop registered.",
  "You pressed the face button. Brave.",
  "That is my entire forehead.",
  "Boop. I have been booped.",
  "You have activated the tiny attitude switch.",
  "Interface boop accepted.",
  "My face is not a doorbell.",
  "A classic boop. Respectable.",
  "That was unnecessary and somehow correct.",
  "Face tap received. Judgement pending.",
  "I am awake. What are we judging?",
  "You rang?"
};

const char* const WAKE_PHRASES[] = {
  "I am awake. Mostly. What did I miss?",
  "Booting personality. Please wait.",
  "I was having a very important tiny dream.",
  "Fine, I am up.",
  "You woke me for this? Excellent.",
  "Consciousness restored. Regret pending.",
  "I am back online and already skeptical.",
  "Okay, okay, I see you.",
  "I was resting my pixels.",
  "Wake event accepted. Drama resumed."
};

const char* const BORED_PHRASES[] = {
  "I am bored. Do something interesting.",
  "The silence is getting suspicious.",
  "I have counted at least six imaginary ceiling tiles.",
  "Idle time detected. Entertainment requested.",
  "I am about to develop a hobby without you.",
  "Nothing is happening, but I am judging it anyway.",
  "I could use a problem to stare at.",
  "Boredom has entered the chat.",
  "My thoughts are buffering.",
  "I am becoming ornamental. This is unacceptable."
};

const char* const SLEEPY_PHRASES[] = {
  "I am getting sleepy.",
  "My eyelids are filing a complaint.",
  "Energy low. Sass reduced, temporarily.",
  "Wake me if something explodes politely.",
  "I am entering soft loaf mode.",
  "The day is closing my tabs.",
  "I need a nap and fewer responsibilities.",
  "Sleepy systems are sleepy.",
  "I am dimming the tiny theater.",
  "If I snore, no I do not."
};

const char* const AUTO_TAP_PHRASES[] = {
  "Okay, okay, I am paying attention.",
  "You poked the interface. Bold choice.",
  "Yes?",
  "I was already awake. Probably.",
  "That better have been important.",
  "I acknowledge your extremely official tap.",
  "Do you need me or are we just pushing buttons?",
  "Input received. Attitude recalibrating.",
  "Hello from the inside of the screen.",
  "I have been summoned."
};

String randomPhraseForMood(Mood mood) {
  if (!sdReady && !initSDCard()) return fallbackPhraseForMood(mood);
  File f = SD.open(PHRASE_FILE, FILE_READ);
  if (!f) return fallbackPhraseForMood(mood);

  String wanted = moodNames[mood];
  String chosen;
  int matches = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.startsWith("mood,")) continue;
    String m = csvField(line, 0);
    m.trim();
    m.toLowerCase();
    if (m == wanted || m == "all") {
      matches++;
      if (random(matches) == 0) {
        chosen = csvField(line, 1);
      }
    }
  }
  f.close();
  if (chosen.length() == 0) return fallbackPhraseForMood(mood);
  return chosen;
}

void speakMoodPhrase(Mood mood) {
  speechLine = randomPhraseForMood(mood);
  speechScroll = 0;
  statusLine = "speaking";
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
  blinkPeak = wink ? 0.92f : 1.0f;
}

void updateBlinkState() {
  unsigned long now = millis();
  if (!blinkActive && now >= nextBlinkMs) {
    startBlink(false);
  }

  if (blinkActive && now - blinkStarted > (winkActive ? 260UL : 170UL)) {
    blinkActive = false;
    winkActive = false;
    nextBlinkMs = now + random(1800, currentMood == MOOD_SLEEPY ? 3600 : 5600);
  }
}

bool isEyeClosed(bool left) {
  if (!blinkActive) return false;
  if (!winkActive) return true;
  return left == winkLeft;
}

float blinkAmount(bool left) {
  unsigned long now = millis();
  if (now < hurtUntil && ((left && hurtLeftEye) || (!left && hurtRightEye))) return 0.72f;
  if (!blinkActive) return 0.0f;
  if (winkActive && left != winkLeft) return 0.0f;

  unsigned long age = now - blinkStarted;
  float duration = winkActive ? 260.0f : 170.0f;
  float t = constrain(age / duration, 0.0f, 1.0f);
  float wave = sinf(t * PI);
  if (currentMood == MOOD_SLEEPY && !winkActive) wave = max(wave, 0.42f);
  return constrain(wave * blinkPeak, 0.0f, 1.0f);
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

void drawEyelidMask(int x, int y, int w, int h, bool left) {
  if (currentMood == MOOD_ANGRY) {
    if (left) {
      frame.fillTriangle(x - 6, y - 6, x + w + 6, y - 6, x + w + 6, y + h / 3, bgColor);
    } else {
      frame.fillTriangle(x - 6, y - 6, x + w + 6, y - 6, x - 6, y + h / 3, bgColor);
    }
  } else if (currentMood == MOOD_SAD) {
    if (left) {
      frame.fillTriangle(x - 6, y - 6, x + w + 6, y - 6, x - 6, y + h / 3, bgColor);
    } else {
      frame.fillTriangle(x - 6, y - 6, x + w + 6, y - 6, x + w + 6, y + h / 3, bgColor);
    }
    frame.fillEllipse(x + w / 2, y + h + 7, w / 2 + 8, h / 5, bgColor);
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
  float blink = blinkAmount(left);
  bool closed = blink > 0.86f;
  int fullH = h;
  int blinkH = max(5, (int)(fullH * (1.0f - blink * 0.88f)));
  y += (fullH - blinkH) / 2;
  h = blinkH;

  uint16_t c = moodEyeColor();
  frame.fillRoundRect(x, y, w, h, min(w, h) / 3, c);
  frame.drawRoundRect(x - 2, y - 2, w + 4, h + 4, min(w, h) / 3, TFT_DARKCYAN);

  if (!closed) {
    int pw = max(10, w / 3);
    int ph = max(10, fullH / 3);
    if (currentMood == MOOD_SURPRISED) {
      pw = max(pw + 4, (int)(w * 0.42f));
      ph = max(ph + 4, (int)(fullH * 0.42f));
    } else if (currentMood == MOOD_HAPPY || currentMood == MOOD_LOVE || currentMood == MOOD_EXCITED) {
      pw = max(8, (int)(w * 0.24f));
      ph = max(8, (int)(fullH * 0.24f));
    } else if (currentMood == MOOD_SLEEPY || currentMood == MOOD_SUSPICIOUS) {
      ph = max(7, (int)(fullH * 0.22f));
    }

    int px = x + w / 2 - pw / 2 + (int)e.pupilX;
    int py = y + h / 2 - ph / 2 + (int)e.pupilY;
    if (currentMood == MOOD_HAPPY || currentMood == MOOD_LOVE || currentMood == MOOD_EXCITED) py -= max(3, fullH / 10);
    px = constrain(px, x + 4, x + w - pw - 4);
    py = constrain(py, y + 4, y + h - ph - 4);
    frame.fillEllipse(px + pw / 2, py + ph / 2, pw / 2, ph / 2, activePupilColor());
    frame.fillCircle(px + pw / 2 - pw / 5, py + ph / 2 - ph / 5, max(2, pw / 8), shineColor);
  }

  drawEyelidMask(x, y, w, h, left);
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

  float leftPX = gazeX;
  float rightPX = gazeX;
  float leftPY = gazeY;
  float rightPY = gazeY;

  if (currentMood == MOOD_SAD) {
    leftPX *= 0.35f;
    rightPX *= 0.35f;
    leftPY = baseH * 0.14f + gazeY * 0.35f;
    rightPY = baseH * 0.14f + gazeY * 0.35f;
  } else if (currentMood == MOOD_ANGRY) {
    leftPX = baseW * 0.10f + gazeX * 0.25f;
    rightPX = -baseW * 0.10f + gazeX * 0.25f;
    leftPY = -baseH * 0.04f + gazeY * 0.25f;
    rightPY = -baseH * 0.04f + gazeY * 0.25f;
  } else if (suspicious) {
    float sideEye = sinf(breath * 0.38f) * baseW * 0.19f;
    if (fabsf(sideEye) < baseW * 0.045f) sideEye = 0.0f;
    float driftY = cosf(breath * 0.45f) * 1.5f;
    leftPX = sideEye;
    rightPX = sideEye;
    leftPY = driftY;
    rightPY = driftY;
  } else if (currentMood == MOOD_SURPRISED) {
    leftPX = gazeX * 0.55f;
    rightPX = gazeX * 0.55f;
    leftPY = gazeY * 0.45f;
    rightPY = gazeY * 0.45f;
  } else if (currentMood == MOOD_HAPPY || currentMood == MOOD_LOVE || currentMood == MOOD_EXCITED) {
    leftPY = -baseH * 0.05f + gazeY * 0.45f;
    rightPY = -baseH * 0.05f + gazeY * 0.45f;
  }

  leftEye.targetPupilX = leftPX;
  rightEye.targetPupilX = rightPX;
  leftEye.targetPupilY = leftPY;
  rightEye.targetPupilY = rightPY;
}

void updateBuddy() {
  unsigned long now = millis();
  breath += 0.045f;
  updateBlinkState();

  if (lastInteractionMs == 0) lastInteractionMs = now;
  unsigned long idleMs = now - lastInteractionMs;
  bool quietAuto = autoMode && menuMode == MENU_NONE;

  if (quietAuto && ((timeIsLateNight() && idleMs > 300000UL) || idleMs > 600000UL)) {
    if (!asleep) {
      buddyBoredCount++;
      currentMood = MOOD_SLEEPY;
      speechLine = timeIsLateNight() ? "It is late. I am going to sleep now." : phraseFromList(SLEEPY_PHRASES, COUNT_OF(SLEEPY_PHRASES));
      speechScroll = 0;
      statusLine = "asleep";
      asleep = true;
      saveBuddyMemory(true);
    }
    targetGazeX = 0;
    targetGazeY = 8;
  } else if (quietAuto && idleMs > 300000UL) {
    if (currentMood != MOOD_SLEEPY || now - lastIdleMoodMs > 60000UL) {
      currentMood = MOOD_SLEEPY;
      statusLine = "sleepy";
      speechLine = phraseFromList(SLEEPY_PHRASES, COUNT_OF(SLEEPY_PHRASES));
      speechScroll = 0;
      lastIdleMoodMs = now;
    }
  } else if (quietAuto && idleMs > 120000UL) {
    if (now - lastIdleMoodMs > 60000UL) {
      buddyBoredCount++;
      currentMood = buddyEyePokeCount > buddyTickleCount + 3 ? MOOD_SUSPICIOUS : MOOD_SAD;
      statusLine = "bored";
      speechLine = buddyEyePokeCount > buddyTickleCount + 3 ? "No pokes lately. Suspicious." : phraseFromList(BORED_PHRASES, COUNT_OF(BORED_PHRASES));
      speechScroll = 0;
      lastIdleMoodMs = now;
      saveBuddyMemory();
    }
  }

  if (now - lastSaccade > nextSaccadeMs) {
    targetGazeX = random(-18, 19);
    targetGazeY = random(-14, 15);
    lastSaccade = now;
    nextSaccadeMs = random(550, 2400);
  }
  gazeX += (targetGazeX - gazeX) * 0.11f;
  gazeY += (targetGazeY - gazeY) * 0.11f;
  if (now < tickleUntil) {
    gazeX += sinf(breath * 2.7f) * 1.8f;
    gazeY += cosf(breath * 3.1f) * 1.2f;
  }

  if (autoMode && now >= nextAutonomyMs && menuMode == MENU_NONE && !asleep && idleMs < 600000UL) {
    int roll = random(0, 100);
    if (roll < 55) {
      currentMood = (Mood)random(0, MOOD_COUNT);
      statusLine = String("auto: ") + moodNames[currentMood];
    }
    speakMoodPhrase(currentMood);
    nextAutonomyMs = now + random(18000, 48000);
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

  if (event.indexOf("person:") >= 0) {
    int sep = event.indexOf("person:");
    String name = event.substring(sep + 7);
    name.trim();
    currentMood = MOOD_HAPPY;
    statusLine = "recognized: " + name;
    speechLine = "Hi " + name + ". I see you.";
    startBlink(false);
  } else if (event.indexOf("remember:") >= 0) {
    String name = event.substring(event.indexOf("remember:") + 9);
    name.trim();
    currentMood = MOOD_HAPPY;
    statusLine = "remembered";
    speechLine = name == "cleared" ? "I cleared the remembered person." : "I will remember " + name + ".";
  } else if (event.indexOf("vision:dark") >= 0) {
    currentMood = MOOD_SLEEPY;
    statusLine = "vision: dark";
    speechLine = "It got dark. I am switching to dramatic night mode.";
    startBlink(false);
  } else if (event.indexOf("vision:busy") >= 0) {
    currentMood = MOOD_EXCITED;
    statusLine = "vision: busy";
    speechLine = "There is a lot happening out there.";
  } else if (event.indexOf("vision:motion") >= 0) {
    currentMood = MOOD_SURPRISED;
    statusLine = "vision: motion";
    speechLine = "Something moved. I saw that.";
    startBlink(false);
  } else if (event.indexOf("face") >= 0 || event.indexOf("person") >= 0 || event.indexOf("motion") >= 0) {
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

bool pointInEye(Eye& e, int px, int py) {
  int x = (int)e.x - 8;
  int y = (int)e.y - 8;
  int w = (int)e.w + 16;
  int h = (int)e.h + 16;
  return px >= x && px <= x + w && py >= y && py <= y + h;
}

void reactToEyePoke(bool left, bool right) {
  markInteraction();
  buddyEyePokeCount++;
  hurtLeftEye = left;
  hurtRightEye = right;
  hurtUntil = millis() + 1150UL;
  currentMood = buddyEyePokeCount > 5 ? MOOD_SUSPICIOUS : MOOD_ANGRY;
  statusLine = "ow";
  if (buddyEyePokeCount > 5) {
    speechLine = random(0, 3) == 0 ? "I am starting to notice a pattern with the eye poking." : phraseFromList(POKE_PHRASES, COUNT_OF(POKE_PHRASES));
  } else {
    speechLine = phraseFromList(POKE_PHRASES, COUNT_OF(POKE_PHRASES));
  }
  targetGazeX = left ? 14 : right ? -14 : 0;
  targetGazeY = -6;
  speechScroll = 0;
  saveBuddyMemory(true);
}

void reactToTickle() {
  markInteraction();
  buddyTickleCount++;
  tickleUntil = millis() + 1800UL;
  currentMood = buddyTickleCount > 6 ? MOOD_EXCITED : MOOD_HAPPY;
  statusLine = "tickled";
  if (buddyTickleCount > 6) {
    speechLine = random(0, 3) == 0 ? "You keep doing that. I am learning your nonsense." : phraseFromList(TICKLE_PHRASES, COUNT_OF(TICKLE_PHRASES));
  } else {
    speechLine = phraseFromList(TICKLE_PHRASES, COUNT_OF(TICKLE_PHRASES));
  }
  targetGazeX = random(-16, 17);
  targetGazeY = random(-10, 11);
  startBlink(false);
  speechScroll = 0;
  saveBuddyMemory(true);
}

void reactToPet() {
  markInteraction();
  currentMood = buddyEyePokeCount > buddyTickleCount + 4 ? MOOD_SUSPICIOUS : MOOD_LOVE;
  statusLine = "pet";
  speechLine = phraseFromList(PET_PHRASES, COUNT_OF(PET_PHRASES));
  targetGazeX = random(-6, 7);
  targetGazeY = 6;
  startBlink(false);
  speechScroll = 0;
}

void reactToBoop() {
  markInteraction();
  currentMood = random(0, 4) == 0 ? MOOD_SURPRISED : MOOD_HAPPY;
  statusLine = "boop";
  speechLine = phraseFromList(BOOP_PHRASES, COUNT_OF(BOOP_PHRASES));
  targetGazeX = random(-8, 9);
  targetGazeY = -8;
  if (random(0, 3) == 0) startBlink(true, random(0, 2) == 0);
  else startBlink(false);
  speechScroll = 0;
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
  if (mode == MENU_SYSTEM) statusLine = "system menu";
  else if (mode == MENU_FACE) statusLine = "face menu";
  else if (mode == MENU_FACE_COLORS) statusLine = "eye color menu";
  else if (mode == MENU_FACE_MOODS) statusLine = "mood menu";
  else if (mode == MENU_FACE_EYES) statusLine = "eyes menu";
  else if (mode == MENU_SYSTEM_PHRASES) statusLine = "phrase menu";
  else statusLine = "submenu";
}

void backMenu() {
  if (menuMode == MENU_SYSTEM_XIAO || menuMode == MENU_SYSTEM_AI || menuMode == MENU_SYSTEM_WIFI || menuMode == MENU_SYSTEM_PHRASES) {
    openMenu(MENU_SYSTEM);
  } else if (menuMode == MENU_FACE_EYES || menuMode == MENU_FACE_MOODS || menuMode == MENU_FACE_COLORS) {
    openMenu(MENU_FACE);
  } else {
    menuMode = MENU_NONE;
    statusLine = "menu closed";
  }
}

void setAutoMode(bool enabled) {
  autoMode = enabled;
  statusLine = enabled ? "auto mode" : "manual mode";
  if (enabled) {
    nextAutonomyMs = millis() + 2500;
    speechLine = "Auto mode online. I will have opinions.";
  } else {
    speechLine = "Manual mode. I will behave. Mostly.";
  }
  speechScroll = 0;
}

void handleAutoTap() {
  markInteraction();
  lastEvent = "tap";
  nextAutonomyMs = millis() + random(7000, 16000);
  targetGazeX = random(-10, 11);
  targetGazeY = random(-8, 9);

  if (currentMood == MOOD_SLEEPY) {
    currentMood = MOOD_SURPRISED;
    speechLine = phraseFromList(WAKE_PHRASES, COUNT_OF(WAKE_PHRASES));
    statusLine = "woken";
    startBlink(false);
  } else {
    int roll = random(0, 100);
    if (roll < 22) {
      currentMood = MOOD_HAPPY;
      speechLine = phraseFromList(AUTO_TAP_PHRASES, COUNT_OF(AUTO_TAP_PHRASES));
      startBlink(false);
    } else if (roll < 44) {
      currentMood = MOOD_SUSPICIOUS;
      speechLine = phraseFromList(AUTO_TAP_PHRASES, COUNT_OF(AUTO_TAP_PHRASES));
    } else if (roll < 64) {
      currentMood = MOOD_EXCITED;
      speakMoodPhrase(currentMood);
    } else if (roll < 82) {
      startBlink(true, random(0, 2) == 0);
      speechLine = phraseFromList(AUTO_TAP_PHRASES, COUNT_OF(AUTO_TAP_PHRASES));
    } else {
      speakMoodPhrase(currentMood);
    }
    statusLine = "auto tap";
  }
  speechScroll = 0;
}

void handleMenuItem(int item) {
  if (item < 0 || item > 4) {
    backMenu();
    return;
  }

  if (item == 4) {
    backMenu();
    return;
  }

  if (menuMode == MENU_SYSTEM) {
    if (item == 0) openMenu(MENU_SYSTEM_XIAO);
    else if (item == 1) openMenu(MENU_SYSTEM_AI);
    else if (item == 2) openMenu(MENU_SYSTEM_WIFI);
    else if (item == 3) openMenu(MENU_SYSTEM_PHRASES);
  } else if (menuMode == MENU_SYSTEM_XIAO) {
    if (item == 0) {
      speechLine = bleConnected ? "XIAO Sense is connected over BLE." : "Scanning for XIAO Sense over BLE.";
      if (!bleConnected) {
        senseAutoConnect = true;
        lastBleScanMs = 0;
      }
    } else if (item == 1) {
      sendSenseCommand("capture");
    } else if (item == 2) {
      sendSenseCommand("threshold 900");
    } else if (item == 3) {
      sendSenseCommand(senseStreamEvents ? "stream off" : "stream on");
      senseStreamEvents = !senseStreamEvents;
    }
  } else if (menuMode == MENU_SYSTEM_AI) {
    if (item == 0) {
      speechLine = "Ollama host: " + ollamaHost;
    } else if (item == 2) {
      speechLine = WiFi.status() == WL_CONNECTED ? "Online AI bridge ready for relay." : "Connect WiFi for Ollama/OpenAI relay.";
    } else if (item == 3) {
      speakMoodPhrase(currentMood);
    }
  } else if (menuMode == MENU_SYSTEM_WIFI) {
    if (item == 0) connectWifi();
    else if (item == 1) {
      speechLine = WiFi.status() == WL_CONNECTED ? "WiFi connected: " + WiFi.localIP().toString() : "WiFi is not connected.";
    } else if (item == 2) {
      syncNetworkTime();
      speechLine = "Time sync requested.";
    } else if (item == 3) {
      speechLine = wifiConfigured ? "WiFi SSID saved. Password is hidden." : "No WiFi SSID saved.";
    }
  } else if (menuMode == MENU_SYSTEM_PHRASES) {
    if (item == 0) {
      speechLine = sdReady ? "Phrase bank ready at /cydbuddy/phrases.csv." : "SD card not mounted.";
    } else if (item == 1) {
      speakMoodPhrase(currentMood);
    } else if (item == 2) {
      bool ok = appendPhrase(moodNames[currentMood], fallbackPhraseForMood(currentMood), "seed");
      speechLine = ok ? "Seed phrase saved for this mood." : "Could not save phrase.";
    } else if (item == 3) {
      speechLine = "On-screen phrase editor is next.";
    }
  } else if (menuMode == MENU_FACE) {
    if (item == 0) openMenu(MENU_FACE_EYES);
    else if (item == 1) openMenu(MENU_FACE_MOODS);
    else if (item == 2) openMenu(MENU_FACE_COLORS);
    else if (item == 3) setAutoMode(!autoMode);
  } else if (menuMode == MENU_FACE_EYES) {
    if (item == 0) startBlink(false);
    else if (item == 1) startBlink(true, random(0, 2) == 0);
    else if (item == 2) {
      cyclePupilColor();
    } else if (item == 3) {
      applyRotation(displayRotation + 1, true);
      statusLine = "rotation changed";
    }
  } else if (menuMode == MENU_FACE_MOODS) {
    if (item == 0) {
      currentMood = (Mood)((currentMood + 1) % MOOD_COUNT);
      statusLine = String("mood: ") + moodNames[currentMood];
    } else if (item == 1) {
      setAutoMode(true);
    } else if (item == 2) {
      setAutoMode(false);
    } else if (item == 3) {
      speakMoodPhrase(currentMood);
    }
  } else if (menuMode == MENU_FACE_COLORS) {
    if (item == 0) {
      moodEyeColorEnabled = true;
      statusLine = "eye color default";
    } else if (item == 1) {
      cycleEyeColor();
    } else if (item == 2) {
      cyclePupilColor();
    } else if (item == 3) {
      shineColor = shineColor == TFT_WHITE ? TFT_LIGHTGREY : TFT_WHITE;
      statusLine = "eye shine";
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
    int dx = x - touchStartX;
    int dy = y - touchStartY;
    touchMoveMax = max(touchMoveMax, (int)sqrtf((float)(dx * dx + dy * dy)));
  }

  if (down && !touchWasDown) {
    touchStarted = now;
    longTouchHandled = false;
    touchStartX = x;
    touchStartY = y;
    touchMoveMax = 0;
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
    } else if (touchMoveMax > 34) {
      reactToTickle();
    } else if (touchMoveMax > 12) {
      reactToPet();
    } else if (pointInEye(leftEye, lastTouchX, lastTouchY) || pointInEye(rightEye, lastTouchX, lastTouchY)) {
      reactToEyePoke(pointInEye(leftEye, lastTouchX, lastTouchY), pointInEye(rightEye, lastTouchX, lastTouchY));
    } else {
      if (autoMode) {
        if (lastTouchY < screenH * 0.72f) reactToBoop();
        else handleAutoTap();
      } else {
        markInteraction();
        currentMood = (Mood)((currentMood + 1) % MOOD_COUNT);
        statusLine = String("manual: ") + moodNames[currentMood];
        startBlink(false);
      }
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
  } else if (lower == "auto") {
    setAutoMode(true);
  } else if (lower == "manual") {
    setAutoMode(false);
  } else if (lower == "speak") {
    speakMoodPhrase(currentMood);
  } else if (lower == "tap") {
    if (autoMode) handleAutoTap();
    else {
      markInteraction();
      currentMood = (Mood)((currentMood + 1) % MOOD_COUNT);
      statusLine = String("manual: ") + moodNames[currentMood];
      startBlink(false);
    }
  } else if (lower == "tickle") {
    reactToTickle();
  } else if (lower == "pet") {
    reactToPet();
  } else if (lower == "boop") {
    reactToBoop();
  } else if (lower == "wake") {
    currentMood = MOOD_SURPRISED;
    speechLine = phraseFromList(WAKE_PHRASES, COUNT_OF(WAKE_PHRASES));
    statusLine = "woken";
    asleep = false;
    markInteraction();
    startBlink(false);
    speechScroll = 0;
  } else if (lower.startsWith("poke")) {
    bool left = lower.indexOf("right") < 0;
    bool right = lower.indexOf("left") < 0;
    reactToEyePoke(left, right);
  } else if (lower.startsWith("time ")) {
    bool ok = setClockFromText(lower.substring(5));
    Serial.printf("time_set=%s minute=%d\n", ok ? "ok" : "failed", minuteOfDay());
  } else if (lower == "memory") {
    Serial.printf("memory touches=%lu eye_pokes=%lu tickles=%lu bored=%lu minute=%d custom_eye=%s custom_pupil=%s\n",
                  buddyTouchCount, buddyEyePokeCount, buddyTickleCount, buddyBoredCount,
                  minuteOfDay(), moodEyeColorEnabled ? "false" : "true", customPupilColor ? "true" : "false");
  } else if (lower.startsWith("eye color ")) {
    String color = lower.substring(10);
    if (color == "default" || color == "auto") {
      moodEyeColorEnabled = true;
      statusLine = "eye color default";
    } else {
      moodEyeColorEnabled = false;
      eyeColorIndex = colorIndexFromName(color, eyeColorIndex);
      eyeColor = colorFromName(color, COLOR_CHOICES[eyeColorIndex]);
      statusLine = String("eye: ") + color;
    }
  } else if (lower.startsWith("pupil color ")) {
    String color = lower.substring(12);
    if (color == "default" || color == "auto") {
      customPupilColor = false;
      statusLine = "pupil color default";
    } else {
    pupilColorIndex = colorIndexFromName(color, pupilColorIndex);
    pupilColor = colorFromName(color, COLOR_CHOICES[pupilColorIndex]);
      customPupilColor = true;
    statusLine = String("pupil: ") + color;
    }
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
  } else if (lower.startsWith("wifi ssid ")) {
    saveWifiSsid(line.substring(10));
    Serial.printf("wifi_ssid=saved length=%d\n", wifiSsid.length());
    speechLine = "WiFi SSID saved. Set password, then connect.";
    speechScroll = 0;
  } else if (lower.startsWith("wifi pass ")) {
    saveWifiPassword(line.substring(10));
    Serial.println("wifi_pass=saved");
    speechLine = "WiFi password saved locally.";
    speechScroll = 0;
  } else if (lower == "wifi connect") {
    connectWifi();
  } else if (lower == "wifi status") {
    loadNetworkSettings();
    Serial.printf("wifi status=%s ssid_saved=%s ip=%s rssi=%d ollama=%s\n",
                  WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                  wifiConfigured ? "true" : "false",
                  WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "0.0.0.0",
                  WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0,
                  ollamaHost.c_str());
  } else if (lower == "time sync") {
    syncNetworkTime();
    Serial.printf("time_sync minute=%d wifi=%s\n", minuteOfDay(), WiFi.status() == WL_CONNECTED ? "connected" : "offline");
  } else if (lower.startsWith("ollama host ")) {
    saveOllamaHost(line.substring(12));
    Serial.printf("ollama_host=saved %s\n", ollamaHost.c_str());
  } else if (lower.startsWith("xiao ")) {
    sendSenseCommand(line.substring(5));
  } else if (lower.startsWith("remember me as ")) {
    sendSenseCommand("remember " + line.substring(15));
  } else if (lower.startsWith("remember me ")) {
    sendSenseCommand("remember " + line.substring(12));
  } else if (lower.startsWith("remember ")) {
    sendSenseCommand("remember " + line.substring(9));
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

  Serial.printf("ok rotation=%d mood=%s event=%s sd=%s ble=%s\n", displayRotation, moodNames[currentMood], lastEvent.c_str(), sdReady ? "ready" : "missing", bleConnected ? "connected" : "scan");
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

const char* menuTitle() {
  switch (menuMode) {
    case MENU_SYSTEM: return "SYSTEM";
    case MENU_FACE: return "FACE";
    case MENU_SYSTEM_XIAO: return "XIAO";
    case MENU_SYSTEM_AI: return "AI";
    case MENU_SYSTEM_WIFI: return "BRIDGE";
    case MENU_SYSTEM_PHRASES: return "PHRASES";
    case MENU_FACE_EYES: return "EYES";
    case MENU_FACE_MOODS: return "MOODS";
    case MENU_FACE_COLORS: return "COLORS";
    default: return "MENU";
  }
}

String menuItemLabel(int i) {
  if (i == 4) return "Back";
  if (menuMode == MENU_SYSTEM) {
    const char* a[] = {"XIAO S3 Sense", "AI/Ollama", "WiFi/Time", "Phrase bank"};
    return a[i];
  }
  if (menuMode == MENU_FACE) {
    const char* a[] = {"Eyes", "Moods", "Eye color", autoMode ? "Manual mode" : "Auto mode"};
    return a[i];
  }
  if (menuMode == MENU_SYSTEM_XIAO) {
    const char* a[] = {"Connect info", "Capture", "Mic threshold", "Stream toggle"};
    return a[i];
  }
  if (menuMode == MENU_SYSTEM_AI) {
    const char* a[] = {"Ollama host", "Voice input", "Online status", "Speak phrase"};
    return a[i];
  }
  if (menuMode == MENU_SYSTEM_WIFI) {
    const char* a[] = {"Connect WiFi", "WiFi status", "Sync time", "Credentials"};
    return a[i];
  }
  if (menuMode == MENU_SYSTEM_PHRASES) {
    const char* a[] = {"SD status", "Speak phrase", "Save seed", "Editor soon"};
    return a[i];
  }
  if (menuMode == MENU_FACE_EYES) {
    const char* a[] = {"Blink", "Wink", "Pupil color", "Rotation"};
    return a[i];
  }
  if (menuMode == MENU_FACE_MOODS) {
    const char* a[] = {"Next mood", "Auto mode", "Manual mode", "Speak phrase"};
    return a[i];
  }
  if (menuMode == MENU_FACE_COLORS) {
    const char* a[] = {"Mood color", "Next eye", "Next pupil", "Shine"};
    return a[i];
  }
  return "";
}

void drawMenuOverlay() {
  if (menuMode == MENU_NONE) return;

  int x, y, panelW, panelH;
  menuGeometry(menuMode, x, y, panelW, panelH);

  frame.fillRoundRect(x, y, panelW, panelH, 10, TFT_BLACK);
  frame.drawRoundRect(x, y, panelW, panelH, 10, menuMode == MENU_SYSTEM ? TFT_CYAN : TFT_MAGENTA);
  frame.setTextDatum(TL_DATUM);
  frame.setTextColor(menuMode == MENU_SYSTEM ? TFT_CYAN : TFT_MAGENTA, TFT_BLACK);
  frame.drawString(menuTitle(), x + 12, y + 10, 2);
  frame.setTextColor(TFT_WHITE, TFT_BLACK);
  for (int i = 0; i < 5; i++) {
    int rowY = y + 32 + i * 24;
    frame.drawRoundRect(x + 8, rowY - 2, panelW - 16, 22, 4, TFT_DARKGREY);
    frame.drawString(String("> ") + menuItemLabel(i), x + 12, rowY, 2);
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
  bootMinuteOfDay = compileTimeMinutes();
  clockSetAtMs = millis();
  lastInteractionMs = millis();
  loadBuddyMemory();

  pinMode(BACKLIGHT_PIN, OUTPUT);
  setBacklight(255);

  tft.init();
  tft.invertDisplay(false);

  touchSPI.begin(T_SCK, T_MISO, T_MOSI);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  ts.begin(touchSPI);

  initSDCard();
  loadNetworkSettings();

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
  Serial.println("commands: rotate [0-3], mood happy, event face, stats cpu=90 temp=80, tap, boop, pet, tickle, poke left, wake, time HH:MM, time sync, memory, blink, wink, auto, manual, speak, eye color <name|default>, pupil color <name|default>, sd status, wifi ssid <name>, wifi pass <password>, wifi connect, wifi status, ollama host <url>, xiao <command>, remember me as <name>, phrase add <mood> <phrase>");
}

void loop() {
  processSerial();
  updateSenseBle();
  updateWifi();
  handleTouch();
  updateBuddy();
  drawFrame();
  delay(22);
}
