#include <WiFi.h>
#include <esp32-hal-ledc.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "esp_http_server.h"

const char* ssid = "153";
const char* password = "12345678";

const char* apssid = "ESP32-Chu";
const char* appassword = "12345678";

typedef struct {
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY= "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART ="Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

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

void initCamera()
{
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
 config.pin_sscb_sda = SIOD_GPIO_NUM;
 config.pin_sscb_scl = SIOC_GPIO_NUM;
 config.pin_pwdn = PWDN_GPIO_NUM;
 config.pin_reset = RESET_GPIO_NUM;
 config.xclk_freq_hz = 20000000;
 config.pixel_format = PIXFORMAT_JPEG; 
  
 if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
 } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
 }
  
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
}
static size_t jpg_encode_stream(void *arg,size_t index, const void* data, size_t len)
{
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if(!index)
    {
        j->len=0;
    }
    if (httpd_resp_send_chunk(j->req,(const char *)data,len)!=ESP_OK)
    {
        return 0;
    }
    j ->len += len;
    return len;
    
}
static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;

    fb= esp_camera_fb_get();
    if(!fb)
    {
        Serial.println("Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content_Disposition","inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Contorl-Allow-Orgin","*");

    size_t fb_len=0;
    if(fb->format == PIXFORMAT_JPEG)
    {
        fb_len = fb->len;
        res = httpd_resp_send(req, (const char*) fb ->buf,fb->len);
    }
    else
    {
        jpg_chunking_t jchunk = {req,0};
        res = frame2jpg_cb(fb, 80, jpg_encode_stream,&jchunk)?ESP_OK:ESP_FAIL;
        httpd_resp_send_chunk(req,NULL,0);
        fb_len = jchunk.len;
    }
    esp_camera_fb_return(fb);
    return res;

}
static esp_err_t stream_handler(httpd_req *req)
{
    camera_fb_t *fb=NULL;
    esp_err_t res =ESP_OK;
    size_t _jpg_buf_len =0;
    uint8_t *_jpg_buf =NULL;
    char *part_buff[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
    {
        return res;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin","*");
    while (true)
    {
        fb = esp_camera_fb_get();
        if(!fb)
        {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
        }
        else
        {
            if(fb ->format != PIXFORMAT_JPEG)
            {
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                esp_camera_fb_return(fb);
                fb=NULL;
                if(!jpeg_converted)
                {
                    Serial.println("JPEGG compression failed");
                    res = ESP_FAIL;
                }
                else
                {
                    _jpg_buf_len =fb->len;
                    _jpg_buf = fb ->buf;
                }
            }
        }
    if(res == ESP_OK)
    {
        res = httpd_resp_send_chunk(req, (const char *)_jpg_buf,_jpg_buf_len);
    }
    if (res == ESP_OK)
    {
        res = httpd_resp_send_chunk(req , _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    

    }
    
    
}
void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t index_uri ={
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL
    };

    httpd_uri_t status_uri ={
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL
    };

    httpd_uri_t cmd_uri ={
        .uri = "/control",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = NULL
    };

    httpd_uri_t capture_uri ={
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
        .user_ctx = NULL
    };

    httpd_uri_t stream_uri ={
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL
    };

    Serial.printf("Starting web server on port: '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config)==ESP_OK){
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
    }

    config.server_port +=1;
    config.ctrl_port +=1;
    Serial.printf("Starting stream server on port : '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config)==ESP_OK){
        httpd_register_uri_handler(camera_httpd, &stream_uri);
    }

}

void setup()
{
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    Serial.begin(115200);
    Serial.println();
    initCamera();

    ledcAttachPin(4, 4);
    ledcSetup(4,5000,8);

    WiFi.mode(WIFI_AP_STA);

    for (int i=0;i<2;i++){
        WiFi.begin(ssid,password);
        delay(1000);
        Serial.println("");
        Serial.print("Connecting to ");
        Serial.println(ssid);

        long int StartTime=millis();
        while (WiFi.status() != WL_CONNECTED){
            delay(500);
            if((StartTime+5000) < millis()) break;
        }

        if(WiFi.status() == WL_CONNECTED){
            WiFi.softAP(WiFi.localIP().toString()+"_"+(String)apssid.c_str(),appassword);
            Serial.println("");
            Serial.println("STAIP address: ");
            Serial.println(WiFi.localIP());
            Serial.println("");
        }
    }

    Serial.println("");
    Serial.println("APIP address: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("");

    startCameraServer();

}

void loop()
{
    
}
