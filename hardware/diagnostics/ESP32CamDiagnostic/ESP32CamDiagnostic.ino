#include <Arduino.h>
#include <esp_camera.h>
#include <esp_system.h>
#include <string.h>

// AI Thinker ESP32-CAM camera pin map.
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

static bool cameraReady = false;
static uint32_t lastAttempt = 0;
static uint32_t framesCaptured = 0;

static bool startCamera() {
  camera_config_t config;
  memset(&config, 0, sizeof(config));
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.frame_size = FRAMESIZE_QQVGA;
    config.jpeg_quality = 14;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("CAMERA_INIT_FAIL code=0x%x free_heap=%u\n", err,
                  static_cast<unsigned>(ESP.getFreeHeap()));
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  Serial.printf("CAMERA_INIT_OK pid=0x%04x psram=%s frame=%s\n",
                sensor ? sensor->id.PID : 0,
                psramFound() ? "yes" : "no",
                psramFound() ? "QVGA" : "QQVGA");
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("ESP32-CAM STANDALONE DIAGNOSTIC");
  Serial.printf("CHIP=%s REV=%u FLASH=%uKB PSRAM=%s FREE_HEAP=%u RESET_REASON=%d\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                static_cast<unsigned>(ESP.getFlashChipSize() / 1024),
                psramFound() ? "yes" : "no",
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<int>(esp_reset_reason()));
  cameraReady = startCamera();
  lastAttempt = millis();
}

void loop() {
  if (!cameraReady) {
    if (millis() - lastAttempt >= 5000) {
      lastAttempt = millis();
      Serial.println("CAMERA_RETRY");
      cameraReady = startCamera();
    }
    delay(20);
    return;
  }

  if (millis() - lastAttempt >= 3000) {
    lastAttempt = millis();
    camera_fb_t *frame = esp_camera_fb_get();
    if (!frame) {
      Serial.println("FRAME_CAPTURE_FAIL");
      return;
    }
    ++framesCaptured;
    Serial.printf("FRAME_OK count=%lu bytes=%u format=%d free_heap=%u\n",
                  static_cast<unsigned long>(framesCaptured),
                  static_cast<unsigned>(frame->len),
                  static_cast<int>(frame->format),
                  static_cast<unsigned>(ESP.getFreeHeap()));
    esp_camera_fb_return(frame);
  }
  delay(10);
}
