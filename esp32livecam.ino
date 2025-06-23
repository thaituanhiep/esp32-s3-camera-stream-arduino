/**
 *  Minimal Camera WebServer for ESP32-S3-EYE
 *  Stream MJPEG + Snapshot qua HTTP  (full-screen + UI layout)
 *  — thaitl 06-2025
 */

#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "esp_http_server.h"

/* ─────────── Wi-Fi ─────────── */
const char* ssid = "realme GT3 240W";
const char* pass = "12345678";

/* ────────── Pinout S3-EYE ────────── */
#define CAM_PIN_PWDN  -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK  15
#define CAM_PIN_SIOD  4
#define CAM_PIN_SIOC  5
#define CAM_PIN_D7    16
#define CAM_PIN_D6    17
#define CAM_PIN_D5    18
#define CAM_PIN_D4    12
#define CAM_PIN_D3    10
#define CAM_PIN_D2    8
#define CAM_PIN_D1    9
#define CAM_PIN_D0    11
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF  7
#define CAM_PIN_PCLK  13

/* ───────── MJPEG stream handler ───────── */
static httpd_handle_t stream_httpd = nullptr;

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb;
  static const char* BOUND="--frame";
  char hdr[64];

  httpd_resp_set_type(req,
    "multipart/x-mixed-replace;boundary=frame");

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) return ESP_FAIL;

    snprintf(hdr,sizeof(hdr),
      "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",fb->len);

    if (  httpd_resp_send_chunk(req, BOUND, strlen(BOUND)) != ESP_OK ||
          httpd_resp_send_chunk(req, "\r\n", 2)               != ESP_OK ||
          httpd_resp_send_chunk(req, hdr, strlen(hdr))        != ESP_OK ||
          httpd_resp_send_chunk(req, (char*)fb->buf, fb->len) != ESP_OK ||
          httpd_resp_send_chunk(req, "\r\n", 2)               != ESP_OK ){
      esp_camera_fb_return(fb);
      break;
    }
    esp_camera_fb_return(fb);
  }
  return ESP_OK;
}

/* ─── HTML UI (header + 2-column layout) ─── */
static const char PROGMEM page_index[] = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-S3-Cam</title>
<style>
  *{box-sizing:border-box;font-family:system-ui,Arial}
  body,html{height:100%;margin:0;background:#111;color:#eee}
  header{padding:10px 20px;background:#20232a}
  header h1{margin:0;font-size:18px}
  #wrapper{display:flex;height:calc(100% - 60px);}
  #camBox,#infoBox{flex:1;margin:10px;background:#181b23;border-radius:12px;overflow:hidden}
  #cam{width:100%;height:100%;object-fit:cover}
  /* cảm giác “đẹp mắt” hơn chút */
</style>
</head><body>
<header>
  <h1>ESP32-S3 Camera • <span id="ip"></span></h1>
</header>

<div id="wrapper">
  <div id="camBox"><img id="cam" src="/stream"></div>
  <div id="infoBox">
    <!-- Bạn render dữ liệu cảm biến hoặc đồ thị tại đây -->
    <div style="padding:15px">
      <h2>Sensor data</h2>
      <pre id="sensors">Waiting...</pre>
    </div>
  </div>
</div>

<script>
  /* Hiển thị IP nhận từ server (replace at runtime) */
  fetch('/ip').then(r=>r.text()).then(txt=>{
    document.getElementById('ip').textContent = txt;
  });

  /* ví dụ lấy data cảm biến 5s/lần */
  setInterval(()=>{
      fetch('/sensor').then(r=>r.text())
        .then(t=>document.getElementById('sensors').textContent=t)
        .catch(()=>{});
  },5000);
</script></body></html>
)rawliteral";

/* index & helper endpoints */
static esp_err_t index_handler(httpd_req_t *req){
  httpd_resp_set_type(req,"text/html");
  return httpd_resp_send(req, (const char*)page_index,
                         strlen(page_index));
}
static esp_err_t ip_handler(httpd_req_t *req){
  String ip = WiFi.localIP().toString();
  return httpd_resp_sendstr(req, ip.c_str());
}
/* mock sensor endpoint – bạn thay thế bằng dữ liệu thực */
static esp_err_t sensor_handler(httpd_req_t *req){
  static uint32_t counter=0; counter++;
  char buf[64];
  snprintf(buf,sizeof(buf),
           "Temp : %d °C\nCount: %lu",
           25+counter%5, (unsigned long)counter);
  return httpd_resp_sendstr(req, buf);
}

/* ─── start HTTP server ─── */
void startServer(){
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.ctrl_port = 32768;

  httpd_uri_t uri_idx = { .uri="/",       .method=HTTP_GET, .handler=index_handler };
  httpd_uri_t uri_str = { .uri="/stream", .method=HTTP_GET, .handler=stream_handler };
  httpd_uri_t uri_ip  = { .uri="/ip",     .method=HTTP_GET, .handler=ip_handler };
  httpd_uri_t uri_sen = { .uri="/sensor", .method=HTTP_GET, .handler=sensor_handler };

  if (httpd_start(&stream_httpd,&cfg)==ESP_OK){
      httpd_register_uri_handler(stream_httpd,&uri_idx);
      httpd_register_uri_handler(stream_httpd,&uri_str);
      httpd_register_uri_handler(stream_httpd,&uri_ip);
      httpd_register_uri_handler(stream_httpd,&uri_sen);
  }
}

/* ─── camera config ─── */
static camera_config_t cam = {
  .pin_pwdn = CAM_PIN_PWDN, .pin_reset = CAM_PIN_RESET, .pin_xclk  = CAM_PIN_XCLK,
  .pin_sccb_sda = CAM_PIN_SIOD, .pin_sccb_scl = CAM_PIN_SIOC,
  .pin_d7 = CAM_PIN_D7, .pin_d6 = CAM_PIN_D6, .pin_d5 = CAM_PIN_D5, .pin_d4 = CAM_PIN_D4,
  .pin_d3 = CAM_PIN_D3, .pin_d2 = CAM_PIN_D2, .pin_d1 = CAM_PIN_D1, .pin_d0 = CAM_PIN_D0,
  .pin_vsync = CAM_PIN_VSYNC, .pin_href = CAM_PIN_HREF, .pin_pclk = CAM_PIN_PCLK,

  .xclk_freq_hz = 24000000,               // 10 MHz an toàn cho cảm biến nhỏ
  .ledc_timer   = LEDC_TIMER_0,
  .ledc_channel = LEDC_CHANNEL_0,
  .pixel_format = PIXFORMAT_JPEG,
  .frame_size   = FRAMESIZE_SVGA,         // 320×240
  .jpeg_quality = 12,
  .fb_count     = 3,
  .grab_mode    = CAMERA_GRAB_LATEST
};

/* ─── SETUP ─── */
void setup(){
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nBooting…");

  /* Camera */
  if (esp_camera_init(&cam)!=ESP_OK){
      Serial.println("Camera init failed!");
  }

  /* Wi-Fi */
  WiFi.begin(ssid,pass);
  Serial.print("Wi-Fi");
  while(WiFi.status()!=WL_CONNECTED){Serial.print('.');delay(200);}
  Serial.printf("\nConnected: %s\n", WiFi.localIP().toString().c_str());

  startServer();
  Serial.println("HTTP server started");
}

/* ─── LOOP ─── */
void loop(){
  delay(10000);   // nhàn – mọi thứ hoạt động qua interrupts
}
