//#define CAMERA_MODEL_WROVER_KIT // ESP32-CAM (official ESP-WROVER-KIT)
//#define CAMERA_MODEL_ESP_EYE    // ESP-EYE (official Espressif)
//#define CAMERA_MODEL_M5STACK_PSRAM // M5Stack Camera PSRAM (OV2640 with PSRAM)
//#define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Stack Camera PSRAM v2 (OV2640 with PSRAM)
//#define CAMERA_MODEL_M5STACK_WIDE // M5Stack Wide Camera (OV3660)
//#define CAMERA_MODEL_M5STACK_ESP32CAM // M5Stack ESP32-CAM (without PSRAM, max XGA)
//#define CAMERA_MODEL_TTGO_T_JOURNAL // TTGO T-Journal (with OLED)
//#define CAMERA_MODEL_ARDUCAM_ESP32S_UNO // ArduCAM ESP32S UNO (Similar to AI-Thinker)
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"
#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
// Removed: #include "esp_http_server.h" // Not needed for AWS streaming only
#include <Preferences.h>
#include <HardwareSerial.h>
#include <esp_task_wdt.h>

WiFiClientSecure client;
HTTPClient http;
bool httpConnected = false;

// --- NEW GLOBAL SETTINGS & NVS KEYS ---
String serverURLReceive = "https://www.domain.co/receive"; // Now a String to be modifiable
const char* SERVER_URL_KEY = "server_url"; // NVS key for server URL

framesize_t currentFrameSize = FRAMESIZE_VGA; // Default resolution
const char* RESOLUTION_KEY = "resolution";    // NVS key for resolution

int currentJpegQuality = 15; // Default JPEG quality
const char* QUALITY_KEY = "jpeg_quality";    // NVS key for quality
// --- END NEW GLOBAL SETTINGS ---

HardwareSerial& serialPort = Serial;
Preferences preferences;
const int ledPin = LAMP_PIN; // LED pin for status indication
const int statusLedPin = LED_PIN;
const char* WIFI_SSID_KEY = "wifi_ssid";
const char* WIFI_PASSWORD_KEY = "wifi_password";
volatile bool wifiConnected = false;
volatile bool streamingActive = false;
bool streamOn = false;
unsigned long lastCommandCheck = 0;
const long commandCheckInterval = 50;


// --- Helper function to convert string to framesize_t ---
framesize_t parseResolution(String res) {
  res.toUpperCase();
  if (res == "QQVGA") return FRAMESIZE_QQVGA;     // 160x120
  if (res == "QVGA")  return FRAMESIZE_QVGA;      // 320x240
  if (res == "CIF")   return FRAMESIZE_CIF;       // 352x288
  if (res == "VGA")   return FRAMESIZE_VGA;       // 640x480
  if (res == "SVGA")  return FRAMESIZE_SVGA;      // 800x600
  if (res == "XGA")   return FRAMESIZE_XGA;       // 1024x768
  if (res == "SXGA")  return FRAMESIZE_SXGA;      // 1280x1024
  if (res == "UXGA")  return FRAMESIZE_UXGA;      // 1600x1200
  // Default to VGA if unknown or invalid
  Serial.println("Invalid resolution. Using VGA.");
  return FRAMESIZE_VGA;
}

// --- Helper function to convert framesize_t to string (for reporting) ---
String framesizeToString(framesize_t fs) {
  switch (fs) {
    case FRAMESIZE_QQVGA: return "QQVGA (160x120)";
    case FRAMESIZE_QVGA:  return "QVGA (320x240)";
    case FRAMESIZE_CIF:   return "CIF (352x288)";
    case FRAMESIZE_VGA:   return "VGA (640x480)";
    case FRAMESIZE_SVGA:  return "SVGA (800x600)";
    case FRAMESIZE_XGA:   return "XGA (1024x768)";
    case FRAMESIZE_SXGA:  return "SXGA (1280x1024)";
    case FRAMESIZE_UXGA:  return "UXGA (1600x1200)";
    default: return "UNKNOWN";
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
    esp_task_wdt_reset();
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
    if (!isNewConnection) { // Only send ERROR:WIFI_CONNECT_FAILED on initial saved connect failure
      serialPort.println("ERROR:WIFI_CONNECT_FAILED");
    }
    wifiConnected = false;
    return false;
  }
}

// --- setupCamera now takes parameters ---
void setupCamera(framesize_t frame_size, int jpeg_quality) {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

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
  config.pixel_format = PIXFORMAT_JPEG;
  config.fb_location = CAMERA_FB_IN_PSRAM; // Ensure PSRAM is used if available and needed

  // --- Apply requested frame_size and jpeg_quality ---
  config.frame_size = frame_size;
  config.jpeg_quality = jpeg_quality;

  if(psramFound()){
    // Use the passed frame_size and quality, but ensure fb_count for PSRAM
    config.fb_count = 2;
  } else {
    // For no PSRAM, override to lower resolution and 1 framebuffer
    Serial.println("PSRAM not found. Limiting resolution and framebuffer count.");
    config.frame_size = FRAMESIZE_SVGA; // Max for no PSRAM usually
    config.jpeg_quality = 12; // Lower quality for no PSRAM
    config.fb_count = 1;
  }

  // Deinitialize camera before re-initializing if it was already initialized
  esp_camera_deinit();
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    // Handle camera initialization failure appropriately
    // Consider returning false and handling in main loop or blocking
    while(true) { 
      delay(1000); 
      esp_task_wdt_reset(); // Pet WDT here!
    } // Stop here if camera fails
  } else {
    Serial.printf("Camera initialized to %s, Quality: %d\n", framesizeToString(currentFrameSize).c_str(), currentJpegQuality);
  }
}

void captureAndSendFrameToAWS() {
  unsigned long totalFunctionStart = millis();

  unsigned long captureStart = millis();
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("  [CAM] Camera frame capture failed");
    return;
  }
  unsigned long captureEnd = millis();
  // Serial.printf("  [CAM] Capture & Comp. Time: %lu ms. Frame Size: %u bytes\n", captureEnd - captureStart, fb->len); // Optional log

  if (fb->len == 0) {
      Serial.println("  [CAM] Warning: Empty frame captured. Skipping POST.");
      esp_camera_fb_return(fb);
      return;
  }

  if (!httpConnected) {
    Serial.println("  [CAM] Initializing HTTPClient for persistent connection...");
    http.begin(client, serverURLReceive);
    http.addHeader("Content-Type", "image/jpeg");
    httpConnected = true;
    Serial.println("  [CAM] HTTPClient initialized.");
  } else {
    if (!http.connected()) {
        Serial.println("  [CAM] HTTPClient connection lost, attempting to reconnect...");
        http.end();
        http.begin(client, serverURLReceive);
        http.addHeader("Content-Type", "image/jpeg");
    }
  }

  unsigned long uploadStart = millis();
  int httpCode = http.POST(fb->buf, fb->len);
  unsigned long uploadEnd = millis();

  // Serial.printf("  [CAM] HTTP POST Upload Time: %lu ms. HTTP Code: %d\n", uploadEnd - uploadStart, httpCode); // Optional log

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      // Serial.println("  [CAM] Frame sent successfully."); // Optional success log
    } else {
      Serial.printf("  [CAM] HTTP POST failed with code: %d. Closing connection for retry.\n", httpCode);
      http.end();
      httpConnected = false;
    }
  } else {
    Serial.printf("  [CAM] HTTP POST failed, error: %s. Closing connection for retry.\n", http.errorToString(httpCode).c_str());
    http.end();
    httpConnected = false;
  }

  esp_camera_fb_return(fb);
  // Serial.printf("  [CAM] Total captureAndSendFrameToAWS() time: %lu ms\n", totalFunctionEnd - totalFunctionStart); // Optional log
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  Serial.println("\nESP32-CAM Wi-Fi Client with NVS and Streaming Control (to AWS)");
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(statusLedPin, OUTPUT); // Initialize status LED pin
  digitalWrite(statusLedPin, LED_OFF); // Ensure status LED is off initially

  preferences.begin("wifi-config", false);
  String savedSSID = preferences.getString(WIFI_SSID_KEY, "");
  String savedPassword = preferences.getString(WIFI_PASSWORD_KEY, "");
  streamOn = preferences.getBool("streamOn", false);

  // --- Load persisted camera settings and URL ---
  currentFrameSize = (framesize_t)preferences.getUChar(RESOLUTION_KEY, FRAMESIZE_VGA); // Default to VGA
  currentJpegQuality = preferences.getInt(QUALITY_KEY, 15); // Default to 15
  serverURLReceive = preferences.getString(SERVER_URL_KEY, "https://www.domain.co/receive"); // Default URL
  // --- END Load persisted settings ---

  preferences.end();

  streamingActive = streamOn; // Initialize runtime streaming state from persisted NVS state

  setupCamera(currentFrameSize, currentJpegQuality); // Use loaded settings to initialize camera

  esp_task_wdt_init(20, true);
  esp_task_wdt_add(NULL);
  client.setInsecure();
  client.setTimeout(10000);
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  WiFi.mode(WIFI_STA);

  if (savedSSID.length() > 0 && savedPassword.length() > 0) {
    Serial.println("Found saved Wi-Fi credentials. Attempting to connect...");
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    if (connectToWiFi(false)) {
      wifiConnected = true;
    } else {
      wifiConnected = false;
      Serial.println("Failed to connect with saved credentials.");
    }
  } else {
    Serial.println("No saved Wi-Fi credentials. Waiting for provisioning...");
  }

  if (streamingActive) {
      Serial.println("Device previously set to stream. Will attempt to resume streaming once Wi-Fi is connected.");
  }
}

void loop() {
  esp_task_wdt_reset();

  if (millis() - lastCommandCheck >= commandCheckInterval) {
    lastCommandCheck = millis();
    if (serialPort.available() > 0) {
      String command = serialPort.readStringUntil('\n');
      command.trim();
      Serial.print("Received command: ");
      Serial.println(command);

      // --- WIFI_CREDENTIALS command (existing) ---
      if (command == "WIFI_CREDENTIALS") {
        Serial.println("Receiving Wi-Fi credentials...");
        String ssid = "";
        String password = "";
        unsigned long startTime = millis();

        while ((millis() - startTime < 10000) && serialPort.available() == 0) { delay(1); esp_task_wdt_reset(); } // Wait for data
        ssid = serialPort.readStringUntil('\n');
        ssid.trim();
        Serial.print("Received SSID: '"); Serial.print(ssid); Serial.println("'");

        if (!ssid.isEmpty()) {
          startTime = millis();
          while ((millis() - startTime < 10000) && serialPort.available() == 0) { delay(1); esp_task_wdt_reset(); } // Wait for data
          password = serialPort.readStringUntil('\n');
          password.trim();
          Serial.print("Received Password: '"); Serial.print(password); Serial.println("'");
        }

        if (!ssid.isEmpty() && !password.isEmpty()) {
          Serial.println("Attempting to connect with received credentials...");
          preferences.begin("wifi-config", false);
          WiFi.begin(ssid.c_str(), password.c_str());
          if (connectToWiFi(true)) {
            Serial.println("Saving new Wi-Fi credentials to NVS...");
            preferences.putString(WIFI_SSID_KEY, ssid);
            preferences.putString(WIFI_PASSWORD_KEY, password);
            preferences.end();
            Serial.println("New credentials saved.");
            wifiConnected = WiFi.status() == WL_CONNECTED;
          } else {
            Serial.println("Failed to connect with new credentials. Keeping old credentials.");
            serialPort.println("ERROR:NEW_WIFI_CONNECT_FAILED");
            preferences.end(); // Always close preferences
          }
        } else {
          Serial.println("Error: Did not receive SSID and/or password within timeout or they were empty.");
          serialPort.println("ERROR:WIFI_CRED_TIMEOUT");
        }
      }
      // --- START_STREAM command (existing) ---
      else if (command == "START_STREAM") {
        if (WiFi.isConnected()) {
          Serial.println("Starting stream to AWS...");
          preferences.begin("wifi-config", false);
          streamOn = true;
          preferences.putBool("streamOn", streamOn);
          preferences.end();
          streamingActive = true;
        } else {
          Serial.println("Cannot start stream: Wi-Fi not connected.");
        }
      }
      // --- STOP_STREAM command (existing) ---
      else if (command == "STOP_STREAM") {
        Serial.println("Stopping stream.");
        preferences.begin("wifi-config", false);
        streamOn = false;
        preferences.putBool("streamOn", streamOn);
        preferences.end();
        streamingActive = false;
        if (httpConnected) {
            http.end();
            httpConnected = false;
            Serial.println("HTTP connection closed.");
        }
      }
      // --- REQUEST_IP command (existing) ---
      else if (command == "REQUEST_IP" && WiFi.isConnected()) {
        Serial.print("IP:");
        Serial.println(WiFi.localIP().toString());
        Serial.println("Sent IP address.");
      }
      // --- LAMP_ON/OFF commands (for the flash LED) ---
      else if (command == "LED_ON") {
        digitalWrite(ledPin, HIGH); // Controls the flash lamp
        Serial.println("LAMP ON");
      } else if (command == "LED_OFF") {
        digitalWrite(ledPin, LOW); // Controls the flash lamp
        Serial.println("LAMP OFF");
      }
      // --- NEW: STATUS_LED_ON/OFF commands ---
      else if (command == "STATUS_LED_ON") {
        digitalWrite(statusLedPin, LED_ON); // Controls the small status LED
        Serial.println("STATUS LED ON");
      } else if (command == "STATUS_LED_OFF") {
        digitalWrite(statusLedPin, LED_OFF); // Controls the small status LED
        Serial.println("STATUS LED OFF");
      }
      // --- NEW: SET_RESOLUTION command ---
      else if (command == "SET_RESOLUTION") {
        Serial.println("Waiting for resolution (e.g., VGA, SVGA, UXGA)...");
        unsigned long startTime = millis();
        while ((millis() - startTime < 5000) && serialPort.available() == 0) { delay(1); esp_task_wdt_reset(); }
        String resStr = serialPort.readStringUntil('\n');
        resStr.trim();

        framesize_t newRes = parseResolution(resStr);
        if (newRes != currentFrameSize) {
          currentFrameSize = newRes;
          Serial.printf("Setting resolution to: %s\n", framesizeToString(currentFrameSize).c_str());
          preferences.begin("wifi-config", false);
          preferences.putUChar(RESOLUTION_KEY, (uint8_t)currentFrameSize);
          preferences.end();
          setupCamera(currentFrameSize, currentJpegQuality); // Reinitialize camera with new resolution
          Serial.println("Resolution updated and saved.");
        } else {
          Serial.println("Resolution already set to that value. No change.");
        }
      }
      // --- NEW: SET_QUALITY command ---
      else if (command == "SET_QUALITY") {
        Serial.println("Waiting for JPEG quality (0-63, 0=high quality, 63=low quality)...");
        unsigned long startTime = millis();
        while ((millis() - startTime < 5000) && serialPort.available() == 0) { delay(1); esp_task_wdt_reset(); }
        String qualityStr = serialPort.readStringUntil('\n');
        qualityStr.trim();
        int newQuality = qualityStr.toInt();

        if (newQuality >= 0 && newQuality <= 63) {
          if (newQuality != currentJpegQuality) {
            currentJpegQuality = newQuality;
            Serial.printf("Setting JPEG quality to: %d\n", currentJpegQuality);
            preferences.begin("wifi-config", false);
            preferences.putInt(QUALITY_KEY, currentJpegQuality);
            preferences.end();
            setupCamera(currentFrameSize, currentJpegQuality); // Reinitialize camera with new quality
            Serial.println("JPEG quality updated and saved.");
          } else {
            Serial.println("JPEG quality already set to that value. No change.");
          }
        } else {
          Serial.println("Invalid quality value. Must be between 0 and 63.");
        }
      }
      // --- NEW: SET_URL command ---
      else if (command == "SET_URL") {
        Serial.println("Waiting for new server URL (e.g., https://example.com/receive)...");
        unsigned long startTime = millis();
        while ((millis() - startTime < 10000) && serialPort.available() == 0) { delay(1); esp_task_wdt_reset(); }
        String newUrl = serialPort.readStringUntil('\n');
        newUrl.trim();

        if (newUrl.startsWith("http://") || newUrl.startsWith("https://")) {
          if (newUrl != serverURLReceive) {
            serverURLReceive = newUrl;
            Serial.printf("Setting server URL to: %s\n", serverURLReceive.c_str());
            preferences.begin("wifi-config", false);
            preferences.putString(SERVER_URL_KEY, serverURLReceive);
            preferences.end();
            // Force re-initialization of HTTPClient on next frame send
            if (httpConnected) {
                http.end();
                httpConnected = false;
                Serial.println("HTTP connection closed. Will re-establish with new URL.");
            }
            Serial.println("Server URL updated and saved.");
          } else {
            Serial.println("Server URL is already set to that value. No change.");
          }
        } else {
          Serial.println("Invalid URL format. Must start with http:// or https://");
        }
      }
      // --- NEW: GET_SETTINGS command ---
      else if (command == "GET_SETTINGS") {
          Serial.println("--- Current Settings ---");
          Serial.printf("Resolution: %s\n", framesizeToString(currentFrameSize).c_str());
          Serial.printf("JPEG Quality: %d\n", currentJpegQuality);
          Serial.printf("Server URL: %s\n", serverURLReceive.c_str());
          Serial.printf("Streaming Status: %s\n", streamingActive ? "ACTIVE" : "INACTIVE");
          Serial.printf("Wi-Fi Status: %s\n", WiFi.isConnected() ? "CONNECTED" : "DISCONNECTED");
          Serial.println("------------------------");
      }
      // --- Unknown command (existing) ---
      else {
        Serial.println("Unknown command or Wi-Fi not connected.");
      }
    }
  }

  // --- Streaming Logic ---
  wifiConnected = WiFi.isConnected();

  if (streamingActive && wifiConnected) {
    captureAndSendFrameToAWS();
  } else if (streamingActive && !wifiConnected) {
    Serial.println("Streaming active but no Wi-Fi. Stopping stream.");
    streamingActive = false;
    if (httpConnected) {
        http.end();
        httpConnected = false;
    }
  } else {
    // If not streaming or not connected, just delay to avoid busy-looping
    for(int i = 0; i < 10; i++) { // Delay for 10 * 100ms = 1 second total
      delay(100);
      esp_task_wdt_reset(); // Pet the watchdog during the delay
    }
  }
}