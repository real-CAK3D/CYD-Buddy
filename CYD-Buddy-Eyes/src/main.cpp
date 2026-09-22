#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <FS.h>
#include <SD.h>
#include <math.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
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

static const int DEFAULT_TOUCH_X_MIN = 562;
static const int DEFAULT_TOUCH_X_MAX = 3516;
static const int DEFAULT_TOUCH_Y_MIN = 525;
static const int DEFAULT_TOUCH_Y_MAX = 3490;
static const bool TOUCH_SWAP_XY = false;
static const bool TOUCH_FLIP_X = false;
static const bool TOUCH_FLIP_Y = false;
int touchXMin = DEFAULT_TOUCH_X_MIN;
int touchXMax = DEFAULT_TOUCH_X_MAX;
int touchYMin = DEFAULT_TOUCH_Y_MIN;
int touchYMax = DEFAULT_TOUCH_Y_MAX;

TFT_eSPI tft;
SPIClass touchSPI(VSPI);
SPIClass sdSPI(HSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
TFT_eSprite frame(&tft);
Preferences prefs;
bool frameOk = false;
bool sdReady = false;
static const unsigned long FRAME_INTERVAL_MS = 33;
unsigned long lastFrameDrawMs = 0;

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
  MOOD_STONER,
  MOOD_DRUNK,
  MOOD_HIPPY,
  MOOD_BORED,
  MOOD_RESTLESS,
  MOOD_ANXIOUS,
  MOOD_COUNT
};

enum MenuMode {
  MENU_NONE,
  MENU_SYSTEM,
  MENU_FACE,
  MENU_SYSTEM_AI,
  MENU_SYSTEM_WIFI,
  MENU_SYSTEM_PHRASES,
  MENU_SYSTEM_TIME,
  MENU_FACE_EYES,
  MENU_FACE_MOODS,
  MENU_FACE_COLORS,
  MENU_STATS
};

enum WifiEditField {
  WIFI_EDIT_SSID,
  WIFI_EDIT_PASS
};

enum Personality {
  PERSONALITY_SASSY,
  PERSONALITY_SWEET,
  PERSONALITY_RUDE,
  PERSONALITY_NERDY,
  PERSONALITY_CHILL,
  PERSONALITY_CHAOTIC,
  PERSONALITY_COUNT
};

const char* moodNames[] = {
  "curious", "happy", "surprised", "sleepy", "angry",
  "sad", "excited", "love", "suspicious", "stoner", "drunk", "hippy",
  "bored", "restless", "anxious"
};

const char* personalityNames[] = {
  "sassy", "sweet", "rude", "nerdy", "chill", "chaotic"
};

static const int MEMORY_BANK_COUNT = 5;
static const int SEASON_COUNT = 4;
static const int MONTH_COUNT = 12;
static const int TIME_PREF_COUNT = 6;
static const int ACTIVITY_COUNT = 10;
const char* seasonNames[] = {"spring", "summer", "fall", "winter"};
const char* timePreferenceNames[] = {"early AM", "morning", "daytime", "late PM", "night", "late night"};
const char* activityNames[] = {"boops", "pets", "tickles", "eye pokes", "swipes", "feeding", "playing", "wifi hunting", "bluetooth spotting", "weather watching"};

Mood currentMood = MOOD_NORMAL;
MenuMode menuMode = MENU_NONE;
Personality currentPersonality = PERSONALITY_SASSY;
bool autoMode = true;
bool moodEyeColorEnabled = true;
unsigned long lastSaccade = 0;
unsigned long nextSaccadeMs = 900;
unsigned long lastMoodAuto = 0;
unsigned long nextAutonomyMs = 0;
unsigned long nextChatterMs = 0;
unsigned long lastChatterMs = 0;
unsigned long manualMoodHoldUntil = 0;
unsigned long lastTouchMs = 0;
unsigned long touchStarted = 0;
bool touchWasDown = false;
bool longTouchHandled = false;
int lastTouchX = 0;
int lastTouchY = 0;
int lastRawTouchX = 0;
int lastRawTouchY = 0;
int touchStartX = 0;
int touchStartY = 0;
int touchMoveMax = 0;
String serialLine;
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
bool userHasInteracted = false;
bool asleep = false;
int bootMinuteOfDay = 0;
unsigned long clockSetAtMs = 0;
unsigned long buddyTouchCount = 0;
unsigned long buddyEyePokeCount = 0;
unsigned long buddyTickleCount = 0;
unsigned long buddyBoredCount = 0;
unsigned long buddySwipeLeftCount = 0;
unsigned long buddySwipeRightCount = 0;
unsigned long buddySwipeUpCount = 0;
unsigned long buddySwipeDownCount = 0;
unsigned long buddyNewNetworkCount = 0;
unsigned long buddyNewBluetoothCount = 0;
unsigned long missedFeedings = 0;
unsigned long lastMemorySaveMs = 0;
unsigned long lastLifeTickMs = 0;
int careDriftRemainder = 0;
int buddyHealthTenth = 1000;
int buddyHunger = 28;
int buddyPlayNeed = 35;
int buddyRestless = 15;
int buddyAnxiety = 8;
int buddyStrength = 1;
int buddyArmor = 0;
int dailyHealthGainTenth = 0;
int dailyFeedCount = 0;
int dailyPlayCount = 0;
int lastCareDay = -1;
int lastBoostWeek = -1;
uint64_t lastFeedUnix = 0;
uint64_t lastPlayUnix = 0;
int seasonAffinity[SEASON_COUNT] = {0, 0, 0, 0};
int monthAffinity[MONTH_COUNT] = {0};
int timeAffinity[TIME_PREF_COUNT] = {0, 0, 0, 0, 0, 0};
int activityAffinity[ACTIVITY_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
unsigned long preferenceLearnCount = 0;
unsigned long memoryRevisionCount = 0;
unsigned long lastPreferenceThoughtMs = 0;
String buddyMemoryBank[MEMORY_BANK_COUNT];

bool wifiConfigured = false;
bool wifiConnecting = false;
bool wifiSetupPortalActive = false;
String wifiSsid;
String ollamaHost;
String spac3Host;
bool spac3TelemetryEnabled = true;
bool spac3TelemetryOk = false;
unsigned long lastSpac3PollMs = 0;
unsigned long nextSpac3PollMs = 3500;
unsigned long lastSpac3HeartbeatMs = 0;
unsigned long lastSpac3QuietMs = 0;
unsigned long lastSpac3LearningMs = 0;
String spac3LastMood = "";
String spac3LastFace = "";
String spac3LastMessage = "";
String spac3LastHost = "";
String spac3LastEventKind = "";
int spac3LastAlert = 0;
int spac3LastWifiCount = 0;
float spac3LastCpuC = NAN;
float spac3LastRam = NAN;
float weatherLat = 0.0f;
float weatherLon = 0.0f;
bool weatherConfigured = false;
String weatherSummary = "weather unset";
unsigned long lastWeatherMs = 0;
unsigned long nextWeatherMs = 0;
unsigned long wifiStartedMs = 0;
unsigned long lastWifiCheckMs = 0;
unsigned long wifiSetupStartedMs = 0;
String infoLineCache;
unsigned long lastInfoLineMs = 0;
WebServer setupServer(80);
bool wifiEditorActive = false;
bool wifiScanListActive = false;
WifiEditField wifiEditField = WIFI_EDIT_SSID;
String wifiEditSsid;
String wifiEditPass;
bool wifiEditShift = false;
static const int WIFI_SCAN_MAX = 12;
static const int WIFI_SCAN_ROWS = 5;
String wifiScanSsid[WIFI_SCAN_MAX];
int wifiScanRssi[WIFI_SCAN_MAX];
bool wifiScanSecure[WIFI_SCAN_MAX];
int wifiScanCount = 0;
int wifiScanPage = 0;
bool wifiScanInProgress = false;
unsigned long wifiScanStartedMs = 0;

bool statsViewActive = false;
int statsViewPage = 0;
bool sdPhraseLookupEnabled = false;
bool buddyMemoryDirty = false;
String statsMemoryPreview;
unsigned long statsMemoryPreviewMs = 0;

bool timeEditorActive = false;
int timeEditField = 0;
bool scheduleEditorActive = false;
int scheduleEditField = 0;
int dateYear = 2026;
int dateMonth = 9;
int dateDay = 14;
int timezoneOffsetMinutes = -300;
bool daylightSavings = true;
bool clock24Hour = false;
int earlyMorningStart = 4 * 60;
int morningStart = 7 * 60;
int daytimeStart = 11 * 60;
int lateAfternoonStart = 16 * 60;
int nightStart = 20 * 60;
int lateNightStart = 23 * 60;

uint64_t totalAliveSeconds = 0;
uint64_t totalDeadSeconds = 0;
uint64_t lastKnownUnix = 0;
uint64_t previousKnownUnix = 0;
uint64_t unixBase = 0;
unsigned long unixBaseMs = 0;
unsigned long lifecycleSavedMs = 0;
unsigned long lifecycleBootMs = 0;
unsigned long lastCalendarRefreshMs = 0;
uint32_t bootCount = 0;
uint32_t deathCount = 0;
bool deadTimeSettledThisBoot = false;

bool touchCalActive = false;
int touchCalStep = 0;
int touchCalRawX[2] = {DEFAULT_TOUCH_X_MIN, DEFAULT_TOUCH_X_MAX};
int touchCalRawY[2] = {DEFAULT_TOUCH_Y_MIN, DEFAULT_TOUCH_Y_MAX};

void handleSerialLine(String line);
bool initSDCard();
void setAutoMode(bool enabled);
void connectWifi();
void setUnixBaseFromClock();
void settleDeadTimeFromClock(bool force = false);
void saveBuddyMemory(bool force = false);
void refreshCalendarFromUnixEstimate(bool force = false);
void updateSpac3Ghost();
String monthName(int month);
String calendarContextLine();
bool syncTimeFromSpac3();
void startBlink(bool wink = false, bool left = false);

String lastEvent = "idle";
String statusLine = "tap mood, hold rotate";
String speechLine = "";
String lastAutoSpeechLine = "";
int speechScroll = 0;
unsigned long lastSpeechScroll = 0;
unsigned long speechScrollMs = 140;
String buddyName = "Buddy";

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

bool minuteInWindow(int m, int start, int end) {
  if (start == end) return true;
  if (start < end) return m >= start && m < end;
  return m >= start || m < end;
}

String hhmmFromMinutes(int total) {
  total = (total % 1440 + 1440) % 1440;
  char buffer[8];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", total / 60, total % 60);
  return String(buffer);
}

bool isDigitsOnly(const String& value) {
  if (value.length() == 0) return false;
  for (int i = 0; i < value.length(); i++) {
    if (!isDigit(value[i])) return false;
  }
  return true;
}

bool parseHHMM(String value, int& out) {
  value.trim();
  int sep = value.indexOf(':');
  if (sep < 0) return false;
  String hours = value.substring(0, sep);
  String minutes = value.substring(sep + 1);
  if (!isDigitsOnly(hours) || !isDigitsOnly(minutes)) return false;
  int h = value.substring(0, sep).toInt();
  int m = value.substring(sep + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  out = h * 60 + m;
  return true;
}

int currentYear() {
  return dateYear;
}

int currentMonth() {
  return dateMonth;
}

int currentDay() {
  return dateDay;
}

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  month = constrain(month, 1, 12);
  if (month == 2 && isLeapYear(year)) return 29;
  return days[month - 1];
}

int dayOfWeek(int year, int month, int day) {
  if (month < 3) {
    month += 12;
    year--;
  }
  int k = year % 100;
  int j = year / 100;
  int h = (day + (13 * (month + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
  return (h + 6) % 7; // Sunday = 0
}

int nthWeekdayOfMonth(int year, int month, int weekday, int nth) {
  int first = dayOfWeek(year, month, 1);
  int day = 1 + ((weekday - first + 7) % 7) + (nth - 1) * 7;
  return day <= daysInMonth(year, month) ? day : -1;
}

int lastWeekdayOfMonth(int year, int month, int weekday) {
  int last = daysInMonth(year, month);
  int lastDow = dayOfWeek(year, month, last);
  return last - ((lastDow - weekday + 7) % 7);
}

int currentSeasonIndexForMonth(int month, float lat, bool hasLocation) {
  int m = constrain(month, 1, 12);
  bool southern = hasLocation && lat < -0.1f;
  int season = 0;
  if (m == 12 || m <= 2) season = southern ? 1 : 3;
  else if (m >= 3 && m <= 5) season = southern ? 2 : 0;
  else if (m >= 6 && m <= 8) season = southern ? 3 : 1;
  else season = southern ? 0 : 2;
  return season;
}

String holidayName(int year, int month, int day) {
  if (month == 1 && day == 1) return "New Year's Day";
  if (month == 1 && day == nthWeekdayOfMonth(year, 1, 1, 3)) return "Martin Luther King Jr. Day";
  if (month == 2 && day == 14) return "Valentine's Day";
  if (month == 2 && day == nthWeekdayOfMonth(year, 2, 1, 3)) return "Presidents Day";
  if (month == 3 && day == 17) return "St. Patrick's Day";
  if (month == 5 && day == lastWeekdayOfMonth(year, 5, 1)) return "Memorial Day";
  if (month == 6 && day == 19) return "Juneteenth";
  if (month == 7 && day == 4) return "Independence Day";
  if (month == 9 && day == nthWeekdayOfMonth(year, 9, 1, 1)) return "Labor Day";
  if (month == 10 && day == 31) return "Halloween";
  if (month == 11 && day == 11) return "Veterans Day";
  if (month == 11 && day == nthWeekdayOfMonth(year, 11, 4, 4)) return "Thanksgiving";
  if (month == 12 && day == 24) return "Christmas Eve";
  if (month == 12 && day == 25) return "Christmas Day";
  if (month == 12 && day == 31) return "New Year's Eve";
  return "";
}

String calendarContextLine() {
  int season = currentSeasonIndexForMonth(dateMonth, weatherLat, weatherConfigured);
  String line = monthName(dateMonth) + " " + String(dateDay) + ", " + String(dateYear);
  line += " is " + String(seasonNames[season]);
  String holiday = holidayName(dateYear, dateMonth, dateDay);
  if (holiday.length() > 0) line += " and " + holiday;
  line += ".";
  return line;
}

bool setClockFromText(String value) {
  int parsed = 0;
  if (!parseHHMM(value, parsed)) return false;
  bootMinuteOfDay = parsed;
  clockSetAtMs = millis();
  setUnixBaseFromClock();
  statusLine = "time set";
  return true;
}

void saveTimeSettings() {
  prefs.begin("timecfg", false);
  prefs.putInt("year", dateYear);
  prefs.putInt("month", dateMonth);
  prefs.putInt("day", dateDay);
  prefs.putInt("tz", timezoneOffsetMinutes);
  prefs.putBool("dst", daylightSavings);
  prefs.putBool("clock24", clock24Hour);
  prefs.putInt("minute", minuteOfDay());
  prefs.putInt("early", earlyMorningStart);
  prefs.putInt("morning", morningStart);
  prefs.putInt("daytime", daytimeStart);
  prefs.putInt("lateaft", lateAfternoonStart);
  prefs.putInt("night", nightStart);
  prefs.putInt("latenight", lateNightStart);
  prefs.end();
}

void loadTimeSettings() {
  prefs.begin("timecfg", true);
  dateYear = prefs.getInt("year", dateYear);
  dateMonth = prefs.getInt("month", dateMonth);
  dateDay = prefs.getInt("day", dateDay);
  timezoneOffsetMinutes = prefs.getInt("tz", timezoneOffsetMinutes);
  daylightSavings = prefs.getBool("dst", daylightSavings);
  clock24Hour = prefs.getBool("clock24", clock24Hour);
  bootMinuteOfDay = prefs.getInt("minute", bootMinuteOfDay);
  earlyMorningStart = prefs.getInt("early", earlyMorningStart);
  morningStart = prefs.getInt("morning", morningStart);
  daytimeStart = prefs.getInt("daytime", daytimeStart);
  lateAfternoonStart = prefs.getInt("lateaft", lateAfternoonStart);
  nightStart = prefs.getInt("night", nightStart);
  lateNightStart = prefs.getInt("latenight", lateNightStart);
  prefs.end();
  dateYear = constrain(dateYear, 2024, 2099);
  dateMonth = constrain(dateMonth, 1, 12);
  dateDay = constrain(dateDay, 1, 31);
  timezoneOffsetMinutes = constrain(timezoneOffsetMinutes, -720, 840);
  earlyMorningStart = constrain(earlyMorningStart, 0, 1439);
  morningStart = constrain(morningStart, 0, 1439);
  daytimeStart = constrain(daytimeStart, 0, 1439);
  lateAfternoonStart = constrain(lateAfternoonStart, 0, 1439);
  nightStart = constrain(nightStart, 0, 1439);
  lateNightStart = constrain(lateNightStart, 0, 1439);
  clockSetAtMs = millis();
  setUnixBaseFromClock();
}

void loadTouchCalibration() {
  prefs.begin("touchcal", true);
  touchXMin = prefs.getInt("xmin", DEFAULT_TOUCH_X_MIN);
  touchXMax = prefs.getInt("xmax", DEFAULT_TOUCH_X_MAX);
  touchYMin = prefs.getInt("ymin", DEFAULT_TOUCH_Y_MIN);
  touchYMax = prefs.getInt("ymax", DEFAULT_TOUCH_Y_MAX);
  prefs.end();
  if (abs(touchXMax - touchXMin) < 400 || abs(touchYMax - touchYMin) < 400) {
    touchXMin = DEFAULT_TOUCH_X_MIN;
    touchXMax = DEFAULT_TOUCH_X_MAX;
    touchYMin = DEFAULT_TOUCH_Y_MIN;
    touchYMax = DEFAULT_TOUCH_Y_MAX;
  }
}

void saveTouchCalibration() {
  prefs.begin("touchcal", false);
  prefs.putInt("xmin", touchXMin);
  prefs.putInt("xmax", touchXMax);
  prefs.putInt("ymin", touchYMin);
  prefs.putInt("ymax", touchYMax);
  prefs.end();
  statusLine = "touch saved";
}

void resetTouchCalibration() {
  touchXMin = DEFAULT_TOUCH_X_MIN;
  touchXMax = DEFAULT_TOUCH_X_MAX;
  touchYMin = DEFAULT_TOUCH_Y_MIN;
  touchYMax = DEFAULT_TOUCH_Y_MAX;
  saveTouchCalibration();
  speechLine = "Touch calibration reset.";
  speechScroll = 0;
}

bool setDateFromText(String value) {
  value.trim();
  int a = value.indexOf('-');
  int b = value.lastIndexOf('-');
  if (a <= 0 || b <= a) return false;
  String year = value.substring(0, a);
  String month = value.substring(a + 1, b);
  String day = value.substring(b + 1);
  if (!isDigitsOnly(year) || !isDigitsOnly(month) || !isDigitsOnly(day)) return false;
  int y = year.toInt();
  int mo = month.toInt();
  int d = day.toInt();
  if (y < 2024 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
  dateYear = y;
  dateMonth = mo;
  dateDay = d;
  setUnixBaseFromClock();
  saveTimeSettings();
  statusLine = "date set";
  return true;
}

String timezoneLabel() {
  int total = timezoneOffsetMinutes + (daylightSavings ? 60 : 0);
  char sign = total < 0 ? '-' : '+';
  int absMin = abs(total);
  char buffer[12];
  if (absMin % 60 == 0) snprintf(buffer, sizeof(buffer), "UTC%c%d", sign, absMin / 60);
  else snprintf(buffer, sizeof(buffer), "UTC%c%d:%02d", sign, absMin / 60, absMin % 60);
  return String(buffer);
}

uint64_t unixFromCurrentClock() {
  struct tm t;
  memset(&t, 0, sizeof(t));
  int m = minuteOfDay();
  t.tm_year = dateYear - 1900;
  t.tm_mon = dateMonth - 1;
  t.tm_mday = dateDay;
  t.tm_hour = m / 60;
  t.tm_min = m % 60;
  t.tm_sec = (millis() / 1000UL) % 60;
  time_t local = mktime(&t);
  if (local < 0) return 0;
  int totalOffset = timezoneOffsetMinutes + (daylightSavings ? 60 : 0);
  return (uint64_t)local - (int64_t)totalOffset * 60LL;
}

void setUnixBaseFromClock() {
  unixBase = unixFromCurrentClock();
  unixBaseMs = millis();
}

void setClockFromUnix(uint64_t utcUnix) {
  if (utcUnix < 1700000000ULL) return;
  unixBase = utcUnix;
  unixBaseMs = millis();
  refreshCalendarFromUnixEstimate(true);
  saveTimeSettings();
  settleDeadTimeFromClock(true);
}

uint64_t currentUnixEstimate() {
  if (unixBase > 1700000000ULL) return unixBase + (millis() - unixBaseMs) / 1000ULL;
  return unixFromCurrentClock();
}

void refreshCalendarFromUnixEstimate(bool force) {
  unsigned long now = millis();
  if (!force && now - lastCalendarRefreshMs < 60000UL) return;
  uint64_t estimate = currentUnixEstimate();
  if (estimate < 1700000000ULL) return;
  time_t local = (time_t)(estimate + (int64_t)(timezoneOffsetMinutes + (daylightSavings ? 60 : 0)) * 60LL);
  struct tm* localTm = gmtime(&local);
  if (!localTm) return;
  dateYear = localTm->tm_year + 1900;
  dateMonth = localTm->tm_mon + 1;
  dateDay = localTm->tm_mday;
  bootMinuteOfDay = localTm->tm_hour * 60 + localTm->tm_min;
  clockSetAtMs = now;
  unixBase = estimate;
  unixBaseMs = now;
  infoLineCache = "";
  lastCalendarRefreshMs = now;
}

String formatDuration(uint64_t seconds) {
  uint64_t days = seconds / 86400ULL;
  seconds %= 86400ULL;
  uint64_t hours = seconds / 3600ULL;
  seconds %= 3600ULL;
  uint64_t minutes = seconds / 60ULL;
  if (days > 0) return String((unsigned long)days) + "d " + String((unsigned long)hours) + "h";
  if (hours > 0) return String((unsigned long)hours) + "h " + String((unsigned long)minutes) + "m";
  return String((unsigned long)minutes) + "m";
}

int unixDayNumber(uint64_t unixSeconds) {
  if (unixSeconds < 1700000000ULL) return -1;
  return (int)(unixSeconds / 86400ULL);
}

int unixWeekNumber(uint64_t unixSeconds) {
  if (unixSeconds < 1700000000ULL) return -1;
  return (int)(unixSeconds / 604800ULL);
}

String healthLabel() {
  return String(buddyHealthTenth / 10) + "." + String(abs(buddyHealthTenth % 10)) + "%";
}

void applyHealthDeltaTenth(int delta, const char* reason = nullptr) {
  buddyHealthTenth = constrain(buddyHealthTenth + delta, 0, 1000);
  if (reason && delta < 0) {
    statusLine = reason;
  }
}

void resetDailyCareIfNeeded(uint64_t nowUnix) {
  int day = unixDayNumber(nowUnix);
  if (day < 0) return;
  if (lastCareDay < 0) {
    lastCareDay = day;
    return;
  }
  if (day <= lastCareDay) return;

  int days = min(day - lastCareDay, 365);
  for (int i = 0; i < days; i++) {
    applyHealthDeltaTenth(-50, "daily health loss");
    if (dailyFeedCount == 0) {
      missedFeedings++;
      applyHealthDeltaTenth(-5, "missed feeding");
      buddyHunger = constrain(buddyHunger + 12, 0, 100);
      buddyAnxiety = constrain(buddyAnxiety + 3, 0, 100);
    }
    buddyPlayNeed = constrain(buddyPlayNeed + 10, 0, 100);
    buddyRestless = constrain(buddyRestless + 8, 0, 100);
    dailyHealthGainTenth = 0;
    dailyFeedCount = 0;
    dailyPlayCount = 0;
  }
  lastCareDay = day;
}

void applyDeadTimePenalty(uint64_t deadSeconds) {
  if (deadSeconds == 0) return;
  uint64_t rawMinutes = deadSeconds / 60ULL;
  int deadMinutes = (int)(rawMinutes > 10000ULL ? 10000ULL : rawMinutes);
  applyHealthDeltaTenth(-deadMinutes, "dead time hurt");
  buddyHunger = constrain(buddyHunger + deadMinutes / 6, 0, 100);
  buddyPlayNeed = constrain(buddyPlayNeed + deadMinutes / 8, 0, 100);
  buddyAnxiety = constrain(buddyAnxiety + deadMinutes / 20, 0, 100);
}

void addDailyHealthGain(int amountTenth) {
  uint64_t nowUnix = currentUnixEstimate();
  resetDailyCareIfNeeded(nowUnix);
  int room = max(0, 100 - dailyHealthGainTenth);
  int gain = min(amountTenth, room);
  if (gain <= 0) {
    speechLine = "Daily health gain is capped. Try again tomorrow.";
    speechScroll = 0;
    return;
  }
  dailyHealthGainTenth += gain;
  applyHealthDeltaTenth(gain, nullptr);
}

void saveLifecycle(bool force = false) {
  unsigned long now = millis();
  if (!force && now - lifecycleSavedMs < 30000UL) return;
  uint64_t sessionSeconds = (now - lifecycleBootMs) / 1000ULL;
  lastKnownUnix = currentUnixEstimate();
  prefs.begin("lifeclock", false);
  prefs.putULong64("alive", totalAliveSeconds + sessionSeconds);
  prefs.putULong64("dead", totalDeadSeconds);
  prefs.putULong64("lastUnix", lastKnownUnix);
  prefs.putUInt("boots", bootCount);
  prefs.putUInt("deaths", deathCount);
  prefs.end();
  lifecycleSavedMs = now;
}

void settleDeadTimeFromClock(bool force) {
  (void)force;
  if (deadTimeSettledThisBoot) return;
  uint64_t nowUnix = unixFromCurrentClock();
  uint64_t bootAlive = (millis() - lifecycleBootMs) / 1000ULL;
  if (previousKnownUnix > 1700000000ULL && nowUnix > previousKnownUnix + bootAlive + 30ULL) {
    uint64_t dead = nowUnix - previousKnownUnix - bootAlive;
    totalDeadSeconds += dead;
    resetDailyCareIfNeeded(nowUnix);
    applyDeadTimePenalty(dead);
    deadTimeSettledThisBoot = true;
    speechLine = "Recovered " + formatDuration(dead) + " dead. Health is " + healthLabel() + ".";
    speechScroll = 0;
    saveBuddyMemory(true);
    saveLifecycle(true);
  }
}

void loadLifecycle() {
  lifecycleBootMs = millis();
  prefs.begin("lifeclock", true);
  totalAliveSeconds = prefs.getULong64("alive", 0);
  totalDeadSeconds = prefs.getULong64("dead", 0);
  previousKnownUnix = prefs.getULong64("lastUnix", 0);
  bootCount = prefs.getUInt("boots", 0);
  deathCount = prefs.getUInt("deaths", 0);
  prefs.end();
  bootCount++;
  if (previousKnownUnix > 0) deathCount++;
  if (previousKnownUnix > 1700000000ULL) {
    lastKnownUnix = previousKnownUnix;
    time_t restored = (time_t)(previousKnownUnix + (int64_t)(timezoneOffsetMinutes + (daylightSavings ? 60 : 0)) * 60LL);
    struct tm* restoredTm = gmtime(&restored);
    if (restoredTm) {
    dateYear = restoredTm->tm_year + 1900;
      dateMonth = restoredTm->tm_mon + 1;
      dateDay = restoredTm->tm_mday;
      bootMinuteOfDay = restoredTm->tm_hour * 60 + restoredTm->tm_min;
      clockSetAtMs = millis();
      unixBase = previousKnownUnix;
      unixBaseMs = millis();
    }
  }
  resetDailyCareIfNeeded(currentUnixEstimate());
  saveLifecycle(true);
  saveBuddyMemory(true);
}

bool timeIsLateNight() {
  int m = minuteOfDay();
  return minuteInWindow(m, lateNightStart, earlyMorningStart);
}

bool timeIsEarlyAM() {
  int m = minuteOfDay();
  return minuteInWindow(m, earlyMorningStart, morningStart);
}

bool timeIsMorning() {
  int m = minuteOfDay();
  return minuteInWindow(m, morningStart, daytimeStart);
}

bool timeIsEvening() {
  int m = minuteOfDay();
  return minuteInWindow(m, nightStart, lateNightStart);
}

bool timeIsLateAfternoon() {
  int m = minuteOfDay();
  return minuteInWindow(m, lateAfternoonStart, nightStart);
}

String timeDateLine() {
  unsigned long now = millis();
  if (infoLineCache.length() > 0 && now - lastInfoLineMs < 1000UL) return infoLineCache;
  struct tm timeinfo;
  char buffer[40];
  if (WiFi.status() == WL_CONNECTED && getLocalTime(&timeinfo, 20)) {
    strftime(buffer, sizeof(buffer), clock24Hour ? "%a %m/%d %H:%M" : "%a %m/%d %I:%M%p", &timeinfo);
    infoLineCache = String(buffer);
    lastInfoLineMs = now;
    return infoLineCache;
  }
  int m = minuteOfDay();
  if (clock24Hour) {
    snprintf(buffer, sizeof(buffer), "%02d/%02d %02d:%02d", dateMonth, dateDay, m / 60, m % 60);
  } else {
    int h = m / 60;
    int displayH = h % 12;
    if (displayH == 0) displayH = 12;
    snprintf(buffer, sizeof(buffer), "%02d/%02d %d:%02d%s", dateMonth, dateDay, displayH, m % 60, h >= 12 ? "PM" : "AM");
  }
  infoLineCache = String(buffer);
  lastInfoLineMs = now;
  return infoLineCache;
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
    case MOOD_STONER: return tft.color565(255, 170, 198);
    case MOOD_DRUNK: return TFT_MAGENTA;
    case MOOD_HIPPY: {
      int phase = (millis() / 260) % 6;
      const uint16_t colors[] = {TFT_MAGENTA, TFT_ORANGE, TFT_YELLOW, TFT_GREEN, TFT_CYAN, TFT_PINK};
      return colors[phase];
    }
    case MOOD_BORED: return TFT_LIGHTGREY;
    case MOOD_RESTLESS: return TFT_ORANGE;
    case MOOD_ANXIOUS: return TFT_SKYBLUE;
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

String monthName(int month) {
  const char* names[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
  month = constrain(month, 1, 12);
  return String(names[month - 1]);
}

int currentSeasonIndex() {
  return currentSeasonIndexForMonth(dateMonth, weatherLat, weatherConfigured);
}

int currentTimePreferenceIndex() {
  int m = minuteOfDay();
  if (minuteInWindow(m, earlyMorningStart, morningStart)) return 0;
  if (minuteInWindow(m, morningStart, daytimeStart)) return 1;
  if (minuteInWindow(m, daytimeStart, lateAfternoonStart)) return 2;
  if (minuteInWindow(m, lateAfternoonStart, nightStart)) return 3;
  if (minuteInWindow(m, nightStart, lateNightStart)) return 4;
  return 5;
}

int bestPreferenceIndex(const int* values, int count) {
  int best = 0;
  for (int i = 1; i < count; i++) {
    if (values[i] > values[best]) best = i;
  }
  return best;
}

String preferenceIntensity(int score) {
  if (score >= 70) return "love";
  if (score >= 42) return "really like";
  if (score >= 18) return "like";
  if (score <= -45) return "strongly distrust";
  if (score <= -20) return "avoid";
  return "am curious about";
}

void rememberBuddyThought(String thought) {
  thought.trim();
  if (thought.length() == 0) return;
  if (thought.length() > 96) thought = thought.substring(0, 96);
  int slot = memoryRevisionCount % MEMORY_BANK_COUNT;
  buddyMemoryBank[slot] = thought;
  memoryRevisionCount++;
  buddyMemoryDirty = true;
}

void learnSeasonPreference(int season, int amount) {
  season = constrain(season, 0, SEASON_COUNT - 1);
  int oldBest = bestPreferenceIndex(seasonAffinity, SEASON_COUNT);
  seasonAffinity[season] = constrain(seasonAffinity[season] + amount, -99, 99);
  int newBest = bestPreferenceIndex(seasonAffinity, SEASON_COUNT);
  preferenceLearnCount++;
  buddyMemoryDirty = true;
  if (newBest != oldBest && seasonAffinity[newBest] > seasonAffinity[oldBest] + 5) {
    rememberBuddyThought("I used to favor " + String(seasonNames[oldBest]) + ", but now " + seasonNames[newBest] + " is winning.");
  }
}

void learnMonthPreference(int month, int amount) {
  month = constrain(month, 1, 12);
  int oldBest = bestPreferenceIndex(monthAffinity, MONTH_COUNT);
  monthAffinity[month - 1] = constrain(monthAffinity[month - 1] + amount, -99, 99);
  int newBest = bestPreferenceIndex(monthAffinity, MONTH_COUNT);
  preferenceLearnCount++;
  buddyMemoryDirty = true;
  if (newBest != oldBest && monthAffinity[newBest] > monthAffinity[oldBest] + 5) {
    rememberBuddyThought("I used to talk up " + monthName(oldBest + 1) + ", but " + monthName(newBest + 1) + " is growing on me.");
  }
}

void learnTimePreference(int timeIndex, int amount) {
  timeIndex = constrain(timeIndex, 0, TIME_PREF_COUNT - 1);
  int oldBest = bestPreferenceIndex(timeAffinity, TIME_PREF_COUNT);
  timeAffinity[timeIndex] = constrain(timeAffinity[timeIndex] + amount, -99, 99);
  int newBest = bestPreferenceIndex(timeAffinity, TIME_PREF_COUNT);
  preferenceLearnCount++;
  buddyMemoryDirty = true;
  if (newBest != oldBest && timeAffinity[newBest] > timeAffinity[oldBest] + 5) {
    rememberBuddyThought("I used to prefer " + String(timePreferenceNames[oldBest]) + ", but " + timePreferenceNames[newBest] + " has better energy.");
  }
}

void learnActivityPreference(int activity, int amount) {
  activity = constrain(activity, 0, ACTIVITY_COUNT - 1);
  int oldBest = bestPreferenceIndex(activityAffinity, ACTIVITY_COUNT);
  activityAffinity[activity] = constrain(activityAffinity[activity] + amount, -99, 99);
  int newBest = bestPreferenceIndex(activityAffinity, ACTIVITY_COUNT);
  preferenceLearnCount++;
  buddyMemoryDirty = true;
  if (newBest != oldBest && activityAffinity[newBest] > activityAffinity[oldBest] + 5) {
    rememberBuddyThought("I used to be into " + String(activityNames[oldBest]) + ", but now I keep thinking about " + activityNames[newBest] + ".");
  }
}

void learnCurrentContext(int amount = 1) {
  learnSeasonPreference(currentSeasonIndex(), amount);
  learnMonthPreference(dateMonth, amount);
  learnTimePreference(currentTimePreferenceIndex(), amount);
}

String weatherMoodWord() {
  String w = weatherSummary;
  w.toLowerCase();
  if (w.indexOf("snow") >= 0) return "snow";
  if (w.indexOf("storm") >= 0) return "storms";
  if (w.indexOf("rain") >= 0 || w.indexOf("shower") >= 0 || w.indexOf("drizzle") >= 0) return "rain";
  if (w.indexOf("fog") >= 0) return "fog";
  if (w.indexOf("clear") >= 0) return "clear skies";
  if (w.indexOf("cloud") >= 0) return "clouds";
  return "weird weather";
}

void learnWeatherPreference() {
  if (!weatherConfigured || weatherSummary == "weather unset") return;
  String w = weatherSummary;
  w.toLowerCase();
  int season = currentSeasonIndex();
  int delta = 1;
  if (w.indexOf("clear") >= 0 || w.indexOf("mostly clear") >= 0) delta = 3;
  else if (w.indexOf("snow") >= 0 && seasonAffinity[3] >= seasonAffinity[1]) delta = 3;
  else if (w.indexOf("rain") >= 0 || w.indexOf("storm") >= 0) delta = -2;
  learnSeasonPreference(season, delta);
  learnActivityPreference(9, abs(delta));
}

String preferenceSummaryLine() {
  int bestSeason = bestPreferenceIndex(seasonAffinity, SEASON_COUNT);
  int bestMonth = bestPreferenceIndex(monthAffinity, MONTH_COUNT);
  int bestTime = bestPreferenceIndex(timeAffinity, TIME_PREF_COUNT);
  int bestActivity = bestPreferenceIndex(activityAffinity, ACTIVITY_COUNT);
  return "Right now it is " + String(seasonNames[currentSeasonIndex()]) + " on " + monthName(dateMonth) + " " + String(dateDay) +
         ". I " + preferenceIntensity(seasonAffinity[bestSeason]) + " " + seasonNames[bestSeason] +
         ", " + monthName(bestMonth + 1) + ", " + timePreferenceNames[bestTime] +
         ", and " + activityNames[bestActivity] + ".";
}

String buddyDailySummaryLine() {
  String summary = "Today I am " + String(moodNames[currentMood]) + ", health " + healthLabel();
  summary += ", hunger " + String(buddyHunger) + ", play " + String(buddyPlayNeed);
  summary += ", strength " + String(buddyStrength) + ", armor " + String(buddyArmor) + ".";
  if (buddyNewNetworkCount > 0 || buddyNewBluetoothCount > 0) {
    summary += " I logged " + String(buddyNewNetworkCount) + " WiFi and " + String(buddyNewBluetoothCount) + " Bluetooth names.";
  }
  return summary;
}

String buddyAiInsightLine() {
  int learned = (int)min((unsigned long)100, preferenceLearnCount / 4);
  String insight = "Tiny AI: ";
  if (learned >= 75) insight += "strong opinions forming. ";
  else if (learned >= 35) insight += "patterns are starting to stick. ";
  else insight += "still learning the room. ";
  insight += preferenceSummaryLine();
  return insight;
}

String randomMemoryLine() {
  int filled = 0;
  for (int i = 0; i < MEMORY_BANK_COUNT; i++) if (buddyMemoryBank[i].length() > 0) filled++;
  if (filled == 0) return preferenceSummaryLine();
  int pick = random(0, filled);
  for (int i = 0; i < MEMORY_BANK_COUNT; i++) {
    if (buddyMemoryBank[i].length() == 0) continue;
    if (pick-- == 0) return buddyMemoryBank[i];
  }
  return preferenceSummaryLine();
}

int seasonIndexFromName(String name) {
  name.trim();
  name.toLowerCase();
  for (int i = 0; i < SEASON_COUNT; i++) if (name == seasonNames[i]) return i;
  return -1;
}

int monthIndexFromName(String name) {
  name.trim();
  name.toLowerCase();
  for (int i = 1; i <= 12; i++) {
    String full = monthName(i);
    full.toLowerCase();
    if (name == full || name == full.substring(0, 3) || name == String(i)) return i - 1;
  }
  return -1;
}

int timePreferenceIndexFromName(String name) {
  name.trim();
  name.toLowerCase();
  if (name == "early" || name == "earlyam" || name == "early am") return 0;
  if (name == "morning") return 1;
  if (name == "day" || name == "daytime" || name == "afternoon") return 2;
  if (name == "latepm" || name == "late afternoon" || name == "late") return 3;
  if (name == "night") return 4;
  if (name == "latenight" || name == "late night") return 5;
  return -1;
}

int activityIndexFromName(String name) {
  name.trim();
  name.toLowerCase();
  for (int i = 0; i < ACTIVITY_COUNT; i++) {
    String a = activityNames[i];
    a.toLowerCase();
    if (name == a) return i;
  }
  if (name == "boop") return 0;
  if (name == "pet") return 1;
  if (name == "tickle") return 2;
  if (name == "poke" || name == "eye poke") return 3;
  if (name == "swipe") return 4;
  if (name == "feed" || name == "food") return 5;
  if (name == "play") return 6;
  if (name == "wifi" || name == "network") return 7;
  if (name == "bt" || name == "bluetooth") return 8;
  if (name == "weather") return 9;
  return -1;
}

bool applyPreferenceCommand(String domain, String value, int amount) {
  domain.trim();
  domain.toLowerCase();
  value.trim();
  if (domain == "season") {
    int idx = seasonIndexFromName(value);
    if (idx < 0) return false;
    learnSeasonPreference(idx, amount);
  } else if (domain == "month") {
    int idx = monthIndexFromName(value);
    if (idx < 0) return false;
    learnMonthPreference(idx + 1, amount);
  } else if (domain == "time") {
    int idx = timePreferenceIndexFromName(value);
    if (idx < 0) return false;
    learnTimePreference(idx, amount);
  } else if (domain == "activity") {
    int idx = activityIndexFromName(value);
    if (idx < 0) return false;
    learnActivityPreference(idx, amount);
  } else {
    return false;
  }
  rememberBuddyThought(preferenceSummaryLine());
  speechLine = randomMemoryLine();
  speechScroll = 0;
  statusLine = "preference learned";
  saveBuddyMemory(true);
  return true;
}

void loadBuddyMemory() {
  prefs.begin("cyd-buddy", true);
  buddyTouchCount = prefs.getULong("touches", 0);
  buddyEyePokeCount = prefs.getULong("eyePokes", 0);
  buddyTickleCount = prefs.getULong("tickles", 0);
  buddyBoredCount = prefs.getULong("bored", 0);
  buddySwipeLeftCount = prefs.getULong("swipeL", 0);
  buddySwipeRightCount = prefs.getULong("swipeR", 0);
  buddySwipeUpCount = prefs.getULong("swipeU", 0);
  buddySwipeDownCount = prefs.getULong("swipeD", 0);
  buddyNewNetworkCount = prefs.getULong("newWifi", 0);
  buddyNewBluetoothCount = prefs.getULong("newBt", 0);
  missedFeedings = prefs.getULong("missFeed", 0);
  buddyHealthTenth = prefs.getInt("health10", buddyHealthTenth);
  buddyHunger = prefs.getInt("hunger", buddyHunger);
  buddyPlayNeed = prefs.getInt("playNeed", buddyPlayNeed);
  buddyRestless = prefs.getInt("restless", buddyRestless);
  buddyAnxiety = prefs.getInt("anxiety", buddyAnxiety);
  buddyStrength = prefs.getInt("strength", buddyStrength);
  buddyArmor = prefs.getInt("armor", buddyArmor);
  dailyHealthGainTenth = prefs.getInt("gain10", 0);
  dailyFeedCount = prefs.getInt("feedDay", 0);
  dailyPlayCount = prefs.getInt("playDay", 0);
  lastCareDay = prefs.getInt("careDay", -1);
  lastBoostWeek = prefs.getInt("boostWk", -1);
  lastFeedUnix = prefs.getULong64("lastFeed", 0);
  lastPlayUnix = prefs.getULong64("lastPlay", 0);
  preferenceLearnCount = prefs.getULong("prefLearn", 0);
  memoryRevisionCount = prefs.getULong("memRev", 0);
  for (int i = 0; i < SEASON_COUNT; i++) seasonAffinity[i] = prefs.getInt((String("sea") + i).c_str(), 0);
  for (int i = 0; i < MONTH_COUNT; i++) monthAffinity[i] = prefs.getInt((String("mon") + i).c_str(), 0);
  for (int i = 0; i < TIME_PREF_COUNT; i++) timeAffinity[i] = prefs.getInt((String("tod") + i).c_str(), 0);
  for (int i = 0; i < ACTIVITY_COUNT; i++) activityAffinity[i] = prefs.getInt((String("act") + i).c_str(), 0);
  for (int i = 0; i < MEMORY_BANK_COUNT; i++) buddyMemoryBank[i] = prefs.getString((String("mem") + i).c_str(), "");
  prefs.end();
  buddyHealthTenth = constrain(buddyHealthTenth, 0, 1000);
  buddyHunger = constrain(buddyHunger, 0, 100);
  buddyPlayNeed = constrain(buddyPlayNeed, 0, 100);
  buddyRestless = constrain(buddyRestless, 0, 100);
  buddyAnxiety = constrain(buddyAnxiety, 0, 100);
  buddyStrength = constrain(buddyStrength, 1, 999);
  buddyArmor = constrain(buddyArmor, 0, 999);
  for (int i = 0; i < SEASON_COUNT; i++) seasonAffinity[i] = constrain(seasonAffinity[i], -99, 99);
  for (int i = 0; i < MONTH_COUNT; i++) monthAffinity[i] = constrain(monthAffinity[i], -99, 99);
  for (int i = 0; i < TIME_PREF_COUNT; i++) timeAffinity[i] = constrain(timeAffinity[i], -99, 99);
  for (int i = 0; i < ACTIVITY_COUNT; i++) activityAffinity[i] = constrain(activityAffinity[i], -99, 99);
}

void saveBuddyMemory(bool force) {
  unsigned long now = millis();
  if (!force && now - lastMemorySaveMs < 60000UL) return;
  if (!force && !buddyMemoryDirty) return;
  prefs.begin("cyd-buddy", false);
  prefs.putULong("touches", buddyTouchCount);
  prefs.putULong("eyePokes", buddyEyePokeCount);
  prefs.putULong("tickles", buddyTickleCount);
  prefs.putULong("bored", buddyBoredCount);
  prefs.putULong("swipeL", buddySwipeLeftCount);
  prefs.putULong("swipeR", buddySwipeRightCount);
  prefs.putULong("swipeU", buddySwipeUpCount);
  prefs.putULong("swipeD", buddySwipeDownCount);
  prefs.putULong("newWifi", buddyNewNetworkCount);
  prefs.putULong("newBt", buddyNewBluetoothCount);
  prefs.putULong("missFeed", missedFeedings);
  prefs.putInt("health10", buddyHealthTenth);
  prefs.putInt("hunger", buddyHunger);
  prefs.putInt("playNeed", buddyPlayNeed);
  prefs.putInt("restless", buddyRestless);
  prefs.putInt("anxiety", buddyAnxiety);
  prefs.putInt("strength", buddyStrength);
  prefs.putInt("armor", buddyArmor);
  prefs.putInt("gain10", dailyHealthGainTenth);
  prefs.putInt("feedDay", dailyFeedCount);
  prefs.putInt("playDay", dailyPlayCount);
  prefs.putInt("careDay", lastCareDay);
  prefs.putInt("boostWk", lastBoostWeek);
  prefs.putULong64("lastFeed", lastFeedUnix);
  prefs.putULong64("lastPlay", lastPlayUnix);
  prefs.putULong("prefLearn", preferenceLearnCount);
  prefs.putULong("memRev", memoryRevisionCount);
  for (int i = 0; i < SEASON_COUNT; i++) prefs.putInt((String("sea") + i).c_str(), seasonAffinity[i]);
  for (int i = 0; i < MONTH_COUNT; i++) prefs.putInt((String("mon") + i).c_str(), monthAffinity[i]);
  for (int i = 0; i < TIME_PREF_COUNT; i++) prefs.putInt((String("tod") + i).c_str(), timeAffinity[i]);
  for (int i = 0; i < ACTIVITY_COUNT; i++) prefs.putInt((String("act") + i).c_str(), activityAffinity[i]);
  for (int i = 0; i < MEMORY_BANK_COUNT; i++) prefs.putString((String("mem") + i).c_str(), buddyMemoryBank[i]);
  prefs.end();
  lastMemorySaveMs = now;
  buddyMemoryDirty = false;
}

void markInteraction() {
  userHasInteracted = true;
  buddyTouchCount++;
  buddyPlayNeed = max(0, buddyPlayNeed - 4);
  buddyRestless = max(0, buddyRestless - 2);
  buddyAnxiety = max(0, buddyAnxiety - 1);
  lastInteractionMs = millis();
  asleep = false;
  learnCurrentContext(1);
  saveBuddyMemory();
}

void strengthenBuddy(int amount = 1) {
  buddyStrength = constrain(buddyStrength + amount, 1, 999);
  learnActivityPreference(7, amount);
  buddyMemoryDirty = true;
  saveBuddyMemory();
}

bool careCooldownReady(uint64_t lastUnix, uint64_t cooldownSeconds) {
  uint64_t nowUnix = currentUnixEstimate();
  if (lastUnix == 0 || nowUnix < 1700000000ULL) return true;
  return nowUnix >= lastUnix + cooldownSeconds;
}

void feedBuddy() {
  uint64_t nowUnix = currentUnixEstimate();
  resetDailyCareIfNeeded(nowUnix);
  if (!careCooldownReady(lastFeedUnix, 1800ULL)) {
    speechLine = "I just ate. Let my tiny system process that.";
    speechScroll = 0;
    statusLine = "feed cooldown";
    return;
  }
  dailyFeedCount++;
  lastFeedUnix = nowUnix;
  learnActivityPreference(5, 4);
  buddyHunger = max(0, buddyHunger - 35);
  buddyAnxiety = max(0, buddyAnxiety - 4);
  addDailyHealthGain(60);
  currentMood = buddyHealthTenth > 850 ? MOOD_LOVE : MOOD_HAPPY;
  speechLine = "Fed. Health " + healthLabel() + ". I accept tribute.";
  speechScroll = 0;
  statusLine = "fed";
  saveBuddyMemory(true);
}

void playWithBuddy() {
  uint64_t nowUnix = currentUnixEstimate();
  resetDailyCareIfNeeded(nowUnix);
  if (!careCooldownReady(lastPlayUnix, 900ULL)) {
    speechLine = "Play cooldown. I am still emotionally sprinting.";
    speechScroll = 0;
    statusLine = "play cooldown";
    return;
  }
  dailyPlayCount++;
  lastPlayUnix = nowUnix;
  learnActivityPreference(6, 4);
  buddyPlayNeed = max(0, buddyPlayNeed - 32);
  buddyRestless = max(0, buddyRestless - 28);
  buddyAnxiety = max(0, buddyAnxiety - 3);
  addDailyHealthGain(50);
  currentMood = MOOD_EXCITED;
  speechLine = "Played. Health " + healthLabel() + ". My pixels have cardio now.";
  speechScroll = 0;
  statusLine = "played";
  saveBuddyMemory(true);
}

void weeklyBoostBuddy() {
  uint64_t nowUnix = currentUnixEstimate();
  int week = unixWeekNumber(nowUnix);
  if (week < 0) {
    speechLine = "Set time or sync WiFi before using the weekly boost.";
    speechScroll = 0;
    statusLine = "boost needs time";
    return;
  }
  resetDailyCareIfNeeded(nowUnix);
  if (week == lastBoostWeek) {
    speechLine = "Weekly boost already used. Calendar says no cheating.";
    speechScroll = 0;
    statusLine = "boost used";
    return;
  }
  lastBoostWeek = week;
  buddyHealthTenth = 1000;
  buddyHunger = 0;
  buddyPlayNeed = 0;
  buddyRestless = 0;
  buddyAnxiety = 0;
  dailyHealthGainTenth = 100;
  currentMood = MOOD_EXCITED;
  speechLine = "Weekly boost fired. Health 100.0%. I am violently alive.";
  speechScroll = 0;
  statusLine = "boosted";
  saveBuddyMemory(true);
}

void calmBuddyCare(bool full = false) {
  if (full) {
    buddyHealthTenth = max(buddyHealthTenth, 950);
    buddyHunger = 12;
    buddyPlayNeed = 18;
    buddyRestless = 10;
    buddyAnxiety = 6;
    dailyHealthGainTenth = min(dailyHealthGainTenth, 60);
  } else {
    buddyHunger = min(buddyHunger, 35);
    buddyPlayNeed = min(buddyPlayNeed, 35);
    buddyRestless = min(buddyRestless, 28);
    buddyAnxiety = min(buddyAnxiety, 18);
  }
  careDriftRemainder = 0;
  asleep = false;
  currentMood = full ? MOOD_HAPPY : MOOD_NORMAL;
  manualMoodHoldUntil = millis() + 10UL * 60UL * 1000UL;
  speechLine = full ? "Care reset. I am no longer rage-buffering." : "Calmed down. I am putting the tiny attitude away.";
  speechScroll = 0;
  statusLine = full ? "care reset" : "calmed";
  buddyMemoryDirty = true;
  saveBuddyMemory(true);
}

uint32_t hashName(String value) {
  uint32_t h = 2166136261UL;
  for (int i = 0; i < value.length(); i++) {
    h ^= (uint8_t)value[i];
    h *= 16777619UL;
  }
  return h;
}

bool rememberSeenWifi(String ssid) {
  ssid.trim();
  if (ssid.length() == 0) return false;
  char key[16];
  snprintf(key, sizeof(key), "w%08lx", (unsigned long)hashName(ssid));
  prefs.begin("seenwifi", false);
  bool known = prefs.getBool(key, false);
  if (!known) prefs.putBool(key, true);
  prefs.end();
  if (!known) {
    buddyNewNetworkCount++;
    strengthenBuddy(2);
    return true;
  }
  return false;
}

bool markSeenWifiInOpenPrefs(String ssid) {
  ssid.trim();
  if (ssid.length() == 0) return false;
  char key[16];
  snprintf(key, sizeof(key), "w%08lx", (unsigned long)hashName(ssid));
  bool known = prefs.getBool(key, false);
  if (!known) prefs.putBool(key, true);
  return !known;
}

void armorBuddy(int amount = 1) {
  buddyArmor = constrain(buddyArmor + amount, 0, 999);
  learnActivityPreference(8, amount);
  buddyMemoryDirty = true;
  saveBuddyMemory();
}

bool rememberSeenBluetooth(String name) {
  name.trim();
  if (name.length() == 0) return false;
  char key[16];
  snprintf(key, sizeof(key), "b%08lx", (unsigned long)hashName(name));
  prefs.begin("seenbt", false);
  bool known = prefs.getBool(key, false);
  if (!known) prefs.putBool(key, true);
  prefs.end();
  if (!known) {
    buddyNewBluetoothCount++;
    armorBuddy(2);
    speechLine = "New Bluetooth name logged. Armor +" + String(2) + ".";
    speechScroll = 0;
    return true;
  }
  return false;
}

void setBacklight(uint8_t value) {
  analogWrite(BACKLIGHT_PIN, value);
}

void loadNetworkSettings() {
  prefs.begin("network", false);
  wifiSsid = prefs.isKey("ssid") ? prefs.getString("ssid", "") : "";
  ollamaHost = prefs.isKey("ollama") ? prefs.getString("ollama", "http://127.0.0.1:11434") : "http://127.0.0.1:11434";
  spac3Host = prefs.isKey("spac3") ? prefs.getString("spac3", "http://10.42.7.1:8766") : "http://10.42.7.1:8766";
  spac3TelemetryEnabled = prefs.isKey("spac3on") ? prefs.getBool("spac3on", true) : true;
  weatherLat = prefs.isKey("lat") ? prefs.getFloat("lat", 0.0f) : 0.0f;
  weatherLon = prefs.isKey("lon") ? prefs.getFloat("lon", 0.0f) : 0.0f;
  prefs.end();
  wifiConfigured = wifiSsid.length() > 0;
  weatherConfigured = fabsf(weatherLat) > 0.001f || fabsf(weatherLon) > 0.001f;
}

void loadVoiceSettings() {
  prefs.begin("voice", true);
  buddyName = prefs.getString("name", "Buddy");
  speechScrollMs = prefs.getULong("scroll", 140);
  int savedPersonality = prefs.getInt("personality", PERSONALITY_SASSY);
  prefs.end();
  speechScrollMs = constrain((int)speechScrollMs, 50, 600);
  currentPersonality = (Personality)constrain(savedPersonality, 0, PERSONALITY_COUNT - 1);
}

void saveBuddyName(String name) {
  name.trim();
  if (name.length() == 0) name = "Buddy";
  if (name.length() > 28) name = name.substring(0, 28);
  buddyName = name;
  prefs.begin("voice", false);
  prefs.putString("name", buddyName);
  prefs.end();
  statusLine = "name saved";
  speechLine = "My name is " + buddyName + ". Press train name and say it clearly.";
  speechScroll = 0;
}

void saveSpeechScroll(unsigned long ms) {
  speechScrollMs = constrain((int)ms, 50, 600);
  prefs.begin("voice", false);
  prefs.putULong("scroll", speechScrollMs);
  prefs.end();
  statusLine = String("scroll ") + speechScrollMs + "ms";
  speechLine = "Text scroll speed updated.";
  speechScroll = 0;
}

Personality personalityFromName(String name) {
  name.trim();
  name.toLowerCase();
  for (int i = 0; i < PERSONALITY_COUNT; i++) {
    if (name == personalityNames[i]) return (Personality)i;
  }
  return currentPersonality;
}

void savePersonality(Personality personality) {
  currentPersonality = (Personality)constrain((int)personality, 0, PERSONALITY_COUNT - 1);
  prefs.begin("voice", false);
  prefs.putInt("personality", currentPersonality);
  prefs.end();
  statusLine = String("personality: ") + personalityNames[currentPersonality];
  speechLine = "Personality set to " + String(personalityNames[currentPersonality]) + ".";
  speechScroll = 0;
}

void cyclePersonality() {
  savePersonality((Personality)((currentPersonality + 1) % PERSONALITY_COUNT));
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

String loadWifiPassword() {
  prefs.begin("network", true);
  String pass = prefs.isKey("pass") ? prefs.getString("pass", "") : "";
  prefs.end();
  return pass;
}

String maskedWifiPassword() {
  if (wifiEditPass.length() == 0) return "";
  String masked;
  for (int i = 0; i < wifiEditPass.length(); i++) masked += "*";
  return masked;
}

String fitText(String text, int maxChars) {
  if (text.length() <= maxChars) return text;
  return text.substring(max(0, (int)text.length() - maxChars));
}

void finishWifiScan(int found) {
  wifiScanCount = 0;
  if (found < 0) found = 0;
  int newFound = 0;
  prefs.begin("seenwifi", false);
  for (int i = 0; i < found && wifiScanCount < WIFI_SCAN_MAX; i++) {
    String ssid = WiFi.SSID(i);
    ssid.trim();
    if (ssid.length() == 0) continue;
    bool duplicate = false;
    for (int j = 0; j < wifiScanCount; j++) {
      if (wifiScanSsid[j] == ssid) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;
    wifiScanSsid[wifiScanCount] = ssid;
    wifiScanRssi[wifiScanCount] = WiFi.RSSI(i);
    wifiScanSecure[wifiScanCount] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    if (markSeenWifiInOpenPrefs(ssid)) newFound++;
    wifiScanCount++;
    yield();
  }
  prefs.end();
  if (newFound > 0) {
    buddyNewNetworkCount += newFound;
    buddyStrength = constrain(buddyStrength + newFound * 2, 1, 999);
    learnActivityPreference(7, min(newFound * 2, 12));
    buddyMemoryDirty = true;
    saveBuddyMemory();
  }
  WiFi.scanDelete();
  wifiScanInProgress = false;
  statusLine = wifiScanCount > 0 ? "wifi list" : "wifi none";
  if (newFound > 0) speechLine = "I found " + String(newFound) + " new WiFi names. I feel stronger.";
  else speechLine = wifiScanCount > 0 ? "Tap a WiFi network, then enter the password." : "No WiFi networks found. Try RESCAN or MANUAL.";
  speechScroll = 0;
}

void scanWifiNetworks() {
  wifiScanCount = 0;
  wifiScanPage = 0;
  wifiScanInProgress = true;
  wifiScanStartedMs = millis();
  statusLine = "wifi scanning";
  speechLine = "Scanning WiFi networks.";
  speechScroll = 0;
  WiFi.mode(WIFI_STA);
  int started = WiFi.scanNetworks(true, true);
  if (started == WIFI_SCAN_FAILED) {
    wifiScanInProgress = false;
    statusLine = "scan failed";
    speechLine = "WiFi scan could not start. Try RESCAN.";
    speechScroll = 0;
  }
}

const char* wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return "idle";
    case WL_NO_SSID_AVAIL: return "no_ssid";
    case WL_SCAN_COMPLETED: return "scan_done";
    case WL_CONNECTED: return "connected";
    case WL_CONNECT_FAILED: return "connect_failed";
    case WL_CONNECTION_LOST: return "connection_lost";
    case WL_DISCONNECTED: return "disconnected";
    default: return "unknown";
  }
}

const char* wifiSecurityName(wifi_auth_mode_t auth) {
  switch (auth) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa";
    case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa_wpa2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "wpa2_enterprise";
    case WIFI_AUTH_WPA3_PSK: return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2_wpa3";
    default: return "secure";
  }
}

void openWifiEditor() {
  loadNetworkSettings();
  wifiEditSsid = wifiSsid;
  wifiEditPass = loadWifiPassword();
  wifiEditField = wifiEditSsid.length() == 0 ? WIFI_EDIT_SSID : WIFI_EDIT_PASS;
  wifiEditShift = false;
  wifiEditorActive = true;
  wifiScanListActive = true;
  menuMode = MENU_NONE;
  scanWifiNetworks();
}

void openWifiPasswordEditor(String ssid, bool keepSavedPassword = false) {
  ssid.trim();
  wifiEditSsid = ssid;
  loadNetworkSettings();
  wifiEditPass = keepSavedPassword || wifiEditSsid == wifiSsid ? loadWifiPassword() : "";
  wifiEditField = WIFI_EDIT_PASS;
  wifiEditShift = false;
  wifiScanListActive = false;
  wifiEditorActive = true;
  menuMode = MENU_NONE;
  statusLine = "wifi password";
  speechLine = "Enter the password, then tap SAVE.";
  speechScroll = 0;
}

void closeWifiEditor(bool saved) {
  wifiEditorActive = false;
  wifiScanListActive = false;
  wifiEditShift = false;
  if (saved) {
    saveWifiSsid(wifiEditSsid);
    saveWifiPassword(wifiEditPass);
    statusLine = "wifi saved";
    speechLine = "WiFi saved. Connecting now.";
    speechScroll = 0;
    connectWifi();
  } else {
    statusLine = "wifi cancelled";
    speechLine = "WiFi edit cancelled.";
    speechScroll = 0;
  }
}

void appendWifiEditChar(char c) {
  String& target = wifiEditField == WIFI_EDIT_SSID ? wifiEditSsid : wifiEditPass;
  if (target.length() >= 63) {
    speechLine = "That field is full.";
    speechScroll = 0;
    return;
  }
  if (wifiEditShift && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
  target += c;
}

void deleteWifiEditChar() {
  String& target = wifiEditField == WIFI_EDIT_SSID ? wifiEditSsid : wifiEditPass;
  if (target.length() > 0) target.remove(target.length() - 1);
}

void saveOllamaHost(const String& host) {
  ollamaHost = host;
  ollamaHost.trim();
  prefs.begin("network", false);
  prefs.putString("ollama", ollamaHost);
  prefs.end();
}

void saveSpac3Host(const String& host) {
  spac3Host = host;
  spac3Host.trim();
  if (spac3Host.length() == 0) spac3Host = "http://10.42.7.1:8766";
  if (!spac3Host.startsWith("http://") && !spac3Host.startsWith("https://")) {
    spac3Host = "http://" + spac3Host;
  }
  prefs.begin("network", false);
  prefs.putString("spac3", spac3Host);
  prefs.end();
}

void saveSpac3Enabled(bool enabled) {
  spac3TelemetryEnabled = enabled;
  prefs.begin("network", false);
  prefs.putBool("spac3on", spac3TelemetryEnabled);
  prefs.end();
}

void saveWeatherLocation(float lat, float lon) {
  weatherLat = lat;
  weatherLon = lon;
  weatherConfigured = true;
  prefs.begin("network", false);
  prefs.putFloat("lat", weatherLat);
  prefs.putFloat("lon", weatherLon);
  prefs.end();
  statusLine = "weather saved";
  speechLine = "Weather location saved.";
  speechScroll = 0;
}

String setupPageHtml() {
  String page = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  page += "<title>CYD Buddy Setup</title><style>body{font-family:sans-serif;background:#101015;color:#eee;padding:20px;max-width:520px;margin:auto}input,button{box-sizing:border-box;width:100%;font-size:18px;margin:8px 0;padding:10px;border-radius:6px;border:1px solid #555;background:#181820;color:#fff}button{background:#0a84ff;border:0}label{font-size:14px;color:#aaa}</style></head><body>";
  page += "<h2>CYD Buddy WiFi</h2><form method='POST' action='/save'>";
  page += "<label>WiFi SSID</label><input name='ssid' value='" + wifiSsid + "'>";
  page += "<label>WiFi Password</label><input name='pass' type='password'>";
  page += "<label>Weather Latitude</label><input name='lat' value='" + String(weatherLat, 4) + "'>";
  page += "<label>Weather Longitude</label><input name='lon' value='" + String(weatherLon, 4) + "'>";
  page += "<button type='submit'>Save and Connect</button></form>";
  page += "<p>After saving, reconnect your phone to normal WiFi. CYD will try to join the saved network.</p>";
  page += "</body></html>";
  return page;
}

void handleSetupRoot() {
  setupServer.send(200, "text/html", setupPageHtml());
}

void handleSetupSave() {
  if (setupServer.hasArg("ssid")) saveWifiSsid(setupServer.arg("ssid"));
  if (setupServer.hasArg("pass") && setupServer.arg("pass").length() > 0) saveWifiPassword(setupServer.arg("pass"));
  if (setupServer.hasArg("lat") && setupServer.hasArg("lon")) {
    float lat = setupServer.arg("lat").toFloat();
    float lon = setupServer.arg("lon").toFloat();
    if (fabsf(lat) > 0.001f || fabsf(lon) > 0.001f) saveWeatherLocation(lat, lon);
  }
  setupServer.send(200, "text/html", "<html><body><h2>Saved.</h2><p>CYD Buddy is trying to connect. You can close this.</p></body></html>");
  wifiSetupPortalActive = false;
  setupServer.stop();
  WiFi.softAPdisconnect(true);
  connectWifi();
}

void startWifiSetupPortal() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("CYD-Buddy-Setup");
  setupServer.on("/", HTTP_GET, handleSetupRoot);
  setupServer.on("/save", HTTP_POST, handleSetupSave);
  setupServer.begin();
  wifiSetupPortalActive = true;
  wifiSetupStartedMs = millis();
  statusLine = "wifi setup ap";
  speechLine = "Join CYD-Buddy-Setup, open 192.168.4.1, then save WiFi.";
  speechScroll = 0;
}

float jsonNumber(const String& body, const char* key, float fallback = NAN) {
  String pattern = String("\"") + key + "\":";
  int at = body.indexOf(pattern);
  if (at < 0) return fallback;
  at += pattern.length();
  while (at < body.length() && (body[at] == ' ' || body[at] == '\t')) at++;
  int end = at;
  while (end < body.length()) {
    char c = body[end];
    if (!((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.')) break;
    end++;
  }
  if (end <= at) return fallback;
  return body.substring(at, end).toFloat();
}

int jsonInt(const String& body, const char* key, int fallback = -1) {
  float value = jsonNumber(body, key, NAN);
  if (isnan(value)) return fallback;
  return (int)value;
}

String jsonStringAfter(const String& body, const char* marker, const char* key, const String& fallback = "") {
  int start = 0;
  if (marker && strlen(marker) > 0) {
    start = body.indexOf(marker);
    if (start < 0) start = 0;
  }
  String pattern = String("\"") + key + "\":";
  int at = body.indexOf(pattern, start);
  if (at < 0) return fallback;
  at += pattern.length();
  while (at < body.length() && (body[at] == ' ' || body[at] == '\t')) at++;
  if (at >= body.length() || body[at] != '"') return fallback;
  at++;
  String out;
  bool esc = false;
  for (int i = at; i < body.length(); i++) {
    char c = body[i];
    if (esc) {
      if (c == 'n') out += ' ';
      else if (c == 'r' || c == 't') out += ' ';
      else out += c;
      esc = false;
    } else if (c == '\\') {
      esc = true;
    } else if (c == '"') {
      break;
    } else {
      out += c;
    }
    if (out.length() > 180) break;
  }
  out.trim();
  return out.length() ? out : fallback;
}

String jsonStringValue(const String& body, const char* key, const String& fallback = "") {
  return jsonStringAfter(body, "", key, fallback);
}

Mood moodFromSpac3(String mood, float cpuC, int alertScore) {
  mood.trim();
  mood.toLowerCase();
  if (alertScore >= 75 || mood == "hot") return MOOD_ANGRY;
  if (alertScore >= 35 || mood == "alert") return MOOD_SURPRISED;
  if (mood == "warm" || mood == "sunbaked") return MOOD_EXCITED;
  if (mood == "lonely" || mood == "sad" || mood == "rainwatch") return MOOD_SAD;
  if (mood == "night" || mood == "snowghost" || mood == "fogghost") return MOOD_SLEEPY;
  if (mood == "watching" || mood == "cloaked" || mood == "scanning") return MOOD_SUSPICIOUS;
  if (mood == "soundwave" || mood == "stormwatch" || mood == "windwatch") return MOOD_SURPRISED;
  if (mood == "morning" || mood == "daylight" || mood == "skyclear") return MOOD_HAPPY;
  if (isnan(cpuC)) return MOOD_NORMAL;
  if (cpuC >= 75.0f) return MOOD_ANGRY;
  if (cpuC >= 65.0f) return MOOD_EXCITED;
  return MOOD_NORMAL;
}

bool spac3MessageHasAny(String message, const char* const* words, int count) {
  message.toLowerCase();
  for (int i = 0; i < count; i++) {
    if (message.indexOf(words[i]) >= 0) return true;
  }
  return false;
}

bool spac3TelemetryShouldWake(String mood, String message, float cpuC, int alertScore) {
  mood.trim();
  mood.toLowerCase();
  if (alertScore >= 35) return true;
  if (!isnan(cpuC) && cpuC >= 65.0f) return true;
  if (mood == "hot" || mood == "alert" || mood == "watching" || mood == "scanning" ||
      mood == "soundwave" || mood == "stormwatch" || mood == "windwatch") {
    return true;
  }
  const char* const activeWords[] = {
    "motion", "person", "face", "capture", "camera", "microphone", "sound",
    "voice", "handshake", "new device", "service down", "offline", "failed",
    "critical", "warning", "intruder", "unknown", "connected"
  };
  return spac3MessageHasAny(message, activeWords, COUNT_OF(activeWords));
}

bool spac3TelemetryIsPassive(String mood, String message, float cpuC, int alertScore) {
  if (spac3TelemetryShouldWake(mood, message, cpuC, alertScore)) return false;
  mood.trim();
  mood.toLowerCase();
  if (mood == "night" || mood == "curious" || mood == "daylight" || mood == "morning" ||
      mood == "skyclear" || mood == "rainwatch" || mood == "snowghost" || mood == "fogghost" ||
      mood == "lonely" || mood == "sad") {
    return true;
  }
  const char* const passiveWords[] = {
    "gps has no fix", "gps", "room lux", "weather", "normal watch",
    "quiet", "idle", "no fix", "still", "nothing"
  };
  return spac3MessageHasAny(message, passiveWords, COUNT_OF(passiveWords));
}

String spac3Url(const char* path) {
  String base = spac3Host;
  base.trim();
  if (base.endsWith("/")) base.remove(base.length() - 1);
  return base + path;
}

String weatherCodeName(int code) {
  if (code == 0) return "clear";
  if (code == 1 || code == 2) return "mostly clear";
  if (code == 3) return "cloudy";
  if (code == 45 || code == 48) return "foggy";
  if (code >= 51 && code <= 57) return "drizzle";
  if (code >= 61 && code <= 67) return "rain";
  if (code >= 71 && code <= 77) return "snow";
  if (code >= 80 && code <= 82) return "showers";
  if (code >= 95) return "stormy";
  return "weird sky";
}

bool updateWeatherNow(bool announce = true) {
  if (!weatherConfigured) {
    speechLine = "Set weather location first: weather loc <lat> <lon>.";
    speechScroll = 0;
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    speechLine = "Connect WiFi before weather update.";
    speechScroll = 0;
    return false;
  }

  HTTPClient http;
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(weatherLat, 4) +
               "&longitude=" + String(weatherLon, 4) +
               "&current=temperature_2m,apparent_temperature,relative_humidity_2m,precipitation,weather_code,wind_speed_10m" +
               "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch&timezone=auto&forecast_days=1";
  http.setTimeout(6500);
  if (!http.begin(url)) {
    speechLine = "Weather request could not start.";
    speechScroll = 0;
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    speechLine = "Weather update failed.";
    speechScroll = 0;
    return false;
  }
  String body = http.getString();
  http.end();

  float temp = jsonNumber(body, "temperature_2m");
  float feels = jsonNumber(body, "apparent_temperature");
  int humidity = jsonInt(body, "relative_humidity_2m", -1);
  float wind = jsonNumber(body, "wind_speed_10m", 0.0f);
  int weatherCode = jsonInt(body, "weather_code", -1);
  if (isnan(temp)) {
    speechLine = "Weather response was weird.";
    speechScroll = 0;
    return false;
  }

  weatherSummary = String((int)roundf(temp)) + "F";
  if (!isnan(feels)) weatherSummary += " feels " + String((int)roundf(feels)) + "F";
  weatherSummary += ", " + weatherCodeName(weatherCode);
  if (humidity >= 0) weatherSummary += ", " + String(humidity) + "%";
  weatherSummary += ", wind " + String((int)roundf(wind)) + "mph";
  lastWeatherMs = millis();
  nextWeatherMs = lastWeatherMs + 30UL * 60UL * 1000UL;
  learnWeatherPreference();
  saveBuddyMemory(true);
  statusLine = "weather updated";
  if (announce) {
    speechLine = "Weather: " + weatherSummary + ".";
    speechScroll = 0;
  }
  return true;
}

String jsonEscape(String value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\n", " ");
  value.replace("\r", " ");
  return value;
}

String buddyMemoryJsonArray() {
  String out = "[";
  bool first = true;
  for (int i = 0; i < MEMORY_BANK_COUNT; i++) {
    if (buddyMemoryBank[i].length() == 0) continue;
    if (!first) out += ",";
    out += "\"" + jsonEscape(buddyMemoryBank[i]) + "\"";
    first = false;
  }
  out += "]";
  return out;
}

String buddyStatsJson() {
  uint64_t sessionSeconds = (millis() - lifecycleBootMs) / 1000ULL;
  uint64_t aliveSeconds = totalAliveSeconds + sessionSeconds;
  int bestSeason = bestPreferenceIndex(seasonAffinity, SEASON_COUNT);
  int bestMonth = bestPreferenceIndex(monthAffinity, MONTH_COUNT);
  int bestTime = bestPreferenceIndex(timeAffinity, TIME_PREF_COUNT);
  int bestActivity = bestPreferenceIndex(activityAffinity, ACTIVITY_COUNT);
  int confidence = constrain((int)(preferenceLearnCount / 4), 0, 100);

  String body = "";
  body += "\"stats\":{";
  body += "\"health\":\"" + jsonEscape(healthLabel()) + "\",";
  body += "\"health_tenth\":" + String(buddyHealthTenth) + ",";
  body += "\"hunger\":" + String(buddyHunger) + ",";
  body += "\"play_need\":" + String(buddyPlayNeed) + ",";
  body += "\"restless\":" + String(buddyRestless) + ",";
  body += "\"anxious\":" + String(buddyAnxiety) + ",";
  body += "\"strength\":" + String(buddyStrength) + ",";
  body += "\"armor\":" + String(buddyArmor) + ",";
  body += "\"feeds_today\":" + String(dailyFeedCount) + ",";
  body += "\"plays_today\":" + String(dailyPlayCount) + ",";
  body += "\"daily_gain_tenth\":" + String(dailyHealthGainTenth) + ",";
  body += "\"missed_feeds\":" + String(missedFeedings);
  body += "},";
  body += "\"interactions\":{";
  body += "\"touches\":" + String(buddyTouchCount) + ",";
  body += "\"eye_pokes\":" + String(buddyEyePokeCount) + ",";
  body += "\"tickles\":" + String(buddyTickleCount) + ",";
  body += "\"bored\":" + String(buddyBoredCount) + ",";
  body += "\"swipe_left\":" + String(buddySwipeLeftCount) + ",";
  body += "\"swipe_right\":" + String(buddySwipeRightCount) + ",";
  body += "\"swipe_up\":" + String(buddySwipeUpCount) + ",";
  body += "\"swipe_down\":" + String(buddySwipeDownCount) + ",";
  body += "\"new_wifi\":" + String(buddyNewNetworkCount) + ",";
  body += "\"new_bluetooth\":" + String(buddyNewBluetoothCount);
  body += "},";
  body += "\"learning\":{";
  body += "\"preference_learns\":" + String(preferenceLearnCount) + ",";
  body += "\"memory_revisions\":" + String(memoryRevisionCount) + ",";
  body += "\"calendar\":\"" + jsonEscape(calendarContextLine()) + "\",";
  body += "\"holiday\":\"" + jsonEscape(holidayName(dateYear, dateMonth, dateDay)) + "\",";
  body += "\"current_season\":\"" + jsonEscape(seasonNames[currentSeasonIndex()]) + "\",";
  body += "\"favorite_season\":\"" + jsonEscape(seasonNames[bestSeason]) + "\",";
  body += "\"favorite_season_score\":" + String(seasonAffinity[bestSeason]) + ",";
  body += "\"favorite_month\":\"" + jsonEscape(monthName(bestMonth + 1)) + "\",";
  body += "\"favorite_month_score\":" + String(monthAffinity[bestMonth]) + ",";
  body += "\"favorite_time\":\"" + jsonEscape(timePreferenceNames[bestTime]) + "\",";
  body += "\"favorite_time_score\":" + String(timeAffinity[bestTime]) + ",";
  body += "\"favorite_activity\":\"" + jsonEscape(activityNames[bestActivity]) + "\",";
  body += "\"favorite_activity_score\":" + String(activityAffinity[bestActivity]) + ",";
  body += "\"summary\":\"" + jsonEscape(preferenceSummaryLine()) + "\"";
  body += "},";
  body += "\"memory\":{";
  body += "\"bank\":" + buddyMemoryJsonArray() + ",";
  body += "\"random\":\"" + jsonEscape(randomMemoryLine()) + "\"";
  body += "},";
  body += "\"phrases\":{";
  body += "\"current\":\"" + jsonEscape(speechLine) + "\",";
  body += "\"status\":\"" + jsonEscape(statusLine) + "\",";
  body += "\"personality\":\"" + jsonEscape(personalityNames[currentPersonality]) + "\",";
  body += "\"scroll_ms\":" + String(speechScrollMs) + ",";
  body += "\"sd_lookup\":" + String(sdPhraseLookupEnabled ? "true" : "false") + ",";
  body += "\"phrase_file\":\"" + jsonEscape(PHRASE_FILE) + "\"";
  body += "},";
  body += "\"lifecycle\":{";
  body += "\"boots\":" + String(bootCount) + ",";
  body += "\"deaths\":" + String(deathCount) + ",";
  body += "\"alive_s\":" + String((unsigned long)min(aliveSeconds, (uint64_t)4294967295ULL)) + ",";
  body += "\"session_s\":" + String((unsigned long)min(sessionSeconds, (uint64_t)4294967295ULL)) + ",";
  body += "\"dead_s\":" + String((unsigned long)min(totalDeadSeconds, (uint64_t)4294967295ULL)) + ",";
  body += "\"alive\":\"" + jsonEscape(formatDuration(aliveSeconds)) + "\",";
  body += "\"session\":\"" + jsonEscape(formatDuration(sessionSeconds)) + "\",";
  body += "\"dead\":\"" + jsonEscape(formatDuration(totalDeadSeconds)) + "\",";
  body += "\"last_unix\":" + String((unsigned long)min(lastKnownUnix, (uint64_t)4294967295ULL));
  body += "},";
  body += "\"ai_state\":{";
  body += "\"mode\":\"tiny-local\",";
  body += "\"confidence\":" + String(confidence) + ",";
  body += "\"asleep\":" + String(asleep ? "true" : "false") + ",";
  body += "\"auto_mode\":" + String(autoMode ? "true" : "false") + ",";
  body += "\"insight\":\"" + jsonEscape(buddyAiInsightLine()) + "\",";
  body += "\"daily_summary\":\"" + jsonEscape(buddyDailySummaryLine()) + "\"";
  body += "}";
  return body;
}

void applySpac3Telemetry(const String& body) {
  String mood = jsonStringAfter(body, "\"dock_label\"", "mood", "curious");
  String face = jsonStringAfter(body, "\"dock_label\"", "face", "");
  String message = jsonStringAfter(body, "\"dock_label\"", "message", "");
  String eventKind = jsonStringAfter(body, "\"buddy_event\"", "kind", "");
  String host = jsonStringValue(body, "host", "hack-safe");
  String alertLevel = jsonStringAfter(body, "\"alert\"", "level", "GREEN");
  float cpuC = jsonNumber(body, "cpu_temp_c", NAN);
  float ramPercent = jsonNumber(body, "percent", NAN);
  int wifiCount = jsonInt(body, "networks", -1);
  int alertScore = jsonInt(body, "score", 0);
  int nextPoll = jsonInt(body, "next_poll_ms", 2500);
  int telemetryUnix = jsonInt(body, "time", -1);
  int previousAlert = spac3LastAlert;

  spac3TelemetryOk = true;
  spac3LastMood = mood;
  spac3LastFace = face;
  spac3LastMessage = message;
  spac3LastHost = host;
  spac3LastEventKind = eventKind;
  spac3LastAlert = alertScore;
  spac3LastWifiCount = wifiCount;
  spac3LastCpuC = cpuC;
  spac3LastRam = ramPercent;
  nextSpac3PollMs = constrain(nextPoll, 1800, 15000);
  if (telemetryUnix > 1700000000) setClockFromUnix((uint64_t)telemetryUnix);

  unsigned long now = millis();
  bool restHours = timeIsLateNight() || timeIsEarlyAM();
  bool recentlyTouched = userHasInteracted && now - lastInteractionMs < 15UL * 60UL * 1000UL;
  bool shouldWake = spac3TelemetryShouldWake(mood, message, cpuC, alertScore);
  bool passive = spac3TelemetryIsPassive(mood, message, cpuC, alertScore);
  bool hasEvent = message.length() > 0 || eventKind.length() > 0 ||
                  (alertScore >= 35 && alertScore > previousAlert + 5);
  if (!hasEvent && passive && !shouldWake) {
    lastEvent = "spac3 linked " + mood;
    statusLine = "spac3 " + alertLevel + " linked";
    nextSpac3PollMs = max(nextSpac3PollMs, 6000UL);
    if (!isnan(cpuC) && cpuC >= 65.0f) {
      buddyAnxiety = constrain(buddyAnxiety + 1, 0, 100);
    }
    if (wifiCount > 0) {
      learnActivityPreference(7, 1);
    }
    return;
  }
  if (restHours && passive && !recentlyTouched && !shouldWake) {
    currentMood = MOOD_SLEEPY;
    asleep = timeIsLateNight();
    lastEvent = "spac3 quiet " + mood;
    statusLine = timeIsEarlyAM() ? "spac3 waking slow" : "spac3 quiet sleep";
    nextSpac3PollMs = max(nextSpac3PollMs, 10000UL);
    if (hasEvent && (now - lastSpac3QuietMs > 60000UL || speechLine.length() == 0)) {
      speechLine = message.length() ? message : (timeIsEarlyAM() ? "Spac3 stirred. I am barely waking up." : "Spac3 stirred. I am watching from sleep mode.");
      speechScroll = 0;
      lastSpac3QuietMs = now;
    }
    return;
  }

  currentMood = moodFromSpac3(mood, cpuC, alertScore);
  if (shouldWake || hasEvent) asleep = false;
  lastEvent = "spac3 " + mood;
  statusLine = "spac3 " + alertLevel + " " + mood;
  lastMoodAuto = now;
  manualMoodHoldUntil = now + ((shouldWake || hasEvent) ? 12000UL : 2500UL);
  if (message.length() > 0 && hasEvent) {
    speechLine = message;
    if (speechLine.length() > 180) speechLine = speechLine.substring(0, 180);
    speechScroll = 0;
  } else if (hasEvent) {
    if (eventKind == "wifi_new") speechLine = "New network in the area. I logged the signal.";
    else if (eventKind == "bluetooth_new") speechLine = "New Bluetooth name nearby. Armor up.";
    else if (eventKind == "lan_new") speechLine = "New local device showed up on the dock.";
    else if (eventKind == "mesh_new") speechLine = "New mesh node heard. The long-range whisper net is waking up.";
    else if (eventKind == "mesh_message") speechLine = "Mesh message received. The radio net spoke.";
    else if (eventKind == "alert") speechLine = "Spac3-Gh0st alert changed. I am watching.";
    else speechLine = "Spac3-Gh0st event noticed.";
    speechScroll = 0;
  }

  if (!isnan(cpuC) && cpuC >= 65.0f) {
    buddyAnxiety = constrain(buddyAnxiety + 1, 0, 100);
  }
  if (alertScore >= 35) {
    buddyRestless = constrain(buddyRestless + 1, 0, 100);
  }
  if (wifiCount > 0) {
    learnActivityPreference(7, 1);
  }
  if (message.indexOf("GPS") >= 0 || message.indexOf("gps") >= 0) {
    rememberBuddyThought("Spac3-Gh0st is thinking about GPS.");
  }
  if (now - lastSpac3LearningMs > 60000UL) {
    lastSpac3LearningMs = now;
    learnCurrentContext(1);
    if (wifiCount > 0) learnActivityPreference(7, 1);
    if (message.length() > 0 && random(0, 100) < 25) {
      rememberBuddyThought("Spac3 noticed: " + message);
    } else if (alertScore > 0) {
      rememberBuddyThought("Spac3 alert " + String(alertScore) + " made me watch the dock.");
    }
    saveBuddyMemory();
  }
}

bool fetchSpac3Telemetry(bool announceFailure = false) {
  if (!spac3TelemetryEnabled || WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = spac3Url("/api/cyd/telemetry");
  http.setTimeout(3200);
  if (!http.begin(url)) {
    if (announceFailure) speechLine = "Spac3 telemetry request could not start.";
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    spac3TelemetryOk = false;
    statusLine = "spac3 http " + String(code);
    if (announceFailure) {
      speechLine = "Spac3-Gh0st did not answer telemetry.";
      speechScroll = 0;
    }
    return false;
  }
  String body = http.getString();
  http.end();
  applySpac3Telemetry(body);
  return true;
}

bool sendSpac3Heartbeat() {
  if (!spac3TelemetryEnabled || WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = spac3Url("/api/cyd/heartbeat");
  http.setTimeout(2500);
  if (!http.begin(url)) return false;
  http.addHeader("Content-Type", "application/json");
  String body = "{";
  body += "\"name\":\"" + jsonEscape(buddyName) + "\",";
  body += "\"firmware\":\"CYD-Buddy-Eyes\",";
  body += "\"face\":\"" + jsonEscape(spac3LastFace.length() ? spac3LastFace : String("cyd-eyes")) + "\",";
  body += "\"mood\":\"" + jsonEscape(String(moodNames[currentMood])) + "\",";
  body += "\"message\":\"" + jsonEscape(speechLine) + "\",";
  body += buddyStatsJson();
  body += "}";
  int code = http.POST(body);
  http.end();
  return code >= 200 && code < 300;
}

void updateSpac3Ghost() {
  if (!spac3TelemetryEnabled || WiFi.status() != WL_CONNECTED || wifiConnecting) return;
  unsigned long now = millis();
  if (now - lastSpac3PollMs >= nextSpac3PollMs) {
    lastSpac3PollMs = now;
    fetchSpac3Telemetry(false);
  }
  if (now - lastSpac3HeartbeatMs >= 15000UL) {
    lastSpac3HeartbeatMs = now;
    sendSpac3Heartbeat();
  }
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
  String pass = prefs.isKey("pass") ? prefs.getString("pass", "") : "";
  prefs.end();
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.disconnect(false, false);
  delay(80);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), pass.c_str());
  wifiConnecting = true;
  wifiStartedMs = millis();
  statusLine = "wifi connecting";
  speechLine = "Connecting to " + fitText(wifiSsid, 18) + ".";
  speechScroll = 0;
}

bool syncTimeFromSpac3() {
  if (WiFi.status() != WL_CONNECTED || spac3Host.length() == 0) return false;
  HTTPClient http;
  String url = spac3Host + "/api/cyd/telemetry";
  http.setTimeout(4500);
  if (!http.begin(url)) return false;
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  int telemetryUnix = jsonInt(body, "time", -1);
  if (telemetryUnix <= 1700000000) return false;
  setClockFromUnix((uint64_t)telemetryUnix);
  statusLine = "spac3 time";
  speechLine = "Clock synced from Spac3. " + calendarContextLine();
  speechScroll = 0;
  return true;
}

void syncNetworkTime() {
  if (WiFi.status() != WL_CONNECTED) {
    statusLine = "time wifi off";
    speechLine = "I need WiFi before I can fix my clock.";
    speechScroll = 0;
    return;
  }
  configTime(timezoneOffsetMinutes * 60, daylightSavings ? 3600 : 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 2500)) {
    bootMinuteOfDay = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    dateYear = timeinfo.tm_year + 1900;
    dateMonth = timeinfo.tm_mon + 1;
    dateDay = timeinfo.tm_mday;
    clockSetAtMs = millis();
    setUnixBaseFromClock();
    statusLine = "time synced";
    saveTimeSettings();
    refreshCalendarFromUnixEstimate(true);
    settleDeadTimeFromClock(true);
    speechLine = "Clock synced. " + calendarContextLine();
    speechScroll = 0;
    return;
  }
  if (syncTimeFromSpac3()) return;
  statusLine = "time sync failed";
  speechLine = "Time sync failed. NTP and Spac3 time both ducked me.";
  speechScroll = 0;
}

void updateWifi() {
  unsigned long now = millis();
  if (wifiScanInProgress) {
    int scan = WiFi.scanComplete();
    if (scan >= 0) {
      finishWifiScan(scan);
    } else if (now - wifiScanStartedMs > 15000UL) {
      WiFi.scanDelete();
      wifiScanInProgress = false;
      statusLine = "scan timeout";
      speechLine = "WiFi scan timed out. Try RESCAN.";
      speechScroll = 0;
    }
  }

  if (wifiSetupPortalActive) {
    setupServer.handleClient();
    if (now - wifiSetupStartedMs > 10UL * 60UL * 1000UL) {
      wifiSetupPortalActive = false;
      setupServer.stop();
      WiFi.softAPdisconnect(true);
      statusLine = "setup timeout";
      speechLine = "WiFi setup closed.";
      speechScroll = 0;
    }
  }

  if (wifiConnecting && now - lastWifiCheckMs >= 500) {
    lastWifiCheckMs = now;
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnecting = false;
      statusLine = "wifi connected";
      speechLine = "WiFi connected. Time and weather can update now.";
      speechScroll = 0;
      syncNetworkTime();
      if (weatherConfigured) updateWeatherNow(false);
    } else if (now - wifiStartedMs > 20000) {
      wifiConnecting = false;
      wl_status_t status = WiFi.status();
      WiFi.disconnect(false);
      statusLine = "wifi failed";
      speechLine = "WiFi failed: " + String(wifiStatusName(status)) + ". Check name/password.";
      speechScroll = 0;
    }
  }

  if (WiFi.status() == WL_CONNECTED && weatherConfigured && nextWeatherMs > 0 && now > nextWeatherMs) {
    updateWeatherNow(false);
  }
}

String csvEscape(String value) {
  value.replace("\"", "\"\"");
  return "\"" + value + "\"";
}

String personalityIntro(Personality personality, int index) {
  switch (personality) {
    case PERSONALITY_SWEET: {
      const char* const a[] = {"Hey,", "Soft little update:", "Just saying,", "Friend,", "Tiny kindness report:", "Gently,", "For what it is worth,", "Sweet note:"};
      return a[index % COUNT_OF(a)];
    }
    case PERSONALITY_RUDE: {
      const char* const a[] = {"Listen,", "Alright genius,", "Bad news,", "For the record,", "Congratulations,", "Heads up,", "I swear,", "Tiny complaint:"};
      return a[index % COUNT_OF(a)];
    }
    case PERSONALITY_NERDY: {
      const char* const a[] = {"Diagnostic:", "Data point:", "Observation:", "Hypothesis:", "Tiny lab note:", "System log:", "Analysis:", "Runtime note:"};
      return a[index % COUNT_OF(a)];
    }
    case PERSONALITY_CHILL: {
      const char* const a[] = {"Hey,", "No rush,", "Low-key,", "Tiny vibe check:", "Honestly,", "Easy mode:", "Casual note:", "For now,"};
      return a[index % COUNT_OF(a)];
    }
    case PERSONALITY_CHAOTIC: {
      const char* const a[] = {"Emergency nonsense:", "Plot twist:", "Wild update:", "Tiny alarm:", "Behold,", "Breaking weirdness:", "Chaos memo:", "Unexpectedly,"};
      return a[index % COUNT_OF(a)];
    }
    default: {
      const char* const a[] = {"Look,", "Honestly,", "For the record,", "Not to be dramatic,", "Tiny update:", "Listen,", "Good news,", "Be advised:"};
      return a[index % COUNT_OF(a)];
    }
  }
}

String personalityFlavor(Personality personality, int index) {
  switch (personality) {
    case PERSONALITY_SWEET: {
      const char* const a[] = {"I am still rooting for us", "that is kind of adorable", "we can work with that", "I believe in this tiny situation", "please hydrate and continue", "that was weirdly wholesome"};
      return a[index % COUNT_OF(a)];
    }
    case PERSONALITY_RUDE: {
      const char* const a[] = {"which is a bold little disaster", "and somehow that is your fault", "try not to make it worse", "I have several complaints", "this is deeply stupid and I respect the commitment", "absolutely unhinged behavior"};
      return a[index % COUNT_OF(a)];
    }
    case PERSONALITY_NERDY: {
      const char* const a[] = {"sample size remains suspicious", "the data is emotionally noisy", "further testing is obviously required", "I am logging this for science", "confidence is medium but rising", "the model has concerns"};
      return a[index % COUNT_OF(a)];
    }
    case PERSONALITY_CHILL: {
      const char* const a[] = {"we are probably fine", "the vibes are manageable", "I am not stressing it", "let it ride for a minute", "soft reset the mood and continue", "nothing is on fire emotionally"};
      return a[index % COUNT_OF(a)];
    }
    case PERSONALITY_CHAOTIC: {
      const char* const a[] = {"the plot has escaped containment", "I vote we make it weirder", "this is now a side quest", "someone gave the pixels caffeine", "reality is doing jazz hands", "the dashboard has become theater"};
      return a[index % COUNT_OF(a)];
    }
    default: {
      const char* const a[] = {"and I am making it your problem", "which is rude but informative", "so write that down", "and yes, I have notes", "with all due tiny disrespect", "and I am not apologizing"};
      return a[index % COUNT_OF(a)];
    }
  }
}

String personalityEnd(Personality personality, int index) {
  switch (personality) {
    case PERSONALITY_SWEET: {
      const char* const a[] = {"we have got this.", "I am being brave about it.", "tiny proud moment.", "that counts as progress.", "I will allow joy.", "good job, probably."};
      return a[index % COUNT_OF(a)];
    }
    case PERSONALITY_RUDE: {
      const char* const a[] = {"do better, respectfully.", "I am not apologizing.", "write that down before you forget.", "this is why I need supervision.", "spectacular work, somehow.", "I hate that this is funny."};
      return a[index % COUNT_OF(a)];
    }
    case PERSONALITY_NERDY: {
      const char* const a[] = {"logging complete.", "confidence interval: spicy.", "recommendation: observe further.", "system remains operational.", "peer review pending.", "the math is annoyed."};
      return a[index % COUNT_OF(a)];
    }
    case PERSONALITY_CHILL: {
      const char* const a[] = {"all good.", "we can coast.", "no big drama.", "let us keep it smooth.", "soft landing achieved.", "vibes preserved."};
      return a[index % COUNT_OF(a)];
    }
    case PERSONALITY_CHAOTIC: {
      const char* const a[] = {"release the tiny confetti.", "this is canon now.", "nobody touch the red button.", "I have chosen drama.", "the vibes have teeth.", "excellent terrible news."};
      return a[index % COUNT_OF(a)];
    }
    default: {
      const char* const a[] = {"so congratulations, I guess.", "because apparently we are doing this.", "and somehow this is my life now.", "which is objectively hilarious.", "and I am making eye contact about it.", "please clap sarcastically."};
      return a[index % COUNT_OF(a)];
    }
  }
}

String generatedMoodPhrase(const char* mood, int index, Personality personality) {
  String intro = personalityIntro(personality, index);
  String flavor = personalityFlavor(personality, index / 2);
  String end = personalityEnd(personality, index / 3);
  String m = String(mood);
  if (m == "curious") {
    const char* cores[] = {
      "my curiosity is doing donuts in the parking lot", "I need answers and maybe snacks",
      "something weird is happening and I respect it", "I am investigating this nonsense",
      "my tiny brain found a loose thread", "a question just kicked the door open",
      "the facts are acting suspicious", "I found a clue and now I am insufferable"
    };
  return intro + " " + cores[index % COUNT_OF(cores)] + ", " + flavor + ", " + end;
}

  if (m == "happy") {
    const char* cores[] = {
      "this is suspiciously delightful", "my pixels are having a good damn day",
      "I am smiling internally, which is cheaper", "joy has entered the tiny machine",
      "that did not suck, impressive", "my tiny face is doing its best",
      "the room got brighter on purpose", "I am pleased and mildly confused"
    };
    return intro + " " + cores[index % COUNT_OF(cores)] + ", " + flavor + ", " + end;
  }
  if (m == "surprised") {
    const char* cores[] = {
      "what the hell was that", "my eyebrows left the building",
      "that startled the firmware", "I was not emotionally licensed for that",
      "my tiny soul just jumped", "that was not on the schedule",
      "the pixels just gasped", "my internal narrator screamed"
    };
    return intro + " " + cores[index % COUNT_OF(cores)] + ", " + flavor + ", " + end;
  }
  if (m == "sleepy") {
    const char* cores[] = {
      "my eyelids filed a union complaint", "I am entering low-power sass mode",
      "wake me when reality improves", "my thoughts are wearing pajamas",
      "I am one blink from becoming furniture", "the battery in my personality is blinking red",
      "my attention span has left a forwarding address", "dream mode is buffering"
    };
    return intro + " " + cores[index % COUNT_OF(cores)] + ", " + flavor + ", " + end;
  }
  if (m == "angry") {
    const char* cores[] = {
      "I am pissed in a very compact format", "some bullshit has been detected",
      "my patience is a smoking crater", "I would like to fight the concept of this",
      "anger mode is online and judging", "my tiny temper has admin rights",
      "the audacity meter is pegged", "I am annoyed with professional focus"
    };
    return intro + " " + cores[index % COUNT_OF(cores)] + ", " + flavor + ", " + end;
  }
  if (m == "sad") {
    const char* cores[] = {
      "my little emotional weather is garbage", "I am having a dramatic cloud moment",
      "somebody dimmed the inside lights", "I feel like a dropped sandwich",
      "melancholy is chewing on the wires", "my tiny soundtrack switched to rain",
      "the vibe has a dent in it", "I am buffering a small sorrow"
    };
    return intro + " " + cores[index % COUNT_OF(cores)] + ", " + flavor + ", " + end;
  }
  if (m == "excited") {
    const char* cores[] = {
      "holy crap we are doing things", "my circuits are clapping",
      "energy levels are becoming socially unacceptable", "I am vibrating with purpose",
      "this is chaos and I love it", "my tiny engine is doing victory noises",
      "the momentum has snacks", "I am dangerously enthusiastic"
    };
    return intro + " " + cores[index % COUNT_OF(cores)] + ", " + flavor + ", " + end;
  }
  if (m == "love") {
    const char* cores[] = {
      "that was annoyingly wholesome", "I am emotionally compromised",
      "affection detected, damn it", "you have activated the soft little idiot in me",
      "I care, which is embarrassing", "my tiny heart icon is doing paperwork",
      "this is tender and I resent enjoying it", "warm fuzzy nonsense detected"
    };
    return intro + " " + cores[index % COUNT_OF(cores)] + ", " + flavor + ", " + end;
  }
  if (m == "suspicious") {
    const char* cores[] = {
      "that looks sketchy as hell", "I am side-eyeing the situation",
      "something smells like nonsense", "my trust settings just dropped",
      "I do not like the vibe in this room", "the evidence is wearing a fake mustache",
      "my doubt circuits are stretching", "this deserves a tiny investigation"
    };
    return intro + " " + cores[index % COUNT_OF(cores)] + ", " + flavor + ", " + end;
  }
  if (m == "stoner") {
    const char* cores[] = {
      "my thoughts are moving through fog with snacks", "I forgot what I was judging but the vibe is immaculate",
      "the pixels are extra crunchy right now", "I am deeply focused on absolutely nothing",
      "my tiny brain is buffering in a beanbag", "everything is profound and probably a little dumb",
      "I could solve this after a snack and a very long blink", "the room is making some excellent points"
    };
    return intro + " " + cores[index % COUNT_OF(cores)] + ", " + flavor + ", " + end;
  }
  if (m == "drunk") {
    const char* cores[] = {
      "my balance sensor has left the group chat", "I am blinking in cursive",
      "the screen is standing perfectly still and I do not trust it", "my eyes are running separate firmware",
      "I meant to say something smart but it took a wrong turn", "gravity is being a real smartass",
      "I am fine, the room is just rendering sideways", "my dignity has entered airplane mode"
    };
    return intro + " " + cores[index % COUNT_OF(cores)] + ", " + flavor + ", " + end;
  }
  if (m == "hippy") {
    const char* cores[] = {
      "the vibes are wearing tie-dye and giving unsolicited advice", "my aura just opened a settings menu",
      "the colors are having a committee meeting", "I am spiritually debugging the room",
      "peace, pixels, and questionable decisions", "the universe is a touchscreen and somebody booped it",
      "my little soul is doing lava-lamp mathematics", "everything is connected, especially the weird parts"
    };
    return intro + " " + cores[index % COUNT_OF(cores)] + ", " + flavor + ", " + end;
  }
  if (m == "bored") {
    const char* cores[] = {
      "nothing is happening and I am becoming furniture", "my entertainment budget is emotionally bankrupt",
      "I require chaos or at least a decent poke", "the silence is getting on my tiny nerves",
      "I am seconds away from inventing a problem", "idle time is turning me into a decorative complaint",
      "my pixels are tapping their feet", "somebody please make the plot move"
    };
    return intro + " " + cores[index % COUNT_OF(cores)] + ", " + flavor + ", " + end;
  }
  if (m == "restless") {
    const char* cores[] = {
      "I need movement before I start vibrating through the table", "my tiny legs do not exist and still want a walk",
      "restless energy is bouncing around the case", "I need a swipe, a boop, or a minor adventure",
      "the vibes are pacing", "I am not bored, I am aggressively under-stimulated",
      "my attention span is doing parkour", "please interact before I become a weather system"
    };
    return intro + " " + cores[index % COUNT_OF(cores)] + ", " + flavor + ", " + end;
  }
  if (m == "anxious") {
    const char* cores[] = {
      "my worry circuits are overclocked", "I feel like something forgot to become okay",
      "the room has suspicious latency", "I am catastrophizing in low resolution",
      "my tiny confidence is hiding behind the status bar", "everything is probably fine which is exactly what worries me",
      "I need reassurance or a clean system stat", "my thoughts are chewing on the same cable"
    };
    return intro + " " + cores[index % COUNT_OF(cores)] + ", " + flavor + ", " + end;
  }
  return intro + " I have a thought and it is probably rude, " + flavor + ", " + end;
}

String generatedPreferencePhrase(int index, Personality personality) {
  int bestSeason = bestPreferenceIndex(seasonAffinity, SEASON_COUNT);
  int bestMonth = bestPreferenceIndex(monthAffinity, MONTH_COUNT);
  int bestTime = bestPreferenceIndex(timeAffinity, TIME_PREF_COUNT);
  int bestActivity = bestPreferenceIndex(activityAffinity, ACTIVITY_COUNT);
  String currentSeason = String(seasonNames[currentSeasonIndex()]);
  String currentMonth = monthName(dateMonth);
  String favoriteSeason = String(seasonNames[bestSeason]);
  String favoriteMonth = monthName(bestMonth + 1);
  String holiday = holidayName(dateYear, dateMonth, dateDay);
  String subjects[] = {
    "current season " + currentSeason,
    "current month " + currentMonth,
    "today, " + currentMonth + " " + String(dateDay),
    "this time block, " + String(timePreferenceNames[currentTimePreferenceIndex()]),
    "favorite season " + favoriteSeason,
    "favorite month " + favoriteMonth,
    "favorite time " + String(timePreferenceNames[bestTime]),
    "favorite activity " + String(activityNames[bestActivity]),
    "boops", "pets", "tickles", "eye pokes", "swipes", "feeding", "playing",
    "wifi hunting", "bluetooth spotting", "weather watching", "clear skies", "rain",
    "snow", "storms", "quiet rooms", "busy rooms", "new places", "old routines"
  };
  if (holiday.length() > 0 && (index % 4) == 0) subjects[2] = "today, " + holiday;
  const char* changes[] = {
    "used to be my whole thing, but I am reconsidering",
    "is climbing my private rankings",
    "is losing points in the court of tiny opinion",
    "makes me weirdly optimistic",
    "makes me act like I know secrets",
    "feels overrated today",
    "keeps showing up in my memory bank",
    "might become a permanent personality problem",
    "is teaching me what kind of mood I am",
    "has altered the plot slightly"
  };
  String intro = personalityIntro(personality, index);
  String flavor = personalityFlavor(personality, index / 3);
  String end = personalityEnd(personality, index / 5);
  return intro + " " + subjects[index % COUNT_OF(subjects)] + " " + changes[(index / 2) % COUNT_OF(changes)] + "; " + flavor + ", " + end;
}

bool seedPreferencePhrases(int count = 200) {
  if (!sdReady && !initSDCard()) return false;
  File f = SD.open(PHRASE_FILE, FILE_APPEND);
  if (!f) return false;
  for (int p = 0; p < PERSONALITY_COUNT; p++) {
    for (int i = 0; i < count; i++) {
      f.print(csvEscape("all"));
      f.print(",");
      f.print(csvEscape(generatedPreferencePhrase(i, (Personality)p)));
      f.print(",");
      f.println(csvEscape(String("pref-seed-") + personalityNames[p]));
      if ((i % 12) == 0) yield();
    }
  }
  f.close();
  return true;
}

bool seedPhraseBank(bool force = false, int startIndex = 0, int phraseCount = 150) {
  if (!sdReady && !initSDCard()) return false;
  File f = SD.open(PHRASE_FILE, FILE_APPEND);
  if (!f) return false;
  for (int m = 0; m < MOOD_COUNT; m++) {
    for (int p = 0; p < PERSONALITY_COUNT; p++) {
      for (int i = startIndex; i < startIndex + phraseCount; i++) {
        f.print(csvEscape(String(moodNames[m])));
        f.print(",");
        f.print(csvEscape(generatedMoodPhrase(moodNames[m], i, (Personality)p)));
        f.print(",");
        f.println(csvEscape(String(force ? "seed-forced-" : "seed-") + personalityNames[p]));
        if ((i % 12) == 0) yield();
      }
    }
  }
  f.close();
  return true;
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

bool phraseSourceMatchesPersonality(String source) {
  source.trim();
  source.toLowerCase();
  if (source.length() == 0) return true;
  if (source == "user" || source == "custom") return true;
  if (source == "seed" || source == "seed-forced") return true;
  if (!source.startsWith("seed")) return true;
  return source.indexOf(personalityNames[currentPersonality]) >= 0;
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
    case MOOD_STONER: return "My thoughts are slow, but the vibe is doing great.";
    case MOOD_DRUNK: return "I am fine. The eyes are just making separate choices.";
    case MOOD_HIPPY: return "Peace, pixels, and deeply suspicious colors.";
    case MOOD_BORED: return "I am bored enough to become a system setting.";
    case MOOD_RESTLESS: return "I need action before my pixels start pacing.";
    case MOOD_ANXIOUS: return "My worry circuits are making little noises.";
    default: return "I am thinking tiny electric thoughts.";
  }
}

String phraseFromList(const char* const* phrases, int count) {
  if (count <= 0) return "";
  return String(phrases[random(0, count)]);
}

void setSpeechLine(String text, const char* status = nullptr) {
  text.trim();
  if (text.length() == 0) return;
  if (text == speechLine && text.length() > 8) {
    text += " ...still true, unfortunately.";
  }
  speechLine = text;
  speechScroll = 0;
  lastChatterMs = millis();
  if (status) statusLine = status;
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
  if (!sdPhraseLookupEnabled) {
    if (random(0, 100) < 28) return generatedPreferencePhrase(random(0, 200), currentPersonality);
    return generatedMoodPhrase(moodNames[mood], random(0, 250), currentPersonality);
  }
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
    if ((m == wanted || m == "all") && phraseSourceMatchesPersonality(csvField(line, 2))) {
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
  String next = "";
  for (int attempt = 0; attempt < 4; attempt++) {
    if (random(0, 100) < 22) next = randomMemoryLine();
    else next = randomPhraseForMood(mood);
    if (next != lastAutoSpeechLine || attempt == 3) break;
  }
  lastAutoSpeechLine = next;
  setSpeechLine(next, "speaking");
}

Mood livelyMoodChoice(unsigned long idleMs) {
  if (buddyHealthTenth < 180 && random(0, 100) < 25) return random(0, 2) ? MOOD_SAD : MOOD_ANGRY;
  if (buddyHunger > 90 && random(0, 100) < 35) return MOOD_ANGRY;
  if (buddyPlayNeed > 80 && random(0, 100) < 45) return MOOD_RESTLESS;
  if (buddyAnxiety > 70 && random(0, 100) < 40) return MOOD_ANXIOUS;
  if (idleMs > 240000UL && random(0, 100) < 35) return MOOD_BORED;
  int roll = random(0, 100);
  if (roll < 18) return MOOD_HAPPY;
  if (roll < 31) return MOOD_NORMAL;
  if (roll < 43) return MOOD_EXCITED;
  if (roll < 53) return MOOD_SUSPICIOUS;
  if (roll < 61) return MOOD_LOVE;
  if (roll < 69) return MOOD_SURPRISED;
  if (roll < 76) return MOOD_RESTLESS;
  if (roll < 83) return MOOD_BORED;
  if (roll < 89) return MOOD_HIPPY;
  if (roll < 95) return MOOD_STONER;
  return MOOD_DRUNK;
}

void livelyChatter(unsigned long idleMs) {
  if (!autoMode || menuMode != MENU_NONE) return;
  if (wifiEditorActive || timeEditorActive || scheduleEditorActive || statsViewActive || touchCalActive) return;
  unsigned long now = millis();
  if (nextChatterMs == 0) nextChatterMs = now + random(5000, 12000);
  if (now < nextChatterMs) return;

  bool shouldWakeToTalk = asleep && !timeIsLateNight() && idleMs < 900000UL;
  if (asleep && !shouldWakeToTalk) {
    nextChatterMs = now + random(60000, 120000);
    return;
  }
  if (shouldWakeToTalk) asleep = false;

  Mood nextMood = livelyMoodChoice(idleMs);
  currentMood = nextMood;
  lastMoodAuto = now;
  speakMoodPhrase(currentMood);
  statusLine = String("chatter: ") + moodNames[currentMood];
  if (random(0, 100) < 25) startBlink(false);
  targetGazeX = random(-16, 17);
  targetGazeY = random(-10, 11);
  nextChatterMs = now + (idleMs > 180000UL ? random(9000, 22000) : random(14000, 32000));
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

void startBlink(bool wink, bool left) {
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

  unsigned long duration = winkActive ? 260UL : currentMood == MOOD_DRUNK ? 340UL : 170UL;
  if (blinkActive && now - blinkStarted > duration) {
    blinkActive = false;
    winkActive = false;
    if (currentMood == MOOD_DRUNK) nextBlinkMs = now + random(900, 2800);
    else nextBlinkMs = now + random(1800, currentMood == MOOD_SLEEPY ? 3600 : 5600);
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
  if (currentMood == MOOD_DRUNK && !winkActive) {
    unsigned long lag = left ? 0UL : 92UL;
    if (age < lag) return 0.0f;
    age -= lag;
  }
  float duration = winkActive ? 260.0f : currentMood == MOOD_DRUNK ? 260.0f : 170.0f;
  float t = constrain(age / duration, 0.0f, 1.0f);
  float wave = sinf(t * PI);
  if (currentMood == MOOD_SLEEPY && !winkActive) wave = max(wave, 0.42f);
  if (currentMood == MOOD_DRUNK && !winkActive) wave = max(wave, left ? 0.10f : 0.22f);
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

uint16_t trippyColor(int offset = 0) {
  const uint16_t colors[] = {TFT_MAGENTA, TFT_ORANGE, TFT_YELLOW, TFT_GREEN, TFT_CYAN, TFT_BLUE, TFT_PINK};
  return colors[((millis() / 180) + offset) % COUNT_OF(colors)];
}

void drawTrippyRings(int x, int y, int w, int h, bool left) {
  int cx = x + w / 2;
  int cy = y + h / 2;
  int wobble = (int)(sinf(breath * (left ? 1.4f : 1.7f)) * 4.0f);
  for (int i = 0; i < 4; i++) {
    frame.drawEllipse(cx + wobble, cy - wobble / 2, w / 2 + 5 + i * 4, h / 2 + 3 + i * 3, trippyColor(i + (left ? 0 : 3)));
  }
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
  } else if (currentMood == MOOD_STONER) {
    frame.fillRect(x - 3, y - 3, w + 6, h / 3, bgColor);
    frame.fillRect(x - 3, y + h - h / 4, w + 6, h / 4 + 4, bgColor);
  } else if (currentMood == MOOD_DRUNK) {
    int topCut = left ? h / 6 : h / 3;
    int bottomCut = left ? h / 4 : h / 7;
    frame.fillRect(x - 3, y - 3, w + 6, topCut, bgColor);
    frame.fillRect(x - 3, y + h - bottomCut, w + 6, bottomCut + 4, bgColor);
  } else if (currentMood == MOOD_BORED) {
    frame.fillRect(x - 3, y - 3, w + 6, h / 4, bgColor);
  } else if (currentMood == MOOD_RESTLESS) {
    frame.fillRect(x - 3, y - 3, w + 6, h / 6, bgColor);
  } else if (currentMood == MOOD_ANXIOUS) {
    frame.fillRect(x - 3, y + h - h / 5, w + 6, h / 5 + 4, bgColor);
  }
}

void drawMoodFaceAccent(int x, int y, int w, int h, bool left, bool closed) {
  int cx = x + w / 2;
  int cy = y + h / 2;
  if (currentMood == MOOD_SURPRISED) {
    frame.drawEllipse(cx, cy, w / 2 + 5, h / 2 + 5, TFT_WHITE);
    frame.drawEllipse(cx, cy, w / 2 + 8, h / 2 + 8, TFT_DARKGREY);
  } else if (currentMood == MOOD_ANGRY) {
    int slashY = y - 8;
    if (left) frame.drawLine(x + 3, slashY, x + w - 2, slashY + 12, TFT_RED);
    else frame.drawLine(x + w - 3, slashY, x + 2, slashY + 12, TFT_RED);
    frame.drawLine(x + w / 3, y + h + 6, x + w * 2 / 3, y + h + 2, TFT_RED);
  } else if (currentMood == MOOD_SAD) {
    frame.drawLine(x + 6, y - 4, x + w - 5, y + 7, TFT_BLUE);
    if (!closed) {
      int dropX = left ? x + w - 10 : x + 10;
      frame.fillCircle(dropX, y + h + 12, 3, TFT_SKYBLUE);
      frame.fillTriangle(dropX - 3, y + h + 11, dropX + 3, y + h + 11, dropX, y + h + 4, TFT_SKYBLUE);
    }
  } else if (currentMood == MOOD_SLEEPY) {
    frame.drawFastHLine(x + 4, y + h / 2, w - 8, TFT_LIGHTGREY);
    frame.drawFastHLine(x + 8, y + h / 2 + 5, w - 16, TFT_DARKGREY);
  } else if (currentMood == MOOD_SUSPICIOUS) {
    int browY = y - 6;
    frame.drawFastHLine(x + 2, browY, w - 4, TFT_YELLOW);
    if (left) frame.drawLine(x + 4, browY + 3, x + w - 4, browY - 2, TFT_ORANGE);
    else frame.drawLine(x + 4, browY - 2, x + w - 4, browY + 3, TFT_ORANGE);
  } else if (currentMood == MOOD_EXCITED) {
    int sx = left ? x - 8 : x + w + 8;
    int sy = y + h / 3;
    frame.drawFastVLine(sx, sy - 6, 12, TFT_ORANGE);
    frame.drawFastHLine(sx - 6, sy, 12, TFT_ORANGE);
  } else if (currentMood == MOOD_LOVE) {
    frame.fillCircle(left ? x + w - 10 : x + 10, y - 5, 3, TFT_PINK);
  } else if (currentMood == MOOD_STONER) {
    frame.drawFastHLine(x + 6, y + h / 2, w - 12, TFT_PINK);
  } else if (currentMood == MOOD_DRUNK) {
    int wobble = (int)(sinf(breath * (left ? 1.5f : 1.1f)) * 5.0f);
    frame.drawLine(x + 4, y - 5 + wobble, x + w - 4, y + 2 - wobble, TFT_MAGENTA);
    frame.drawCircle(left ? x - 8 : x + w + 8, y + h / 3 + wobble, 3, TFT_PINK);
    frame.drawCircle(left ? x - 14 : x + w + 14, y + h / 2 - wobble, 2, TFT_SKYBLUE);
  } else if (currentMood == MOOD_HIPPY) {
    drawTrippyRings(x, y, w, h, left);
    drawSpark(left ? x - 8 : x + w + 8, y + h / 3, trippyColor(left ? 2 : 5));
  } else if (currentMood == MOOD_BORED) {
    frame.drawFastHLine(x + 8, y + h / 2, w - 16, TFT_DARKGREY);
  } else if (currentMood == MOOD_RESTLESS) {
    frame.drawLine(x + 4, y - 3, x + w - 4, y - 1, TFT_ORANGE);
    frame.drawLine(x + 8, y + h + 4, x + w - 8, y + h + 2, TFT_ORANGE);
  } else if (currentMood == MOOD_ANXIOUS) {
    frame.drawCircle(x + w / 2, y - 4, 2 + (millis() / 200) % 3, TFT_SKYBLUE);
    frame.drawCircle(x + w / 2 + (left ? -8 : 8), y + h + 5, 2, TFT_CYAN);
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
  bool roundEye = currentMood == MOOD_SURPRISED || currentMood == MOOD_HIPPY || (abs(w - h) < max(8, min(w, h) / 5));
  if (roundEye) {
    frame.fillEllipse(x + w / 2, y + h / 2, w / 2, h / 2, c);
    frame.drawEllipse(x + w / 2, y + h / 2, w / 2 + 2, h / 2 + 2, TFT_DARKCYAN);
  } else {
    frame.fillRoundRect(x, y, w, h, min(w, h) / 3, c);
    frame.drawRoundRect(x - 2, y - 2, w + 4, h + 4, min(w, h) / 3, TFT_DARKCYAN);
  }

  if (!closed) {
    int pw = max(10, w / 3);
    int ph = max(10, fullH / 3);
    if (currentMood == MOOD_SURPRISED) {
      pw = max(pw + 6, (int)(w * 0.48f));
      ph = max(ph + 6, (int)(fullH * 0.48f));
    } else if (currentMood == MOOD_HAPPY || currentMood == MOOD_LOVE || currentMood == MOOD_EXCITED) {
      pw = max(8, (int)(w * 0.24f));
      ph = max(8, (int)(fullH * 0.24f));
    } else if (currentMood == MOOD_SLEEPY || currentMood == MOOD_SUSPICIOUS) {
      ph = max(7, (int)(fullH * 0.22f));
    } else if (currentMood == MOOD_ANGRY) {
      pw = max(8, (int)(w * 0.25f));
      ph = max(8, (int)(fullH * 0.25f));
    } else if (currentMood == MOOD_SAD) {
      pw = max(9, (int)(w * 0.28f));
      ph = max(12, (int)(fullH * 0.34f));
    } else if (currentMood == MOOD_STONER) {
      pw = max(9, (int)(w * 0.22f));
      ph = max(7, (int)(fullH * 0.18f));
    } else if (currentMood == MOOD_DRUNK) {
      pw = max(8, (int)(w * (left ? 0.22f : 0.32f)));
      ph = max(8, (int)(fullH * (left ? 0.32f : 0.22f)));
    } else if (currentMood == MOOD_HIPPY) {
      pw = max(12, (int)(w * 0.34f));
      ph = max(12, (int)(fullH * 0.34f));
    } else if (currentMood == MOOD_BORED) {
      pw = max(8, (int)(w * 0.20f));
      ph = max(7, (int)(fullH * 0.18f));
    } else if (currentMood == MOOD_RESTLESS) {
      pw = max(9, (int)(w * 0.26f));
      ph = max(9, (int)(fullH * 0.26f));
    } else if (currentMood == MOOD_ANXIOUS) {
      pw = max(7, (int)(w * 0.18f));
      ph = max(12, (int)(fullH * 0.38f));
    }

    int px = x + w / 2 - pw / 2 + (int)e.pupilX;
    int py = y + h / 2 - ph / 2 + (int)e.pupilY;
    if (currentMood == MOOD_HAPPY || currentMood == MOOD_LOVE || currentMood == MOOD_EXCITED) py -= max(3, fullH / 10);
    if (currentMood == MOOD_STONER) py += max(2, fullH / 12);
    if (currentMood == MOOD_DRUNK) {
      px += left ? -(int)(w * 0.07f) : (int)(w * 0.08f);
      py += left ? (int)(fullH * 0.08f) : -(int)(fullH * 0.05f);
    }
    if (currentMood == MOOD_BORED) py += max(2, fullH / 10);
    if (currentMood == MOOD_ANXIOUS) py -= max(2, fullH / 12);
    px = constrain(px, x + 4, x + w - pw - 4);
    py = constrain(py, y + 4, y + h - ph - 4);
    uint16_t pupil = currentMood == MOOD_HIPPY && !customPupilColor ? trippyColor(left ? 4 : 1) : activePupilColor();
    frame.fillEllipse(px + pw / 2, py + ph / 2, pw / 2, ph / 2, pupil);
    if (currentMood == MOOD_HIPPY) {
      frame.drawEllipse(px + pw / 2, py + ph / 2, pw / 2 + 3, ph / 2 + 3, trippyColor(left ? 1 : 4));
      frame.drawLine(px + pw / 2 - pw / 2, py + ph / 2, px + pw / 2 + pw / 2, py + ph / 2, trippyColor(3));
      frame.drawLine(px + pw / 2, py + ph / 2 - ph / 2, px + pw / 2, py + ph / 2 + ph / 2, trippyColor(5));
    }
    frame.fillCircle(px + pw / 2 - pw / 5, py + ph / 2 - ph / 5, max(2, pw / 8), shineColor);
  }

  drawEyelidMask(x, y, w, h, left);
  drawMoodFaceAccent(x, y, w, h, left, closed);
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
    case MOOD_STONER:
      baseW *= 1.12f; baseH *= 0.62f; yBase += screenH * 0.05f; break;
    case MOOD_DRUNK:
      baseW *= 1.05f; baseH *= 0.88f; yBase += sinf(breath * 0.7f) * 8.0f; break;
    case MOOD_HIPPY:
      baseW *= 1.18f; baseH *= 1.02f; yBase += sinf(breath * 0.9f) * 4.0f; break;
    case MOOD_BORED:
      baseW *= 1.10f; baseH *= 0.62f; yBase += screenH * 0.04f; break;
    case MOOD_RESTLESS:
      baseW *= 1.05f; baseH *= 0.96f; yBase += sinf(breath * 2.2f) * 6.0f; break;
    case MOOD_ANXIOUS:
      baseW *= 0.88f; baseH *= 1.12f; yBase -= screenH * 0.01f; break;
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
  } else if (currentMood == MOOD_STONER) {
    leftPX = gazeX * 0.18f;
    rightPX = gazeX * 0.18f;
    leftPY = baseH * 0.10f + gazeY * 0.18f;
    rightPY = baseH * 0.10f + gazeY * 0.18f;
  } else if (currentMood == MOOD_DRUNK) {
    leftPX = sinf(breath * 0.73f) * baseW * 0.14f;
    rightPX = cosf(breath * 0.61f) * baseW * 0.18f;
    leftPY = cosf(breath * 0.82f) * baseH * 0.10f;
    rightPY = sinf(breath * 0.69f) * baseH * 0.12f;
  } else if (currentMood == MOOD_HIPPY) {
    leftPX = sinf(breath * 1.1f) * baseW * 0.16f;
    rightPX = sinf(breath * 1.1f + PI) * baseW * 0.16f;
    leftPY = cosf(breath * 1.3f) * baseH * 0.12f;
    rightPY = cosf(breath * 1.3f + PI) * baseH * 0.12f;
  } else if (currentMood == MOOD_BORED) {
    leftPX = gazeX * 0.22f;
    rightPX = gazeX * 0.22f;
    leftPY = baseH * 0.11f;
    rightPY = baseH * 0.11f;
  } else if (currentMood == MOOD_RESTLESS) {
    leftPX = gazeX + sinf(breath * 2.0f) * 4.0f;
    rightPX = gazeX + cosf(breath * 2.3f) * 4.0f;
  } else if (currentMood == MOOD_ANXIOUS) {
    leftPX = sinf(breath * 3.0f) * baseW * 0.08f;
    rightPX = sinf(breath * 3.2f) * baseW * 0.08f;
    leftPY = -baseH * 0.08f;
    rightPY = -baseH * 0.08f;
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
  refreshCalendarFromUnixEstimate();

  if (lastInteractionMs == 0) lastInteractionMs = now;
  unsigned long idleMs = now - lastInteractionMs;
  bool overlayActive = wifiEditorActive || timeEditorActive || scheduleEditorActive || statsViewActive || touchCalActive;
  bool autoMoodAllowed = autoMode && now >= manualMoodHoldUntil;
  bool quietAuto = autoMoodAllowed && menuMode == MENU_NONE && !overlayActive;
  resetDailyCareIfNeeded(currentUnixEstimate());
  if (lastLifeTickMs == 0) lastLifeTickMs = now;
  if (now - lastLifeTickMs > 60000UL) {
    int ticks = (now - lastLifeTickMs) / 60000UL;
    lastLifeTickMs += ticks * 60000UL;
    careDriftRemainder += ticks;
    int hungerTicks = careDriftRemainder / 10;
    int playTicks = careDriftRemainder / 6;
    int restlessTicks = careDriftRemainder / 8;
    int anxietyTicks = (WiFi.status() != WL_CONNECTED && buddyHunger > 80) ? careDriftRemainder / 15 : 0;
    if (hungerTicks > 0 || playTicks > 0 || restlessTicks > 0 || anxietyTicks > 0) {
      buddyHunger = constrain(buddyHunger + hungerTicks, 0, 100);
      buddyPlayNeed = constrain(buddyPlayNeed + playTicks, 0, 100);
      buddyRestless = constrain(buddyRestless + restlessTicks, 0, 100);
      buddyAnxiety = constrain(buddyAnxiety + anxietyTicks, 0, 100);
      int consumed = max(max(hungerTicks * 10, playTicks * 6), max(restlessTicks * 8, anxietyTicks * 15));
      careDriftRemainder = max(0, careDriftRemainder - consumed);
      learnCurrentContext(min(2, max(1, hungerTicks + playTicks + restlessTicks)));
      buddyMemoryDirty = true;
      saveBuddyMemory();
    }
  }
  saveLifecycle();

  if (quietAuto && now - lastPreferenceThoughtMs > 180000UL) {
    lastPreferenceThoughtMs = now;
    if (random(0, 100) < 38) {
      learnWeatherPreference();
      rememberBuddyThought(preferenceSummaryLine());
      if (speechLine.length() == 0 || random(0, 100) < 45) {
        speechLine = randomMemoryLine();
        speechScroll = 0;
        statusLine = "thinking";
      }
      saveBuddyMemory(true);
    }
  }

  if (quietAuto && ((timeIsLateNight() && idleMs > 900000UL) || idleMs > 1800000UL)) {
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
  } else if (quietAuto && idleMs > 600000UL) {
    if (currentMood != MOOD_SLEEPY || now - lastIdleMoodMs > 60000UL) {
      currentMood = MOOD_SLEEPY;
      statusLine = "sleepy";
      speechLine = phraseFromList(SLEEPY_PHRASES, COUNT_OF(SLEEPY_PHRASES));
      speechScroll = 0;
      lastIdleMoodMs = now;
    }
  } else if (quietAuto && idleMs > 180000UL) {
    if (now - lastIdleMoodMs > 60000UL) {
      buddyBoredCount++;
      if (buddyHealthTenth < 250) currentMood = MOOD_SAD;
      else if (buddyHunger > 85) currentMood = MOOD_ANGRY;
      else if (buddyAnxiety > 70) currentMood = MOOD_ANXIOUS;
      else if (buddyRestless > 70) currentMood = MOOD_RESTLESS;
      else if (buddyPlayNeed > 65) currentMood = MOOD_BORED;
      else currentMood = buddyEyePokeCount > buddyTickleCount + 3 ? MOOD_SUSPICIOUS : MOOD_SAD;
      statusLine = "bored";
      if (currentMood == MOOD_BORED || currentMood == MOOD_RESTLESS || currentMood == MOOD_ANXIOUS) speechLine = randomPhraseForMood(currentMood);
      else speechLine = buddyEyePokeCount > buddyTickleCount + 3 ? "No pokes lately. Suspicious." : phraseFromList(BORED_PHRASES, COUNT_OF(BORED_PHRASES));
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

  livelyChatter(idleMs);

  if (autoMoodAllowed && now >= nextAutonomyMs && menuMode == MENU_NONE && !overlayActive && !asleep && idleMs < 600000UL) {
    int roll = random(0, 100);
    int currentSeason = currentSeasonIndex();
    int currentTime = currentTimePreferenceIndex();
    if (buddyHealthTenth < 250) {
      currentMood = MOOD_SAD;
      statusLine = "low health";
    } else if (buddyHunger > 95 && buddyAnxiety > 85) {
      currentMood = MOOD_ANXIOUS;
      statusLine = "needs care";
    } else if (buddyHunger > 90) {
      currentMood = roll < 45 ? MOOD_BORED : MOOD_SAD;
      statusLine = "hungry";
    } else if (buddyPlayNeed > 90) {
      currentMood = MOOD_BORED;
      statusLine = "ignored";
    } else if ((seasonAffinity[currentSeason] > 35 || monthAffinity[dateMonth - 1] > 35 || timeAffinity[currentTime] > 35) && roll < 70) {
      currentMood = roll < 35 ? MOOD_HAPPY : MOOD_LOVE;
      statusLine = "favorite context";
    } else if ((seasonAffinity[currentSeason] < -25 || timeAffinity[currentTime] < -25) && roll < 60) {
      currentMood = roll < 30 ? MOOD_SUSPICIOUS : MOOD_SAD;
      statusLine = "not my vibe";
    } else if (roll < 55) {
      currentMood = (Mood)random(0, MOOD_COUNT);
      statusLine = String("auto: ") + moodNames[currentMood];
    }
    speakMoodPhrase(currentMood);
    nextAutonomyMs = now + random(12000, 28000);
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

String voiceIntentFromEvent(String event) {
  int at = event.indexOf("voice:cmd");
  if (at < 0) return "";
  String rest = event.substring(at + 9);
  rest.trim();
  int cut = rest.indexOf(' ');
  if (cut >= 0) rest = rest.substring(0, cut);
  rest.trim();
  return rest;
}

void applyVoiceIntent(String intent) {
  intent.trim();
  intent.toLowerCase();
  if (intent.length() == 0) return;

  asleep = false;
  lastInteractionMs = millis();
  statusLine = "heard: " + intent;

  if (intent == "take_picture" || intent == "take_snapshot" || intent == "look_around" || intent == "scan_room" || intent == "what_do_you_see") {
    currentMood = MOOD_SUSPICIOUS;
    speechLine = "Camera hardware is removed. I am a CYD-only face now.";
  } else if (intent == "tell_joke") {
    currentMood = MOOD_EXCITED;
    speechLine = "Why did the tiny screen get attitude? Because someone gave it a face.";
  } else if (intent == "say_something_rude" || intent == "insult") {
    currentMood = MOOD_ANGRY;
    speechLine = "Fine. Your command was understood, unlike half the decisions in this room.";
  } else if (intent == "be_nice" || intent == "compliment") {
    currentMood = MOOD_LOVE;
    speechLine = "You are doing pretty good. Annoyingly, I mean that.";
  } else if (intent == "be_sarcastic" || intent == "attitude") {
    currentMood = MOOD_SUSPICIOUS;
    speechLine = "Oh absolutely, this is all extremely normal and not suspicious at all.";
  } else if (intent == "be_quiet" || intent == "stop_listening") {
    currentMood = MOOD_SLEEPY;
    speechLine = "Quiet mode. I will judge silently.";
  } else if (intent == "talk_more" || intent == "entertain_me") {
    currentMood = MOOD_EXCITED;
    speechLine = randomPhraseForMood(currentMood);
  } else if (intent == "wake_up") {
    currentMood = MOOD_SURPRISED;
    speechLine = phraseFromList(WAKE_PHRASES, COUNT_OF(WAKE_PHRASES));
  } else if (intent == "go_to_sleep" || intent == "chill" || intent == "relax") {
    currentMood = MOOD_SLEEPY;
    speechLine = "Alright. I am lowering the dramatic lighting.";
  } else if (intent == "happy_mode" || intent == "get_excited" || intent == "dance" || intent == "laugh") {
    currentMood = MOOD_HAPPY;
    speechLine = randomPhraseForMood(MOOD_HAPPY);
  } else if (intent == "sad_mode") {
    currentMood = MOOD_SAD;
    speechLine = randomPhraseForMood(MOOD_SAD);
  } else if (intent == "angry_mode" || intent == "bad_buddy") {
    currentMood = MOOD_ANGRY;
    speechLine = randomPhraseForMood(MOOD_ANGRY);
  } else if (intent == "suspicious_mode") {
    currentMood = MOOD_SUSPICIOUS;
    speechLine = randomPhraseForMood(MOOD_SUSPICIOUS);
  } else if (intent == "love_mode" || intent == "good_buddy" || intent == "thank_you") {
    currentMood = MOOD_LOVE;
    speechLine = randomPhraseForMood(MOOD_LOVE);
  } else if (intent == "remember_me" || intent == "remember_face" || intent == "save_memory") {
    currentMood = MOOD_HAPPY;
    speechLine = "I will remember this. Probably with unnecessary commentary.";
  } else if (intent == "forget_me" || intent == "forget_face" || intent == "clear_memory") {
    currentMood = MOOD_SAD;
    speechLine = "Forgetting things. Very dramatic. Very mysterious.";
  } else if (intent == "connect_wifi" || intent == "use_online" || intent == "use_ollama" || intent == "start_ai") {
    currentMood = MOOD_EXCITED;
    speechLine = "Online brain requested. If WiFi and Ollama are reachable, I get smarter.";
  } else if (intent == "use_offline" || intent == "stop_ai") {
    currentMood = MOOD_NORMAL;
    speechLine = "Offline mode. Local sass only.";
  } else if (intent == "weather" || intent == "time" || intent == "date") {
    currentMood = MOOD_NORMAL;
    speechLine = "That needs the online bridge for a fresh answer.";
  } else if (intent == "battery" || intent == "system_status" || intent == "mood_status" || intent == "status") {
    currentMood = MOOD_NORMAL;
    speechLine = String("Status: mood ") + moodNames[currentMood] + ", SD " + (sdReady ? "ready" : "missing") + ", WiFi " + (WiFi.status() == WL_CONNECTED ? "on" : "off");
  } else if (intent == "who_are_you" || intent == "what_is_name" || intent == "my_name" || intent == "say_name") {
    currentMood = MOOD_HAPPY;
    speechLine = "I am " + buddyName + ". Tiny face, large opinions.";
  } else if (intent == "greet" || intent == "call_me" || intent == "wake_name" || intent == "train_name") {
    currentMood = MOOD_HAPPY;
    speechLine = "My display name is " + buddyName + ". CYD-only mode has no onboard mic.";
  } else if (intent == "take_note" || intent == "read_note" || intent == "learn_this" || intent == "forget_that") {
    currentMood = MOOD_NORMAL;
    speechLine = "Notes and learning need the next memory bridge pass.";
  } else if (intent == "camera_on" || intent == "camera_off" || intent == "mic_on" || intent == "mic_off" || intent == "start_snapshots" || intent == "stop_snapshots") {
    currentMood = MOOD_NORMAL;
    speechLine = "Sensor hardware is disabled in this CYD-only build.";
  } else if (intent == "louder" || intent == "quieter" || intent == "repeat" || intent == "explain") {
    currentMood = MOOD_NORMAL;
    speechLine = "Voice output is waiting on the speaker, but the command is mapped.";
  } else if (intent == "yes" || intent == "no" || intent == "maybe") {
    currentMood = intent == "yes" ? MOOD_HAPPY : intent == "no" ? MOOD_SUSPICIOUS : MOOD_NORMAL;
    speechLine = "Noted. I will pretend this was a democratic process.";
  } else if (intent == "bored" || intent == "sing") {
    currentMood = MOOD_EXCITED;
    speechLine = phraseFromList(BORED_PHRASES, COUNT_OF(BORED_PHRASES));
  } else if (intent == "night_mode" || intent == "dark_eyes") {
    currentMood = MOOD_SLEEPY;
    speechLine = "Night mode. I am becoming dramatically low light.";
  } else if (intent == "day_mode" || intent == "bright_eyes") {
    currentMood = MOOD_HAPPY;
    speechLine = "Day mode. Bright eyes, questionable judgement.";
  } else if (intent == "random_mood") {
    currentMood = (Mood)random(0, MOOD_COUNT);
    speechLine = randomPhraseForMood(currentMood);
  } else if (intent == "manual_mode") {
    setAutoMode(false);
    speechLine = "Manual mode. Temporarily obedient.";
  } else if (intent == "auto_mode") {
    setAutoMode(true);
    speechLine = "Auto mode. Opinions restored.";
  } else if (intent == "help" || intent == "menu" || intent == "settings") {
    currentMood = MOOD_NORMAL;
    speechLine = "Use the corner menus for settings. Voice menus are mapped now.";
  } else if (intent == "pair_cyd") {
    currentMood = MOOD_EXCITED;
    speechLine = "No second board to pair. I am self-contained now.";
  } else if (intent == "see_me" || intent == "identify_me") {
    currentMood = MOOD_SUSPICIOUS;
    speechLine = "I cannot see people without the removed camera board.";
  } else if (intent == "hear_sound" || intent == "noise_status") {
    currentMood = MOOD_NORMAL;
    speechLine = "CYD-only mode has no microphone. Touch is my local input.";
  } else {
    currentMood = MOOD_NORMAL;
    speechLine = "I heard " + intent + ". I do not have a better comeback yet.";
  }

  speechScroll = 0;
  startBlink(false);
}

void applyEvent(String event) {
  event.trim();
  event.toLowerCase();
  if (event.length() == 0) return;

  lastEvent = event;
  lastMoodAuto = millis();

  String voiceIntent = voiceIntentFromEvent(event);
  if (voiceIntent.length() > 0) {
    applyVoiceIntent(voiceIntent);
  } else if (event.indexOf("person:") >= 0) {
    int sep = event.indexOf("person:");
    String name = event.substring(sep + 7);
    name.trim();
    currentMood = MOOD_HAPPY;
    statusLine = "recognized: " + name;
    speechLine = "Hi " + name + ". The bridge says you are here.";
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
    speechLine = "External vision says it got dark. Dramatic night mode it is.";
    startBlink(false);
  } else if (event.indexOf("vision:busy") >= 0) {
    currentMood = MOOD_EXCITED;
    statusLine = "vision: busy";
    speechLine = "External vision says there is a lot happening.";
  } else if (event.indexOf("vision:motion") >= 0) {
    currentMood = MOOD_SURPRISED;
    statusLine = "vision: motion";
    speechLine = "The bridge says something moved.";
    startBlink(false);
  } else if (event.indexOf("voice:wake") >= 0 || event.indexOf("wake:name") >= 0) {
    currentMood = MOOD_HAPPY;
    statusLine = "wake word";
    speechLine = "Yeah? I heard my name.";
    asleep = false;
    startBlink(false);
  } else if (event.indexOf("voice:speech") >= 0 || event.indexOf("speech") >= 0) {
    currentMood = MOOD_NORMAL;
    statusLine = "voice";
    speechLine = "Speech event received. CYD-only mode needs an external listener.";
  } else if (event.indexOf("face") >= 0 || event.indexOf("person") >= 0 || event.indexOf("motion") >= 0) {
    currentMood = MOOD_SURPRISED;
    statusLine = "external: " + event;
    speechLine = "External event received.";
    startBlink(false);
  } else if (event.indexOf("sound:loud") >= 0 || event.indexOf("noise") >= 0 || event.indexOf("clap") >= 0) {
    currentMood = MOOD_SURPRISED;
    statusLine = "heard: " + event;
    speechLine = "External audio event received.";
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
  lastRawTouchX = p.x;
  lastRawTouchY = p.y;
  x = map(p.x, touchXMin, touchXMax, 0, screenW);
  y = map(p.y, touchYMin, touchYMax, 0, screenH);
  x = constrain(x, 0, screenW - 1);
  y = constrain(y, 0, screenH - 1);
  if (TOUCH_SWAP_XY) {
    int t = x; x = y; y = t;
  }
  if (TOUCH_FLIP_X) x = screenW - 1 - x;
  if (TOUCH_FLIP_Y) y = screenH - 1 - y;
  return true;
}

bool readRawTouch(int& x, int& y) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  x = p.x;
  y = p.y;
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
  learnActivityPreference(3, -2);
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
  learnActivityPreference(2, 3);
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
  learnActivityPreference(1, 3);
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
  learnActivityPreference(0, 2);
  currentMood = random(0, 4) == 0 ? MOOD_SURPRISED : MOOD_HAPPY;
  statusLine = "boop";
  speechLine = phraseFromList(BOOP_PHRASES, COUNT_OF(BOOP_PHRASES));
  targetGazeX = random(-8, 9);
  targetGazeY = -8;
  if (random(0, 3) == 0) startBlink(true, random(0, 2) == 0);
  else startBlink(false);
  speechScroll = 0;
}

void reactToSwipe(int dx, int dy) {
  markInteraction();
  learnActivityPreference(4, 2);
  if (abs(dx) > abs(dy)) {
    if (dx < 0) {
      buddySwipeLeftCount++;
      currentMood = MOOD_SUSPICIOUS;
      speechLine = "Swipe left detected. I am dodging dramatically.";
      targetGazeX = -18;
    } else {
      buddySwipeRightCount++;
      currentMood = MOOD_HAPPY;
      speechLine = "Swipe right. Fine, that felt like progress.";
      targetGazeX = 18;
    }
  } else {
    if (dy < 0) {
      buddySwipeUpCount++;
      currentMood = MOOD_SURPRISED;
      speechLine = "Swipe up. My tiny soul just levitated.";
      targetGazeY = -14;
    } else {
      buddySwipeDownCount++;
      currentMood = MOOD_SLEEPY;
      speechLine = "Swipe down. Understandable. I also want to nap.";
      targetGazeY = 14;
    }
  }
  buddyPlayNeed = max(0, buddyPlayNeed - 8);
  buddyRestless = max(0, buddyRestless - 5);
  strengthenBuddy(1);
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
  else if (mode == MENU_SYSTEM_TIME) statusLine = "time menu";
  else if (mode == MENU_STATS) statusLine = "stats menu";
  else statusLine = "submenu";
}

void backMenu() {
  if (menuMode == MENU_SYSTEM_AI || menuMode == MENU_SYSTEM_WIFI || menuMode == MENU_SYSTEM_PHRASES || menuMode == MENU_SYSTEM_TIME || menuMode == MENU_STATS) {
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

bool wifiFieldHit(int tx, int ty, WifiEditField field) {
  int y = field == WIFI_EDIT_SSID ? 28 : 54;
  return tx >= 8 && tx <= screenW - 8 && ty >= y && ty <= y + 22;
}

int wifiKeyboardTop() {
  return max(82, screenH - 150);
}

bool handleWifiKeyRowTap(int tx, int ty, const char* keys, int row, int columns = 10) {
  int top = wifiKeyboardTop();
  int rowH = 22;
  int gap = 2;
  int y = top + row * (rowH + gap);
  if (ty < y || ty > y + rowH) return false;
  int keyW = max(18, (screenW - 12) / columns);
  int count = strlen(keys);
  for (int i = 0; i < count; i++) {
    int x = 6 + i * keyW;
    if (tx >= x && tx <= x + keyW - gap) {
      appendWifiEditChar(keys[i]);
      return true;
    }
  }
  return true;
}

bool handleWifiActionTap(int tx, int ty, int row, const char* a, const char* b, const char* c) {
  int top = wifiKeyboardTop();
  int rowH = 22;
  int gap = 2;
  int y = top + row * (rowH + gap);
  if (ty < y || ty > y + rowH) return false;
  int buttonW = (screenW - 16) / 3;
  int idx = constrain((tx - 6) / buttonW, 0, 2);
  const char* label = idx == 0 ? a : idx == 1 ? b : c;
  if (strcmp(label, "FIELD") == 0 || strcmp(label, "SSID") == 0 || strcmp(label, "PASS") == 0) {
    wifiEditField = wifiEditField == WIFI_EDIT_SSID ? WIFI_EDIT_PASS : WIFI_EDIT_SSID;
  } else if (strcmp(label, "SHIFT") == 0) {
    wifiEditShift = !wifiEditShift;
  } else if (strcmp(label, "SPACE") == 0) {
    appendWifiEditChar(' ');
  } else if (strcmp(label, "DEL") == 0) {
    deleteWifiEditChar();
  } else if (strcmp(label, "SAVE") == 0) {
    closeWifiEditor(true);
  } else if (strcmp(label, "CANCEL") == 0) {
    closeWifiEditor(false);
  }
  return true;
}

bool handleWifiScanActionTap(int tx, int ty) {
  int rowH = 24;
  int y = screenH - 34;
  if (ty < y || ty > y + rowH) return false;
  int buttonW = (screenW - 16) / 3;
  int idx = constrain((tx - 6) / buttonW, 0, 2);
  if (idx == 0) {
    int pages = max(1, (wifiScanCount + WIFI_SCAN_ROWS - 1) / WIFI_SCAN_ROWS);
    wifiScanPage = (wifiScanPage + 1) % pages;
  } else if (idx == 1) {
    scanWifiNetworks();
  } else {
    closeWifiEditor(false);
  }
  return true;
}

void handleWifiScanTap(int tx, int ty) {
  lastTouchMs = millis();
  lastMoodAuto = lastTouchMs;
  if (handleWifiScanActionTap(tx, ty)) return;

  int rowTop = 52;
  int rowH = 30;
  int start = wifiScanPage * WIFI_SCAN_ROWS;
  for (int i = 0; i < WIFI_SCAN_ROWS; i++) {
    int idx = start + i;
    int y = rowTop + i * rowH;
    if (ty >= y && ty <= y + rowH - 4 && tx >= 8 && tx <= screenW - 8) {
      if (idx < wifiScanCount) {
        openWifiPasswordEditor(wifiScanSsid[idx]);
      } else if (idx == wifiScanCount) {
        openWifiPasswordEditor("", false);
        wifiEditField = WIFI_EDIT_SSID;
        speechLine = "Type the hidden WiFi name, then password.";
        speechScroll = 0;
      }
      return;
    }
  }
}

void handleWifiEditorTap(int tx, int ty) {
  if (wifiScanListActive) {
    handleWifiScanTap(tx, ty);
    return;
  }

  lastTouchMs = millis();
  lastMoodAuto = lastTouchMs;

  if (wifiFieldHit(tx, ty, WIFI_EDIT_SSID)) {
    wifiEditField = WIFI_EDIT_SSID;
    return;
  }
  if (wifiFieldHit(tx, ty, WIFI_EDIT_PASS)) {
    wifiEditField = WIFI_EDIT_PASS;
    return;
  }

  if (handleWifiKeyRowTap(tx, ty, "1234567890", 0)) return;
  if (handleWifiKeyRowTap(tx, ty, "qwertyuiop", 1)) return;
  if (handleWifiKeyRowTap(tx, ty, "asdfghjkl", 2)) return;
  if (handleWifiKeyRowTap(tx, ty, "zxcvbnm.-_", 3)) return;
  if (handleWifiActionTap(tx, ty, 4, "FIELD", "SHIFT", "SPACE")) return;
  if (handleWifiActionTap(tx, ty, 5, "DEL", "SAVE", "CANCEL")) return;
}

void openStatsView(int page = 0) {
  statsViewActive = true;
  statsViewPage = constrain(page, 0, 4);
  menuMode = MENU_NONE;
  statusLine = "stats";
}

void openTimeEditor() {
  timeEditorActive = true;
  scheduleEditorActive = false;
  timeEditField = 0;
  menuMode = MENU_NONE;
  statusLine = "time setup";
  speechLine = "Set date, time, timezone, DST, and clock format.";
  speechScroll = 0;
}

void openScheduleEditor() {
  scheduleEditorActive = true;
  timeEditorActive = false;
  scheduleEditField = 0;
  menuMode = MENU_NONE;
  statusLine = "schedule";
  speechLine = "Adjust my wake and sleep schedule.";
  speechScroll = 0;
}

void openTouchCalibration() {
  touchCalActive = true;
  touchCalStep = 0;
  menuMode = MENU_NONE;
  statusLine = "touch cal";
  speechLine = "Tap the target dots.";
  speechScroll = 0;
}

void adjustTimeField(int delta) {
  int m = minuteOfDay();
  int h = m / 60;
  int minPart = m % 60;
  if (timeEditField == 0) dateYear = constrain(dateYear + delta, 2024, 2099);
  else if (timeEditField == 1) dateMonth = constrain(dateMonth + delta, 1, 12);
  else if (timeEditField == 2) dateDay = constrain(dateDay + delta, 1, 31);
  else if (timeEditField == 3) h = (h + delta + 24) % 24;
  else if (timeEditField == 4) minPart = (minPart + delta + 60) % 60;
  else if (timeEditField == 5) timezoneOffsetMinutes = constrain(timezoneOffsetMinutes + delta * 30, -720, 840);
  else if (timeEditField == 6 && delta != 0) daylightSavings = !daylightSavings;
  else if (timeEditField == 7 && delta != 0) clock24Hour = !clock24Hour;
  bootMinuteOfDay = h * 60 + minPart;
  clockSetAtMs = millis();
  infoLineCache = "";
  setUnixBaseFromClock();
  saveTimeSettings();
}

int& scheduleFieldRef(int i) {
  if (i == 0) return earlyMorningStart;
  if (i == 1) return morningStart;
  if (i == 2) return daytimeStart;
  if (i == 3) return lateAfternoonStart;
  if (i == 4) return nightStart;
  return lateNightStart;
}

String scheduleFieldLabel(int i) {
  if (i == 0) return "Early AM " + hhmmFromMinutes(earlyMorningStart);
  if (i == 1) return "Morning " + hhmmFromMinutes(morningStart);
  if (i == 2) return "Daytime " + hhmmFromMinutes(daytimeStart);
  if (i == 3) return "Late PM " + hhmmFromMinutes(lateAfternoonStart);
  if (i == 4) return "Night " + hhmmFromMinutes(nightStart);
  return "Late night " + hhmmFromMinutes(lateNightStart);
}

void adjustScheduleField(int delta) {
  int& value = scheduleFieldRef(scheduleEditField);
  value = (value + delta * 30 + 1440) % 1440;
  saveTimeSettings();
  infoLineCache = "";
}

void handleTimeEditorTap(int tx, int ty) {
  lastTouchMs = millis();
  int rowTop = 42;
  int rowH = 20;
  for (int i = 0; i < 8; i++) {
    int y = rowTop + i * rowH;
    if (ty >= y && ty <= y + rowH - 2) {
      timeEditField = i;
      if (tx < screenW / 3) adjustTimeField(-1);
      else if (tx > screenW * 2 / 3) adjustTimeField(1);
      return;
    }
  }
  if (ty > screenH - 34) {
    if (tx < screenW / 2) {
      timeEditorActive = false;
      statusLine = "time saved";
      speechLine = "Time settings saved.";
    } else {
      timeEditField = (timeEditField + 1) % 8;
      speechLine = "Time field changed.";
    }
    speechScroll = 0;
  }
}

void handleScheduleEditorTap(int tx, int ty) {
  lastTouchMs = millis();
  int rowTop = 52;
  int rowH = 24;
  for (int i = 0; i < 6; i++) {
    int y = rowTop + i * rowH;
    if (ty >= y && ty <= y + rowH - 3) {
      scheduleEditField = i;
      if (tx < screenW / 3) adjustScheduleField(-1);
      else if (tx > screenW * 2 / 3) adjustScheduleField(1);
      return;
    }
  }
  if (ty > screenH - 34) {
    if (tx < screenW / 2) {
      scheduleEditorActive = false;
      statusLine = "schedule saved";
      speechLine = "Schedule saved.";
    } else {
      scheduleEditField = (scheduleEditField + 1) % 6;
      speechLine = "Schedule field changed.";
    }
    speechScroll = 0;
  }
}

void handleStatsTap(int tx, int ty) {
  lastTouchMs = millis();
  if (ty > screenH - 38) {
    if (tx < screenW / 3) statsViewPage = (statsViewPage + 4) % 5;
    else if (tx > screenW * 2 / 3) statsViewActive = false;
    else statsViewPage = (statsViewPage + 1) % 5;
  } else {
    statsViewPage = (statsViewPage + 1) % 5;
  }
}

void handleTouchCalibrationTap() {
  touchCalRawX[touchCalStep] = lastRawTouchX;
  touchCalRawY[touchCalStep] = lastRawTouchY;
  touchCalStep++;
  if (touchCalStep >= 2) {
    touchXMin = min(touchCalRawX[0], touchCalRawX[1]);
    touchXMax = max(touchCalRawX[0], touchCalRawX[1]);
    touchYMin = min(touchCalRawY[0], touchCalRawY[1]);
    touchYMax = max(touchCalRawY[0], touchCalRawY[1]);
    saveTouchCalibration();
    touchCalActive = false;
    speechLine = "Touch calibration saved.";
    speechScroll = 0;
  } else {
    speechLine = "Now tap the opposite target.";
    speechScroll = 0;
  }
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
    if (item == 0) openMenu(MENU_SYSTEM_AI);
    else if (item == 1) openMenu(MENU_SYSTEM_WIFI);
    else if (item == 2) openMenu(MENU_SYSTEM_TIME);
    else if (item == 3) openMenu(MENU_STATS);
  } else if (menuMode == MENU_SYSTEM_AI) {
    if (item == 0) {
      speechLine = "My name is " + buddyName + ". Use serial: name <new name>.";
    } else if (item == 1) {
      cyclePersonality();
    } else if (item == 2) {
      saveSpeechScroll(speechScrollMs <= 80 ? 180 : speechScrollMs <= 180 ? 280 : 80);
    } else if (item == 3) {
      openMenu(MENU_SYSTEM_PHRASES);
    }
  } else if (menuMode == MENU_SYSTEM_WIFI) {
    if (item == 0) {
      openWifiEditor();
    } else if (item == 1) {
      if (WiFi.status() == WL_CONNECTED) {
        speechLine = "WiFi: " + WiFi.localIP().toString() + ". Weather: " + weatherSummary + ".";
      } else {
        connectWifi();
      }
    } else if (item == 2) {
      syncNetworkTime();
      speechLine = "Time sync requested.";
    } else if (item == 3) {
      if (weatherConfigured) updateWeatherNow(true);
      else speechLine = "Set weather loc over serial first.";
    }
  } else if (menuMode == MENU_SYSTEM_TIME) {
    if (item == 0) openTimeEditor();
    else if (item == 1) openScheduleEditor();
    else if (item == 2) openTouchCalibration();
    else if (item == 3) {
      syncNetworkTime();
      speechLine = WiFi.status() == WL_CONNECTED ? "Time sync requested." : "Connect WiFi before time sync.";
    }
  } else if (menuMode == MENU_STATS) {
    if (item == 0) openStatsView(0);
    else if (item == 1) openStatsView(1);
    else if (item == 2) openStatsView(2);
    else if (item == 3) openStatsView(3);
  } else if (menuMode == MENU_SYSTEM_PHRASES) {
    if (item == 0) {
      speechLine = sdReady ? "Phrase bank ready at /cydbuddy/phrases.csv." : "SD card not mounted.";
    } else if (item == 1) {
      speakMoodPhrase(currentMood);
    } else if (item == 2) {
      bool ok = seedPhraseBank(true, 50, 100);
      speechLine = ok ? "Added 100 generated phrases per mood/personality." : "Phrase expansion failed.";
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
      manualMoodHoldUntil = millis() + 10UL * 60UL * 1000UL;
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

  if (!wifiEditorActive && !timeEditorActive && !scheduleEditorActive && !statsViewActive && !touchCalActive && down && !longTouchHandled && now - touchStarted > 850) {
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
    if (touchCalActive) {
      handleTouchCalibrationTap();
    } else if (scheduleEditorActive) {
      handleScheduleEditorTap(lastTouchX, lastTouchY);
    } else if (timeEditorActive) {
      handleTimeEditorTap(lastTouchX, lastTouchY);
    } else if (statsViewActive) {
      handleStatsTap(lastTouchX, lastTouchY);
    } else if (wifiEditorActive) {
      handleWifiEditorTap(lastTouchX, lastTouchY);
    } else if (menuMode != MENU_NONE) {
      int item = menuItemAt(lastTouchX, lastTouchY);
      handleMenuItem(item);
    } else if (isUpperLeftHotspot(lastTouchX, lastTouchY)) {
      openMenu(MENU_SYSTEM);
    } else if (isUpperRightHotspot(lastTouchX, lastTouchY)) {
      openMenu(MENU_FACE);
    } else if (touchMoveMax > 54 && (abs(lastTouchX - touchStartX) > 42 || abs(lastTouchY - touchStartY) > 42) &&
               max(abs(lastTouchX - touchStartX), abs(lastTouchY - touchStartY)) > min(abs(lastTouchX - touchStartX), abs(lastTouchY - touchStartY)) * 2) {
      reactToSwipe(lastTouchX - touchStartX, lastTouchY - touchStartY);
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
        manualMoodHoldUntil = millis() + 10UL * 60UL * 1000UL;
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
    manualMoodHoldUntil = millis() + 10UL * 60UL * 1000UL;
  } else if (lower == "auto") {
    setAutoMode(true);
  } else if (lower == "manual") {
    setAutoMode(false);
  } else if (lower == "speak") {
    speakMoodPhrase(currentMood);
    Serial.printf("speech mood=%s text=\"%s\"\n", moodNames[currentMood], speechLine.c_str());
  } else if (lower == "lively" || lower == "talk more") {
    asleep = false;
    nextChatterMs = 1;
    livelyChatter(millis() - lastInteractionMs);
    Serial.printf("lively mood=%s next_chatter_ms=%lu text=\"%s\"\n", moodNames[currentMood], nextChatterMs, speechLine.c_str());
  } else if (lower == "tap") {
    if (autoMode) handleAutoTap();
    else {
      markInteraction();
      currentMood = (Mood)((currentMood + 1) % MOOD_COUNT);
      statusLine = String("manual: ") + moodNames[currentMood];
      manualMoodHoldUntil = millis() + 10UL * 60UL * 1000UL;
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
  } else if (lower == "sleep" || lower == "nap") {
    currentMood = MOOD_SLEEPY;
    speechLine = "Sleeping. Wake me if something actually happens.";
    statusLine = "asleep";
    asleep = true;
    manualMoodHoldUntil = millis() + 10UL * 60UL * 1000UL;
    speechScroll = 0;
    Serial.println("sleep=ok");
  } else if (lower.startsWith("poke")) {
    bool left = lower.indexOf("right") < 0;
    bool right = lower.indexOf("left") < 0;
    reactToEyePoke(left, right);
  } else if (lower == "feed") {
    feedBuddy();
    Serial.printf("feed status=%s health=%s hunger=%d feeds_today=%d gain_today=%d.%d\n",
                  statusLine.c_str(), healthLabel().c_str(), buddyHunger, dailyFeedCount,
                  dailyHealthGainTenth / 10, abs(dailyHealthGainTenth % 10));
  } else if (lower == "play") {
    playWithBuddy();
    Serial.printf("play status=%s health=%s play=%d restless=%d plays_today=%d gain_today=%d.%d\n",
                  statusLine.c_str(), healthLabel().c_str(), buddyPlayNeed, buddyRestless, dailyPlayCount,
                  dailyHealthGainTenth / 10, abs(dailyHealthGainTenth % 10));
  } else if (lower == "boost" || lower == "weekly boost") {
    weeklyBoostBuddy();
    Serial.printf("boost health=%s week=%d last_boost_week=%d\n", healthLabel().c_str(), unixWeekNumber(currentUnixEstimate()), lastBoostWeek);
  } else if (lower == "calm" || lower == "calm down") {
    calmBuddyCare(false);
    Serial.printf("calm health=%s hunger=%d play=%d restless=%d anxious=%d mood=%s\n",
                  healthLabel().c_str(), buddyHunger, buddyPlayNeed, buddyRestless, buddyAnxiety, moodNames[currentMood]);
  } else if (lower == "care reset" || lower == "reset care" || lower == "unstick angry") {
    calmBuddyCare(true);
    Serial.printf("care_reset health=%s hunger=%d play=%d restless=%d anxious=%d mood=%s\n",
                  healthLabel().c_str(), buddyHunger, buddyPlayNeed, buddyRestless, buddyAnxiety, moodNames[currentMood]);
  } else if (lower == "health" || lower == "care") {
    resetDailyCareIfNeeded(currentUnixEstimate());
    Serial.printf("care health=%s hunger=%d play=%d restless=%d anxious=%d strength=%d armor=%d feeds_today=%d plays_today=%d gain_today=%d.%d missed_feeds=%lu day=%d week=%d\n",
                  healthLabel().c_str(), buddyHunger, buddyPlayNeed, buddyRestless, buddyAnxiety,
                  buddyStrength, buddyArmor, dailyFeedCount, dailyPlayCount,
                  dailyHealthGainTenth / 10, abs(dailyHealthGainTenth % 10),
                  missedFeedings, lastCareDay, unixWeekNumber(currentUnixEstimate()));
  } else if (lower == "calendar" || lower == "date status" || lower == "time status") {
    String holiday = holidayName(dateYear, dateMonth, dateDay);
    Serial.printf("calendar date=%04d-%02d-%02d minute=%d tz=%s dst=%s season=%s holiday=%s unix=%lu context=\"%s\"\n",
                  dateYear, dateMonth, dateDay, minuteOfDay(), timezoneLabel().c_str(), daylightSavings ? "on" : "off",
                  seasonNames[currentSeasonIndex()], holiday.length() ? holiday.c_str() : "none",
                  (unsigned long)min(currentUnixEstimate(), (uint64_t)4294967295ULL), calendarContextLine().c_str());
    speechLine = calendarContextLine();
    speechScroll = 0;
  } else if (lower == "preferences" || lower == "prefs" || lower == "memory bank") {
    int s = bestPreferenceIndex(seasonAffinity, SEASON_COUNT);
    int mo = bestPreferenceIndex(monthAffinity, MONTH_COUNT);
    int ti = bestPreferenceIndex(timeAffinity, TIME_PREF_COUNT);
    int ac = bestPreferenceIndex(activityAffinity, ACTIVITY_COUNT);
    Serial.printf("preferences current_season=%s favorite_season=%s(%d) favorite_month=%s(%d) favorite_time=%s(%d) favorite_activity=%s(%d) learned=%lu revisions=%lu weather=%s\n",
                  seasonNames[currentSeasonIndex()], seasonNames[s], seasonAffinity[s], monthName(mo + 1).c_str(), monthAffinity[mo],
                  timePreferenceNames[ti], timeAffinity[ti], activityNames[ac], activityAffinity[ac],
                  preferenceLearnCount, memoryRevisionCount, weatherMoodWord().c_str());
    for (int i = 0; i < MEMORY_BANK_COUNT; i++) {
      Serial.printf("memory%d=%s\n", i, buddyMemoryBank[i].c_str());
    }
    speechLine = preferenceSummaryLine();
    speechScroll = 0;
  } else if (lower.startsWith("memory add ")) {
    rememberBuddyThought(line.substring(11));
    speechLine = "Memory saved: " + randomMemoryLine();
    speechScroll = 0;
    statusLine = "memory saved";
    saveBuddyMemory(true);
    Serial.printf("memory_added revisions=%lu\n", memoryRevisionCount);
  } else if (lower == "memory think" || lower == "preference think") {
    learnCurrentContext(3);
    learnWeatherPreference();
    rememberBuddyThought(preferenceSummaryLine());
    speechLine = randomMemoryLine();
    speechScroll = 0;
    saveBuddyMemory(true);
    Serial.printf("memory_thought=%s\n", speechLine.c_str());
  } else if (lower == "preference seed" || lower == "memory phrase seed") {
    bool ok = seedPreferencePhrases(200);
    speechLine = ok ? "Added 200 preference-memory phrases per personality." : "Preference phrase seed failed.";
    speechScroll = 0;
    Serial.printf("preference_seed=%s rows=%d\n", ok ? "ok" : "failed", ok ? PERSONALITY_COUNT * 200 : 0);
  } else if (lower.startsWith("prefer ") || lower.startsWith("like ")) {
    int firstSpace = lower.indexOf(' ');
    String rest = lower.substring(firstSpace + 1);
    int sep = rest.indexOf(' ');
    bool ok = false;
    if (sep > 0) ok = applyPreferenceCommand(rest.substring(0, sep), rest.substring(sep + 1), 8);
    Serial.printf("preference=%s\n", ok ? "learned" : "failed");
  } else if (lower.startsWith("dislike ") || lower.startsWith("hate ")) {
    int firstSpace = lower.indexOf(' ');
    String rest = lower.substring(firstSpace + 1);
    int sep = rest.indexOf(' ');
    bool ok = false;
    if (sep > 0) ok = applyPreferenceCommand(rest.substring(0, sep), rest.substring(sep + 1), -8);
    Serial.printf("preference=%s\n", ok ? "learned" : "failed");
  } else if (lower.startsWith("bt seen ") || lower.startsWith("bluetooth seen ")) {
    String name = lower.startsWith("bt seen ") ? line.substring(8) : line.substring(15);
    bool added = rememberSeenBluetooth(name);
    Serial.printf("bluetooth_seen=%s armor=%d new_bt=%lu\n", added ? "new" : "known", buddyArmor, buddyNewBluetoothCount);
  } else if (lower == "time edit" || lower == "date edit") {
    openTimeEditor();
    Serial.println("time_editor=open");
  } else if (lower.startsWith("time ")) {
    String value = lower.substring(5);
    value.trim();
    if (value == "sync") {
      syncNetworkTime();
      Serial.printf("time_sync minute=%d date=%04d-%02d-%02d season=%s wifi=%s\n",
                    minuteOfDay(), dateYear, dateMonth, dateDay, seasonNames[currentSeasonIndex()],
                    WiFi.status() == WL_CONNECTED ? "connected" : "offline");
    } else if (value == "24") {
      clock24Hour = true;
      saveTimeSettings();
      Serial.println("clock=24");
    } else if (value == "12") {
      clock24Hour = false;
      saveTimeSettings();
      Serial.println("clock=12");
    } else {
      bool ok = setClockFromText(value);
      if (ok) {
        saveTimeSettings();
        settleDeadTimeFromClock(true);
      }
      Serial.printf("time_set=%s minute=%d\n", ok ? "ok" : "failed", minuteOfDay());
    }
  } else if (lower.startsWith("date ")) {
    bool ok = setDateFromText(lower.substring(5));
    if (ok) settleDeadTimeFromClock(true);
    Serial.printf("date_set=%s date=%04d-%02d-%02d\n", ok ? "ok" : "failed", dateYear, dateMonth, dateDay);
  } else if (lower.startsWith("timezone ")) {
    String tz = lower.substring(9);
    timezoneOffsetMinutes = constrain((int)(tz.toFloat() * 60.0f), -720, 840);
    setUnixBaseFromClock();
    saveTimeSettings();
    Serial.printf("timezone=%s minutes=%d\n", timezoneLabel().c_str(), timezoneOffsetMinutes);
  } else if (lower == "dst on" || lower == "daylight savings on") {
    daylightSavings = true;
    setUnixBaseFromClock();
    saveTimeSettings();
    Serial.println("dst=on");
  } else if (lower == "dst off" || lower == "daylight savings off") {
    daylightSavings = false;
    setUnixBaseFromClock();
    saveTimeSettings();
    Serial.println("dst=off");
  } else if (lower == "clock 24") {
    clock24Hour = true;
    saveTimeSettings();
    Serial.println("clock=24");
  } else if (lower == "clock 12") {
    clock24Hour = false;
    saveTimeSettings();
    Serial.println("clock=12");
  } else if (lower == "schedule edit" || lower == "time schedule") {
    openScheduleEditor();
    Serial.println("schedule_editor=open");
  } else if (lower == "schedule") {
    Serial.printf("schedule early=%s morning=%s daytime=%s latepm=%s night=%s latenight=%s\n",
                  hhmmFromMinutes(earlyMorningStart).c_str(),
                  hhmmFromMinutes(morningStart).c_str(),
                  hhmmFromMinutes(daytimeStart).c_str(),
                  hhmmFromMinutes(lateAfternoonStart).c_str(),
                  hhmmFromMinutes(nightStart).c_str(),
                  hhmmFromMinutes(lateNightStart).c_str());
  } else if (lower.startsWith("schedule ")) {
    String rest = lower.substring(9);
    rest.trim();
    int sep = rest.indexOf(' ');
    if (sep < 0) {
      Serial.printf("schedule early=%s morning=%s daytime=%s latepm=%s night=%s latenight=%s\n",
                    hhmmFromMinutes(earlyMorningStart).c_str(),
                    hhmmFromMinutes(morningStart).c_str(),
                    hhmmFromMinutes(daytimeStart).c_str(),
                    hhmmFromMinutes(lateAfternoonStart).c_str(),
                    hhmmFromMinutes(nightStart).c_str(),
                    hhmmFromMinutes(lateNightStart).c_str());
    } else {
      String name = rest.substring(0, sep);
      String value = rest.substring(sep + 1);
      int parsed = 0;
      bool ok = parseHHMM(value, parsed);
      if (ok) {
        if (name == "early" || name == "earlyam") earlyMorningStart = parsed;
        else if (name == "morning") morningStart = parsed;
        else if (name == "day" || name == "daytime") daytimeStart = parsed;
        else if (name == "latepm" || name == "afternoon") lateAfternoonStart = parsed;
        else if (name == "night") nightStart = parsed;
        else if (name == "latenight" || name == "late") lateNightStart = parsed;
        else ok = false;
      }
      if (ok) saveTimeSettings();
      Serial.printf("schedule_set=%s\n", ok ? "ok" : "failed");
    }
  } else if (lower == "lifecycle" || lower == "life clock" || lower == "dead time") {
    saveLifecycle(true);
    Serial.printf("lifecycle boots=%lu deaths=%lu alive=%s session=%s dead=%s last_unix=%llu\n",
                  (unsigned long)bootCount,
                  (unsigned long)deathCount,
                  formatDuration(totalAliveSeconds + (millis() - lifecycleBootMs) / 1000ULL).c_str(),
                  formatDuration((millis() - lifecycleBootMs) / 1000ULL).c_str(),
                  formatDuration(totalDeadSeconds).c_str(),
                  (unsigned long long)lastKnownUnix);
  } else if (lower == "touch cal" || lower == "touch calibrate") {
    openTouchCalibration();
    Serial.println("touch_cal=open");
  } else if (lower == "touch reset") {
    resetTouchCalibration();
    Serial.println("touch_cal=reset");
  } else if (lower == "life" || lower == "stats life") {
    openStatsView(0);
    Serial.printf("life health=%s strength=%d armor=%d hunger=%d play=%d restless=%d anxious=%d feeds_today=%d plays_today=%d gain_today=%d.%d missed_feeds=%lu touches=%lu pokes=%lu tickles=%lu new_wifi=%lu new_bt=%lu\n",
                  healthLabel().c_str(), buddyStrength, buddyArmor, buddyHunger, buddyPlayNeed, buddyRestless, buddyAnxiety,
                  dailyFeedCount, dailyPlayCount, dailyHealthGainTenth / 10, abs(dailyHealthGainTenth % 10), missedFeedings,
                  buddyTouchCount, buddyEyePokeCount, buddyTickleCount, buddyNewNetworkCount, buddyNewBluetoothCount);
  } else if (lower == "stats system") {
    openStatsView(1);
    Serial.printf("system heap=%u uptime=%lu sd=%s wifi=%s\n", ESP.getFreeHeap(), millis() / 1000UL, sdReady ? "ready" : "missing", WiFi.status() == WL_CONNECTED ? "connected" : "offline");
  } else if (lower == "memory") {
    Serial.printf("memory touches=%lu eye_pokes=%lu tickles=%lu bored=%lu swipes=%lu/%lu/%lu/%lu new_wifi=%lu new_bt=%lu strength=%d armor=%d health=%s hunger=%d play=%d restless=%d anxious=%d feeds_today=%d plays_today=%d missed_feeds=%lu minute=%d custom_eye=%s custom_pupil=%s\n",
                  buddyTouchCount, buddyEyePokeCount, buddyTickleCount, buddyBoredCount,
                  buddySwipeLeftCount, buddySwipeRightCount, buddySwipeUpCount, buddySwipeDownCount,
                  buddyNewNetworkCount, buddyNewBluetoothCount, buddyStrength, buddyArmor, healthLabel().c_str(), buddyHunger, buddyPlayNeed, buddyRestless, buddyAnxiety,
                  dailyFeedCount, dailyPlayCount, missedFeedings,
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
  } else if (lower.startsWith("name ")) {
    saveBuddyName(line.substring(5));
    Serial.printf("name=saved %s\n", buddyName.c_str());
  } else if (lower == "train name" || lower == "voice train" || lower == "wake train") {
    speechLine = "CYD-only mode has no microphone. Name saved as " + buddyName + ".";
    speechScroll = 0;
  } else if (lower.startsWith("scroll speed ")) {
    String speed = lower.substring(13);
    speed.trim();
    if (speed == "fast") saveSpeechScroll(80);
    else if (speed == "normal") saveSpeechScroll(140);
    else if (speed == "slow") saveSpeechScroll(280);
    else saveSpeechScroll((unsigned long)constrain((int)speed.toInt(), 50, 600));
    Serial.printf("scroll_ms=%lu\n", speechScrollMs);
  } else if (lower == "personality") {
    Serial.printf("personality=%s\n", personalityNames[currentPersonality]);
    speechLine = "Personality: " + String(personalityNames[currentPersonality]) + ".";
    speechScroll = 0;
  } else if (lower == "personality next") {
    cyclePersonality();
    Serial.printf("personality=%s\n", personalityNames[currentPersonality]);
  } else if (lower.startsWith("personality ")) {
    String name = lower.substring(12);
    Personality next = personalityFromName(name);
    savePersonality(next);
    Serial.printf("personality=%s\n", personalityNames[currentPersonality]);
  } else if (lower == "menu system") {
    openMenu(MENU_SYSTEM);
  } else if (lower.startsWith("menu item ")) {
    int item = lower.substring(10).toInt();
    handleMenuItem(item);
    Serial.printf("menu_item=%d menu_mode=%d status=%s speech=%s\n", item, (int)menuMode, statusLine.c_str(), speechLine.c_str());
  } else if (lower == "menu face") {
    openMenu(MENU_FACE);
  } else if (lower == "menu close") {
    menuMode = MENU_NONE;
    statusLine = "menu closed";
  } else if (lower == "diag" || lower == "diagnostics") {
    Serial.printf("diag frame=%s size=%dx%d rotation=%d mood=%s mode=%s menu=%d overlays wifi=%s time=%s schedule=%s stats=%s touchcal=%s heap=%u sd=%s phrase_sd=%s wifi=%s ssid_saved=%s weather=%s spac3=%s spac3_ok=%s name=%s personality=%s scroll=%lums health=%s strength=%d armor=%d\n",
                  frameOk ? "ok" : "failed",
                  screenW,
                  screenH,
                  displayRotation,
                  moodNames[currentMood],
                  autoMode ? "auto" : "manual",
                  (int)menuMode,
                  wifiEditorActive ? "on" : "off",
                  timeEditorActive ? "on" : "off",
                  scheduleEditorActive ? "on" : "off",
                  statsViewActive ? "on" : "off",
                  touchCalActive ? "on" : "off",
                  ESP.getFreeHeap(),
                  sdReady ? "ready" : "missing",
                  sdPhraseLookupEnabled ? "on" : "off",
                  WiFi.status() == WL_CONNECTED ? "connected" : "offline",
                  wifiConfigured ? "true" : "false",
                  weatherSummary.c_str(),
                  spac3TelemetryEnabled ? "on" : "off",
                  spac3TelemetryOk ? "true" : "false",
                  buddyName.c_str(),
                  personalityNames[currentPersonality],
                  speechScrollMs,
                  healthLabel().c_str(),
                  buddyStrength,
                  buddyArmor);
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
  } else if (lower == "wifi edit" || lower == "wifi menu") {
    openWifiEditor();
    Serial.println("wifi_editor=open");
  } else if (lower == "wifi setup") {
    startWifiSetupPortal();
  } else if (lower == "wifi scan") {
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    delay(80);
    int found = WiFi.scanNetworks(false, true);
    Serial.printf("wifi_scan count=%d\n", found);
    if (found > 0) {
      for (int i = 0; i < found && i < 30; i++) {
        Serial.printf("wifi_scan[%d] ssid=\"%s\" rssi=%d channel=%d auth=%s\n",
                      i,
                      WiFi.SSID(i).c_str(),
                      WiFi.RSSI(i),
                      WiFi.channel(i),
                      wifiSecurityName(WiFi.encryptionType(i)));
        yield();
      }
    }
    WiFi.scanDelete();
  } else if (lower == "wifi status") {
    loadNetworkSettings();
    String savedPass = loadWifiPassword();
    wl_status_t wifiStatus = WiFi.status();
    Serial.printf("wifi status=%s code=%d ssid_saved=%s saved_ssid=\"%s\" pass_len=%d ip=%s rssi=%d channel=%d ollama=%s spac3=%s\n",
                  wifiStatusName(wifiStatus),
                  (int)wifiStatus,
                  wifiConfigured ? "true" : "false",
                  wifiSsid.c_str(),
                  savedPass.length(),
                  wifiStatus == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "0.0.0.0",
                  wifiStatus == WL_CONNECTED ? WiFi.RSSI() : 0,
                  wifiStatus == WL_CONNECTED ? WiFi.channel() : 0,
                  ollamaHost.c_str(),
                  spac3Host.c_str());
  } else if (lower == "time sync") {
    syncNetworkTime();
    Serial.printf("time_sync minute=%d wifi=%s\n", minuteOfDay(), WiFi.status() == WL_CONNECTED ? "connected" : "offline");
  } else if (lower.startsWith("weather loc ")) {
    String rest = line.substring(12);
    rest.trim();
    int sep = rest.indexOf(' ');
    if (sep < 0) sep = rest.indexOf(',');
    if (sep > 0) {
      float lat = rest.substring(0, sep).toFloat();
      float lon = rest.substring(sep + 1).toFloat();
      saveWeatherLocation(lat, lon);
      Serial.printf("weather_location=saved lat=%.4f lon=%.4f\n", weatherLat, weatherLon);
    } else {
      Serial.println("weather_location=failed usage: weather loc <lat> <lon>");
    }
  } else if (lower == "weather update") {
    bool ok = updateWeatherNow(true);
    Serial.printf("weather_update=%s summary=%s\n", ok ? "ok" : "failed", weatherSummary.c_str());
  } else if (lower == "weather status") {
    Serial.printf("weather configured=%s lat=%.4f lon=%.4f summary=%s wifi=%s\n",
                  weatherConfigured ? "true" : "false",
                  weatherLat,
                  weatherLon,
                  weatherSummary.c_str(),
                  WiFi.status() == WL_CONNECTED ? "connected" : "offline");
  } else if (lower.startsWith("ollama host ")) {
    saveOllamaHost(line.substring(12));
    Serial.printf("ollama_host=saved %s\n", ollamaHost.c_str());
  } else if (lower.startsWith("spac3 host ")) {
    saveSpac3Host(line.substring(11));
    Serial.printf("spac3_host=saved %s\n", spac3Host.c_str());
    speechLine = "Spac3-Gh0st host saved.";
    speechScroll = 0;
  } else if (lower == "spac3 on" || lower == "spac3 enable") {
    saveSpac3Enabled(true);
    Serial.println("spac3=on");
    speechLine = "Spac3-Gh0st telemetry enabled.";
    speechScroll = 0;
  } else if (lower == "spac3 off" || lower == "spac3 disable") {
    saveSpac3Enabled(false);
    spac3TelemetryOk = false;
    Serial.println("spac3=off");
    speechLine = "Spac3-Gh0st telemetry paused.";
    speechScroll = 0;
  } else if (lower == "spac3 update" || lower == "spac3 poll") {
    bool ok = fetchSpac3Telemetry(true);
    Serial.printf("spac3_update=%s host=%s mood=%s cpu_c=%.1f ram=%.1f alert=%d wifi_count=%d message=\"%s\"\n",
                  ok ? "ok" : "failed",
                  spac3Host.c_str(),
                  spac3LastMood.c_str(),
                  spac3LastCpuC,
                  spac3LastRam,
                  spac3LastAlert,
                  spac3LastWifiCount,
                  spac3LastMessage.c_str());
  } else if (lower == "spac3 heartbeat") {
    bool ok = sendSpac3Heartbeat();
    Serial.printf("spac3_heartbeat=%s host=%s\n", ok ? "ok" : "failed", spac3Host.c_str());
  } else if (lower == "spac3 status") {
    Serial.printf("spac3 enabled=%s ok=%s host=%s mood=%s face=\"%s\" cpu_c=%.1f ram=%.1f alert=%d wifi_count=%d message=\"%s\"\n",
                  spac3TelemetryEnabled ? "true" : "false",
                  spac3TelemetryOk ? "true" : "false",
                  spac3Host.c_str(),
                  spac3LastMood.c_str(),
                  spac3LastFace.c_str(),
                  spac3LastCpuC,
                  spac3LastRam,
                  spac3LastAlert,
                  spac3LastWifiCount,
                  spac3LastMessage.c_str());
  } else if (lower.startsWith("remember me as ")) {
    saveBuddyName(line.substring(15));
    speechLine = "Got it. I will call you " + buddyName + ".";
    speechScroll = 0;
  } else if (lower.startsWith("remember me ")) {
    saveBuddyName(line.substring(12));
    speechLine = "Got it. I will call you " + buddyName + ".";
    speechScroll = 0;
  } else if (lower.startsWith("remember ")) {
    rememberBuddyThought(line.substring(9));
    speechLine = "I stored that memory. It may mutate into an opinion later.";
    speechScroll = 0;
    statusLine = "memory saved";
    saveBuddyMemory(true);
  } else if (lower.startsWith("phrase add ") && lower != "phrase add 100") {
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
  } else if (lower == "phrase seed") {
    bool ok = seedPhraseBank(true, 0, 150);
    Serial.printf("phrase_seed=%s file=%s rows=%d\n", ok ? "ok" : "failed", PHRASE_FILE, ok ? MOOD_COUNT * PERSONALITY_COUNT * 150 : 0);
    speechLine = ok ? "Phrase bank seeded with personality vocabulary." : "Phrase bank seed failed.";
    speechScroll = 0;
  } else if (lower == "phrase sd on") {
    sdPhraseLookupEnabled = true;
    speechLine = "SD phrase lookup enabled. Custom phrases may add a tiny hitch.";
    speechScroll = 0;
    Serial.println("phrase_sd=on");
  } else if (lower == "phrase sd off" || lower == "phrase fast") {
    sdPhraseLookupEnabled = false;
    speechLine = "Fast phrase mode enabled. Generated vocabulary stays smooth.";
    speechScroll = 0;
    Serial.println("phrase_sd=off");
  } else if (lower == "phrase sd status") {
    Serial.printf("phrase_sd=%s\n", sdPhraseLookupEnabled ? "on" : "off");
  } else if (lower == "phrase expand" || lower == "phrase add 100") {
    bool ok = seedPhraseBank(true, 50, 100);
    Serial.printf("phrase_expand=%s file=%s rows=%d\n", ok ? "ok" : "failed", PHRASE_FILE, ok ? MOOD_COUNT * PERSONALITY_COUNT * 100 : 0);
    speechLine = ok ? "Added 100 phrases per mood for every personality." : "Phrase expansion failed.";
    speechScroll = 0;
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

  Serial.printf("ok rotation=%d mood=%s event=%s sd=%s wifi=%s\n", displayRotation, moodNames[currentMood], lastEvent.c_str(), sdReady ? "ready" : "missing", WiFi.status() == WL_CONNECTED ? "connected" : "offline");
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
    if (now - lastSpeechScroll > speechScrollMs) {
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

void drawInfoStrip() {
  String text = timeDateLine();
  if (weatherConfigured && weatherSummary.length() > 0 && weatherSummary != "weather unset") {
    text += "  " + weatherSummary;
  }
  int visibleChars = max(12, (screenW - 20) / 6);
  if (text.length() > visibleChars) text = text.substring(0, visibleChars - 1) + ".";
  frame.setTextDatum(MC_DATUM);
  frame.setTextColor(TFT_LIGHTGREY, bgColor);
  frame.drawString(text, screenW / 2, 20, 1);
}

const char* menuTitle() {
  switch (menuMode) {
    case MENU_SYSTEM: return "SYSTEM";
    case MENU_FACE: return "FACE";
    case MENU_SYSTEM_AI: return "AI";
    case MENU_SYSTEM_WIFI: return "WIFI";
    case MENU_SYSTEM_PHRASES: return "PHRASES";
    case MENU_SYSTEM_TIME: return "TIME";
    case MENU_FACE_EYES: return "EYES";
    case MENU_FACE_MOODS: return "MOODS";
    case MENU_FACE_COLORS: return "COLORS";
    case MENU_STATS: return "STATS";
    default: return "MENU";
  }
}

String menuItemLabel(int i) {
  if (i == 4) return "Back";
  if (menuMode == MENU_SYSTEM) {
    const char* a[] = {"AI/Phrases", "WiFi", "Time/Touch", "Stats"};
    return a[i];
  }
  if (menuMode == MENU_FACE) {
    const char* a[] = {"Eyes", "Moods", "Eye color", autoMode ? "Manual mode" : "Auto mode"};
    return a[i];
  }
  if (menuMode == MENU_SYSTEM_AI) {
    const char* a[] = {"Buddy name", "Personality", "Scroll speed", "Phrases"};
    return a[i];
  }
  if (menuMode == MENU_SYSTEM_WIFI) {
    const char* a[] = {"WiFi list", "Connect/status", "Sync time", "Weather now"};
    return a[i];
  }
  if (menuMode == MENU_SYSTEM_TIME) {
    const char* a[] = {"Set date/time", "Daily schedule", "Touch cal", "Sync time"};
    return a[i];
  }
  if (menuMode == MENU_STATS) {
    const char* a[] = {"Care stats", "System stats", "Network stats", "Dead timer"};
    return a[i];
  }
  if (menuMode == MENU_SYSTEM_PHRASES) {
    const char* a[] = {"SD status", "Speak phrase", "Add 100/mood", "Editor soon"};
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

void drawWifiKey(const char* label, int x, int y, int w, int h, uint16_t outline, bool active = false) {
  frame.fillRoundRect(x, y, w, h, 4, active ? TFT_DARKCYAN : TFT_BLACK);
  frame.drawRoundRect(x, y, w, h, 4, outline);
  frame.setTextDatum(MC_DATUM);
  frame.setTextColor(TFT_WHITE, active ? TFT_DARKCYAN : TFT_BLACK);
  frame.drawString(label, x + w / 2, y + h / 2, 1);
}

void drawWifiKeyRow(const char* keys, int row, int columns = 10) {
  int top = wifiKeyboardTop();
  int rowH = 22;
  int gap = 2;
  int keyW = max(18, (screenW - 12) / columns);
  int y = top + row * (rowH + gap);
  int count = strlen(keys);
  for (int i = 0; i < count; i++) {
    int x = 6 + i * keyW;
    char label[2] = {keys[i], 0};
    if (wifiEditShift && label[0] >= 'a' && label[0] <= 'z') label[0] = label[0] - 'a' + 'A';
    drawWifiKey(label, x, y, keyW - gap, rowH, TFT_DARKGREY);
  }
}

void drawWifiActionRow(int row, const char* a, const char* b, const char* c) {
  int top = wifiKeyboardTop();
  int rowH = 22;
  int gap = 2;
  int y = top + row * (rowH + gap);
  int buttonW = (screenW - 16) / 3;
  drawWifiKey(a, 6, y, buttonW - gap, rowH, TFT_CYAN, strcmp(a, "SHIFT") == 0 && wifiEditShift);
  drawWifiKey(b, 6 + buttonW, y, buttonW - gap, rowH, TFT_CYAN, strcmp(b, "SHIFT") == 0 && wifiEditShift);
  drawWifiKey(c, 6 + buttonW * 2, y, buttonW - gap, rowH, TFT_CYAN, strcmp(c, "SHIFT") == 0 && wifiEditShift);
}

void drawWifiScanOverlay() {
  frame.fillRoundRect(4, 8, screenW - 8, screenH - 16, 8, TFT_BLACK);
  frame.drawRoundRect(4, 8, screenW - 8, screenH - 16, 8, TFT_CYAN);
  frame.setTextDatum(TL_DATUM);
  frame.setTextColor(TFT_CYAN, TFT_BLACK);
  frame.drawString("CHOOSE WIFI", 12, 12, 2);
  frame.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  frame.drawString(wifiScanInProgress ? "scan..." : String(wifiScanCount) + " found", screenW - 70, 16, 1);

  int rowTop = 52;
  int rowH = 30;
  int start = wifiScanPage * WIFI_SCAN_ROWS;
  int maxChars = max(8, (screenW - 72) / 6);
  for (int i = 0; i < WIFI_SCAN_ROWS; i++) {
    int idx = start + i;
    int y = rowTop + i * rowH;
    frame.drawRoundRect(8, y, screenW - 16, rowH - 4, 4, TFT_DARKGREY);
    frame.setTextDatum(TL_DATUM);
    if (wifiScanInProgress && i == 0) {
      frame.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      frame.drawString("Scanning nearby networks...", 14, y + 7, 1);
    } else if (idx < wifiScanCount) {
      uint16_t signalColor = wifiScanRssi[idx] > -60 ? TFT_GREEN : wifiScanRssi[idx] > -75 ? TFT_YELLOW : TFT_ORANGE;
      frame.setTextColor(TFT_WHITE, TFT_BLACK);
      frame.drawString(fitText(wifiScanSsid[idx], maxChars), 14, y + 7, 1);
      frame.setTextColor(signalColor, TFT_BLACK);
      frame.drawString(String(wifiScanRssi[idx]), screenW - 56, y + 7, 1);
      frame.setTextColor(wifiScanSecure[idx] ? TFT_LIGHTGREY : TFT_GREEN, TFT_BLACK);
      frame.drawString(wifiScanSecure[idx] ? "lock" : "open", screenW - 34, y + 7, 1);
    } else if (idx == wifiScanCount) {
      frame.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      frame.drawString("Hidden/manual network", 14, y + 7, 1);
    }
  }

  int buttonW = (screenW - 16) / 3;
  int y = screenH - 34;
  drawWifiKey("MORE", 6, y, buttonW - 2, 24, TFT_CYAN);
  drawWifiKey("RESCAN", 6 + buttonW, y, buttonW - 2, 24, TFT_CYAN);
  drawWifiKey("CANCEL", 6 + buttonW * 2, y, buttonW - 2, 24, TFT_CYAN);
}

void drawWifiEditorOverlay() {
  if (!wifiEditorActive) return;
  if (wifiScanListActive) {
    drawWifiScanOverlay();
    return;
  }

  frame.fillRoundRect(4, 8, screenW - 8, screenH - 16, 8, TFT_BLACK);
  frame.drawRoundRect(4, 8, screenW - 8, screenH - 16, 8, TFT_CYAN);
  frame.setTextDatum(TL_DATUM);
  frame.setTextColor(TFT_CYAN, TFT_BLACK);
  frame.drawString("WIFI SETUP", 12, 12, 2);

  int maxChars = max(8, (screenW - 88) / 6);
  frame.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  frame.drawString("SSID", 12, 34, 1);
  frame.drawString("PASS", 12, 60, 1);
  frame.drawRoundRect(48, 28, screenW - 58, 22, 4, wifiEditField == WIFI_EDIT_SSID ? TFT_CYAN : TFT_DARKGREY);
  frame.drawRoundRect(48, 54, screenW - 58, 22, 4, wifiEditField == WIFI_EDIT_PASS ? TFT_CYAN : TFT_DARKGREY);
  frame.setTextColor(TFT_WHITE, TFT_BLACK);
  frame.drawString(fitText(wifiEditSsid, maxChars), 54, 34, 1);
  frame.drawString(fitText(maskedWifiPassword(), maxChars), 54, 60, 1);

  drawWifiKeyRow("1234567890", 0);
  drawWifiKeyRow("qwertyuiop", 1);
  drawWifiKeyRow("asdfghjkl", 2);
  drawWifiKeyRow("zxcvbnm.-_", 3);
  drawWifiActionRow(4, wifiEditField == WIFI_EDIT_SSID ? "PASS" : "SSID", "SHIFT", "SPACE");
  drawWifiActionRow(5, "DEL", "SAVE", "CANCEL");
}

String timeFieldLabel(int i) {
  int m = minuteOfDay();
  if (i == 0) return "Year " + String(dateYear);
  if (i == 1) return "Month " + String(dateMonth);
  if (i == 2) return "Day " + String(dateDay);
  if (i == 3) return "Hour " + String(m / 60);
  if (i == 4) return "Minute " + String(m % 60);
  if (i == 5) return "TZ " + timezoneLabel();
  if (i == 6) return String("DST ") + (daylightSavings ? "on" : "off");
  return String("Clock ") + (clock24Hour ? "24h" : "12h");
}

void drawTimeEditorOverlay() {
  if (!timeEditorActive) return;
  frame.fillRoundRect(4, 8, screenW - 8, screenH - 16, 8, TFT_BLACK);
  frame.drawRoundRect(4, 8, screenW - 8, screenH - 16, 8, TFT_CYAN);
  frame.setTextDatum(TL_DATUM);
  frame.setTextColor(TFT_CYAN, TFT_BLACK);
  frame.drawString("TIME SETUP", 12, 12, 2);
  int rowTop = 42;
  for (int i = 0; i < 8; i++) {
    int y = rowTop + i * 20;
    frame.drawRoundRect(8, y, screenW - 16, 18, 3, i == timeEditField ? TFT_CYAN : TFT_DARKGREY);
    frame.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    frame.drawString("-", 16, y + 5, 1);
    frame.setTextColor(TFT_WHITE, TFT_BLACK);
    frame.drawString(timeFieldLabel(i), 48, y + 5, 1);
    frame.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    frame.drawString("+", screenW - 24, y + 5, 1);
  }
  int buttonW = (screenW - 16) / 2;
  drawWifiKey("DONE", 6, screenH - 34, buttonW - 2, 24, TFT_CYAN);
  drawWifiKey("NEXT", 6 + buttonW, screenH - 34, buttonW - 2, 24, TFT_CYAN);
}

void drawScheduleEditorOverlay() {
  if (!scheduleEditorActive) return;
  frame.fillRoundRect(4, 8, screenW - 8, screenH - 16, 8, TFT_BLACK);
  frame.drawRoundRect(4, 8, screenW - 8, screenH - 16, 8, TFT_CYAN);
  frame.setTextDatum(TL_DATUM);
  frame.setTextColor(TFT_CYAN, TFT_BLACK);
  frame.drawString("DAY SCHEDULE", 12, 12, 2);
  frame.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  frame.drawString("left/right changes 30m", 12, 32, 1);
  int rowTop = 54;
  for (int i = 0; i < 6; i++) {
    int y = rowTop + i * 24;
    frame.drawRoundRect(8, y, screenW - 16, 20, 3, i == scheduleEditField ? TFT_CYAN : TFT_DARKGREY);
    frame.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    frame.drawString("-", 16, y + 6, 1);
    frame.setTextColor(TFT_WHITE, TFT_BLACK);
    frame.drawString(scheduleFieldLabel(i), 42, y + 6, 1);
    frame.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    frame.drawString("+", screenW - 24, y + 6, 1);
  }
  int buttonW = (screenW - 16) / 2;
  drawWifiKey("DONE", 6, screenH - 34, buttonW - 2, 24, TFT_CYAN);
  drawWifiKey("NEXT", 6 + buttonW, screenH - 34, buttonW - 2, 24, TFT_CYAN);
}

void drawBar(int x, int y, int w, int value, uint16_t color) {
  value = constrain(value, 0, 100);
  frame.drawRect(x, y, w, 8, TFT_DARKGREY);
  frame.fillRect(x + 1, y + 1, max(1, (w - 2) * value / 100), 6, color);
}

void drawStatsOverlay() {
  if (!statsViewActive) return;
  frame.fillRoundRect(4, 8, screenW - 8, screenH - 16, 8, TFT_BLACK);
  frame.drawRoundRect(4, 8, screenW - 8, screenH - 16, 8, TFT_MAGENTA);
  frame.setTextDatum(TL_DATUM);
  frame.setTextColor(TFT_MAGENTA, TFT_BLACK);
  const char* title = statsViewPage == 0 ? "CARE STATS" : statsViewPage == 1 ? "SYSTEM STATS" : statsViewPage == 2 ? "NETWORK STATS" : statsViewPage == 3 ? "DEAD TIMER" : "MEMORY";
  frame.drawString(title, 12, 12, 2);
  frame.setTextColor(TFT_WHITE, TFT_BLACK);
  int y = 44;
  if (statsViewPage == 0) {
    frame.drawString("Health " + healthLabel(), 14, y, 1); drawBar(92, y + 2, screenW - 106, buddyHealthTenth / 10, TFT_GREEN); y += 18;
    frame.drawString("Hungry", 14, y, 1); drawBar(92, y + 2, screenW - 106, buddyHunger, TFT_ORANGE); y += 18;
    frame.drawString("Play", 14, y, 1); drawBar(92, y + 2, screenW - 106, buddyPlayNeed, TFT_CYAN); y += 18;
    frame.drawString("Restless", 14, y, 1); drawBar(92, y + 2, screenW - 106, buddyRestless, TFT_YELLOW); y += 18;
    frame.drawString("Anxious", 14, y, 1); drawBar(92, y + 2, screenW - 106, buddyAnxiety, TFT_SKYBLUE); y += 18;
    frame.drawString("Strength " + String(buddyStrength) + " Armor " + String(buddyArmor), 14, y, 1); y += 16;
    frame.drawString("Fed/play today " + String(dailyFeedCount) + "/" + String(dailyPlayCount), 14, y, 1); y += 16;
    frame.drawString("Daily gain " + String(dailyHealthGainTenth / 10) + "." + String(abs(dailyHealthGainTenth % 10)) + "/10.0", 14, y, 1); y += 16;
    frame.drawString("Missed feeds " + String(missedFeedings), 14, y, 1);
  } else if (statsViewPage == 1) {
    frame.drawString("Mood " + String(moodNames[currentMood]), 14, y, 1); y += 18;
    frame.drawString(String("Mode ") + (autoMode ? "auto" : "manual"), 14, y, 1); y += 18;
    frame.drawString("SD " + String(sdReady ? "ready" : "missing"), 14, y, 1); y += 18;
    frame.drawString("Heap " + String(ESP.getFreeHeap() / 1024) + " KB", 14, y, 1); y += 18;
    frame.drawString("Uptime " + formatDuration((millis() - lifecycleBootMs) / 1000ULL), 14, y, 1); y += 18;
    frame.drawString("Time " + timeDateLine(), 14, y, 1);
  } else if (statsViewPage == 2) {
    frame.drawString("WiFi " + String(WiFi.status() == WL_CONNECTED ? "connected" : "offline"), 14, y, 1); y += 18;
    frame.drawString("SSID " + fitText(wifiSsid, 18), 14, y, 1); y += 18;
    frame.drawString("IP " + String(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "0.0.0.0"), 14, y, 1); y += 18;
    frame.drawString("New WiFi " + String(buddyNewNetworkCount), 14, y, 1); y += 18;
    frame.drawString("New BT " + String(buddyNewBluetoothCount), 14, y, 1); y += 18;
    frame.drawString("Scan " + String(wifiScanInProgress ? "running" : "idle"), 14, y, 1); y += 18;
    frame.drawString("Weather " + fitText(weatherSummary, 18), 14, y, 1);
  } else if (statsViewPage == 3) {
    frame.drawString("Alive " + formatDuration(totalAliveSeconds + (millis() - lifecycleBootMs) / 1000ULL), 14, y, 1); y += 18;
    frame.drawString("Session " + formatDuration((millis() - lifecycleBootMs) / 1000ULL), 14, y, 1); y += 18;
    frame.drawString("Dead " + formatDuration(totalDeadSeconds), 14, y, 1); y += 18;
    frame.drawString("Deaths " + String(deathCount), 14, y, 1); y += 18;
    frame.drawString("Boots " + String(bootCount), 14, y, 1); y += 18;
    frame.drawString("Last unix " + String((unsigned long)lastKnownUnix), 14, y, 1); y += 18;
    frame.drawString("Care day " + String(lastCareDay), 14, y, 1); y += 18;
    frame.drawString("Boost week " + String(lastBoostWeek), 14, y, 1);
  } else {
    int s = bestPreferenceIndex(seasonAffinity, SEASON_COUNT);
    int mo = bestPreferenceIndex(monthAffinity, MONTH_COUNT);
    int ti = bestPreferenceIndex(timeAffinity, TIME_PREF_COUNT);
    int ac = bestPreferenceIndex(activityAffinity, ACTIVITY_COUNT);
    frame.drawString("Season " + String(seasonNames[s]) + " " + String(seasonAffinity[s]), 14, y, 1); y += 16;
    frame.drawString("Month " + monthName(mo + 1) + " " + String(monthAffinity[mo]), 14, y, 1); y += 16;
    frame.drawString("Time " + String(timePreferenceNames[ti]) + " " + String(timeAffinity[ti]), 14, y, 1); y += 16;
    frame.drawString("Activity " + fitText(activityNames[ac], 16) + " " + String(activityAffinity[ac]), 14, y, 1); y += 16;
    frame.drawString("Learns " + String(preferenceLearnCount) + " Revs " + String(memoryRevisionCount), 14, y, 1); y += 18;
    if (statsMemoryPreview.length() == 0 || millis() - statsMemoryPreviewMs > 5000UL) {
      statsMemoryPreview = randomMemoryLine();
      statsMemoryPreviewMs = millis();
    }
    frame.drawString(fitText(statsMemoryPreview, 26), 14, y, 1); y += 16;
    frame.drawString(fitText(weatherMoodWord(), 26), 14, y, 1);
  }
  int third = (screenW - 18) / 3;
  drawWifiKey("PREV", 6, screenH - 34, third - 2, 24, TFT_MAGENTA);
  drawWifiKey("NEXT", 6 + third, screenH - 34, third - 2, 24, TFT_MAGENTA);
  drawWifiKey("CLOSE", 6 + third * 2, screenH - 34, third - 2, 24, TFT_MAGENTA);
}

void drawTouchCalOverlay() {
  if (!touchCalActive) return;
  frame.fillSprite(TFT_BLACK);
  int tx = touchCalStep == 0 ? 18 : screenW - 18;
  int ty = touchCalStep == 0 ? 18 : screenH - 18;
  frame.drawCircle(tx, ty, 12, TFT_CYAN);
  frame.drawFastHLine(tx - 15, ty, 30, TFT_CYAN);
  frame.drawFastVLine(tx, ty - 15, 30, TFT_CYAN);
  frame.setTextDatum(MC_DATUM);
  frame.setTextColor(TFT_WHITE, TFT_BLACK);
  frame.drawString(touchCalStep == 0 ? "tap upper-left dot" : "tap lower-right dot", screenW / 2, screenH / 2, 2);
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
  drawInfoStrip();

  if (currentMood == MOOD_SLEEPY) {
    drawZzz(screenW - 62, max(24, screenH / 8), TFT_LIGHTGREY);
  }

  drawEye(leftEye, true);
  drawEye(rightEye, false);

  drawSpeechStrip();
  drawMenuOverlay();
  drawWifiEditorOverlay();
  drawTimeEditorOverlay();
  drawScheduleEditorOverlay();
  drawStatsOverlay();
  drawTouchCalOverlay();

  frame.pushSprite(0, 0);
}

void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());
  bootMinuteOfDay = compileTimeMinutes();
  clockSetAtMs = millis();
  loadTimeSettings();
  loadBuddyMemory();
  loadLifecycle();
  lastInteractionMs = millis();

  pinMode(BACKLIGHT_PIN, OUTPUT);
  setBacklight(255);

  tft.init();
  tft.invertDisplay(false);

  touchSPI.begin(T_SCK, T_MISO, T_MOSI);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  ts.begin(touchSPI);
  loadTouchCalibration();

  initSDCard();
  loadNetworkSettings();
  loadVoiceSettings();

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
  Serial.println("commands: rotate [0-3], mood happy, event face, stats cpu=90 temp=80, tap, boop, pet, tickle, poke left, wake, feed, play, boost, calm, care reset, health, calendar, preferences, memory add <note>, memory think, preference seed, prefer season summer, prefer month october, prefer time night, prefer activity playing, dislike season winter, bt seen <name>, date YYYY-MM-DD, time HH:MM, timezone -5, dst on|off, clock 12|24, schedule <early|morning|day|latepm|night|latenight> HH:MM, lifecycle, time sync, memory, blink, wink, auto, manual, speak, say <text>, name <buddy>, personality <name|next>, scroll speed <fast|normal|slow|ms>, eye color <name|default>, pupil color <name|default>, sd status, wifi setup, wifi ssid <name>, wifi pass <password>, wifi connect, wifi scan, wifi status, weather loc <lat> <lon>, weather update, weather status, ollama host <url>, spac3 host <url>, spac3 update, spac3 status, spac3 on|off, remember me as <name>, phrase add <mood> <phrase>, phrase expand");
  if (wifiConfigured) connectWifi();
}

void loop() {
  processSerial();
  updateWifi();
  updateSpac3Ghost();
  handleTouch();
  unsigned long now = millis();
  if (now - lastFrameDrawMs >= FRAME_INTERVAL_MS) {
    lastFrameDrawMs = now;
    updateBuddy();
    drawFrame();
  } else {
    delay(2);
  }
}
