#include <Arduino.h>
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "driver/i2s.h"

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
static const int SAMPLE_COUNT = 512;

bool cameraReady = false;
bool micReady = false;
bool streamEvents = true;
bool sensorInitAttempted = false;
unsigned long lastSensorMs = 0;
unsigned long lastHeartbeatMs = 0;
int loudThreshold = 900;
int quietThreshold = 80;

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
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = psramFound() ? FRAMESIZE_QVGA : FRAMESIZE_QQVGA;
  config.jpeg_quality = 16;
  config.fb_count = psramFound() ? 2 : 1;
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
  config.dma_buf_len = 256;
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
  if (streamEvents) Serial.println("BUDDY event face");
  esp_camera_fb_return(fb);
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
}

void handleCommand(String line) {
  line.trim();
  line.toLowerCase();
  if (line.length() == 0) return;

  if (line == "status") {
    printJsonStatus("status");
  } else if (line == "init") {
    initSensors();
  } else if (line == "capture" || line == "frame") {
    captureFrame();
  } else if (line == "stream on") {
    streamEvents = true;
    Serial.println("stream=on");
  } else if (line == "stream off") {
    streamEvents = false;
    Serial.println("stream=off");
  } else if (line.startsWith("threshold ")) {
    loudThreshold = max(10, (int)line.substring(10).toInt());
    Serial.printf("threshold=%d\n", loudThreshold);
  } else if (line == "help") {
    Serial.println("commands: status, init, capture, stream on, stream off, threshold <level>, help");
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
  printJsonStatus("boot");
  Serial.println("commands: status, init, capture, stream on, stream off, threshold <level>, help");
  led(false);
}

void loop() {
  processSerial();

  unsigned long now = millis();
  if (now - lastHeartbeatMs > 3000 && !sensorInitAttempted) {
    printJsonStatus("waiting_for_init");
    lastHeartbeatMs = now;
  }

  if (sensorInitAttempted && now - lastSensorMs >= 250) {
    lastSensorMs = now;
    int level = readMicLevel();
    if (level >= 0) {
      if (streamEvents && level > loudThreshold) {
        Serial.printf("BUDDY event sound:loud level=%d\n", level);
      } else if (streamEvents && level < quietThreshold && random(0, 120) == 0) {
        Serial.println("BUDDY event sound:quiet");
      }
      if (now - lastHeartbeatMs > 2000) {
        printJsonStatus("mic", level);
        lastHeartbeatMs = now;
      }
    }
  }
}
