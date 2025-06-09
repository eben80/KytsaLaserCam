#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_http_server.h"
#include <Preferences.h>
#include <HardwareSerial.h>
#include <esp_task_wdt.h> // Required for esp_task_wdt_init and esp_task_wdt_reset

// Define the serial port to use (adjust if needed)
HardwareSerial& serialPort = Serial;
Preferences preferences;
// Define the GPIO pin for the LED (usually on GPIO 4)
const int ledPin = 4;
const char* WIFI_SSID_KEY = "wifi_ssid";
const char* WIFI_PASSWORD_KEY = "wifi_password";
volatile bool wifiConnected = false;
volatile bool streamingActive = false;
httpd_handle_t stream_httpd = NULL;

unsigned long lastCommandCheck = 0;
const long commandCheckInterval = 50; // Check for commands every 50ms (adjust as needed)

// Camera model definitions (ensure the correct one is uncommented)
#define CAMERA_MODEL_AI_THINKER
#define PART_BOUNDARY "123456789000000000000987654321"
#if defined(CAMERA_MODEL_AI_THINKER)
  #define PWDN_GPIO_NUM   32
  #define RESET_GPIO_NUM  -1
  #define XCLK_GPIO_NUM    0
  #define SIOD_GPIO_NUM   26
  #define SIOC_GPIO_NUM   27

  #define Y9_GPIO_NUM     35
  #define Y8_GPIO_NUM     34
  #define Y7_GPIO_NUM     39
  #define Y6_GPIO_NUM     36
  #define Y5_GPIO_NUM     21
  #define Y4_GPIO_NUM     19
  #define Y3_GPIO_NUM     18
  #define Y2_GPIO_NUM      5
  #define VSYNC_GPIO_NUM  25
  #define HREF_GPIO_NUM   23
  #define PCLK_GPIO_NUM   22
#else
  #error "Camera model not selected"
#endif

static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req){
  if (!streamingActive) {
    httpd_resp_send_500(req); // Service Unavailable if not streaming
    return ESP_OK;
  }
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if(res != ESP_OK){
    return res;
  }

  while(streamingActive){
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if(fb->width > 400){
        if(fb->format != PIXFORMAT_JPEG){
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          if(!jpeg_converted){
            Serial.println("JPEG compression failed");
            res = ESP_FAIL;
          }
        } else {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      }
    }

    if (!streamingActive) { // Check again before sending data
      if (fb) {
        esp_camera_fb_return(fb);
      } else if (_jpg_buf) {
        free(_jpg_buf);
      }
      break; // Exit the streaming loop
    }

    if(res == ESP_OK){
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }

    if(fb){
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if(_jpg_buf){
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if(res != ESP_OK){
      break;
    }
    //Serial.printf("MJPG: %uB\n",(uint32_t)(_jpg_buf_len));
  }
  Serial.println("Stream handler finished.");
  return res;
}

void startCameraServer(){
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };

  Serial.printf("Starting web server on port: '%d'\n", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &index_uri);
  } else {
    Serial.println("Error starting stream server");
  }
}

void stopCameraServer() {
  if (stream_httpd) {
    Serial.println("Stopping stream server");
    httpd_stop(stream_httpd);
    stream_httpd = NULL;
  }
}

bool connectToWiFi(bool isNewConnection = false) {
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("Password: ");
  Serial.println(WiFi.psk());

  int connectionAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && connectionAttempts < 30) {
    delay(500);
    Serial.print(".");
    connectionAttempts++;
  }
  Serial.println("");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    wifiConnected = true;
    if (isNewConnection) {
      serialPort.print("IP:");
      serialPort.println(WiFi.localIP().toString());
      Serial.println("IP address sent back after new connection.");
    }
    return true;
  } else {
    Serial.println("Failed to connect to Wi-Fi.");
    if (isNewConnection) {
      // No need to send a generic failure here as it's already handled in the main loop
    } else {
      serialPort.println("ERROR:WIFI_CONNECT_FAILED"); // Still send error on initial saved connect failure
    }
    wifiConnected = false;
    return false;
  }
}

void setupCamera() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.setDebugOutput(false);

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
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if(psramFound()){
    // config.frame_size = FRAMESIZE_UXGA;
    config.frame_size = FRAMESIZE_SVGA;

    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    // Handle camera initialization failure appropriately
    while(true) { delay(1000); } // Stop here
  }
}


void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32-CAM Wi-Fi Client with NVS and Streaming Control");
  pinMode(ledPin, OUTPUT); // Set the LED pin as an output
  digitalWrite(ledPin, LOW);  // Initialize LED as off
  
  preferences.begin("wifi-config", false);

  String savedSSID = preferences.getString(WIFI_SSID_KEY, "");
  String savedPassword = preferences.getString(WIFI_PASSWORD_KEY, "");

  setupCamera(); // Initialize the camera early

  esp_task_wdt_init(5, true); // Initialize watchdog timer with 5-second timeout
  esp_task_wdt_add(NULL);    // Add current task to watchdog

  if (savedSSID.length() > 0 && savedPassword.length() > 0) {
    Serial.println("Found saved Wi-Fi credentials. Attempting to connect...");
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    connectToWiFi();
  } else {
    Serial.println("No saved Wi-Fi credentials. Waiting for provisioning...");
  }
}

void loop() {
  esp_task_wdt_reset(); // Pet the watchdog timer

  if (millis() - lastCommandCheck >= commandCheckInterval) {
    lastCommandCheck = millis();
    if (serialPort.available() > 0) {
      String command = serialPort.readStringUntil('\n');
      command.trim();
      Serial.print("Received command: ");
      Serial.println(command);

      if (command == "WIFI_CREDENTIALS") {
        Serial.println("Receiving Wi-Fi credentials...");
        String ssid = "";
        String password = "";
        unsigned long startTime = millis();

        // Attempt to read SSID with timeout
        while (ssid.isEmpty() && (millis() - startTime < 5000)) {
          if (serialPort.available() > 0) {
            ssid = serialPort.readStringUntil('\n');
            ssid.trim();
            Serial.print("Received SSID: ");
            Serial.println(ssid);
          }
          delay(10); // Small delay to avoid busy-waiting
        }

        if (!ssid.isEmpty()) {
          startTime = millis(); // Reset timeout for password
          // Attempt to read password with timeout
          while (password.isEmpty() && (millis() - startTime < 5000)) {
            if (serialPort.available() > 0) {
              password = serialPort.readStringUntil('\n');
              password.trim();
              Serial.print("Received Password: ");
              Serial.println(password);
            }
            delay(10); // Small delay to avoid busy-waiting
          }
        }

        if (!ssid.isEmpty() && !password.isEmpty()) {
          Serial.println("Attempting to connect with received credentials...");
          WiFi.begin(ssid.c_str(), password.c_str());
          if (connectToWiFi(true)) { // Attempt connection and indicate it's a new connection
            Serial.println("Saving new Wi-Fi credentials to NVS...");
            preferences.putString(WIFI_SSID_KEY, ssid);
            preferences.putString(WIFI_PASSWORD_KEY, password);
            preferences.end();
            Serial.println("New credentials saved.");
          } else {
            Serial.println("Failed to connect with new credentials. Keeping old credentials.");
            serialPort.println("ERROR:NEW_WIFI_CONNECT_FAILED");
          }
        } else {
          Serial.println("Error: Did not receive SSID and/or password within 5 seconds.");
        }
      } else if (command == "START_STREAM" && wifiConnected && !streamingActive) {
        Serial.println("Starting stream...");
        startCameraServer();
        streamingActive = true;
      } else if (command == "STOP_STREAM") {
        Serial.println("Stopping stream by rebooting...");
        esp_restart(); // Reboot the ESP32
      } else if (command == "REQUEST_IP" && wifiConnected) {
        Serial.print("IP:");
        Serial.println(WiFi.localIP().toString());
        Serial.println("Sent IP address.");
      } else if (command == "LED_ON") {
        digitalWrite(ledPin, HIGH); // Turn the LED on
        Serial.println("LED ON");
      } else if (command == "LED_OFF") {
        digitalWrite(ledPin, LOW);  // Turn the LED off
        Serial.println("LED OFF");
      } else {
        Serial.println("Unknown command or Wi-Fi not connected.");
      }
    }
  }
  // No explicit delay() here
}