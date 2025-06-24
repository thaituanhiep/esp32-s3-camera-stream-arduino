#include "esp_camera.h"
#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "esp_http_server.h"

// ========== CONFIGURATION ========== //
#define DEBUG 1
#if DEBUG
#define DEBUG_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUG_LOG(...)
#endif

// ----- WiFi Settings ----- //
const char *ssid = "realme GT3 240W";
const char *pass = "12345678";

// ----- DS18B20 Temperature Sensor ----- //
#define DS18B20_PIN 1
OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);
volatile float currentTemp = NAN;
uint32_t lastTempUpdate = 0;

// ----- Camera Pin Definitions ----- //
#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5
#define CAM_PIN_D7 16
#define CAM_PIN_D6 17
#define CAM_PIN_D5 18
#define CAM_PIN_D4 12
#define CAM_PIN_D3 10
#define CAM_PIN_D2 8
#define CAM_PIN_D1 9
#define CAM_PIN_D0 11
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13

static camera_config_t cam = {
  .pin_pwdn = CAM_PIN_PWDN,
  .pin_reset = CAM_PIN_RESET,
  .pin_xclk = CAM_PIN_XCLK,
  .pin_sccb_sda = CAM_PIN_SIOD,
  .pin_sccb_scl = CAM_PIN_SIOC,
  .pin_d7 = CAM_PIN_D7,
  .pin_d6 = CAM_PIN_D6,
  .pin_d5 = CAM_PIN_D5,
  .pin_d4 = CAM_PIN_D4,
  .pin_d3 = CAM_PIN_D3,
  .pin_d2 = CAM_PIN_D2,
  .pin_d1 = CAM_PIN_D1,
  .pin_d0 = CAM_PIN_D0,
  .pin_vsync = CAM_PIN_VSYNC,
  .pin_href = CAM_PIN_HREF,
  .pin_pclk = CAM_PIN_PCLK,
  .xclk_freq_hz = 24000000,
  .ledc_timer = LEDC_TIMER_0,
  .ledc_channel = LEDC_CHANNEL_0,
  .pixel_format = PIXFORMAT_JPEG,
  .frame_size = FRAMESIZE_SVGA,
  .jpeg_quality = 10,
  .fb_count = 2,
  .grab_mode = CAMERA_GRAB_LATEST
};

// ========== HTTP SERVERS & HANDLERS ========== //
static httpd_handle_t stream_httpd = NULL;
static httpd_handle_t control_httpd = NULL;

// HTML page: served on control port 80
static const char PROGMEM page_index[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32-S3 Monitoring</title>
  <style>
    * { box-sizing: border-box; font-family: system-ui, Arial; }
    body, html { height: 100%; margin: 0; background: #111; color: #eee; }
    header { padding: 10px 20px; background: #20232a; display: flex; justify-content: space-between; align-items: center; }
    #wrapper { display: flex; height: calc(100% - 60px); flex-wrap: wrap; }
    #camBox, #infoBox { flex: 1; min-width: 300px; margin: 10px; background: #181b23; border-radius: 12px; overflow: hidden; }
    #cam { width: 100%; height: 100%; object-fit: cover; }
    .sensor-container { padding: 15px; }
    .sensor-title { margin-bottom: 15px; color: #aaa; }
    .temp-display { font-size: 24px; font-weight: bold; margin: 15px 0; padding: 10px; border-radius: 8px; background: #20232a; display: inline-block; min-width: 200px; text-align: center; }
    .last-update { font-size: 12px; color: #777; margin-top: 5px; }
  </style>
</head>
<body>
  <header>
    <h1>ESP32-S3 Monitoring</h1>
    <div>IP: <span id="ipAddress"></span></div>
  </header>
  <div id="wrapper">
    <div id="camBox"><img id="cam" alt="Camera Stream"></div>
    <div id="infoBox">
      <div class="sensor-container">
        <h2 class="sensor-title">TEMPERATURE MONITOR</h2>
        <div class="temp-display" id="tempData">Loading...</div>
        <div class="last-update" id="lastUpdate"></div>
      </div>
    </div>
  </div>
  <script>
    const host = window.location.hostname;
    document.getElementById('ipAddress').textContent = host;
    const camImg = document.getElementById('cam');
    camImg.src = `http://${host}:81/stream`;
    camImg.onerror = () => { camImg.src = `http://${host}:81/stream?t=${Date.now()}`; };
    async function updateTemperature() {
      try {
        const res = await fetch(`http://${host}/sensor?t=${Date.now()}`);
        if (!res.ok) throw 'Err';
        const txt = await res.text();
        document.getElementById('tempData').textContent = `${txt} °C`;
        document.getElementById('lastUpdate').textContent = 'Last update: ' + new Date().toLocaleTimeString();
      } catch (e) {
        document.getElementById('tempData').textContent = 'Error';
      } finally {
        setTimeout(updateTemperature, 2000);
      }
    }
    updateTemperature();
  </script>
</body>
</html>
)rawliteral";

// STREAM handler on port 81
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb;
  esp_err_t res = ESP_OK;
  static const char *boundary = "--frame";
  char hdr[64];
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      res = ESP_FAIL;
      break;
    }
    snprintf(hdr, sizeof(hdr), "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
    if (httpd_resp_send_chunk(req, boundary, strlen(boundary)) != ESP_OK || httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK || httpd_resp_send_chunk(req, hdr, strlen(hdr)) != ESP_OK || httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len) != ESP_OK || httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }
    esp_camera_fb_return(fb);
  }
  return res;
}

// INDEX handler on port 80
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  httpd_resp_set_hdr(req, "Expires", "0");
  return httpd_resp_send(req, page_index, strlen(page_index));
}

// SENSOR handler on port 80
static esp_err_t sensor_handler(httpd_req_t *req) {
  char buf[20];
  if (isnan(currentTemp)) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    strcpy(buf, "Error");
  } else {
    httpd_resp_set_status(req, "200 OK");
    snprintf(buf, sizeof(buf), "%.2f", currentTemp);
  }
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
  return httpd_resp_sendstr(req, buf);
}

// START camera stream server on port 81
void startCameraServer() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 81;
  cfg.ctrl_port = 32769;
  cfg.max_open_sockets = 3;
  cfg.lru_purge_enable = true;
  if (httpd_start(&stream_httpd, &cfg) == ESP_OK) {
    static httpd_uri_t uri_stream = { "/stream", HTTP_GET, stream_handler };
    httpd_register_uri_handler(stream_httpd, &uri_stream);
    DEBUG_LOG("[HTTP] Stream server on 81\n");
  }
}

// START control server on port 80
void startControlServer() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 80;
  cfg.ctrl_port = 32768;
  cfg.max_open_sockets = 5;
  cfg.lru_purge_enable = true;
  if (httpd_start(&control_httpd, &cfg) == ESP_OK) {
    static httpd_uri_t uri_index = { "/", HTTP_GET, index_handler };
    static httpd_uri_t uri_sensor = { "/sensor", HTTP_GET, sensor_handler };
    httpd_register_uri_handler(control_httpd, &uri_index);
    httpd_register_uri_handler(control_httpd, &uri_sensor);
    DEBUG_LOG("[HTTP] Control server on 80\n");
  }
}

// ========== SETUP & LOOP ========== //
void setup() {
  Serial.begin(115200);
  delay(1000);
  sensors.begin();
  if (sensors.getDeviceCount() == 0) DEBUG_LOG("[DS18B20] No sensor found!\n");
  if (esp_camera_init(&cam) != ESP_OK) {
    DEBUG_LOG("[CAMERA] Init failed!\n");
    while (true) delay(1000);
  }
  WiFi.begin(ssid, pass);
  DEBUG_LOG("[WiFi] Connecting...\n");
  while (WiFi.status() != WL_CONNECTED) delay(500);
  DEBUG_LOG("[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
  startCameraServer();
  startControlServer();
  currentTemp = sensors.getTempCByIndex(0);
  lastTempUpdate = millis();
}

void loop() {
  // Đọc nhiệt độ mới từ cảm biến mỗi 2 giây
  if (millis() - lastTempUpdate > 2000) {
    sensors.requestTemperatures();             // Gửi lệnh lấy nhiệt độ
    currentTemp = sensors.getTempCByIndex(0);  // Đọc kết quả
    lastTempUpdate = millis();
    DEBUG_LOG("[TEMP] %.2f C\n", currentTemp);
  }
  delay(10);
}
