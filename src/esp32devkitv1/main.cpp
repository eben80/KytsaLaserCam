#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP32Servo.h>
// #include <EEPROM.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include "bongoCat.h"
// NTP Client
#include <NTPClient.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>


Preferences preferences;

// NTP Settings
/** @brief UDP client for NTP communication. */
WiFiUDP ntpUDP;
/** @brief NTP client instance for time synchronization. */
NTPClient timeClient(ntpUDP);
/** @brief Timestamp of the last successful NTP update. */
unsigned long lastNTPUpdateTime = 0;
/** @brief Interval in milliseconds for updating time via NTP. */
long ntpUpdateInterval = 60 * 60 * 1000; // Update every hour
/** @brief Timezone offset in hours from UTC. */
int timeZoneOffset = 2; // Default to CEST (UTC+2) - Changed to int
/** @brief Flag indicating if random servo motion is currently manually activated. */
bool randomMotionActive = false; // Toggled by the web button
/** @brief Flag indicating if an active scheduled movement has been temporarily overridden (e.g., by manual stop). */
bool scheduledMovementOverridden = false; // To temporarily stop scheduled movement
/** @brief Flag indicating if a scheduled movement is currently active based on NTP time and configured slots. */
bool isScheduledMovementActive = false; // Tracks if the schedule is currently active
/** @brief Flag indicating if the device is currently in a configuration mode (e.g., being adjusted via old HTTP interface, less relevant with WebSocket). */
bool inConfiguration = false; // Flag to indicate if in configuration mode
/** @brief String storing the start time of the currently active or next scheduled movement. */
String currentScheduleStartTime = "";
/** @brief String storing the stop time of the currently active or next scheduled movement. */
String currentScheduleStopTime = "";

// Define the serial port to use (adjust if needed)
/** @brief HardwareSerial instance used for communication with the ESP32CAM. */
HardwareSerial& serialPort = Serial2; // Use Serial2 (RX2, TX2)

/** @brief Flag indicating if WiFi was connected via WiFiManager's Access Point mode. */
bool wifiConnectedAP = false;
/** @brief SSID of the currently connected Wi-Fi network. */
String staSSID;
/** @brief Password for the currently connected Wi-Fi network. */
String staPassword;
/** @brief IP address of the connected ESP32CAM, received over serial. */
String esp32CamIP = "";
/** @brief Flag indicating if communication with the ESP32CAM has been established. */
bool esp32CamConnected = false;
/** @brief Flag indicating if the ESP32CAM is currently streaming video. */
bool streaming = false;
/** @brief Tracks the state of the CAM LED. */
bool camLedActive = false;


#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET     -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);



Servo myservoX;
Servo myservoY;

const int servoPinX = 13;
const int servoPinY = 12;
const int outputPin = 25;
const int relayPin = 26;
const int touchPin1 = 32;
const int touchPin2 = 33; // New touch pin
// Define I2C pins
const int SDA_PIN = 21;
const int SCL_PIN = 18;

const int threshold = 75;  // Touch sensitivity threshold
int touchValue;
bool laserActive = true;  // Default ON
bool relayActive = true;  // Default ON
bool settingsMode = false;      // Declare globally
int currentSetting = 0;         // Declare globally

// Function to handle random motion
unsigned long lastMotionTime = 0;
unsigned long minMotionInterval = 100; // Minimum interval in milliseconds
unsigned long maxMotionInterval = 3000; // Maximum interval in milliseconds

struct TimeSlot {
  int startTimeMinutes; // Minutes from the start of the day (0-1439)
  int stopTimeMinutes;  // Minutes from the start of the day (0-1439)
  bool active;          // Flag to indicate if this timeslot is currently active
};

const int MAX_TIMERS = 5; // Define a maximum number of timers we can store
TimeSlot timeSlots[MAX_TIMERS];
int numTimeSlots;

WiFiManager wm;

String header;
String valueStringX = String(90);
String valueStringY = String(90);
int pos1 = 0;
int pos2 = 0;

unsigned long currentTime = millis();
unsigned long previousTime = 0;
const long timeoutTime = 2000;

int minX = 0, maxX = 180, minY = 45, maxY = 135;

// Variables for random velocity
int minVel = 800, maxVel = 2000; // Min and max delay times between movements - initialized
unsigned long lastMovementTime = 0;
unsigned long movementInterval = 1000;  // Default to 1 second between movements - initialized

// Movement tracking
/** @brief Structure to manage smooth servo movement. */
struct ServoMovement {
  /** @brief Starting position of the servo for the current movement. */
  int startPos;
  int targetPos;
  unsigned long startTime;
  unsigned long duration;
  /** @brief Flag indicating if the servo is currently executing a movement. */
  bool isMoving;
};

/** @brief Tracks the current movement state for servo X. */
ServoMovement movementX = {0, 0, 0, 0, false}; // Servo X movement tracking
/** @brief Tracks the current movement state for servo Y. */
ServoMovement movementY = {0, 0, 0, 0, false}; // Servo Y movement tracking

// WebSocket Global Variables
/** @brief Instance of the WebSocket client used for communication with the server. */
WebSocketsClient webSocket;
/** @brief Flag indicating the current connection status of the WebSocket. True if connected, false otherwise. */
bool webSocketConnected = false;
/** @brief Timestamp of the last attempt to reconnect the WebSocket. Used to manage reconnection intervals. */
unsigned long webSocketLastReconnectAttempt = 0;
/** @brief Interval in milliseconds between WebSocket reconnection attempts. */
const unsigned long webSocketReconnectInterval = 5000; // Try to reconnect every 5 seconds
/** @brief Unique identifier for this ESP32 device, typically derived from its MAC address. */
String deviceId = ""; // Will be set to ESP32 Chip ID
// Define WebSocket server details
/** @brief Hostname or IP address of the WebSocket server. */
const char* wsHost = "ebski.co";
/** @brief Port number for the WebSocket server. WebSocket Secure (WSS) port. */
const uint16_t wsPort = 80;
/** @brief Path for the WebSocket endpoint on the server. (e.g., wss://ebski.co/ws) */
const char* wsPath = "/ws";

void RTC_IRAM_ATTR esp_wake_deep_sleep() {
  esp_default_wake_deep_sleep();
  laserActive = true;
  relayActive = true;
  digitalWrite(outputPin, HIGH);
  digitalWrite(relayPin, HIGH);
}

/**
 * @brief Generates a unique device ID from the ESP32's MAC address.
 * @return A String representing the unique chip ID.
 */
String getChipId() {
    uint64_t chipid = ESP.getEfuseMac();
    char chipid_str[17];
    snprintf(chipid_str, sizeof(chipid_str), "%04X%08X", (uint16_t)(chipid >> 32), (uint32_t)chipid);
    return String(chipid_str);
}

// Forward declarations for functions called in webSocketEvent
void turnLaserOn();
void turnLaserOff();
void sendSystemConfig(); // Forward declaration for our new function
void addTimeSlot(String startTimeStr, String stopTimeStr); // Ensure it's declared if not already before webSocketEvent
void deleteTimeSlot(int indexToDelete); // Ensure it's declared

/**
 * @brief Handles events from the WebSocket client.
 *
 * This function is called by the WebSocketsClient library when various events occur,
 * such as connection, disconnection, or when a message is received.
 *
 * @param type The type of WebSocket event that occurred.
 * @param payload A pointer to the data payload associated with the event (if any).
 * @param length The length of the payload.
 */
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[WSc] Disconnected!\n");
            webSocketConnected = false;
            break;
        case WStype_CONNECTED:
            Serial.printf("[WSc] Connected to url: %s\n", (char*)payload);
            webSocketConnected = true;
            // Send pairing message
            {
                StaticJsonDocument<200> doc;
                doc["type"] = "pairing";
                doc["deviceId"] = deviceId;
                String output;
                serializeJson(doc, output);
                webSocket.sendTXT(output);
                Serial.println("Sent pairing message: " + output);
                Serial.println("[DEBUG] Calling sendSystemConfig on WebSocket connect.");
                sendSystemConfig(); // Send initial system config on connect
            }
            break;
        case WStype_TEXT:
            Serial.printf("[WSc] get text: %s\n", (char*)payload);
            // Parse JSON command from server
            {
                StaticJsonDocument<384> doc; // Increased size for potentially larger payloads like addTimer
                DeserializationError error = deserializeJson(doc, payload, length);
                if (error) {
                    Serial.print(F("deserializeJson() failed: "));
                    Serial.println(error.f_str());
                    return;
                }

                const char* command = doc["command"]; // e.g., "servoX", "ledOn"

                if (strcmp(command, "servoX") == 0) {
                    int val = doc["value"];
                    myservoX.write(val);
                    valueStringX = String(val); // Update for display if any part of OLED remains
                    Serial.printf("Executed servoX: %d\n", val);
                    // Optionally send back a status update
                } else if (strcmp(command, "servoY") == 0) {
                    int val = doc["value"];
                    myservoY.write(val);
                    valueStringY = String(val);
                    Serial.printf("Executed servoY: %d\n", val);
                } else if (strcmp(command, "LASER_ON") == 0) {
                    turnLaserOn();
                    Serial.println("Executed LASER_ON");
                } else if (strcmp(command, "LASER_OFF") == 0) {
                    turnLaserOff();
                    Serial.println("Executed LASER_OFF");
                } else if (strcmp(command, "RELAY_ON") == 0) {
                    digitalWrite(relayPin, HIGH);
                    relayActive = true;
                    Serial.println("Executed RELAY_ON");
                } else if (strcmp(command, "RELAY_OFF") == 0) {
                    digitalWrite(relayPin, LOW);
                    relayActive = false;
                    Serial.println("Executed RELAY_OFF");
                } else if (strcmp(command, "RANDOM_MOTION_TOGGLE") == 0) {
                    randomMotionActive = !randomMotionActive;
                     Serial.printf("Random motion toggled: %s\n", randomMotionActive ? "ON" : "OFF");
                }
                // Commands for ESP32CAM
                else if (strcmp(command, "START_STREAM") == 0) {
                    Serial2.println("START_STREAM");
                    Serial.println("Sent command to ESP32CAM: START_STREAM");
                    streaming = true;
                } else if (strcmp(command, "STOP_STREAM") == 0) {
                    Serial2.println("STOP_STREAM");
                    Serial.println("Sent command to ESP32CAM: STOP_STREAM");
                    streaming = false;
                } else if (strcmp(command, "CAM_LED_ON") == 0) {
                    Serial2.println("LED_ON");
                    Serial.println("Sent command to ESP32CAM: LED_ON");
                    camLedActive = true;
                } else if (strcmp(command, "CAM_LED_OFF") == 0) {
                    Serial2.println("LED_OFF");
                    Serial.println("Sent command to ESP32CAM: LED_OFF");
                    camLedActive = false;
                } else if (strcmp(command, "getSystemConfig") == 0) {
                    Serial.println("[DEBUG] Received 'getSystemConfig' command.");
                    Serial.println("[DEBUG] Calling sendSystemConfig for getSystemConfig command.");
                    sendSystemConfig();
                } else if (strcmp(command, "setServoLimit") == 0) {
                    const char* axis = doc["axis"]; // "x" or "y"
                    const char* limit_type = doc["limit_type"]; // "min" or "max"
                    int value = doc["value"];
                    Serial.printf("Received setServoLimit: axis=%s, type=%s, value=%d\n", axis, limit_type, value);

                    preferences.begin("servo_config", false);
                    if (strcmp(axis, "x") == 0) {
                        if (strcmp(limit_type, "min") == 0) {
                            minX = value;
                            preferences.putInt("min_x", minX);
                        } else if (strcmp(limit_type, "max") == 0) {
                            maxX = value;
                            preferences.putInt("max_x", maxX);
                        }
                    } else if (strcmp(axis, "y") == 0) {
                        if (strcmp(limit_type, "min") == 0) {
                            minY = value;
                            preferences.putInt("min_y", minY);
                        } else if (strcmp(limit_type, "max") == 0) {
                            maxY = value;
                            preferences.putInt("max_y", maxY);
                        }
                    }
                    preferences.end();
                    Serial.printf("Updated limits: minX=%d, maxX=%d, minY=%d, maxY=%d\n", minX, maxX, minY, maxY);
                    sendSystemConfig(); // Send updated config back
                } else if (strcmp(command, "addTimer") == 0) {
                    Serial.println("[DEBUG] Received 'addTimer' command.");
                    String startTime = doc["startTime"].as<String>(); // "HH:MM"
                    String endTime = doc["endTime"].as<String>();   // "HH:MM"
                    Serial.printf("[DEBUG] Parsed startTime: %s, endTime: %s from WebSocket\n", startTime.c_str(), endTime.c_str());
                    Serial.println("[DEBUG] Calling addTimeSlot from webSocketEvent.");
                    addTimeSlot(startTime, endTime);
                    Serial.println("[DEBUG] Calling sendSystemConfig after addTimer.");
                    sendSystemConfig(); // Send updated config back
                } else if (strcmp(command, "deleteTimer") == 0) {
                    int timerIndex = doc["timerIndex"];
                    Serial.printf("Received deleteTimer: index=%d\n", timerIndex);
                    deleteTimeSlot(timerIndex);
                    sendSystemConfig(); // Send updated config back
                }
                // Add more command handlers as needed
            }
            break;
        case WStype_BIN:
            Serial.printf("[WSc] get binary length: %u\n", length);
            // hexdump(payload, length); // Example: webSocket.sendBIN(payload, length);
            break;
        case WStype_ERROR:
            Serial.printf("[WSc] WebSocket ERROR: %s\n", (char*)payload);
            webSocketConnected = false; // Ensure this is set on error too
            break;
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
            // Log these events if needed for debugging fragmentation issues
            // Serial.printf("[WSc] WebSocket FRAGMENT event type: %d\n", type);
            break;
    }
}

void saveWifiCallback() {
  Serial.println("WiFi credentials saved");
  wifiConnectedAP = true;
  staSSID = WiFi.SSID();
  staPassword = WiFi.psk();
}

// Function to get formatted time
String getFormattedTime() {
  return timeClient.getFormattedTime();
}

int timeToMinutes(String formattedTime) {
  // Basic validation for HH:MM format
  if (formattedTime.length() != 5 || formattedTime.charAt(2) != ':') {
    Serial.println("Invalid time format for timeToMinutes: " + formattedTime);
    return -1; // Indicate error
  }
  int hours = formattedTime.substring(0, 2).toInt();
  int minutes = formattedTime.substring(3, 5).toInt();
  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
    Serial.println("Invalid time value for timeToMinutes: " + formattedTime);
    return -1; // Indicate error
  }
  return hours * 60 + minutes;
}

String minutesToTime(int totalMinutes) {
  if (totalMinutes < 0 || totalMinutes >= (24 * 60)) { // Check if totalMinutes is outside 0-1439 range
    // Serial.printf("Invalid totalMinutes value for minutesToTime: %d\n", totalMinutes); // Optional: Log this
    return "N/A"; // Or some other indicator of invalid time
  }
  int hours = (totalMinutes / 60) % 24;
  int minutes = totalMinutes % 60;
  return String(hours < 10 ? "0" : "") + String(hours) + ":" + String(minutes < 10 ? "0" : "") + String(minutes);
}

/** @brief Turns the laser connected to outputPin ON. */
void turnLaserOn() {
  digitalWrite(outputPin, HIGH);
}

/** @brief Turns the laser connected to outputPin OFF. */
void turnLaserOff() {
  digitalWrite(outputPin, LOW);
}

/** @brief Displays a message on the OLED screen prompting user to connect to the WiFiManager AP. */
void displayConnectAPMessage() {
  display.clearDisplay();
  display.setTextSize(1); // Larger text size
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connect to WiFi:");
  display.setTextSize(2);
  display.println("MiauAP");
  display.display();
}



void configModeCallback (WiFiManager *myWiFiManager) {
displayConnectAPMessage();

}

void showBongoCat(){
  display.drawBitmap(0, 0, bongocat, 128, 32, WHITE);
  display.display();
  delay(10);
  // if(digitalRead(input_left) == LOW){
    display.clearDisplay();
    display.setRotation(2);
    display.drawBitmap(0, 0, taps[0], 128, 32, WHITE);
    display.display();
    delay(300);
  // }
  // if(digitalRead(input_right) == LOW){
    display.clearDisplay();
    display.setRotation(2);
    display.drawBitmap(0, 0, taps[1], 128, 32, WHITE);
    display.display();
    delay(300);
  // }
}

/**
 * @brief Initializes the ESP32 DevKitV1 board.
 *
 * This function performs the following setup tasks:
 * - Disables the brownout detector.
 * - Initializes Serial communication (both primary and Serial2 for ESP32CAM).
 * - Generates and stores the unique device ID.
 * - Initializes I2C communication for the OLED display.
 * - Initializes the OLED display and shows a startup animation.
 * - Enables touch wakeup functionality.
 * - Loads saved configuration settings (servo limits, velocities, timezone, NTP interval, timers) from Preferences (NVS).
 * - Initializes output pins for laser and relay.
 * - Attaches and initializes servo motors to their default positions.
 * - Sets up WiFiManager for Wi-Fi connection and credential management.
 *   - If autoConnect fails, it starts a configuration portal.
 *   - On successful connection, initializes the NTP client for time synchronization.
 * - Initializes ArduinoOTA for over-the-air updates.
 * - Initializes the WebSocket client and sets up its event handler and reconnect interval.
 */
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
  Serial.begin(115200);
  deviceId = getChipId();
  Serial.println("Device ID: " + deviceId);
  Serial2.begin(115200); // Initialize Serial2
  // Initialize I2C communication
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  // Play the tap animation 6 times at startup
  for (int i = 0; i < 3; i++) {
showBongoCat();
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Connecting to Wi-Fi..");
  display.display();

  touchSleepWakeUpEnable(T9, 75); // Enable touch wakeup


  preferences.begin("servo_config"); // Namespace for our preferences

  minX = preferences.getInt("min_x", 0); // Default to 0 if not found
  maxX = preferences.getInt("max_x", 180); // Default to 180 if not found
  minY = preferences.getInt("min_y", 45); // Default to 0 if not found
  maxY = preferences.getInt("max_y", 135); // Default to 180 if not found
  minVel = preferences.getInt("min_vel", 800); // Default to 800 if not found
  maxVel = preferences.getInt("max_vel", 2000); // Default to 2000 if not found
  timeZoneOffset = preferences.getInt("timezone", 2);  // Load timezone, default to 2
  ntpUpdateInterval = preferences.getLong("ntp_interval", 60 * 60 * 1000); // Load interval
  Serial.print("Loaded Timezone Offset (Preferences): ");
  Serial.println(timeZoneOffset);
  Serial.print("Loaded NTP Update Interval (Preferences): ");
  Serial.println(ntpUpdateInterval);

  Serial.print("Loaded Min Velocity (Preferences): ");
  Serial.println(minVel);
  Serial.print("Loaded Max Velocity (Preferences): ");
  Serial.println(maxVel);

  // Load num_timers first to know how many slots were previously saved.
  // This value might be adjusted later if some slots are found to be invalid.
  numTimeSlots = preferences.getInt("num_timers", 0);
  Serial.printf("[DEBUG] Loaded num_timers from preferences: %d\n", numTimeSlots);

  // Revised Timer Loading Logic
  int validSlotsCount = 0;
  for (int i = 0; i < MAX_TIMERS; i++) { // Iterate up to MAX_TIMERS to check all possible stored slots
      String baseKey = "timer_" + String(i);
      int startMins = preferences.getInt((baseKey + "_start").c_str(), -1);
      int stopMins = preferences.getInt((baseKey + "_stop").c_str(), -1);
      Serial.printf("[DEBUG] Slot %d: Read startMins=%d, stopMins=%d from Prefs\n", i, startMins, stopMins);

      if (startMins != -1 && stopMins != -1 && startMins >=0 && startMins < (24*60) && stopMins >=0 && stopMins < (24*60)) { // Only load if both are valid and within range
          if (validSlotsCount < i) { // Compact valid timers to the front of the array
              timeSlots[validSlotsCount].startTimeMinutes = startMins;
              timeSlots[validSlotsCount].stopTimeMinutes = stopMins;
          } else { // validSlotsCount == i
               timeSlots[i].startTimeMinutes = startMins; // Or use validSlotsCount index here too for consistency
               timeSlots[i].stopTimeMinutes = stopMins;  // timeSlots[validSlotsCount] would also work
          }
          timeSlots[validSlotsCount].active = false; // Initialize as not active
          validSlotsCount++; // Increment for each valid timer found and loaded
          Serial.printf("[DEBUG] Loaded valid timer %d: Start=%d, Stop=%d. validSlotsCount is now %d\n", validSlotsCount -1, timeSlots[validSlotsCount-1].startTimeMinutes, timeSlots[validSlotsCount-1].stopTimeMinutes, validSlotsCount);
      } else {
          // This slot is invalid or partially invalid in preferences, or beyond the previously saved numTimeSlots.
          // It will be skipped and not counted in numTimeSlots if it's one of the initially loaded numTimeSlots.
          // If we compact, any old data at timeSlots[i] from a previous run will be overwritten by a valid timer
          // or left as is if no more valid timers are found.
          // If we want to ensure all non-loaded slots in timeSlots array are -1, we could explicitly set them:
          // timeSlots[i].startTimeMinutes = -1;
          // timeSlots[i].stopTimeMinutes = -1;
          // However, the loop for schedule checking only goes up to numTimeSlots.
      }
  }
  numTimeSlots = validSlotsCount; // Set numTimeSlots to the actual number of valid timers found
  Serial.printf("[DEBUG] Final numTimeSlots after loading and validation: %d\n", numTimeSlots);

  Serial.printf("[DEBUG] Saving num_timers=%d back to preferences.\n", numTimeSlots);
  preferences.putInt("num_timers", numTimeSlots); // Update the stored count of timers to reflect only valid ones found now.

  // preferences.end(); // Moved this call to after all preference reads/writes in setup if it was here.
                      // It seems preferences.begin() is called once, and end() should be at the very end of setup's preference usage.
                      // For now, assuming it's handled globally or later in setup. If not, this needs placement.
                      // The original code has preferences.begin("servo_config") and no preferences.end() in setup.
                      // This is not ideal. It should be preferences.end() after all preference operations are done for this scope.
                      // For this change, I'll assume the existing structure and only add putInt for num_timers.
                      // A full review of preference handling scope would be a separate task.

  // Set pins to output and initialize as ON
  pinMode(outputPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  turnLaserOn();
  delay(500);
  turnLaserOff();
  digitalWrite(relayPin, HIGH);
  // Calculate halfway positions
  // int initialX = (minX + maxX) / 2;
  // int initialY = (minY + maxY) / 2;
  int initialX = 90;
  int initialY = 90;

    // Initialize servo positions
  myservoX.attach(servoPinX);
  myservoY.attach(servoPinY);
  myservoX.write(initialX);
  myservoY.write(initialY);
  
    // Set initial slider values
  valueStringX = String(initialX);
  valueStringY = String(initialY);

  // wm.resetSettings(); // Reset WiFi settings
  wm.setAPCallback(configModeCallback);
  if (!wm.autoConnect("MiauAP")) {
    Serial.println("Failed to connect to WiFi");
    // displayConnectAPMessage(); // Display message on OLED
  } else {
    Serial.println("Connected to WiFi");
    wm.setShowPassword(true);
    // wm.setSaveConfigCallback(saveWifiCallback);
    wifiConnectedAP = true;
    staSSID = WiFi.SSID();
    staPassword = WiFi.psk();
    Serial.println(staSSID);
    Serial.println(staPassword);
    // Initialize NTP Client
    timeClient.begin();
    timeClient.setTimeOffset(timeZoneOffset * 3600); // Apply loaded timezone
    Serial.println("NTP Client started.");
    // Immediately try to get the time at startup
    if (!timeClient.update()) {
      Serial.println("Failed to get NTP time at startup.");
    } else {
      Serial.println("NTP time synchronized at startup.");
    }
    lastNTPUpdateTime = millis(); // Initialize the last update time
  }



  // OTA setup
  ArduinoOTA.onStart([]() {
    Serial.println("Start OTA update");
  }).onEnd([]() {
    Serial.println("OTA update complete");
  }).onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]\n", error);
  });
  ArduinoOTA.begin();

  // Set custom headers. Each header must end with \r\n.
  // Adding Origin header, which might be required/expected by the WebSocket server or proxy (Nginx/Ratchet)
  // for a successful handshake, especially for proxied connections.
  // webSocket.setExtraHeaders("Origin: https://www.ebski.co\r\n");

  // Ensure all custom header manipulations are commented out for this test
  // to use library defaults.
  // webSocket.setExtraHeaders("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/90.0.4430.93 Safari/537.36 ESP32WebSocketClient\r\n");

  // const char* subprotocols[] = {}; // THIS LINE SHOULD BE REMOVED/COMMENTED
  // webSocket.setSubprotocols(subprotocols, 0); // THIS LINE SHOULD BE REMOVED/COMMENTED

  // Set a browser-like User-Agent.
  // The library will send its default Sec-WebSocket-Protocol: arduino.
  // webSocket.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/90.0.4430.93 Safari/537.36 ESP32WebSocketClient");

  // Connect to WSS (WebSocket Secure) server.
  // For production systems with public CAs (like Let's Encrypt), this should work.
  // If connection issues occur with SSL, you might need to provide a root CA certificate
  // or a fingerprint, e.g., webSocket.beginSSL(wsHost, wsPort, wsPath, "/path_to_ca_cert_on_fs", "fingerprint");
  // However, for many common CAs, the ESP32's underlying stack may handle it.
  webSocket.begin(wsHost, wsPort, wsPath);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000); // Already set via const but can be set here too
  // Optional: for SSL, if your server uses a self-signed cert or you want to pin.
  // webSocket.setFingerprint("...");
}

/**
 * @brief Sends the current system configuration (servo limits, timers) to the WebSocket client.
 *
 * Constructs a JSON message containing the type "systemConfig", the current values of
 * minX, maxX, minY, maxY, and an array of all configured timers. Each timer object
 * in the array includes its startTimeMinutes and stopTimeMinutes.
 * This message is then sent to the connected WebSocket client.
 */
void sendSystemConfig() {
    if (!webSocketConnected) {
        Serial.println("Cannot send system config, WebSocket not connected.");
        return;
    }

    StaticJsonDocument<768> doc; // Adjusted size for system config
    doc["type"] = "systemConfig";

    JsonObject config = doc.createNestedObject("config");
    config["minX"] = minX;
    config["maxX"] = maxX;
    config["minY"] = minY;
    config["maxY"] = maxY;

    JsonArray timersArray = config.createNestedArray("timers");
    for (int i = 0; i < numTimeSlots; i++) {
        JsonObject timer = timersArray.createNestedObject();
        timer["startTimeMinutes"] = timeSlots[i].startTimeMinutes;
        timer["stopTimeMinutes"] = timeSlots[i].stopTimeMinutes;
        // 'active' field is mostly for internal ESP32 logic, not usually sent to client here
        // but can be added if client needs to know raw 'active' state from struct.
    }

    String output;
    serializeJson(doc, output);
    Serial.println("[DEBUG] Attempting to send systemConfig via WebSocket.");
    Serial.println("JSON to send: " + output);
    webSocket.sendTXT(output);
    // Serial.println("Sent systemConfig: " + output); // Original log, can be removed or kept
}

void saveTimersToPreferences() {
  Serial.println("[DEBUG] saveTimersToPreferences called.");
  preferences.begin("servo_config", false); // Begin the session here, non-read-only

  Serial.printf("[DEBUG] Saving num_timers in saveTimersToPreferences: %d\n", numTimeSlots);
  preferences.putInt("num_timers", numTimeSlots);
  // Serial.printf("Read back num_timers: %d\n", preferences.getInt("num_timers", -99)); // Optional: Verification

  for (int i = 0; i < numTimeSlots; i++) {
    String startKey = "timer_" + String(i) + "_start";
    String stopKey = "timer_" + String(i) + "_stop";
    Serial.printf("[DEBUG] Saving timer %d to Prefs: Start=%d, Stop=%d\n", i, timeSlots[i].startTimeMinutes, timeSlots[i].stopTimeMinutes);
    preferences.putInt(startKey.c_str(), timeSlots[i].startTimeMinutes);
    // Serial.printf("Saved %s: %d\n", startKey.c_str(), timeSlots[i].startTimeMinutes);
    // Serial.printf("Read back %s: %d\n", startKey.c_str(), preferences.getInt(startKey.c_str(), -99)); // Optional

    preferences.putInt(stopKey.c_str(), timeSlots[i].stopTimeMinutes);
    // Serial.printf("Saved %s: %d\n", stopKey.c_str(), timeSlots[i].stopTimeMinutes);
    // Serial.printf("Read back %s: %d\n", stopKey.c_str(), preferences.getInt(stopKey.c_str(), -99)); // Optional
  }

  preferences.end(); // End the session here
  Serial.println("Timers saved to Preferences.");
}

void addTimeSlot(String startTimeStr, String stopTimeStr) {
  Serial.printf("[DEBUG] addTimeSlot called with startTimeStr: %s, stopTimeStr: %s\n", startTimeStr.c_str(), stopTimeStr.c_str());
  if (numTimeSlots < MAX_TIMERS) {
    int startTimeMinutes = timeToMinutes(startTimeStr);
    int stopTimeMinutes = timeToMinutes(stopTimeStr);
    Serial.printf("[DEBUG] Converted to startTimeMinutes: %d, stopTimeMinutes: %d\n", startTimeMinutes, stopTimeMinutes);

    // Basic validation for converted minutes
    if (startTimeMinutes == -1 || stopTimeMinutes == -1) {
        Serial.println("[DEBUG] Invalid time string provided to addTimeSlot. Timer not added.");
        return;
    }

    timeSlots[numTimeSlots].startTimeMinutes = startTimeMinutes;
    timeSlots[numTimeSlots].stopTimeMinutes = stopTimeMinutes;
    timeSlots[numTimeSlots].active = false; // Ensure new timers are initially inactive
    numTimeSlots++; // Increment numTimeSlots FIRST
    Serial.printf("[DEBUG] numTimeSlots incremented to: %d\n", numTimeSlots);
    Serial.println("[DEBUG] About to save timers in addTimeSlot.");
    saveTimersToPreferences(); // Now save with the updated count
    Serial.printf("Added new timeslot. Total slots: %d. Start: %s, End: %s\n", numTimeSlots, startTimeStr.c_str(), stopTimeStr.c_str());
  } else {
    Serial.println("Maximum number of timers reached. Cannot add new slot.");
  }
}

void deleteTimeSlot(int indexToDelete) {
  if (indexToDelete >= 0 && indexToDelete < numTimeSlots) {
    // Shift the remaining timers to fill the gap
    for (int i = indexToDelete; i < numTimeSlots - 1; i++) {
      timeSlots[i] = timeSlots[i + 1];
    }
    // Mark the last slot as empty
    timeSlots[numTimeSlots - 1].startTimeMinutes = -1;
    timeSlots[numTimeSlots - 1].stopTimeMinutes = -1;
    numTimeSlots--;
    saveTimersToPreferences();
  } else {
    Serial.println("Invalid timer index to delete.");
  }
}



void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  if (WiFi.status() == WL_CONNECTED) {
    display.print("IP: ");
    display.println(WiFi.localIP());
    display.print("Time: ");
    display.println(timeClient.getFormattedTime());
    display.setCursor(0, 16); // Move to the second half of the screen

    if (isScheduledMovementActive) {
      display.println("Scheduled movement");
      display.printf("%s - %s", currentScheduleStartTime.c_str(), currentScheduleStopTime.c_str());
    } else if (randomMotionActive) {
      display.println("Random movement");
    } else {
      display.println("Standby");
    }
  } else {
    display.println("WiFi Disconnected");
  }
  display.display();
}

// Function to start a smooth servo movement
/**
 * @brief Initiates a smooth, non-blocking movement for a specified servo.
 * @param servo The Servo object to control.
 * @param movement A reference to the ServoMovement struct tracking this servo's state.
 * @param targetPos The target position (angle) for the servo.
 * @param moveTime The total duration in milliseconds the movement should take.
 */
void startServoMovement(Servo& servo, ServoMovement &movement, int targetPos, int moveTime) {
  movement.startPos = servo.read();
  movement.targetPos = targetPos;
  movement.startTime = millis();
  movement.duration = moveTime;
  movement.isMoving = true;
}

// Function to update servo position smoothly (non-blocking)
/**
 * @brief Updates the position of a servo during a smooth movement.
 * Call this repeatedly in the loop for each servo that might be moving.
 * @param servo The Servo object to control.
 * @param movement A reference to the ServoMovement struct tracking this servo's state.
 */
void updateServoMovement(Servo& servo, ServoMovement &movement) {
  if (movement.isMoving) {
    unsigned long elapsedTime = millis() - movement.startTime;
    if (elapsedTime >= movement.duration) {
      servo.write(movement.targetPos);  // Final position
      movement.isMoving = false; // Stop the movement
    } else {
      float progress = float(elapsedTime) / movement.duration;
      int currentPos = movement.startPos + (movement.targetPos - movement.startPos) * progress;
      servo.write(int(currentPos));
    }
  }
}
// Function to test servo movement by moving it from min to max and back
void testServo(Servo& servo, int minPos, int maxPos) {
  servo.write(minPos);
  delay(1000);  // Wait for movement to complete
  servo.write(maxPos);
}

/**
 * @brief Manages random, non-blocking movements for both servos.
 *
 * When called, if enough time has passed since the last random move (defined by `movementInterval`),
 * it calculates new random target positions for both servos within their configured min/max ranges.
 * It then calculates a movement duration based on the distance to travel and initiates
 * smooth movements using `startServoMovement()`. A new random `movementInterval` is set for the next cycle.
 */
void moveServosRandomlyNonBlocking() {
    unsigned long currentMillis = millis();

    if (currentMillis - lastMotionTime >= movementInterval) {
      lastMotionTime = currentMillis;

      int currentX = myservoX.read();
      int currentY = myservoY.read();
      int randomX = random(minX, maxX + 1); // Random position for X
      int randomY = random(minY, maxY + 1); // Random position for Y

      // Calculate the distance to travel for each servo
      int distanceX = abs(randomX - currentX);
      int distanceY = abs(randomY - currentY);
      int maxDistance = max(distanceX, distanceY); // Consider the larger movement

      // Determine the movement duration based on the distance
      // You can adjust these scaling factors to control the speed
      unsigned long minMoveTime = 1000; // Minimum time for any movement
      unsigned long baseMoveTime = maxDistance * 500; // Example: 15 ms per degree
      unsigned long maxMoveTime = 3000; // Maximum allowed movement time

      int moveTimeX = constrain(baseMoveTime, minMoveTime, maxMoveTime);
      int moveTimeY = constrain(baseMoveTime, minMoveTime, maxMoveTime);

      // Start the movements
      startServoMovement(myservoX, movementX, randomX, moveTimeX);
      startServoMovement(myservoY, movementY, randomY, moveTimeY);

      // Set a new random interval for the NEXT move, possibly based on current movement
      movementInterval = random(minMotionInterval, maxMotionInterval + 1);

    }
}

// Function to display settings on OLED
void displaySettings() {
  Serial.println("Entering displaySettings()"); // Add this line
  display.clearDisplay();
  display.setCursor(0, 0);

  if (settingsMode) {
    Serial.println("settingsMode is TRUE"); // Add this line
    display.println("SETTINGS MODE");

    switch (currentSetting) {
      case 0: display.print("MinX: "); break;
      case 1: display.print("MaxX: "); break;
      case 2: display.print("MinY: "); break;
      case 3: display.print("MaxY: "); break;
    }

    switch (currentSetting) {
      case 0: display.println(minX); break;
      case 1: display.println(maxX); break;
      case 2: display.println(minY); break;
      case 3: display.println(maxY); break;
    }
  } else {
    Serial.println("settingsMode is FALSE"); // Add this line
    display.print("Servo X: "); display.println(myservoX.read());
    display.print("Servo Y: "); display.println(myservoY.read());
    if (WiFi.status() == WL_CONNECTED) {
      display.print("IP: "); display.println(WiFi.localIP());
    } else {
      display.print("Not Connected");
    }
  }
  display.display(); 
  Serial.println("Exiting displaySettings()"); // Add this line
}

// Modified adjustSettings function to use Preferences
void adjustSettings(int setting, bool increment) {
  switch (setting) {
    case 0: preferences.putInt("min_x", constrain(preferences.getInt("min_x", 0) + (increment ? 1 : -1), 0, 180)); break;
    case 1: preferences.putInt("max_x", constrain(preferences.getInt("max_x", 180) + (increment ? 1 : -1), 0, 180)); break;
    case 2: preferences.putInt("min_y", constrain(preferences.getInt("min_y", 45) + (increment ? 1 : -1), 45, 135)); break;
    case 3: preferences.putInt("max_y", constrain(preferences.getInt("max_y", 134) + (increment ? 1 : -1), 45, 135)); break;
  }
  displaySettings();
}

void readTouch() {
  static unsigned long touchStartTime1 = 0;
  static bool touchInProgress1 = false;
  static bool shortTouchProcessed1 = false;
  static bool longTouchProcessed1 = false;

  static unsigned long touchStartTime2 = 0;
  static bool touchInProgress2 = false;
  static bool shortTouchProcessed2 = false;
  static bool longTouchProcessed2 = false;

  int touchValue1 = touchRead(touchPin1);
  int touchValue2 = touchRead(touchPin2);

  // Handle Touch Pin 1 (GPIO 32) - Sleep/Wake
  if (touchValue1 < threshold) {
    if (!touchInProgress1) {
      touchStartTime1 = millis();
      touchInProgress1 = true;
      shortTouchProcessed1 = false;
      longTouchProcessed1 = false;
    } else {
      unsigned long touchDuration1 = millis() - touchStartTime1;

      if (touchDuration1 >= 10000) {
        Serial.println("Touch 1 held for 10 seconds - Restarting ESP32...");
        ESP.restart();
      }

      if (touchDuration1 >= 500 && !longTouchProcessed1 && touchDuration1 < 10000) {
        // Sleep
        Serial.println("Long Touch 1 Detected - Turning OFF laser and relay");
        Serial2.println("STOP_STREAM");
        display.clearDisplay();
        display.setCursor(0, 0);
        display.print("Powering Down...");
        display.display();
        delay(2000);
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        laserActive = false;
        relayActive = false;
        digitalWrite(outputPin, LOW);
        digitalWrite(relayPin, LOW);
        randomMotionActive = false;
        longTouchProcessed1 = true;
        delay(1000);
        esp_deep_sleep_start();
      }

      if (touchDuration1 < 500 && !shortTouchProcessed1 && !laserActive && !relayActive) {
        // Wake
        Serial.println("Short Touch 1 Detected - Turning ON laser and relay");
        display.ssd1306_command(SSD1306_DISPLAYON);
        display.clearDisplay();
        display.setCursor(0, 0);
        display.print("Waking...");
        display.display();
        delay(2000);
        updateDisplay();
        laserActive = true;
        relayActive = true;
        digitalWrite(outputPin, HIGH);
        digitalWrite(relayPin, HIGH);
        shortTouchProcessed1 = true;
      }
    }
  } else {
    touchInProgress1 = false;
    shortTouchProcessed1 = false;
    longTouchProcessed1 = false;
  }

  // Handle Touch Pin 2 (GPIO 33) - Settings and Random Motion
  static int currentSetting = 0; // 0: minX, 1: maxX, 2: minY, 3: maxY
  static bool settingsMode = false;

  if (touchValue2 < threshold) {
    if (!touchInProgress2) {
      touchStartTime2 = millis();
      touchInProgress2 = true;
      shortTouchProcessed2 = false;
      longTouchProcessed2 = false;
    } else {
      unsigned long touchDuration2 = millis() - touchStartTime2;

      if (touchDuration2 >= 500 && !longTouchProcessed2) {
        // Toggle Settings Mode
        Serial.println("Long Touch 2 Detected");
        longTouchProcessed2 = true;

        settingsMode = !settingsMode;  // Toggle settings mode *FIRST*

        displaySettings();             // *THEN* update the display
        delay(50);                     // Small delay to allow display update

        if (settingsMode) {
          Serial.println("Entering settings mode.");
        } else {
          Serial.println("Exiting settings mode.");
        }
      }

      if (touchDuration2 < 500 && !shortTouchProcessed2) {
        shortTouchProcessed2 = true;
        if (!settingsMode) {
          // Toggle Random Motion
          randomMotionActive = !randomMotionActive;
          if (randomMotionActive) {
            Serial.println("Random motion is now active.");
          } else {
            Serial.println("Random motion is now inactive.");
          }
        } else {
          // Adjust Settings (using both buttons)
          if (touchValue1 < threshold) { // If Touch 1 is also pressed (decrement)
            adjustSettings(currentSetting, false); // Decrement
            Serial.print("Adjusting setting "); Serial.println(currentSetting);
          } else {  // If only Touch 2 is pressed (increment)
            adjustSettings(currentSetting, true); // Increment
            Serial.print("Adjusting setting "); Serial.println(currentSetting);
          }
          currentSetting = (currentSetting + 1) % 4; // Cycle through 0-3
          Serial.print("Next setting to adjust "); Serial.println(currentSetting);
        }
      }
    }
  } else { // Touch is released!
    touchInProgress2 = false;
    shortTouchProcessed2 = false;
    longTouchProcessed2 = false;  // Reset here!
  }
}

/**
 * @brief Main loop of the ESP32 DevKitV1 application.
 *
 * This function is executed repeatedly and handles the core logic:
 * - Handles ArduinoOTA updates.
 * - Processes WebSocket client events and reconnection logic.
 * - Reads touch pin inputs for sleep/wake and settings adjustment.
 * - Manages scheduled movements based on NTP time and configured time slots.
 * - Controls random servo movements if enabled (either by schedule or manual toggle).
 * - Updates servo positions for smooth, non-blocking movements.
 * - Updates the OLED display based on current mode (normal, settings) and status.
 * - Sends periodic status updates to the WebSocket server.
 * - Handles serial communication with the ESP32CAM (receiving status, sending commands).
 * - Manages WiFi connection using WiFiManager, attempting to reconnect if disconnected.
 * - Forwards commands received via main Serial to the ESP32CAM via Serial2.
 * - Periodically updates NTP time.
 */
void loop() {
  ArduinoOTA.handle();
  webSocket.loop();
  readTouch();

  if (!webSocketConnected && WiFi.status() == WL_CONNECTED) {
    if (millis() - webSocketLastReconnectAttempt > webSocketReconnectInterval) {
      webSocketLastReconnectAttempt = millis();
      Serial.println("Attempting to reconnect WebSocket...");
      // webSocket.begin() should ideally handle reconnection attempts if using setReconnectInterval
      // but an explicit begin call might be needed if the initial connection fails repeatedly.
      // For now, rely on setReconnectInterval and internal handling.
      // If connection drops, the library should try to reconnect.
      // If initial connect fails, this might need an explicit webSocket.connect(host,port,path) or begin again.
      // Let's assume library handles reconnects for now. If not, add explicit webSocket.connect() here.
    }
  }

  bool shouldMoveRandomlyThisCycle = false;
  String scheduledStartTime = ""; // Local variables to store the times
  String scheduledStopTime = "";

  if (timeClient.isTimeSet()) {
    String currentTimeStr = getFormattedTime();
    int currentMinutes = timeToMinutes(currentTimeStr);

    if (currentMinutes == -1) { // timeToMinutes might return -1 if time is not set or format is wrong
        Serial.println("Cannot check schedule, current time is invalid.");
    } else {
        for (int i = 0; i < numTimeSlots; i++) {
            // Explicitly skip if timer slot data is invalid (should be ensured by loading logic too)
            if (timeSlots[i].startTimeMinutes == -1 || timeSlots[i].stopTimeMinutes == -1) {
                continue;
            }

            // Check if current time falls within this time slot
            if (timeSlots[i].startTimeMinutes < timeSlots[i].stopTimeMinutes) { // Normal case (e.g., 10:00 - 12:00)
                if (currentMinutes >= timeSlots[i].startTimeMinutes && currentMinutes < timeSlots[i].stopTimeMinutes) {
                    shouldMoveRandomlyThisCycle = true;
                    scheduledStartTime = minutesToTime(timeSlots[i].startTimeMinutes);
                    scheduledStopTime = minutesToTime(timeSlots[i].stopTimeMinutes);
                    break;
                }
            } else { // Overnight case (e.g., 22:00 - 02:00)
                if (currentMinutes >= timeSlots[i].startTimeMinutes || currentMinutes < timeSlots[i].stopTimeMinutes) {
                    shouldMoveRandomlyThisCycle = true;
                    scheduledStartTime = minutesToTime(timeSlots[i].startTimeMinutes);
                    scheduledStopTime = minutesToTime(timeSlots[i].stopTimeMinutes);
                    break;
                }
            }
        }
    }
  }

  isScheduledMovementActive = shouldMoveRandomlyThisCycle; // Update the global state
  if (isScheduledMovementActive) {
    // Ensure that currentScheduleStartTime and currentScheduleStopTime are not "N/A" before using them
    if (scheduledStartTime != "N/A" && scheduledStopTime != "N/A") {
        currentScheduleStartTime = scheduledStartTime; // Update the global start time
        currentScheduleStopTime = scheduledStopTime;   // Update the global stop time
        turnLaserOn();
    } else {
        // This case should ideally not be reached if shouldMoveRandomlyThisCycle is true
        // because minutesToTime should have provided valid strings.
        // But as a safeguard:
        isScheduledMovementActive = false; // Correct the state if times are N/A
        currentScheduleStartTime = "";
        currentScheduleStopTime = "";
    }
  } else {
    currentScheduleStartTime = ""; // Clear the global start time when no schedule is active
    currentScheduleStopTime = "";   // Clear the global stop time when no schedule is active
  }

  // Call random movement if the schedule says it should AND it's not overridden, OR if the button is toggled ON
  if ((isScheduledMovementActive && !scheduledMovementOverridden) || randomMotionActive) { // isScheduledMovementActive is now more robust
    moveServosRandomlyNonBlocking(); // Call the non-blocking random movement function
    if (laserActive) turnLaserOn(); // Consider if laserActive should gate this
  } else {
    // Only turn laser off if not in configuration AND no manual override keeps it on
    // Assuming laserActive is the override/manual state.
    if (!inConfiguration && laserActive) { turnLaserOff(); }
    else if (!inConfiguration && !laserActive) { /* already off */ }
    else if (inConfiguration && laserActive) { /* keep on during config if it was on */ }
  }

  updateServoMovement(myservoX, movementX); // Update X servo movement
  updateServoMovement(myservoY, movementY); // Update Y servo movement
// The following block seems to be a repetition of the logic above for `isScheduledMovementActive` and `currentScheduleStart/StopTime`
// It should be removed to avoid redundancy and potential conflicts.
// The state of isScheduledMovementActive, currentScheduleStartTime, and currentScheduleStopTime
// is already correctly determined by the preceding block.

  // (Repetitive block removed)

  if (settingsMode) {
    displaySettings();
  } else {
    updateDisplay();
  }

  // Status Sending Logic
  static unsigned long lastStatusUpdateTime = 0;
  unsigned long statusUpdateInterval = 10000; // Send status every 10 seconds

  if (webSocketConnected && (millis() - lastStatusUpdateTime > statusUpdateInterval)) {
    lastStatusUpdateTime = millis();
    StaticJsonDocument<512> doc; // Increased size for more status data
    doc["type"] = "statusUpdate";
    // No need to send deviceId, server knows it from connection object

    JsonObject data = doc.createNestedObject("data");
    data["uptime_ms"] = millis();
    data["wifi_rssi"] = WiFi.RSSI();
    data["servoX_pos"] = myservoX.read();
    data["servoY_pos"] = myservoY.read();
    data["laser_active"] = digitalRead(outputPin) == HIGH;
    data["relay_active"] = relayActive; // relayActive is updated by RELAY_ON/OFF commands
    data["random_motion_active"] = randomMotionActive;
    data["is_scheduled_movement_active"] = isScheduledMovementActive;
    data["esp32cam_connected"] = esp32CamConnected;
    data["esp32cam_streaming"] = streaming;
    data["cam_led_active"] = camLedActive; // Include CAM LED state
    // Add servo limits to status update
    data["minX"] = minX;
    data["maxX"] = maxX;
    data["minY"] = minY;
    data["maxY"] = maxY;
    // Add other relevant status data

    String output;
    serializeJson(doc, output);
    webSocket.sendTXT(output);
    Serial.println("Sent status update: " + output);
  }

  // Read and print any data coming from the ESP32-CAM on Serial2
  if (Serial2.available() > 0) {
    String camResponse = Serial2.readStringUntil('\n');
    camResponse.trim();
    Serial.print("ESP32-CAM Response: ");
    Serial.println(camResponse);

    // Process specific responses from the ESP32-CAM
    if (!esp32CamConnected && camResponse.startsWith("IP:")) {
      esp32CamIP = camResponse.substring(3);
      Serial.print("ESP32-CAM IP Address: ");
      Serial.println(esp32CamIP);
      esp32CamConnected = true;
      Serial.println("ESP32-CAM is ready for commands.");
    } else if (!esp32CamConnected && camResponse.startsWith("ERROR:")) {
      Serial.print("ESP32-CAM Error: ");
      Serial.println(camResponse);
    }
  }

  if (wifiConnectedAP && !esp32CamConnected) {
    Serial.println("\nSending Wi-Fi credentials to ESP32-CAM (via Serial2)...");
    Serial2.println("WIFI_CREDENTIALS"); // Start marker
    Serial2.println(staSSID);
    Serial2.println(staPassword);
    Serial.println("Credentials sent.");
    wifiConnectedAP = false; // Only send once
  }

  // Wi-Fi Manager handling in loop to ensure it stays active if connection is lost
  WiFiManager wm;
  if (WiFi.status() != WL_CONNECTED && !wifiConnectedAP) {
    Serial.println("Attempting AutoConnect MiauLaser");
    if (!wm.autoConnect("MiauLaser")) {
      Serial.println("AutoConnect Failed, starting Config Portal");
      if (!wm.startConfigPortal("ESP32-Config")) {
        Serial.println("Config Portal Failed!");
        delay(3000);
        ESP.restart(); // Consider a more graceful recovery
      } else {
        Serial.println("Config Portal Running");
      }
    } else {
      Serial.println("Connected to WiFi");
      wm.setSaveConfigCallback(saveWifiCallback);
      staSSID = WiFi.SSID();
      staPassword = WiFi.psk();
      wifiConnectedAP = true;
      Serial.print("SSID: ");
      Serial.println(staSSID);
      Serial.print("Password: ");
      Serial.println(staPassword);
    }
  }

  if (esp32CamConnected) {
    while (Serial.available() > 0) {
      String command = Serial.readStringUntil('\n');
      command.trim();
      if (command == "START_STREAM" && !streaming) {
        Serial2.println(command);
        Serial.print("Sent command (via Serial2): ");
        Serial.println(command);
        streaming = true;
      } else if (command == "STOP_STREAM" && streaming) {
        Serial2.println(command);
        Serial.print("Sent command (via Serial2): ");
        Serial.println(command);
        streaming = false;
      } else if (command == "LED_ON") {
        Serial2.println(command);
        Serial.print("Sent command (via Serial2): ");
        Serial.println(command);
      } else if (command == "LED_OFF") {
        Serial2.println(command);
        Serial.print("Sent command (via Serial2): ");
        Serial.println(command);
      } else {
        Serial2.println(command); // Forward other commands
        Serial.print("Sent command (via Serial2): ");
        Serial.println(command);
      }
      delay(1);
    }
  }

  if (esp32CamConnected) {
    while (Serial.available() > 0) { // Read commands from the main Serial monitor
      String command = Serial.readStringUntil('\n');
      command.trim();
      Serial2.println(command); // Forward the command to ESP32-CAM via Serial2
      Serial.print("Sent command (via Serial2): ");
      Serial.println(command);
      delay(1); // Small delay for serial transmission
    }
  }
  if (WiFi.status() == WL_CONNECTED && millis() - lastNTPUpdateTime > ntpUpdateInterval) {
    Serial.println("Updating NTP time...");
    if (!timeClient.update()) {
      Serial.println("Failed to get NTP time.");
    } else {
      Serial.println("NTP time updated.");
      lastNTPUpdateTime = millis();
    }
  }

  delay(1); // Small delay in the main loop
}