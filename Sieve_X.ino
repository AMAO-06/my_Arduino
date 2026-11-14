#include <A.M.A.O_06-project-1_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"

#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include "sieve.h"
#include "dcmotor.h"
#include <stdarg.h>


// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"

// ===========================
// Enter your WiFi credentials
// ===========================
const char *ssid = "A.M.A.O.";
const char *password = "@2a0m0a6o#";

void startCameraServer();
void setupLedFlash();

// ===========================
// Edge Impulse inference + UART link to the sorting ESP32
// ===========================
// This board (AI-Thinker ESP32-CAM) only classifies. Every Serial line it
// prints is also mirrored over a second UART to the separate ESP32 that
// owns the servo, the L298N-driven conveyor motor, and the MQ135 gas
// sensor -- so its serial monitor shows everything this board is doing too.
//
// Wiring (cross-connected, share a common GND):
//   ESP32-CAM  GPIO14 (TX2)  ->  Sorting ESP32  RX2 (GPIO16)
//   ESP32-CAM  GPIO15 (RX2)  <-  Sorting ESP32  TX2 (GPIO17)
//
// GPIO14/15 are free on the AI-Thinker board (not used by the camera, the
// flash LED on GPIO4, or the USB/debug UART on GPIO1/3). If you later add an
// SD card (HS2 bus: GPIO2/4/12/14/15), move this UART to GPIO12/13 instead.
#define LINK_UART_TX_PIN          14
#define LINK_UART_RX_PIN          15
#define LINK_UART_BAUD          115200

// Line prefixes so the sorting ESP32 can tell an actual sort command apart
// from a mirrored log line (otherwise a log line starting with 'R' could
// get misread as the "ripe" command).
#define LINK_CMD_PREFIX          "CMD:"
#define LINK_LOG_PREFIX          "LOG:"

// Command letters sent to the sorting ESP32 after each classification.
#define CMD_RIPE                'R'   // ripe or old -> good fruit bin
#define CMD_DAMAGED             'D'   // damaged -> reject bin
#define CMD_UNRIPE               'U'   // unsure -> hold at gas-sensor station

// How often (ms) to grab a frame and classify it, while the web server
// keeps streaming independently in its own task. Tune this against your
// conveyor speed so a fruit doesn't pass the camera between checks.
#define INFERENCE_INTERVAL_MS    2000

// Raw frame we capture from the sensor before Edge Impulse crops/resizes
// it down to the model's input size (EI_CLASSIFIER_INPUT_WIDTH/HEIGHT).
// This must match the runtime frame size set on the sensor (FRAMESIZE_QVGA
// = 320x240) further down in setup().
#define EI_CAMERA_RAW_FRAME_BUFFER_COLS   320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS   240
#define EI_CAMERA_FRAME_BYTE_SIZE            3

static HardwareSerial linkSerial(2);  // UART2, remapped to the pins above
static uint8_t *ei_snapshot_buf = nullptr;  // holds the raw->cropped frame for inference
static unsigned long lastInferenceMs = 0;

bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf);
static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr);
void runFruitInference();
void logPrint(const String &s);
void logPrintln(const String &s);
void logPrintf(const char *fmt, ...);

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // Bring up the UART link to the sorting ESP32 first so every log line
  // from here on (WiFi connecting, camera init, etc.) gets mirrored too.
  linkSerial.begin(LINK_UART_BAUD, SERIAL_8N1, LINK_UART_RX_PIN, LINK_UART_TX_PIN);

  camera_config_t config;
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    logPrintf("Camera init failed with error 0x%x\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  logPrint("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    logPrint(".");
  }
  logPrintln("");
  logPrintln("WiFi connected");

  startCameraServer();

  logPrint("Camera Ready! Use 'http://");
  logPrint(WiFi.localIP().toString());
  logPrintln("' to connect");

  logPrintln("Edge Impulse fruit classifier ready");
}

void loop() {
  // The web server runs the streaming/snapshot work in its own task.
  // We just periodically grab a frame here and classify it.
  if (millis() - lastInferenceMs >= INFERENCE_INTERVAL_MS) {
    lastInferenceMs = millis();
    runFruitInference();
  }
  delay(10);
}

/**
 * @brief Capture a frame, run the Edge Impulse classifier on it, print the
 *        winning label + confidence, and send the result to the sorting
 *        ESP32.
 */
void runFruitInference() {
  // Allocate in PSRAM if available (this buffer is ~230KB at QVGA, too big
  // for the ESP32's internal RAM to spare alongside WiFi + the camera).
  ei_snapshot_buf = (uint8_t *)ps_malloc(
      EI_CAMERA_RAW_FRAME_BUFFER_COLS * EI_CAMERA_RAW_FRAME_BUFFER_ROWS * EI_CAMERA_FRAME_BYTE_SIZE);

  if (ei_snapshot_buf == nullptr) {
    logPrintln("ERR: Failed to allocate snapshot buffer");
    return;
  }

  ei::signal_t signal;
  signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
  signal.get_data = &ei_camera_get_data;

  if (!ei_camera_capture((size_t)EI_CLASSIFIER_INPUT_WIDTH, (size_t)EI_CLASSIFIER_INPUT_HEIGHT, ei_snapshot_buf)) {
    logPrintln("Failed to capture image for inference");
    free(ei_snapshot_buf);
    ei_snapshot_buf = nullptr;
    return;
  }

  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false /* debug */);

  free(ei_snapshot_buf);
  ei_snapshot_buf = nullptr;

  if (err != EI_IMPULSE_OK) {
    logPrintf("ERR: run_classifier failed (%d)\n", err);
    return;
  }

  // Find the highest-confidence class
  size_t best_ix = 0;
  float best_value = 0.0f;
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (result.classification[i].value > best_value) {
      best_value = result.classification[i].value;
      best_ix = i;
    }
  }
  const char *best_label = ei_classifier_inferencing_categories[best_ix];

  logPrintf("Prediction: %s (%.1f%% confidence)\n", best_label, best_value * 100.0f);
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    logPrintf("  %s: %.1f%%\n", ei_classifier_inferencing_categories[i], result.classification[i].value * 100.0f);
  }

  // ripe/old -> good fruit, damaged -> reject, unripe -> hold at gas station
  char cmd;
  if (strcmp(best_label, "ripe") == 0 || strcmp(best_label, "old") == 0) {
    cmd = CMD_RIPE;
    logPrintln("-> Sending RIPE to sorting ESP32");
  } else if (strcmp(best_label, "damaged") == 0) {
    cmd = CMD_DAMAGED;
    logPrintln("-> Sending DAMAGED to sorting ESP32");
  } else {
    cmd = CMD_UNRIPE;
    logPrintln("-> Sending UNRIPE to sorting ESP32 (gas sensor stage)");
  }

  linkSerial.print(LINK_CMD_PREFIX);
  linkSerial.write(cmd);
  linkSerial.write('\n');
}

/**
 * @brief Capture a frame from the (already-initialized) camera, convert it
 *        to RGB888, and crop/resize it down to the model's input size.
 */
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    logPrintln("Camera capture failed");
    return false;
  }

  bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, out_buf);
  esp_camera_fb_return(fb);

  if (!converted) {
    logPrintln("RGB conversion failed");
    return false;
  }

  bool do_resize = (img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS)
                 || (img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS);

  if (do_resize) {
    ei::image::processing::crop_and_interpolate_rgb888(
        out_buf,
        EI_CAMERA_RAW_FRAME_BUFFER_COLS,
        EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
        out_buf,
        img_width,
        img_height);
  }

  return true;
}

static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr) {
  size_t pixel_ix = offset * 3;
  size_t pixels_left = length;
  size_t out_ptr_ix = 0;

  while (pixels_left != 0) {
    // Swap BGR -> RGB (see https://github.com/espressif/esp32-camera/issues/379)
    out_ptr[out_ptr_ix] = (ei_snapshot_buf[pixel_ix + 2] << 16)
                         + (ei_snapshot_buf[pixel_ix + 1] << 8)
                         + ei_snapshot_buf[pixel_ix];
    out_ptr_ix++;
    pixel_ix += 3;
    pixels_left--;
  }
  return 0;
}

/**
 * @brief Print a line to the local USB serial monitor AND mirror it, tagged
 *        with "LOG:", over the UART link so the sorting ESP32's serial
 *        monitor can show it too.
 */
void logPrint(const String &s) {
  Serial.print(s);
  linkSerial.print(LINK_LOG_PREFIX);
  linkSerial.print(s);
}

void logPrintln(const String &s) {
  Serial.println(s);
  linkSerial.print(LINK_LOG_PREFIX);
  linkSerial.println(s);
}

void logPrintf(const char *fmt, ...) {
  char buf[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
  linkSerial.print(LINK_LOG_PREFIX);
  linkSerial.print(buf);
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "This impulse was not trained for camera input"
#endif