#include <Arduino.h>
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "driver/i2s.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Preferences.h>

// Seeed XIAO ESP32S3 Sense B2B camera pins.
static const int CAM_PWDN = -1;
static const int CAM_RESET = -1;
static const int CAM_XCLK = 10;
static const int CAM_SIOD = 40;
static const int CAM_SIOC = 39;
static const int CAM_Y9 = 48;
static const int CAM_Y8 = 11;
static const int CAM_Y7 = 12;
static const int CAM_Y6 = 14;
static const int CAM_Y5 = 16;
static const int CAM_Y4 = 18;
static const int CAM_Y3 = 17;
static const int CAM_Y2 = 15;
static const int CAM_VSYNC = 38;
static const int CAM_HREF = 47;
static const int CAM_PCLK = 13;

// Sense expansion PDM mic pins.
static const int MIC_DATA = 41;
static const int MIC_CLK = 42;
static const i2s_port_t MIC_PORT = I2S_NUM_0;

static const int LED_PIN = 21; // active-low user LED on XIAO ESP32S3
static const int SAMPLE_COUNT = 256;
static const char* BLE_NAME = "CYD-Sense";
static BLEUUID BUDDY_SERVICE_UUID("7a2f0001-44b8-4f2a-9c4f-c0d000000001");
static BLEUUID BUDDY_EVENT_UUID("7a2f0002-44b8-4f2a-9c4f-c0d000000002");
static BLEUUID BUDDY_COMMAND_UUID("7a2f0003-44b8-4f2a-9c4f-c0d000000003");

bool cameraReady = false;
bool micReady = false;
bool streamEvents = false;
bool sensorInitAttempted = false;
bool bleClientConnected = false;
bool bleNeedsAdvertising = false;
bool coolMode = true;
bool voiceMode = false;
unsigned long lastSensorMs = 0;
unsigned long lastHeartbeatMs = 0;
unsigned long lastVisionMs = 0;
unsigned long lastBleAdvertiseMs = 0;
unsigned long lastBleNotifyMs = 0;
unsigned long lastWakeEventMs = 0;
unsigned long lastSpeechEventMs = 0;
int loudThreshold = 900;
int quietThreshold = 80;
unsigned long micIntervalMs = 1500;
unsigned long visionIntervalMs = 3000;
int lastFrameBytes = 0;
int stableQuietCount = 0;
int rememberedSignature = 0;
String rememberedName;
String wakeName = "Buddy";
int wakeSignature = 0;
BLEServer* buddyServer = nullptr;
BLECharacteristic* eventCharacteristic = nullptr;
BLECharacteristic* commandCharacteristic = nullptr;
Preferences prefs;

void handleCommand(String line);

class BuddyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) override {
    bleClientConnected = true;
    Serial.println("ble=connected");
  }

  void onDisconnect(BLEServer* server) override {
    bleClientConnected = false;
    bleNeedsAdvertising = true;
    Serial.println("ble=disconnected");
  }
};

void led(bool on) {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, on ? LOW : HIGH);
}

void printJsonStatus(const char* kind, int level = -1, int width = 0, int height = 0, size_t bytes = 0) {
  Serial.printf("{\"kind\":\"%s\",\"camera\":%s,\"mic\":%s,\"level\":%d,\"width\":%d,\"height\":%d,\"bytes\":%u,\"free_heap\":%u,\"psram\":%u}\n",
                kind,
                cameraReady ? "true" : "false",
                micReady ? "true" : "false",
                level,
                width,
                height,
                (unsigned)bytes,
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram());
}

void publishBuddyEvent(const String& eventLine) {
  String line = eventLine;
  line.trim();
  if (!line.startsWith("event ")) line = "event " + line;

  if (streamEvents) {
    Serial.print("BUDDY ");
    Serial.println(line);
  }

  if (eventCharacteristic && bleClientConnected && millis() - lastBleNotifyMs > 150) {
    String bleLine = line;
    if (bleLine.startsWith("event sound:loud")) bleLine = "event sound:loud";
    eventCharacteristic->setValue((uint8_t*)bleLine.c_str(), bleLine.length());
    eventCharacteristic->notify();
    lastBleNotifyMs = millis();
  }
}

int captureVisualSignature() {
  if (!cameraReady) return 0;
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return 0;

  uint32_t hash = fb->len;
  size_t step = max((size_t)1, fb->len / 96);
  for (size_t i = 0; i < fb->len; i += step) {
    hash = ((hash << 5) | (hash >> 27)) ^ fb->buf[i];
  }
  int signature = (int)(hash % 100000);
  esp_camera_fb_return(fb);
  return signature;
}

void loadPersonMemory() {
  prefs.begin("sense", true);
  rememberedName = prefs.getString("person", "");
  rememberedSignature = prefs.getInt("sig", 0);
  wakeName = prefs.getString("wake", "Buddy");
  wakeSignature = prefs.getInt("wakeSig", 0);
  prefs.end();
}

void rememberPerson(String name) {
  name.trim();
  if (name.length() == 0) name = "friend";
  int signature = captureVisualSignature();
  if (signature == 0) {
    Serial.println("remember=failed camera=missing");
    publishBuddyEvent("event remember:failed");
    return;
  }

  rememberedName = name;
  rememberedSignature = signature;
  prefs.begin("sense", false);
  prefs.putString("person", rememberedName);
  prefs.putInt("sig", rememberedSignature);
  prefs.end();

  Serial.printf("remember=ok name=%s sig=%d\n", rememberedName.c_str(), rememberedSignature);
  publishBuddyEvent(String("event remember:") + rememberedName);
}

class BuddyCommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    std::string value = characteristic->getValue();
    if (value.empty()) return;
    String line;
    for (char c : value) line += c;
    handleCommand(line);
  }
};

void beginBle() {
  BLEDevice::init(BLE_NAME);
  buddyServer = BLEDevice::createServer();
  buddyServer->setCallbacks(new BuddyServerCallbacks());
  BLEService* service = buddyServer->createService(BUDDY_SERVICE_UUID);
  eventCharacteristic = service->createCharacteristic(
      BUDDY_EVENT_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  eventCharacteristic->setValue("event hello");
  commandCharacteristic = service->createCharacteristic(
      BUDDY_COMMAND_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  commandCharacteristic->setCallbacks(new BuddyCommandCallbacks());
  service->start();
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BUDDY_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  lastBleAdvertiseMs = millis();
  Serial.println("ble=advertising name=CYD-Sense");
}

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_Y2;
  config.pin_d1 = CAM_Y3;
  config.pin_d2 = CAM_Y4;
  config.pin_d3 = CAM_Y5;
  config.pin_d4 = CAM_Y6;
  config.pin_d5 = CAM_Y7;
  config.pin_d6 = CAM_Y8;
  config.pin_d7 = CAM_Y9;
  config.pin_xclk = CAM_XCLK;
  config.pin_pclk = CAM_PCLK;
  config.pin_vsync = CAM_VSYNC;
  config.pin_href = CAM_HREF;
  config.pin_sccb_sda = CAM_SIOD;
  config.pin_sccb_scl = CAM_SIOC;
  config.pin_pwdn = CAM_PWDN;
  config.pin_reset = CAM_RESET;
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QQVGA;
  config.jpeg_quality = 24;
  config.fb_count = 1;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("camera_init=failed err=0x%x\n", err);
    return false;
  }

  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor) {
    sensor->set_vflip(sensor, 1);
    sensor->set_brightness(sensor, 0);
    sensor->set_saturation(sensor, -1);
  }
  return true;
}

bool initMic() {
  i2s_config_t config = {};
  config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
  config.sample_rate = 16000;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 4;
  config.dma_buf_len = 128;
  config.use_apll = false;
  config.tx_desc_auto_clear = false;
  config.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = I2S_PIN_NO_CHANGE;
  pins.ws_io_num = MIC_CLK;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = MIC_DATA;

  esp_err_t err = i2s_driver_install(MIC_PORT, &config, 0, nullptr);
  if (err != ESP_OK) {
    Serial.printf("mic_driver=failed err=0x%x\n", err);
    return false;
  }
  err = i2s_set_pin(MIC_PORT, &pins);
  if (err != ESP_OK) {
    Serial.printf("mic_pins=failed err=0x%x\n", err);
    i2s_driver_uninstall(MIC_PORT);
    return false;
  }
  i2s_zero_dma_buffer(MIC_PORT);
  return true;
}

int readMicLevel() {
  if (!micReady) return -1;

  int16_t samples[SAMPLE_COUNT];
  size_t bytesRead = 0;
  esp_err_t err = i2s_read(MIC_PORT, samples, sizeof(samples), &bytesRead, 20 / portTICK_PERIOD_MS);
  if (err != ESP_OK || bytesRead == 0) return -1;

  int count = bytesRead / sizeof(int16_t);
  int64_t sum = 0;
  for (int i = 0; i < count; i++) sum += samples[i];
  int32_t mean = sum / max(1, count);

  int64_t energy = 0;
  for (int i = 0; i < count; i++) {
    int32_t centered = samples[i] - mean;
    energy += abs(centered);
  }
  return (int)(energy / max(1, count));
}

int captureWakeSignature() {
  if (!micReady) return 0;
  Serial.printf("wake_train=listen name=%s\n", wakeName.c_str());
  led(true);
  delay(250);
  uint32_t sum = 0;
  int peak = 0;
  int count = 0;
  for (int i = 0; i < 18; i++) {
    int level = readMicLevel();
    if (level > 0) {
      sum += level;
      peak = max(peak, level);
      count++;
    }
    delay(55);
  }
  led(false);
  if (count == 0 || peak < quietThreshold * 2) return 0;
  return (int)((sum / count) * 3 + peak * 2);
}

void saveWakeName(String name) {
  name.trim();
  if (name.length() == 0) name = "Buddy";
  if (name.length() > 28) name = name.substring(0, 28);
  wakeName = name;
  prefs.begin("sense", false);
  prefs.putString("wake", wakeName);
  prefs.end();
  Serial.printf("wake_name=%s\n", wakeName.c_str());
}

void trainWakeName(String name = "") {
  if (name.length() > 0) saveWakeName(name);
  int sig = captureWakeSignature();
  if (sig <= 0) {
    Serial.println("wake_train=failed speak_louder");
    publishBuddyEvent("event voice:train_failed");
    return;
  }
  wakeSignature = sig;
  prefs.begin("sense", false);
  prefs.putString("wake", wakeName);
  prefs.putInt("wakeSig", wakeSignature);
  prefs.end();
  Serial.printf("wake_train=ok name=%s sig=%d\n", wakeName.c_str(), wakeSignature);
  publishBuddyEvent(String("event voice:trained name=") + wakeName);
}

void detectVoiceEvents(int level) {
  if (!voiceMode || level <= 0) return;
  unsigned long now = millis();
  if (wakeSignature > 0 && level > quietThreshold * 2) {
    int sig = level * 5;
    int delta = abs(sig - wakeSignature);
    int allowance = max(900, wakeSignature / 3);
    if (delta < allowance && now - lastWakeEventMs > 2500) {
      lastWakeEventMs = now;
      publishBuddyEvent(String("event voice:wake name=") + wakeName);
      return;
    }
  }
  if (level > loudThreshold && now - lastSpeechEventMs > 1200) {
    lastSpeechEventMs = now;
    publishBuddyEvent(String("event voice:speech level=") + level);
  }
}

void captureFrame() {
  if (!cameraReady) {
    Serial.println("capture=failed camera=missing");
    return;
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("capture=failed fb=null");
    return;
  }

  printJsonStatus("camera_frame", -1, fb->width, fb->height, fb->len);
  publishBuddyEvent("event face");
  esp_camera_fb_return(fb);
}

void captureTinyVisionEvent() {
  if (!cameraReady) return;
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return;

  int bytes = fb->len;
  int delta = lastFrameBytes == 0 ? 0 : abs(bytes - lastFrameBytes);
  lastFrameBytes = bytes;
  int width = fb->width;
  int height = fb->height;
  esp_camera_fb_return(fb);

  printJsonStatus("tiny_vision", -1, width, height, bytes);
  int signature = captureVisualSignature();
  if (rememberedSignature > 0 && rememberedName.length() > 0 && abs(signature - rememberedSignature) < 5500 && random(0, 3) == 0) {
    publishBuddyEvent(String("event person:") + rememberedName);
  } else if (delta > 850) {
    publishBuddyEvent("event vision:motion");
  } else if (bytes < 2300) {
    publishBuddyEvent("event vision:dark");
  } else if (bytes > 7000) {
    publishBuddyEvent("event vision:busy");
  } else if (random(0, 5) == 0) {
    publishBuddyEvent("event face");
  }
}

void initSensors() {
  if (sensorInitAttempted) {
    Serial.println("init=already_attempted");
    printJsonStatus("status");
    return;
  }
  sensorInitAttempted = true;
  Serial.println("init=starting");
  cameraReady = initCamera();
  Serial.printf("camera=%s\n", cameraReady ? "ready" : "missing");
  micReady = initMic();
  Serial.printf("mic=%s\n", micReady ? "ready" : "missing");
  printJsonStatus("init");
  publishBuddyEvent("event hello");
}

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;
  String lower = line;
  lower.toLowerCase();

  if (lower == "status") {
    printJsonStatus("status");
    Serial.printf("remembered=%s sig=%d wake=%s wake_sig=%d threshold=%d stream=%s mode=%s voice=%s mic_ms=%lu vision_ms=%lu ble=%s\n",
                  rememberedName.length() ? rememberedName.c_str() : "none",
                  rememberedSignature,
                  wakeName.c_str(),
                  wakeSignature,
                  loudThreshold,
                  streamEvents ? "on" : "off",
                  coolMode ? "cool" : "active",
                  voiceMode ? "on" : "off",
                  micIntervalMs,
                  visionIntervalMs,
                  bleClientConnected ? "connected" : "advertising");
  } else if (lower == "init") {
    initSensors();
  } else if (lower == "capture" || lower == "frame") {
    captureFrame();
  } else if (lower == "stream on") {
    streamEvents = true;
    Serial.println("stream=on");
  } else if (lower == "stream off") {
    streamEvents = false;
    Serial.println("stream=off");
  } else if (lower.startsWith("threshold ")) {
    loudThreshold = max(10, (int)lower.substring(10).toInt());
    Serial.printf("threshold=%d\n", loudThreshold);
  } else if (lower == "cool") {
    coolMode = true;
    streamEvents = false;
    voiceMode = true;
    micIntervalMs = 120;
    visionIntervalMs = 3000;
    Serial.println("mode=cool stream=off voice=on mic_ms=120 vision_ms=3000");
  } else if (lower == "active") {
    coolMode = false;
    streamEvents = true;
    voiceMode = true;
    micIntervalMs = 500;
    visionIntervalMs = 3000;
    Serial.println("mode=active stream=on voice=on mic_ms=500 vision_ms=3000");
  } else if (lower == "voice" || lower == "voice on") {
    voiceMode = true;
    streamEvents = true;
    micIntervalMs = 120;
    Serial.println("voice=on stream=on mic_ms=120");
  } else if (lower == "voice off") {
    voiceMode = false;
    Serial.println("voice=off");
  } else if (lower.startsWith("snapshot ")) {
    visionIntervalMs = max(1000, (int)lower.substring(9).toInt());
    Serial.printf("vision_ms=%lu\n", visionIntervalMs);
  } else if (lower.startsWith("wake name ")) {
    saveWakeName(line.substring(10));
  } else if (lower.startsWith("wake train ")) {
    trainWakeName(line.substring(11));
  } else if (lower == "wake train" || lower == "train wake") {
    trainWakeName();
  } else if (lower.startsWith("remember ")) {
    rememberPerson(line.substring(9));
  } else if (lower == "forget me" || lower == "forget person") {
    rememberedName = "";
    rememberedSignature = 0;
    prefs.begin("sense", false);
    prefs.remove("person");
    prefs.remove("sig");
    prefs.end();
    Serial.println("remember=cleared");
    publishBuddyEvent("event remember:cleared");
  } else if (lower == "help") {
    Serial.println("commands: status, init, capture, stream on, stream off, cool, active, voice on, voice off, snapshot <ms>, threshold <level>, wake name <name>, wake train [name], remember <name>, forget person, help");
  } else {
    Serial.println("unknown command; try help");
  }
}

void processSerial() {
  static String line;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      handleCommand(line);
      line = "";
    } else if (line.length() < 120) {
      line += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  unsigned long start = millis();
  while (!Serial && millis() - start < 5000) {
    led((millis() / 150) % 2);
    delay(10);
  }

  led(true);
  delay(500);
  Serial.println("XIAO S3 Sense bridge alive");
  Serial.printf("psram=%s free_psram=%u free_heap=%u\n", psramFound() ? "yes" : "no", (unsigned)ESP.getFreePsram(), (unsigned)ESP.getFreeHeap());
  loadPersonMemory();
  beginBle();
  printJsonStatus("boot");
  Serial.println("commands: status, init, capture, stream on, stream off, cool, active, voice on, voice off, snapshot <ms>, threshold <level>, wake name <name>, wake train [name], remember <name>, forget person, help");
  initSensors();
  led(false);
}

void loop() {
  processSerial();

  unsigned long now = millis();
  if (now - lastHeartbeatMs > 3000 && !sensorInitAttempted) {
    printJsonStatus("waiting_for_init");
    lastHeartbeatMs = now;
  }

  if (sensorInitAttempted && now - lastSensorMs >= micIntervalMs) {
    lastSensorMs = now;
    int level = readMicLevel();
    if (level >= 0) {
      detectVoiceEvents(level);
      if (streamEvents && level > loudThreshold) {
        publishBuddyEvent(String("event sound:loud level=") + level);
      } else if (streamEvents && level < quietThreshold) {
        stableQuietCount++;
        if (stableQuietCount > 28 && random(0, 80) == 0) publishBuddyEvent("event sound:quiet");
      } else {
        stableQuietCount = 0;
      }
      if (!coolMode && now - lastHeartbeatMs > 4000) {
        printJsonStatus("mic", level);
        lastHeartbeatMs = now;
      }
    }
  }

  if (sensorInitAttempted && cameraReady && now - lastVisionMs > visionIntervalMs) {
    lastVisionMs = now;
    captureTinyVisionEvent();
  }

  if ((!bleClientConnected && now - lastBleAdvertiseMs > 10000) || bleNeedsAdvertising) {
    bleNeedsAdvertising = false;
    lastBleAdvertiseMs = now;
    BLEDevice::startAdvertising();
  }
}
