#include "esp_camera.h"
#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "esp_http_server.h"
#include "sd_read_write.h"

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

// ========== SD CARD & PHOTO SETTINGS ========== //
#define PHOTO_DIRECTORY "/photos"
volatile bool captureFlag = false;
String photoList = "";
uint32_t lastPhotoListUpdate = 0;
bool photoListNeedsUpdate = true;

// ========== HTTP SERVERS & HANDLERS ========== //
static httpd_handle_t stream_httpd = NULL;  // Port 81 - Camera stream
static httpd_handle_t control_httpd = NULL; // Port 80 - Control interface
static httpd_handle_t photo_httpd = NULL;   // Port 82 - Photo server

// HTML page: served on control port 80
static const char PROGMEM page_index[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32-S3 Monitoring</title>
  <style>
    * {
      box-sizing: border-box;
      font-family: system-ui, Arial, sans-serif;
      margin: 0;
      padding: 0;
    }
    body, html {
      height: 100%;
      margin: 0;
      background: #111;
      color: #eee;
    }
    header {
      padding: 15px 20px;
      background: #20232a;
      display: flex;
      justify-content: space-between;
      align-items: center;
      border-bottom: 1px solid #333;
    }
    h1 {
      font-size: 1.5rem;
      color: #4CAF50;
    }
    #wrapper {
      display: flex;
      height: calc(100% - 60px);
      flex-wrap: wrap;
      padding: 10px;
    }
    #camBox, #infoBox {
      flex: 1;
      min-width: 300px;
      margin: 10px;
      background: #181b23;
      border-radius: 12px;
      overflow: hidden;
      box-shadow: 0 4px 8px rgba(0,0,0,0.2);
    }
    #cam {
      width: 100%;
      height: 100%;
      object-fit: cover;
    }
    .sensor-container {
      padding: 20px;
    }
    .sensor-title {
      margin: 15px 0 10px 0;
      color: #aaa;
      font-size: 1.2rem;
      font-weight: normal;
    }
    .temp-display {
      font-size: 2rem;
      font-weight: bold;
      margin: 10px 0;
      padding: 15px;
      border-radius: 8px;
      background: #20232a;
      display: inline-block;
      min-width: 200px;
      text-align: center;
      color: #4CAF50;
    }
    .last-update {
      font-size: 0.9rem;
      color: #777;
      margin-bottom: 20px;
    }
    #captureBtn {
      background: #4CAF50;
      border: none;
      color: white;
      padding: 12px 24px;
      text-align: center;
      text-decoration: none;
      display: inline-block;
      font-size: 1rem;
      margin: 10px 0;
      cursor: pointer;
      border-radius: 5px;
      transition: background 0.3s;
      width: 100%;
    }
    #captureBtn:hover {
      background: #45a049;
    }
    #photoList {
      margin-top: 15px;
      max-height: 300px;
      overflow-y: auto;
      background: #20232a;
      border-radius: 8px;
      padding: 10px;
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(150px, 1fr));
      gap: 10px;
    }
    .photo-item {
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 10px;
      margin: 0;
      background: #2c2f36;
      border-radius: 4px;
      cursor: pointer;
      transition: transform 0.2s;
    }
    .photo-item:hover {
      background: #3a3e46;
      transform: scale(1.02);
    }
    .photo-thumbnail {
      width: 100%;
      height: 100px;
      object-fit: cover;
      border-radius: 4px;
      margin-bottom: 5px;
    }
    .photo-name {
      font-size: 0.8rem;
      text-align: center;
      word-break: break-all;
      color: #ddd;
    }
    #ipAddress {
      font-family: monospace;
      color: #4CAF50;
    }
    #photoViewer {
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: rgba(0,0,0,0.9);
      display: none;
      justify-content: center;
      align-items: center;
      z-index: 1000;
    }
    #photoViewer img {
      max-width: 90%;
      max-height: 90%;
    }
    #closeViewer {
      position: absolute;
      top: 20px;
      right: 20px;
      color: white;
      font-size: 30px;
      cursor: pointer;
    }
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

        <h2 class="sensor-title">PHOTO CAPTURE</h2>
        <button id="captureBtn">Capture Photo</button>

        <h2 class="sensor-title">PHOTO GALLERY</h2>
        <div id="photoList">Loading photos...</div>
      </div>
    </div>
  </div>

  <div id="photoViewer">
    <span id="closeViewer">&times;</span>
    <img id="viewedPhoto">
  </div>

  <script>
    const host = window.location.hostname;
    document.getElementById('ipAddress').textContent = host;
    const camImg = document.getElementById('cam');
    camImg.src = `http://${host}:81/stream`;
    camImg.onerror = () => { camImg.src = `http://${host}:81/stream?t=${Date.now()}`; };

    // Capture button
    document.getElementById('captureBtn').addEventListener('click', async () => {
      try {
        const res = await fetch(`http://${host}/capture`);
        if (res.ok) {
          alert('Photo captured successfully!');
          setTimeout(updatePhotoList, 10000); // Update photo list after 10 seconds
        }
      } catch (e) {
        alert('Error capturing photo');
      }
    });

    // Update temperature
    async function updateTemperature() {
      try {
        const res = await fetch(`http://${host}/sensor?t=${Date.now()}`);
        if (!res.ok) throw 'Err';
        const txt = await res.text();
        document.getElementById('tempData').textContent = `${txt} °C`;
        document.getElementById('lastUpdate').textContent = 'Last update: ' + new Date().toLocaleTimeString([], {hour: '2-digit', minute:'2-digit', second:'2-digit'});
      } catch (e) {
        document.getElementById('tempData').textContent = 'Error';
      } finally {
        setTimeout(updateTemperature, 2000);
      }
    }

    // Update photo list
    async function updatePhotoList() {
      try {
        const res = await fetch(`http://${host}/photolist?t=${Date.now()}`);
        if (!res.ok) throw 'Err';
        const html = await res.text();
        document.getElementById('photoList').innerHTML = html || '<div style="grid-column: 1 / -1; text-align: center; color: #777;">No photos found</div>';
      } catch (e) {
        document.getElementById('photoList').innerHTML = '<div style="grid-column: 1 / -1; text-align: center; color: #f55;">Error loading photos</div>';
      }
    }

    // Show photo when clicked
    function showPhoto(filename) {
      document.getElementById('viewedPhoto').src = `http://${host}:82/photo?file=${encodeURIComponent(filename)}`;
      document.getElementById('photoViewer').style.display = 'flex';
    }

    // Close photo viewer
    document.getElementById('closeViewer').addEventListener('click', () => {
      document.getElementById('photoViewer').style.display = 'none';
    });

    // Initialize
    updateTemperature();
    updatePhotoList();
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
    if (httpd_resp_send_chunk(req, boundary, strlen(boundary)) != ESP_OK ||
        httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK ||
        httpd_resp_send_chunk(req, hdr, strlen(hdr)) != ESP_OK ||
        httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len) != ESP_OK ||
        httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) {
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

// CAPTURE handler on port 80
static esp_err_t capture_handler(httpd_req_t *req) {
  captureFlag = true;
  return httpd_resp_sendstr(req, "OK");
}

// PHOTOLIST handler on port 80
static esp_err_t photolist_handler(httpd_req_t *req) {
  if (millis() - lastPhotoListUpdate > 10000 || photoListNeedsUpdate) {
    updatePhotoList();
    lastPhotoListUpdate = millis();
    photoListNeedsUpdate = false;
  }

  httpd_resp_set_type(req, "text/html");
  return httpd_resp_sendstr(req, photoList.c_str());
}

// PHOTO handler on port 82 - NEW SERVER FOR PHOTOS
static esp_err_t photo_handler(httpd_req_t *req) {
  char query[128];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    return httpd_resp_send_404(req);
  }
  char file_param[128];
  if (httpd_query_key_value(query, "file", file_param, sizeof(file_param)) != ESP_OK) {
    DEBUG_LOG("[HTTP] No file query\n");
    return httpd_resp_send_404(req);
  }

  // Strip leading slash and build path
  String fname = String(file_param);
  if (fname.startsWith("/")) fname = fname.substring(1);
  String path = String(PHOTO_DIRECTORY) + "/" + fname;

  if (!SD_MMC.exists(path.c_str())) {
    DEBUG_LOG("[SD] File not found: %s\n", path.c_str());
    return httpd_resp_send_404(req);
  }
  File file = SD_MMC.open(path.c_str(), FILE_READ);
  if (!file) {
    DEBUG_LOG("[SD] Failed to open: %s\n", path.c_str());
    return httpd_resp_send_404(req);
  }

  httpd_resp_set_type(req, "image/jpeg");
  const size_t bufSize = 512;
  uint8_t buffer[bufSize];
  while (file.available()) {
    size_t len = file.readBytes((char*)buffer, bufSize);
    if (httpd_resp_send_chunk(req, (const char*)buffer, len) != ESP_OK) {
      file.close();
      return ESP_FAIL;
    }
    vTaskDelay(1);  // let watchdog
  }
  file.close();

  // Properly terminate the response
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
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
  cfg.max_open_sockets = 3;
  cfg.lru_purge_enable = true;
  if (httpd_start(&control_httpd, &cfg) == ESP_OK) {
    static httpd_uri_t uri_index = { "/", HTTP_GET, index_handler };
    static httpd_uri_t uri_sensor = { "/sensor", HTTP_GET, sensor_handler };
    static httpd_uri_t uri_capture = { "/capture", HTTP_GET, capture_handler };
    static httpd_uri_t uri_photolist = { "/photolist", HTTP_GET, photolist_handler };

    httpd_register_uri_handler(control_httpd, &uri_index);
    httpd_register_uri_handler(control_httpd, &uri_sensor);
    httpd_register_uri_handler(control_httpd, &uri_capture);
    httpd_register_uri_handler(control_httpd, &uri_photolist);
    DEBUG_LOG("[HTTP] Control server on 80\n");
  }
}

// START photo server on port 82
void startPhotoServer() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 82;
  cfg.ctrl_port = 32770;
  cfg.max_open_sockets = 4;
  cfg.lru_purge_enable = true;
  if (httpd_start(&photo_httpd, &cfg) == ESP_OK) {
    static httpd_uri_t uri_photo = { "/photo", HTTP_GET, photo_handler };
    httpd_register_uri_handler(photo_httpd, &uri_photo);
    DEBUG_LOG("[HTTP] Photo server on 82\n");
  }
}

// Capture and save photo to SD card
void captureAndSavePhoto() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    DEBUG_LOG("[CAMERA] Capture failed\n");
    return;
  }

  // Create filename based on current time
  char filename[32];
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    strftime(filename, sizeof(filename), PHOTO_DIRECTORY "/%Y-%m-%d_%H-%M-%S.jpg", &timeinfo);
  } else {
    sprintf(filename, PHOTO_DIRECTORY "/photo_%lu.jpg", millis());
  }

  // Create directory if not exists
  if (!SD_MMC.exists(PHOTO_DIRECTORY)) {
    if (!SD_MMC.mkdir(PHOTO_DIRECTORY)) {
      DEBUG_LOG("[SD] Failed to create photos directory\n");
      esp_camera_fb_return(fb);
      return;
    }
  }

  // Save photo
  writejpg(SD_MMC, filename, fb->buf, fb->len);
  esp_camera_fb_return(fb);

  // Mark photo list as needing update
  photoListNeedsUpdate = true;
  DEBUG_LOG("[SD] Photo saved: %s\n", filename);
}

// Update photo list from SD card
void updatePhotoList() {
  photoList = "";

  if (!SD_MMC.exists(PHOTO_DIRECTORY)) {
    photoList = "<div style='grid-column: 1 / -1; text-align: center; color: #777;'>No photos directory found</div>";
    return;
  }

  File root = SD_MMC.open(PHOTO_DIRECTORY);
  if (!root) {
    photoList = "<div style='grid-column: 1 / -1; text-align: center; color: #f55;'>Failed to open directory</div>";
    return;
  }

  File file = root.openNextFile();
  bool hasPhotos = false;

  while (file) {
    if (!file.isDirectory()) {
      String fileName = file.name();
      if (fileName.endsWith(".jpg")) {
        hasPhotos = true;
        String shortName = fileName.substring(strlen(PHOTO_DIRECTORY) + 1);
        photoList += "<div class='photo-item' onclick='showPhoto(\"" + fileName + "\")'>";
        photoList += "<img class='photo-thumbnail' src='http://" + WiFi.localIP().toString() + ":82/photo?file=" + fileName + "'>";
        photoList += "<div class='photo-name'>" + shortName + "</div>";
        photoList += "</div>";
      }
    }
    file = root.openNextFile();
  }

  if (!hasPhotos) {
    photoList = "<div style='grid-column: 1 / -1; text-align: center; color: #777;'>No photos found</div>";
  }
}

// ========== SETUP & LOOP ========== //
void setup() {
  Serial.begin(115200);
  delay(1000);
  sensors.begin();
  if (sensors.getDeviceCount() == 0) DEBUG_LOG("[DS18B20] No sensor found!\n");
  
  // Initialize SD card
  sdmmcInit();
  removeDirRecursive(SD_MMC, PHOTO_DIRECTORY);
  if (!SD_MMC.exists(PHOTO_DIRECTORY)) {
    DEBUG_LOG("[SD] Creating photos directory\n");
    createDir(SD_MMC, PHOTO_DIRECTORY);
    if (!SD_MMC.exists(PHOTO_DIRECTORY)) {
      DEBUG_LOG("[SD] Failed to create photos directory\n");
    }
  }
  
  
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
  startPhotoServer();
  
  currentTemp = sensors.getTempCByIndex(0);
  lastTempUpdate = millis();
  
  // Initialize photo list
  updatePhotoList();
  lastPhotoListUpdate = millis();
}

void loop() {
  // Read temperature every 2 seconds
  if (millis() - lastTempUpdate > 2000) {
    sensors.requestTemperatures();
    currentTemp = sensors.getTempCByIndex(0);
    lastTempUpdate = millis();
  }
  
  // Handle capture request
  if (captureFlag) {
    captureAndSavePhoto();
    captureFlag = false;
  }
  
  delay(10);
}