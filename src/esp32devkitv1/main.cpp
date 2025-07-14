#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP32Servo.h>
// #include <EEPROM.h>
#include "soc/soc.h"             // For `soc_caps.h` and low-level system functions
#include "soc/rtc_cntl_reg.h"    // For `RTC_CNTL_BROWN_OUT_REG`
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
#include <time.h> // For time_t, tm, time(), localtime_r(), strftime()
#include <HTTPUpdate.h>
#include <HTTPClient.h>
#include "certificates.h" // For root_ca_cert

/**
 * @file main.cpp
 * @brief Main firmware for the ESP32 DevKit V1 controlling the Kytsa Laser Toy.
 *
 * This firmware manages WiFi connectivity, WebSocket communication with a remote server,
 * servo control for laser movement, NTP time synchronization, timezone handling,
 * scheduled automated movements ("Kytsa Workouts"), interaction with an ESP32-CAM
 * for video streaming, and an OLED display for status information.
 * It uses Preferences for persistent storage of settings and WiFiManager for
 * initial WiFi configuration.
 */

// Global Objects and Configuration Variables

/** @brief Preferences object for Non-Volatile Storage (NVS). Used to store settings persistently. */
Preferences preferences;

// --- NTP and Time Settings ---
/** @brief UDP client for NTP communication. */
WiFiUDP ntpUDP;
/** @brief NTP client instance for time synchronization. Managed by this firmware. */
NTPClient timeClient(ntpUDP);
/** @brief Timestamp (millis()) of the last successful NTP update. */
unsigned long lastNTPUpdateTime = 0;
/** @brief Interval in milliseconds for attempting NTP updates. Default is 1 hour. Configurable via web UI. */
long ntpUpdateInterval = 60 * 60 * 1000;
/** @brief Timezone POSIX string (e.g., "EST5EDT,M3.2.0/2,M11.1.0/2"). Loaded from NVS, default "UTC0". Configurable. */
String timeZonePosixString = "UTC0";

// --- Motion and Scheduling State ---
/** @brief True if random servo motion is currently manually activated via the web UI. */
bool randomMotionActive = false;
/** @brief True if an active scheduled movement has been temporarily overridden (e.g., by manual stop via UI). Currently not fully implemented. */
bool scheduledMovementOverridden = false;
/** @brief True if a scheduled "Kytsa Workout" is currently active based on NTP time and configured timer slots. */
bool isScheduledMovementActive = false;
/** @brief Flag to indicate if the device is in a special configuration mode (legacy, less relevant with WebSocket). */
bool inConfiguration = false;
/** @brief String storing the HH:MM start time of the currently active or next scheduled movement. For display. */
String currentScheduleStartTime = "";
/** @brief String storing the HH:MM stop time of the currently active or next scheduled movement. For display. */
String currentScheduleStopTime = "";

// --- ESP32-CAM Communication ---
/** @brief HardwareSerial instance (Serial2) used for communication with the ESP32-CAM. */
HardwareSerial& serialPort = Serial2;
/** @brief SSID of the currently connected Wi-Fi network, shared with ESP32-CAM. */
String staSSID;
/** @brief Password for the currently connected Wi-Fi network, shared with ESP32-CAM. */
String staPassword;
/** @brief IP address of the connected ESP32-CAM, received over serial. Empty if not connected/reported. */
String esp32CamIP = "";
/** @brief True if communication with the ESP32-CAM has been established (e.g., IP received). */
bool esp32CamConnected = false;
/** @brief True if the ESP32-CAM is currently commanded to stream video. */
bool streaming = false;
/** @brief True if the ESP32-CAM's LED is currently commanded to be active. */
bool camLedActive = false;

// --- WiFi State ---
/** @brief True if WiFi was connected using credentials obtained via WiFiManager's Access Point mode during the current session. */
bool wifiConnectedAP = false;

// --- Display Settings ---
/** @brief Width of the OLED display in pixels. */
#define SCREEN_WIDTH 128
/** @brief Height of the OLED display in pixels. */
#define SCREEN_HEIGHT 32
/** @brief Reset pin for the OLED display (-1 if not used, managed by I2C). */
#define OLED_RESET     -1
/** @brief Adafruit SSD1306 display object instance. */
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Servo Objects and Pin Definitions ---
/** @brief Servo object for the X-axis (pan). */
Servo myservoX;
/** @brief Servo object for the Y-axis (tilt). */
Servo myservoY;

/** @brief GPIO pin connected to the X-axis servo signal line. */
const int servoPinX = 13;
/** @brief GPIO pin connected to the Y-axis servo signal line. */
const int servoPinY = 12;
/** @brief GPIO pin used to control the laser module. (HIGH for ON, LOW for OFF - needs verification). */
const int outputPin = 25; // Typically for Laser
/** @brief GPIO pin used to control the auxiliary relay. (HIGH for ON, LOW for OFF - needs verification). */
const int relayPin = 26;
/** @brief GPIO pin for Touch Pad 1 (used for sleep/wake and other interactions). */
const int touchPin1 = 32;
/** @brief GPIO pin for Touch Pad 2 (used for settings and other interactions). */
const int touchPin2 = 33;
/** @brief GPIO pin for I2C SDA (connected to OLED display). */
const int SDA_PIN = 21;
/** @brief GPIO pin for I2C SCL (connected to OLED display). */
const int SCL_PIN = 18;

// --- Touch Input and Device State ---
/** @brief Threshold for touch pin sensitivity. Lower values are more sensitive. */
const int threshold = 75;
/** @brief Stores the raw value read from a touch pin. */
int touchValue; // Note: This seems to be a generic variable, might be better localized or removed if not broadly used.
/** @brief True if the laser is currently commanded to be active. Defaults to OFF at boot. Managed by UI commands and automated activities. */
bool laserActive = false;
/** @brief True if the relay is currently commanded to be active. Default ON at boot. */
bool relayActive = true;
/** @brief True if the device is in the touch-based settings adjustment mode. */
bool settingsMode = false;
/** @brief Index for the current setting being adjusted in touch-based settings mode. */
int currentSetting = 0;

// --- Random Motion Parameters ---
/** @brief Timestamp (millis()) of the last random motion execution. */
unsigned long lastMotionTime = 0;
/** @brief Minimum interval (ms) between random movements when `minVel`/`maxVel` are not used (legacy). */
unsigned long minMotionInterval = 100;
/** @brief Maximum interval (ms) between random movements when `minVel`/`maxVel` are not used (legacy). */
unsigned long maxMotionInterval = 3000;

// --- Timer Schedule Data Structures ---
/**
 * @struct TimeSlot
 * @brief Represents a single scheduled time slot for automated "Kytsa Workouts".
 */
struct TimeSlot {
  int startTimeMinutes; ///< Start time of the slot in minutes from the beginning of the day (0-1439).
  int stopTimeMinutes;  ///< Stop time of the slot in minutes from the beginning of the day (0-1439).
  bool active;          ///< Runtime flag, true if this timeslot is currently considered active by the scheduling logic.
};

/** @brief Maximum number of timer slots that can be configured and stored. */
const int MAX_TIMERS = 5;
/** @brief Array to hold the configured timer slots. */
TimeSlot timeSlots[MAX_TIMERS];
/** @brief Current number of active/configured timer slots in the `timeSlots` array. */
int numTimeSlots = 0; // Initialized to 0, loaded from NVS in setup()

// --- WiFiManager ---
/** @brief WiFiManager object instance for simplified WiFi configuration. */
WiFiManager wm;

// --- Legacy/Unused/General Variables (Review for cleanup) ---
String header; // Potentially for HTTP server responses, seems unused in current WebSocket context.
String valueStringX = String(90); // Seems to be for storing servo X position as string, possibly for older UI.
String valueStringY = String(90); // Seems to be for storing servo Y position as string, possibly for older UI.
int pos1 = 0; // Purpose unclear, potentially legacy.
int pos2 = 0; // Purpose unclear, potentially legacy.

unsigned long currentTime = millis(); // Generic timestamp, often better to get current millis() directly when needed.
unsigned long previousTime = 0;    // Generic timestamp, often better to use specific state variables.
const long timeoutTime = 2000;     // Generic timeout, make specific if used for distinct purposes.

// --- Servo Axis Limits ---
/** @brief Minimum angle for the X-axis servo. Loaded from NVS, default 0. */
int minX = 0;
/** @brief Maximum angle for the X-axis servo. Loaded from NVS, default 180. */
int maxX = 180;
/** @brief Minimum angle for the Y-axis servo. Loaded from NVS, default 45. */
int minY = 45;
/** @brief Maximum angle for the Y-axis servo. Loaded from NVS, default 135. */
int maxY = 135;

// --- Random Movement Velocity/Interval Settings ---
/** @brief Minimum interval (ms) between random movements. Loaded from NVS, default 800ms. Configurable. */
int minVel = 800;
/** @brief Maximum interval (ms) between random movements. Loaded from NVS, default 2000ms. Configurable. */
int maxVel = 2000;
/** @brief Timestamp (millis()) of the last random movement command issued. (Potentially redundant with `lastMotionTime` depending on exact usage). */
unsigned long lastMovementTime = 0; // Review: Seems similar to lastMotionTime.
/** @brief Current interval (ms) between random movements, randomized between `minVel` and `maxVel`. */
unsigned long movementInterval = 1000;

// --- Smooth Servo Movement Tracking ---
/**
 * @struct ServoMovement
 * @brief Structure to manage parameters for smooth, non-blocking servo movement.
 */
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

// --- WebSocket Global Variables and Configuration ---
/** @brief Instance of the WebSocketsClient library used for communication with the central server. */
WebSocketsClient webSocket;
/** @brief Flag indicating the current connection status of the WebSocket. True if connected, false otherwise. */
bool webSocketConnected = false;
/** @brief Timestamp (millis()) of the last attempt to reconnect the WebSocket. Used to manage reconnection intervals. */
unsigned long webSocketLastReconnectAttempt = 0;
/** @brief Interval in milliseconds between WebSocket reconnection attempts. Default is 5 seconds. */
const unsigned long webSocketReconnectInterval = 5000;
/** @brief Unique identifier for this ESP32 device, derived from its MAC address. Sent during pairing. */
String deviceId = "";
// --- Firmware & OTA Update ---
/** @brief Current firmware version. Increment this for each new release. */
const int FIRMWARE_VERSION = 1;
/** @brief URL to the firmware binary on the server. */
const char* firmware_binary_url = "https://ebski.co/firmware/firmware.bin";
/** @brief URL to the version file on the server. */
const char* firmware_version_url = "https://ebski.co/firmware/firmware.version";

// --- WebSocket Server Details ---
/** @brief Hostname or IP address of the WebSocket server. */
const char* wsHost = "ebski.co";

// --- OLED Burn-in Prevention ---
/** @brief Timestamp (millis()) of the last detected display activity. */
unsigned long lastDisplayActivityTime = 0;
/** @brief Timeout in milliseconds for turning off OLED due to inactivity. (e.g., 10 minutes) */
const unsigned long DISPLAY_INACTIVITY_TIMEOUT = 10 * 60 * 1000;
/** @brief Flag to track if the OLED is currently off due to inactivity. */
bool isDisplayOffByInactivity = false;
/** @brief Port number for the WebSocket server (e.g., 80 for ws, 443 for wss). Currently set for non-secure WS. */
const uint16_t wsPort = 80;
/** @brief Path for the WebSocket endpoint on the server (e.g., "/ws"). */
const char* wsPath = "/ws";

/**
 * @brief Function executed upon waking from deep sleep.
 * @note This function is marked with RTC_IRAM_ATTR to be placed in IRAM,
 *       which is necessary for functions executed during deep sleep wakeup.
 *       It re-enables the laser and relay by default after wakeup.
 */
void RTC_IRAM_ATTR esp_wake_deep_sleep() {
  esp_default_wake_deep_sleep(); // Default ESP-IDF deep sleep wake stub
  // Re-initialize desired states after wakeup
  laserActive = true;
  relayActive = true;
  digitalWrite(outputPin, HIGH); // Turn laser ON
  digitalWrite(relayPin, HIGH);  // Turn relay ON
}

/**
 * @brief Generates a unique device ID (chip ID) from the ESP32's MAC address.
 * This ID is used to identify the device to the WebSocket server.
 * @return A String representing the unique chip ID (hexadecimal format).
 */
String getChipId() {
    uint64_t chipid = ESP.getEfuseMac(); // Read MAC address
    char chipid_str[17]; // Buffer for string (16 chars + null terminator)
    snprintf(chipid_str, sizeof(chipid_str), "%04X%08X", (uint16_t)(chipid >> 32), (uint32_t)chipid);
    return String(chipid_str);
}

// Forward declarations for functions called in webSocketEvent
void turnLaserOn();
void turnLaserOff();
void sendSystemConfig(); // Forward declaration for our new function
void addTimeSlot(String startTimeStr, String stopTimeStr); // Ensure it's declared if not already before webSocketEvent
void deleteTimeSlot(int indexToDelete); // Ensure it's declared
void updateDisplay(); // Forward declaration for OLED update
void recordDisplayActivity(); // Forward declaration for OLED inactivity feature
void httpUpdateTask(void *pvParameters); // Forward declaration for OTA update task
void performHttpUpdate(); // Forward declaration for the OTA update logic


/**
 * @brief Callback function to handle WebSocket events.
 *
 * This function is registered with the WebSocketsClient and is called when various
 * WebSocket events occur, such as connection, disconnection, text messages, etc.
 * It processes incoming commands from the server (originated by the web UI) and
 * manages the WebSocket connection state.
 *
 * @param type The type of WebSocket event (e.g., WStype_DISCONNECTED, WStype_CONNECTED, WStype_TEXT).
 * @param payload Pointer to the payload data for the event. For TEXT events, this is the message string.
 * @param length Length of the payload data in bytes.
 */
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    // Add a Serial.printf at the top to see ALL event types coming in
    // Serial.printf("[WSc] Event Type Received: %d\n", type);

    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[WSc] Event: WStype_DISCONNECTED\n");
            webSocketConnected = false;
            break;
        case WStype_CONNECTED:
            Serial.printf("[WSc] Event: WStype_CONNECTED to %s\n", (char*)payload);
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
            Serial.printf("[WSc] Event: WStype_TEXT\n"); // Log that we entered TEXT handling
            Serial.printf("[WSc] Raw payload received (for WStype_TEXT): %s\n", (char*)payload); // Moved from top of TEXT block
            Serial.printf("[WSc] Payload length (for WStype_TEXT): %u\n", length);    // Moved from top of TEXT block
            // The old "[WSc] get text:" log is redundant if "Raw payload received" is here.

            // Existing code follows
            {
                StaticJsonDocument<384> doc; // Ensure this size is appropriate, 384 should be fine for this command
                DeserializationError error = deserializeJson(doc, payload, length);
                if (error) {
                    Serial.print(F("deserializeJson() failed: "));
                    Serial.println(error.f_str());
                    return;
                }

                // Main command processing path:
                if (doc.containsKey("command")) {
                    const char* command = doc["command"];
                    Serial.printf("[DEBUG] Received standard command: %s\n", command);

                    if (strcmp(command, "addTimer_value") == 0) { // New handler
                        recordDisplayActivity();
                        Serial.println("[DEBUG] Received 'addTimer_value' command.");
                        const char* dataStr = doc["value"]; // Expect data in "value" field
                        if (dataStr) {
                            String combinedData = String(dataStr);
                            int semicolonIndex = combinedData.indexOf(';');
                            if (semicolonIndex > 0 && semicolonIndex < combinedData.length() - 1) {
                                String startTimeFromData = combinedData.substring(0, semicolonIndex);
                                String endTimeFromData = combinedData.substring(semicolonIndex + 1);
                                Serial.printf("[DEBUG] Parsed from 'value' string -> startTime: %s, endTime: %s\n", startTimeFromData.c_str(), endTimeFromData.c_str());
                                addTimeSlot(startTimeFromData, endTimeFromData);
                            } else {
                                Serial.println("[DEBUG] 'addTimer_value' invalid 'value' field format. Expected 'HH:MM;HH:MM'.");
                            }
                        } else {
                            Serial.println("[DEBUG] 'addTimer_value' missing 'value' field.");
                        }
                        sendSystemConfig(); // Update client
                    } // <<< ****** ADDED MISSING CLOSING BRACE HERE ******
                    // Deprecated addTimer handlers fully removed.
                    // Standard commands:
                    else if (strcmp(command, "servoX") == 0) {
                        recordDisplayActivity();
                        int val = doc["value"];
                        myservoX.write(val);
                        valueStringX = String(val);
                        Serial.printf("Executed servoX: %d\n", val);
                    } else if (strcmp(command, "servoY") == 0) {
                        recordDisplayActivity();
                        int val = doc["value"];
                        myservoY.write(val);
                        valueStringY = String(val);
                        Serial.printf("Executed servoY: %d\n", val);
                    } else if (strcmp(command, "LASER_ON") == 0) {
                        recordDisplayActivity();
                        turnLaserOn(); // Directly controls digitalWrite
                        laserActive = true; // Update state flag
                        Serial.println("Executed LASER_ON, laserActive set to true");
                    } else if (strcmp(command, "LASER_OFF") == 0) {
                        recordDisplayActivity();
                        turnLaserOff(); // Directly controls digitalWrite
                        laserActive = false; // Update state flag
                        Serial.println("Executed LASER_OFF, laserActive set to false");
                    } else if (strcmp(command, "RELAY_ON") == 0) {
                        recordDisplayActivity();
                        digitalWrite(relayPin, HIGH);
                        relayActive = true;
                        Serial.println("Executed RELAY_ON");
                    } else if (strcmp(command, "RELAY_OFF") == 0) {
                        recordDisplayActivity();
                        digitalWrite(relayPin, LOW);
                        relayActive = false;
                        Serial.println("Executed RELAY_OFF");
                    } else if (strcmp(command, "RANDOM_MOTION_TOGGLE") == 0) {
                        recordDisplayActivity();
                        randomMotionActive = !randomMotionActive;
                        Serial.printf("Random motion toggled: %s\n", randomMotionActive ? "ON" : "OFF");
                        sendSystemConfig(); // Send feedback for UI update
                    }
                    // Commands for ESP32CAM
                    else if (strcmp(command, "START_STREAM") == 0) {
                        recordDisplayActivity();
                        Serial2.println("START_STREAM");
                        Serial.println("Sent command to ESP32CAM: START_STREAM");
                        streaming = true;
                    } else if (strcmp(command, "STOP_STREAM") == 0) {
                        recordDisplayActivity();
                        Serial2.println("STOP_STREAM");
                        Serial.println("Sent command to ESP32CAM: STOP_STREAM");
                        streaming = false;
                    } else if (strcmp(command, "CAM_LED_ON") == 0) {
                        recordDisplayActivity();
                        Serial2.println("LED_ON");
                        Serial.println("Sent command to ESP32CAM: LED_ON");
                        camLedActive = true;
                    } else if (strcmp(command, "CAM_LED_OFF") == 0) {
                        recordDisplayActivity();
                        Serial2.println("LED_OFF");
                        Serial.println("Sent command to ESP32CAM: LED_OFF");
                        camLedActive = false;
                    } else if (strcmp(command, "getSystemConfig") == 0) {
                        // No display activity for getSystemConfig, it's a background request
                        Serial.println("[DEBUG] Received 'getSystemConfig' command.");
                        Serial.println("[DEBUG] Calling sendSystemConfig for getSystemConfig command.");
                        sendSystemConfig();
                    } else if (strcmp(command, "setServoLimit") == 0) {
                        recordDisplayActivity();
                        const char* axis = doc["axis"];
                        const char* limit_type = doc["limit_type"];
                        int value = doc["value"]; // Assuming value is passed as int
                        if (axis && limit_type) { // Basic null check
                            Serial.printf("Received setServoLimit: axis=%s, type=%s, value=%d\n", axis, limit_type, value);
                            preferences.begin("servo_config", false);
                            if (strcmp(axis, "x") == 0 && strcmp(limit_type, "min") == 0) { minX = value; preferences.putInt("min_x", minX); }
                            else if (strcmp(axis, "x") == 0 && strcmp(limit_type, "max") == 0) { maxX = value; preferences.putInt("max_x", maxX); }
                            else if (strcmp(axis, "y") == 0 && strcmp(limit_type, "min") == 0) { minY = value; preferences.putInt("min_y", minY); }
                            else if (strcmp(axis, "y") == 0 && strcmp(limit_type, "max") == 0) { maxY = value; preferences.putInt("max_y", maxY); }
                            preferences.end();
                            Serial.printf("Updated limits: minX=%d, maxX=%d, minY=%d, maxY=%d\n", minX, maxX, minY, maxY);
                            sendSystemConfig(); // Send updated config
                        } else {
                             Serial.println("[ERROR] setServoLimit: missing axis or limit_type.");
                        }
                    }
                    else if (strcmp(command, "deleteTimer") == 0) {
                        recordDisplayActivity();
                        int timerIndex = doc["timerIndex"];
                        Serial.printf("Received deleteTimer: index=%d\n", timerIndex);
                        deleteTimeSlot(timerIndex);
                        sendSystemConfig();
                    }
                    else if (strcmp(command, "http_ota_update") == 0) {
                        Serial.println("[WSc] Received 'http_ota_update' command. Creating update task...");
                        recordDisplayActivity();
                        xTaskCreate(
                            httpUpdateTask,         /* Task function. */
                            "HTTPUpdateTask",       /* String with name of task. */
                            8192,                   /* Stack size in bytes. */
                            NULL,                   /* Parameter passed as input of the task */
                            1,                      /* Priority of the task. */
                            NULL);                  /* Task handle. */
                    }
                    else if (strcmp(command, "setPreference") == 0) {
                        recordDisplayActivity(); // Any preference change implies user interaction
                        const char* key = doc["key"];
                        if (key) {
                            preferences.begin("servo_config", false);
                            bool preferenceChanged = false;
                            if (strcmp(key, "min_x") == 0) { minX = doc["value"].as<int>(); preferences.putInt("min_x", minX); preferenceChanged = true; }
                            else if (strcmp(key, "max_x") == 0) { maxX = doc["value"].as<int>(); preferences.putInt("max_x", maxX); preferenceChanged = true; }
                            else if (strcmp(key, "min_y") == 0) { minY = doc["value"].as<int>(); preferences.putInt("min_y", minY); preferenceChanged = true; }
                            else if (strcmp(key, "max_y") == 0) { maxY = doc["value"].as<int>(); preferences.putInt("max_y", maxY); preferenceChanged = true; }
                            else if (strcmp(key, "min_vel") == 0) { minVel = doc["value"].as<int>(); preferences.putInt("min_vel", minVel); preferenceChanged = true; }
                            else if (strcmp(key, "max_vel") == 0) { maxVel = doc["value"].as<int>(); preferences.putInt("max_vel", maxVel); preferenceChanged = true; }
                            // Removed obsolete 'timezone' (integer offset) case
                            else if (strcmp(key, "timezone_posix") == 0) {
                                timeZonePosixString = doc["value"].as<String>();
                                preferences.putString("tz_posix", timeZonePosixString);

                                // Set the Timezone environment variable
                                Serial.printf("[WSc] Setting TZ environment variable to: %s\n", timeZonePosixString.c_str());
                                setenv("TZ", timeZonePosixString.c_str(), 1);
                                tzset(); // Apply the TZ setting

                                // Re-configure system time with NTP server. Offsets are 0 as TZ env var handles it.
                                // configTime(0, 0, "pool.ntp.org"); // Not strictly necessary to call this again if NTP servers haven't changed
                                                                    // and if sntp is already running. tzset() is the key.
                                Serial.printf("[WSc] System TZ updated. Current NTP server 'pool.ntp.org'. TZ set by environment: %s\n", timeZonePosixString.c_str());

                                timeClient.setTimeOffset(0); // Ensure NTPClient knows its offset is 0 relative to system time

                                Serial.println("[WSc] Attempting to update NTP time immediately after timezone change...");
                                if (timeClient.update()) {
                                   Serial.println("[WSc] NTP time updated successfully after timezone change.");
                                } else {
                                   Serial.println("[WSc] NTP time update failed after timezone change. Will retry on next interval.");
                                }
                                lastNTPUpdateTime = millis(); // Reset NTP update timer to force update sooner if configured interval is long
                                preferenceChanged = true;
                                Serial.printf("[WSc] Timezone POSIX string updated to: %s and applied.\n", timeZonePosixString.c_str());
                                // updateDisplay(); // recordDisplayActivity will call this if display was off
                            }
                            else if (strcmp(key, "ntp_interval") == 0) {
                                ntpUpdateInterval = doc["value"].as<long>(); // Value is in ms from web UI
                                preferences.putLong("ntp_interval", ntpUpdateInterval);
                                // NTPClient doesn't have a setUpdateInterval method after begin.
                                // The new interval will be used on the next check in loop().
                                preferenceChanged = true;
                            }
                            else { Serial.printf("[WSc] Unknown preference key: %s\n", key); }

                            if (preferenceChanged) {
                                Serial.printf("[WSc] Preference updated: %s = %s\n", key, doc["value"].as<String>().c_str());
                                preferences.end();
                                sendSystemConfig(); // Send updated config to all clients
                            } else {
                                preferences.end(); // Still need to end if no known key matched
                            }
                        } else {
                            Serial.println("[WSc] setPreference command missing 'key'.");
                        }
                    }
                    else {
                        Serial.printf("[DEBUG] Unknown standard command: %s\n", command);
                    }
                } else {
                    Serial.println("[WSc] Received JSON without 'command' key (and not hyper-simplified 'c':'at').");
                }
            }
            break;
        case WStype_BIN:
            Serial.printf("[WSc] get binary length: %u\n", length);
            // hexdump(payload, length); // Example if needed
            break;
        case WStype_ERROR:
            Serial.printf("[WSc] Event: WStype_ERROR - error: %s\n", (char*)payload);
            webSocketConnected = false;
            break;

        // ADD OR MODIFY THESE CASES FOR FRAGMENTATION LOGGING:
        case WStype_FRAGMENT_TEXT_START:
            Serial.printf("[WSc] Event: WStype_FRAGMENT_TEXT_START\n");
            break;
        case WStype_FRAGMENT_BIN_START:
            Serial.printf("[WSc] Event: WStype_FRAGMENT_BIN_START\n");
            break;
        case WStype_FRAGMENT:
            Serial.printf("[WSc] Event: WStype_FRAGMENT - Current fragment length: %u\n", length);
            // Avoid printing payload here unless sure it's text and null-terminated or handled carefully,
            // as fragments are not necessarily complete messages.
            // For debugging, if you know it's text and want a peek:
            // if (length > 0 && payload) {
            //    char buf[33]; // Print up to 32 chars + null terminator
            //    memcpy(buf, payload, length < 32 ? length : 32);
            //    buf[length < 32 ? length : 32] = '\0'; // Ensure null termination
            //    Serial.printf("[WSc] Fragment Data Peek: %s\n", buf);
            // }
            break;
        case WStype_FRAGMENT_FIN:
            Serial.printf("[WSc] Event: WStype_FRAGMENT_FIN - Final fragment length: %u\n", length);
            // After this, the library should internally reassemble and then issue a WStype_TEXT or WStype_BIN event
            // with the complete payload.
            break;

        default:
            Serial.printf("[WSc] Event: Unknown WStype_t: %d\n", type);
            break;
    }
}

/**
 * @brief Callback function triggered by WiFiManager when new WiFi credentials are saved.
 *
 * This function updates global variables `wifiConnectedAP`, `staSSID`, and `staPassword`
 * with the new credentials. It is typically used when WiFiManager has been in AP mode
 * and successfully obtained credentials from the user.
 * @note This function is registered with `wm.setSaveConfigCallback(saveWifiCallback);`
 *       but that line is currently commented out in `setup()`.
 */
void saveWifiCallback() {
  Serial.println("WiFi credentials saved by WiFiManager.");
  wifiConnectedAP = true; // Indicate that WiFi was connected/configured via AP mode this session.
  staSSID = WiFi.SSID();
  staPassword = WiFi.psk();
}

/**
 * @brief Gets the current time as a formatted string (HH:MM:SS) from the NTPClient.
 * @note This time is UTC unless the NTPClient's offset is explicitly set,
 *       which is not the case here as `timeClient.setTimeOffset(0)` is used.
 *       For localized time, use `time_t` with `localtime_r` and `strftime`.
 * @return String containing the formatted time (HH:MM:SS).
 */
String getFormattedTime() {
  return timeClient.getFormattedTime(); // NTPClient's formatted time (usually UTC)
}

/**
 * @brief Converts a time string in "HH:MM" format to total minutes from the start of the day.
 *
 * @param formattedTime A String representing time in "HH:MM" format.
 * @return int Total minutes from midnight (0-1439), or -1 if the input format is invalid or time is out of range.
 */
int timeToMinutes(String formattedTime) {
  if (formattedTime.length() != 5 || formattedTime.charAt(2) != ':') {
    Serial.println("Invalid time format for timeToMinutes: " + formattedTime + ". Expected HH:MM");
    return -1;
  }
  int hours = formattedTime.substring(0, 2).toInt();
  int minutes = formattedTime.substring(3, 5).toInt();
  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
    Serial.println("Invalid time value for timeToMinutes: " + formattedTime + ". Values out of range.");
    return -1;
  }
  return hours * 60 + minutes;
}

/**
 * @brief Converts total minutes from the start of the day to a time string in "HH:MM" format.
 *
 * @param totalMinutes Total minutes from midnight (0-1439).
 * @return String Time in "HH:MM" format, or "N/A" if `totalMinutes` is out of valid range.
 */
String minutesToTime(int totalMinutes) {
  if (totalMinutes < 0 || totalMinutes >= (24 * 60)) {
    // Serial.printf("Invalid totalMinutes value for minutesToTime: %d\n", totalMinutes); // Optional: Log this
    return "N/A";
  }
  int hours = (totalMinutes / 60) % 24;
  int minutes = totalMinutes % 60;
  return String(hours < 10 ? "0" : "") + String(hours) + ":" + String(minutes < 10 ? "0" : "") + String(minutes);
}

/**
 * @brief Turns the laser connected to `outputPin` ON.
 * Assumes `outputPin` is correctly configured as OUTPUT.
 */
void turnLaserOn() {
  digitalWrite(outputPin, HIGH);
  // laserActive = true; // State variable 'laserActive' should be updated by caller if this function is used standalone.
}

/**
 * @brief Turns the laser connected to `outputPin` OFF.
 * Assumes `outputPin` is correctly configured as OUTPUT.
 */
void turnLaserOff() {
  digitalWrite(outputPin, LOW);
  // laserActive = false; // State variable 'laserActive' should be updated by caller.
}

/**
 * @brief Displays a message on the OLED screen prompting the user to connect to the WiFiManager Access Point ("MiauAP").
 * This is typically called when WiFiManager enters AP mode.
 */
void displayConnectAPMessage() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connect to WiFi:");
  display.setTextSize(2); // Larger text for AP name
  display.println("MiauAP");
  display.display();
}


/**
 * @brief Callback function for WiFiManager when it enters configuration mode (Access Point mode).
 *
 * This function calls `displayConnectAPMessage()` to show instructions on the OLED.
 * @param myWiFiManager Pointer to the WiFiManager instance. Not used in this function but required by the callback signature.
 */
void configModeCallback (WiFiManager *myWiFiManager) {
  // Parameter myWiFiManager is not used in this specific callback implementation,
  // but it's part of the function signature required by WiFiManager.
  // (void)myWiFiManager; // Optional: suppress unused parameter warning if compiler flags are strict.
  displayConnectAPMessage();
}

/**
 * @brief Displays a short "Bongo Cat" animation sequence on the OLED.
 * Used as a startup animation.
 */
void showBongoCat(){
  // Frame 1: Base Bongo Cat
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
  timeZonePosixString = preferences.getString("tz_posix", "UTC0"); // Load POSIX TZ string
  ntpUpdateInterval = preferences.getLong("ntp_interval", 60 * 60 * 1000); // Load interval
  Serial.print("Loaded Timezone POSIX String (Preferences): ");
  Serial.println(timeZonePosixString);
  Serial.print("Loaded NTP Update Interval (Preferences): ");
  Serial.println(ntpUpdateInterval);

  Serial.print("Loaded Min Velocity (Preferences): ");
  Serial.println(minVel);
  Serial.print("Loaded Max Velocity (Preferences): ");
  Serial.println(maxVel);

  // Timezone will be set after WiFi connection and configTime.
  // Serial.printf("Setting TZ environment variable from preferences: %s\n", timeZonePosixString.c_str()); // Old position
  // setenv("TZ", timeZonePosixString.c_str(), 1); // Old position
  // tzset(); // Old position
  // Serial.println("System TZ applied from preferences."); // Old position

  // NTP Client will be initialized after WiFi connects.

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

      // Enhanced validation: valid range AND non-zero duration
      if (startMins >= 0 && startMins < (24*60) &&
          stopMins >= 0 && stopMins < (24*60) &&
          startMins != stopMins) {

          // This is a valid timer slot, proceed to load/compact it
          if (validSlotsCount < i) { // Compact valid timers to the front of the array
              timeSlots[validSlotsCount].startTimeMinutes = startMins;
              timeSlots[validSlotsCount].stopTimeMinutes = stopMins;
          } else { // validSlotsCount == i
               // If we always assign to timeSlots[validSlotsCount], this branch might not be strictly needed
               // as timeSlots[i] would be timeSlots[validSlotsCount]
               timeSlots[validSlotsCount].startTimeMinutes = startMins;
               timeSlots[validSlotsCount].stopTimeMinutes = stopMins;
          }
          timeSlots[validSlotsCount].active = false; // Initialize as not active
          Serial.printf("[DEBUG] Loaded Valid Timer %d (from slot %d): Start=%d, Stop=%d\n", validSlotsCount, i, timeSlots[validSlotsCount].startTimeMinutes, timeSlots[validSlotsCount].stopTimeMinutes);
          validSlotsCount++;
      } else {
          // This timer is invalid (e.g. -1 in prefs, out of minute range, or zero duration)
          Serial.printf("[DEBUG] Invalid or zero-duration timer (Start: %d, Stop: %d) from Prefs for slot %d - Skipping.\n", startMins, stopMins, i);
          // No need to explicitly remove from preferences here if we save the compacted valid list later
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
  turnLaserOff(); // Physical laser pin is now LOW. laserActive is already false by global default.
  // laserActive = false; // This line is now redundant due to global default.
  // Serial.println("Laser test in setup complete. Initial laserActive state set to false."); // Redundant log.
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

    // Configure system time with NTP server. This might affect/reset TZ.
    configTime(0, 0, "pool.ntp.org");
    Serial.println("configTime called to set NTP server 'pool.ntp.org'.");

    // NOW, set the definitive timezone using the loaded POSIX string.
    Serial.printf("Setting definitive TZ environment variable: %s\n", timeZonePosixString.c_str());
    setenv("TZ", timeZonePosixString.c_str(), 1);
    tzset();
    Serial.println("Definitive TZ and tzset applied after configTime.");

    // Initialize NTP Client now that WiFi is connected and system time/TZ are set up.
    timeClient.begin();
    timeClient.setTimeOffset(0); // Offset is 0 because TZ env var + localtime_r handle localization
    Serial.println("NTP Client started. Time offset 0, using system TZ.");

    // Immediately try to get the time at startup
    Serial.println("Attempting initial NTP time synchronization...");
    if (!timeClient.update()) {
      Serial.println("Failed to get NTP time at startup (will retry in loop).");
    } else {
      Serial.println("NTP time synchronized at startup.");
      updateDisplay(); // Update display once IF NTP sync was successful in setup
    }
    lastNTPUpdateTime = millis(); // Initialize the last update time, regardless of initial sync success
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

  preferences.end(); // End preferences access after all setup loading/initial saving.

  lastDisplayActivityTime = millis(); // Initialize display activity timer

    // IMPORTANT: WebSocket Receive Buffer Size for addTimer command
    // The "addTimer" command payload is being truncated, likely due to the default
    // WebSocket client receive buffer size being too small (observed truncation at ~22 bytes).
    // To fix this, you may need to modify a configuration constant within the WebSocket library files.
    //
    // 1. Locate your WebSocket library files:
    //    Typically found in your Arduino libraries folder, under a name like "WebSockets" or "WebSocketsClient".
    //    For this project, it's likely under `lib/WebSockets/src/`.
    //
    // 2. Search for buffer size constants in files like `WebSocketsClient.h`, `WebSockets.h`,
    //    or a specific `WebSocketsOptions.h` or `WebSocketsConfig.h` if it exists.
    //
    // 3. Look for constants such as:
    //    - `WEBSOCKETS_CLIENT_RX_BUFFER_SIZE` (if available, this is the most direct)
    //    - `WEBSOCKETS_TCP_BUFFER_SIZE`
    //    - `TCP_WND` (TCP Window size, sometimes influences this for some libraries)
    //    - `WEBSOCKETS_MAX_FRAME_SIZE` (less likely for RX of small messages, but related)
    //    - Any other obvious buffer size or "max packet" related constant.
    //
    // 4. Increase the value of this constant.
    //    - Default might be small (e.g., 64, 128, or related to TCP MSS ~536).
    //    - Try increasing it to at least 256, or preferably 512 or 1024, to accommodate
    //      JSON commands comfortably. For example:
    //      `#define WEBSOCKETS_CLIENT_RX_BUFFER_SIZE 512`
    //
    // 5. Recompile and upload the firmware.
    //
    // If a specific method like `webSocket.setRxBufferSize(size)` were available, it would be called here.
    // However, this is not standard for the commonly used ESP32 WebSocketsClient library by Markus Sattler.
    // Serial.println("[INFO] Check WebSocket library for RX buffer size if 'addTimer' fails due to truncated payload.");
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
        Serial.println("[sendSystemConfig] WebSocket not connected. Cannot send config.");
        return;
    }

    StaticJsonDocument<768> doc; // Existing size, should be okay for one more field

    // Add deviceId to the message at the top level
    doc["type"] = "systemConfig";
    doc["deviceId"] = deviceId; // <--- ENSURE THIS LINE IS PRESENT AND CORRECT

    // Nest the actual configuration data under a 'config' key
    JsonObject config_obj = doc.createNestedObject("config");
    config_obj["min_x"] = minX; // Use snake_case
    config_obj["max_x"] = maxX; // Use snake_case
    config_obj["min_y"] = minY; // Use snake_case
    config_obj["max_y"] = maxY; // Use snake_case

    config_obj["min_vel"] = minVel;
    config_obj["max_vel"] = maxVel;
    // config_obj["timezone"] = timeZoneOffset; // Old integer offset - REMOVED
    config_obj["timezone_posix"] = timeZonePosixString; // Send POSIX string
    config_obj["ntp_interval"] = ntpUpdateInterval; // Already a long (ms)
    config_obj["firmware_version"] = FIRMWARE_VERSION; // Add firmware version
    // config_obj["cam_led_active"] = camLedActive;

    JsonArray timersArray = config_obj.createNestedArray("timers");
    for (int i = 0; i < numTimeSlots; i++) {
        if (timeSlots[i].startTimeMinutes != -1 && timeSlots[i].stopTimeMinutes != -1) {
            JsonObject timer = timersArray.createNestedObject();
            timer["startTimeMinutes"] = timeSlots[i].startTimeMinutes;
            timer["stopTimeMinutes"] = timeSlots[i].stopTimeMinutes;
        }
    }

    String output;
    serializeJson(doc, output);

    // Existing debug logs
    Serial.println("[DEBUG] Attempting to send systemConfig via WebSocket.");
    Serial.println("JSON to send: " + output);

    bool sent = webSocket.sendTXT(output);
    if (sent) {
        Serial.println("[DEBUG] systemConfig message sent successfully to WebSocket server.");
    } else {
        Serial.println("[ERROR] Failed to send systemConfig message to WebSocket server!");
    }
}

void saveTimersToPreferences() {
  preferences.begin("servo_config", false); // false for read/write

  // Store the current numTimeSlots that we are about to save.
  // This is the count of *active* timers.
  int activeTimeSlotsCount = numTimeSlots;

  preferences.putInt("num_timers", activeTimeSlotsCount);
  Serial.printf("[DEBUG] saveTimers: Saving num_timers (active count): %d\n", activeTimeSlotsCount);

  // Save only the active timers based on the current state of the timeSlots array
  for (int i = 0; i < activeTimeSlotsCount; i++) {
    String startKey = "timer_" + String(i) + "_start";
    String stopKey = "timer_" + String(i) + "_stop";

    // Ensure we are saving valid data from the timeSlots array for the active slots
    if (timeSlots[i].startTimeMinutes != -1 && timeSlots[i].stopTimeMinutes != -1 && timeSlots[i].startTimeMinutes != timeSlots[i].stopTimeMinutes) {
        preferences.putInt(startKey.c_str(), timeSlots[i].startTimeMinutes);
        preferences.putInt(stopKey.c_str(), timeSlots[i].stopTimeMinutes);
        Serial.printf("[DEBUG] saveTimers: Saved Timer %d to Prefs: Start=%d, Stop=%d\n", i, timeSlots[i].startTimeMinutes, timeSlots[i].stopTimeMinutes);
    } else {
        // This case means an invalid timer exists within the active range.
        // This should ideally be prevented by addTimeSlot and loading logic.
        // If found, remove its keys to clean up.
        Serial.printf("[DEBUG] saveTimers: Timer %d in active range (0 to %d-1) was invalid (Start:%d, Stop:%d). Removing its keys from Prefs.\n", i, activeTimeSlotsCount, timeSlots[i].startTimeMinutes, timeSlots[i].stopTimeMinutes);
        if (preferences.isKey(startKey.c_str())) {
            preferences.remove(startKey.c_str());
        }
        if (preferences.isKey(stopKey.c_str())) {
            preferences.remove(stopKey.c_str());
        }
    }
  }

  // Explicitly remove keys for any timer slots beyond the new activeTimeSlotsCount, up to MAX_TIMERS.
  // This cleans up stale data in NVS if numTimeSlots has decreased (e.g., after a deletion).
  for (int i = activeTimeSlotsCount; i < MAX_TIMERS; i++) {
    String startKey = "timer_" + String(i) + "_start";
    String stopKey = "timer_" + String(i) + "_stop";

    if (preferences.isKey(startKey.c_str())) {
        preferences.remove(startKey.c_str());
        Serial.printf("[DEBUG] saveTimers: Removed stale key %s from Prefs for slot %d.\n", startKey.c_str(), i);
    }
    if (preferences.isKey(stopKey.c_str())) {
        preferences.remove(stopKey.c_str());
        Serial.printf("[DEBUG] saveTimers: Removed stale key %s from Prefs for slot %d.\n", stopKey.c_str(), i);
    }
  }

  preferences.end(); // This should commit all changes (puts and removes)
  Serial.println("[DEBUG] Timers saved to Preferences (with cleanup of stale slots).");
}

void addTimeSlot(String startTimeStr, String stopTimeStr) {
  Serial.printf("[DEBUG] addTimeSlot called with startTimeStr: %s, stopTimeStr: %s\n", startTimeStr.c_str(), stopTimeStr.c_str());
  if (numTimeSlots < MAX_TIMERS) {
    int startTimeMinutes = timeToMinutes(startTimeStr);
    int stopTimeMinutes = timeToMinutes(stopTimeStr);
    Serial.printf("[DEBUG] Converted to startTimeMinutes: %d, stopTimeMinutes: %d\n", startTimeMinutes, stopTimeMinutes);

    // Enhanced validation for converted minutes, including zero-duration check
    if (startTimeMinutes == -1 || stopTimeMinutes == -1 || startTimeMinutes == stopTimeMinutes) {
        Serial.printf("[DEBUG] Invalid input or zero-duration timer provided to addTimeSlot (StartMins: %d, StopMins: %d). Timer not added.\n", startTimeMinutes, stopTimeMinutes);
        return; // Exit if invalid or zero-duration
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

  // char *current_tz_env = getenv("TZ"); // Diagnostic logging removed
  // Serial.printf("[updateDisplay] Current getenv(\"TZ\"): %s\n", current_tz_env ? current_tz_env : "NULL"); // Diagnostic logging removed

  if (WiFi.status() == WL_CONNECTED) {
    display.print("IP: ");
    display.println(WiFi.localIP());
    display.print("Time: ");

    time_t now;
    time(&now); // Get current epoch time
    // Serial.printf("updateDisplay: Raw epoch from time(): %lu\n", (unsigned long)now); // Logging removed

    if (now < 1609459200L) { // Check if time is past Jan 1, 2021 UTC (example threshold)
        display.println("Time not set");
        // Serial.printf("[updateDisplay] Time not set. Raw time_t: %lu\n", (unsigned long)now); // Diagnostic logging removed
    } else {
        // Serial.printf("[updateDisplay] Raw time_t 'now': %lu\n", (unsigned long)now); // Diagnostic logging removed
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        // Serial.printf("[updateDisplay] timeinfo after localtime_r: Y=%d, M=%d, D=%d, H=%d, M=%d, S=%d, DST=%d\n", // Diagnostic logging removed
        //               timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, // Diagnostic logging removed
        //               timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, // Diagnostic logging removed
        //               timeinfo.tm_isdst); // Diagnostic logging removed

        char buffer[12]; // Buffer for HH:MM:SS + null
        strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
        display.println(buffer);
    }

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

      // Set a new random interval for the NEXT move using configured minVel and maxVel
      movementInterval = random(minVel, maxVel + 1);

    }
}

/**
 * @brief FreeRTOS task to run the HTTP OTA update process in the background.
 * @param pvParameters Task parameters (not used).
 */
void httpUpdateTask(void *pvParameters) {
    performHttpUpdate();
    vTaskDelete(NULL); // Delete the task when update process is complete or fails
}

/**
 * @brief Performs an HTTP-based OTA update.
 * Checks a version file on the server, and if the server version is newer
 * than the current FIRMWARE_VERSION, it downloads and applies the new binary.
 */
void performHttpUpdate() {
    Serial.println("Starting HTTP OTA Update check...");

    WiFiClientSecure client;
    client.setCACert(root_ca_cert);

    HTTPClient http;
    http.begin(client, firmware_version_url);

    int httpCode = http.GET();
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            int serverVersion = payload.toInt();
            Serial.printf("Server firmware version: %d\n", serverVersion);
            Serial.printf("Current firmware version: %d\n", FIRMWARE_VERSION);

            if (serverVersion > FIRMWARE_VERSION) {
                Serial.println("New firmware available. Starting update...");
                http.end(); // End the version check connection

                // httpUpdate.setLedPin(LED_BUILTIN, HIGH); // Optional: Use a status LED
                t_httpUpdate_return ret = httpUpdate.update(client, firmware_binary_url, String(FIRMWARE_VERSION));

                switch (ret) {
                    case HTTP_UPDATE_FAILED:
                        Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
                        // TODO: Send WebSocket message on failure
                        break;
                    case HTTP_UPDATE_NO_UPDATES:
                        Serial.println("HTTP_UPDATE_NO_UPDATES");
                        break;
                    case HTTP_UPDATE_OK:
                        Serial.println("HTTP_UPDATE_OK"); // This will not be seen as the device reboots
                        break;
                }
            } else {
                Serial.println("Firmware is up to date.");
                // TODO: Send WebSocket message indicating up-to-date
            }
        } else {
            Serial.printf("Version check failed, server returned http code: %d\n", httpCode);
        }
    } else {
        Serial.printf("Version check failed, http.GET() error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
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

/**
 * @brief Records display activity, resetting the inactivity timer and waking the display if it was off.
 * Also calls updateDisplay() to refresh the screen content immediately.
 */
void recordDisplayActivity() {
    lastDisplayActivityTime = millis();
    if (isDisplayOffByInactivity) {
        display.ssd1306_command(SSD1306_DISPLAYON);
        isDisplayOffByInactivity = false;
        Serial.println("Display turned ON due to activity.");
        updateDisplay(); // Refresh display immediately
    }
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
      // recordDisplayActivity(); // Record activity on initial touch detection
    } else {
      unsigned long touchDuration1 = millis() - touchStartTime1;

      if (touchDuration1 >= 10000 && !longTouchProcessed1) { // Ensure restart only happens once
        recordDisplayActivity();
        Serial.println("Touch 1 held for 10 seconds - Restarting ESP32...");
        ESP.restart();
        // longTouchProcessed1 = true; // Not strictly needed before restart but good practice
      }

      if (touchDuration1 >= 500 && !longTouchProcessed1 && touchDuration1 < 10000) {
        recordDisplayActivity();
        // Sleep
        Serial.println("Long Touch 1 Detected - Turning OFF laser and relay");
        Serial2.println("STOP_STREAM");
        display.clearDisplay();
        display.setCursor(0, 0);
        display.print("Powering Down...");
        display.display();
        delay(2000);
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        isDisplayOffByInactivity = false; // Reset this flag as it's a manual off, not inactivity off
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
        recordDisplayActivity();
        // Wake
        Serial.println("Short Touch 1 Detected - Turning ON laser and relay");
        // display.ssd1306_command(SSD1306_DISPLAYON); // recordDisplayActivity will handle this
        // display.clearDisplay(); // updateDisplay in recordDisplayActivity will handle clear
        display.setCursor(0, 0); // Keep this if specific cursor needed before updateDisplay
        display.print("Waking..."); // Keep this immediate feedback
        display.display(); // Keep this immediate feedback
        // delay(2000); // May not be needed if updateDisplay is quick
        // updateDisplay(); // recordDisplayActivity will handle this
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
      // recordDisplayActivity(); // Record activity on initial touch
    } else {
      unsigned long touchDuration2 = millis() - touchStartTime2;

      if (touchDuration2 >= 500 && !longTouchProcessed2) {
        recordDisplayActivity();
        // Toggle Settings Mode
        Serial.println("Long Touch 2 Detected");
        longTouchProcessed2 = true;

        settingsMode = !settingsMode;  // Toggle settings mode *FIRST*

        // displaySettings(); // recordDisplayActivity calls updateDisplay, which handles non-settings mode.
                           // For settings mode, we need to ensure displaySettings is called.
        if (settingsMode) {
            displaySettings(); // Explicitly call for settings mode
        } else {
            // updateDisplay() will be called by recordDisplayActivity if display was off,
            // or by the main loop if it was already on.
        }
        // delay(50); // May not be needed

        if (settingsMode) {
          Serial.println("Entering settings mode.");
        } else {
          Serial.println("Exiting settings mode.");
        }
      }

      if (touchDuration2 < 500 && !shortTouchProcessed2) {
        recordDisplayActivity();
        shortTouchProcessed2 = true;
        if (!settingsMode) {
          // Toggle Random Motion
          randomMotionActive = !randomMotionActive;
          if (randomMotionActive) {
            Serial.println("Random motion is now active.");
          } else {
            Serial.println("Random motion is now inactive.");
          }
          // updateDisplay() will be called by recordDisplayActivity or main loop
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
          // displaySettings() is called within adjustSettings.
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

  // Use local time for schedule checking
  time_t now_epoch;
  time(&now_epoch); // Get current epoch time

  if (now_epoch < 1609459200L) { // Check if time is plausible (e.g., past Jan 1, 2021 UTC)
    // Serial.println("[LOOP] System time not yet synchronized or valid for schedule check.");
  } else {
    struct tm timeinfo_local;
    localtime_r(&now_epoch, &timeinfo_local); // Convert to local time structure

    char localTimeStr[6]; // HH:MM + null terminator
    strftime(localTimeStr, sizeof(localTimeStr), "%H:%M", &timeinfo_local);
    String currentTimeForLogic = String(localTimeStr);

    // Serial.printf("[DEBUG] Local time for schedule logic: %s\n", currentTimeForLogic.c_str());

    int currentMinutes = timeToMinutes(currentTimeForLogic);

    if (currentMinutes == -1) { // timeToMinutes might return -1 if format is wrong (should not happen with strftime)
        Serial.println("Cannot check schedule, current local time conversion failed.");
    } else {
        for (int i = 0; i < numTimeSlots; i++) {
            // Explicitly skip if timer slot data is invalid
            if (timeSlots[i].startTimeMinutes == -1 || timeSlots[i].stopTimeMinutes == -1) {
                continue;
            }

            // Check if current local time falls within this time slot
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

  bool previousScheduledMovementActive = isScheduledMovementActive; // Store previous state
  isScheduledMovementActive = shouldMoveRandomlyThisCycle; // Update the global state

  if (isScheduledMovementActive) {
    if (scheduledStartTime != "N/A" && scheduledStopTime != "N/A") {
        currentScheduleStartTime = scheduledStartTime;
        currentScheduleStopTime = scheduledStopTime;
        turnLaserOn();
        if (!previousScheduledMovementActive) { // If it just became active
            Serial.println("Scheduled movement started. Recording display activity.");
            recordDisplayActivity();
        }
    } else {
        isScheduledMovementActive = false; // Correct the state if times are N/A
        currentScheduleStartTime = "";
        currentScheduleStopTime = "";
        if (previousScheduledMovementActive) { // If it just became inactive due to N/A times
             Serial.println("Scheduled movement ended (invalid times). Recording display activity.");
            recordDisplayActivity();
        }
    }
  } else { // Not active in this cycle
    currentScheduleStartTime = "";
    currentScheduleStopTime = "";
    if (previousScheduledMovementActive) { // If it just became inactive
        Serial.println("Scheduled movement ended. Recording display activity.");
        recordDisplayActivity();
    }
  }

  // Call random movement if the schedule says it should AND it's not overridden, OR if the button is toggled ON
  if ((isScheduledMovementActive && !scheduledMovementOverridden) || randomMotionActive) {
    moveServosRandomlyNonBlocking();
  }

  // New laser control logic:
  // The laser should be ON if:
  //   a) A scheduled movement is active (and not overridden) OR
  //   b) Random (manual) motion is active OR
  //   c) The `laserActive` flag (set by direct command like from UI configuration) is true.
  // Otherwise, it should be OFF.
  bool autoActivityDemandsLaser = (isScheduledMovementActive && !scheduledMovementOverridden) || randomMotionActive;

  if (autoActivityDemandsLaser) {
      digitalWrite(outputPin, HIGH); // Automated activity demands laser to be ON
  } else {
      // No automated activity demanding laser. State depends on the manual/timed `laserActive` flag.
      if (laserActive) {
          digitalWrite(outputPin, HIGH); // Laser was turned ON by command and should remain ON.
      } else {
          digitalWrite(outputPin, LOW);  // Laser is not demanded by activity and is commanded OFF.
      }
  }

  updateServoMovement(myservoX, movementX); // Update X servo movement
  updateServoMovement(myservoY, movementY); // Update Y servo movement
// The following block seems to be a repetition of the logic above for `isScheduledMovementActive` and `currentScheduleStart/StopTime`
// It should be removed to avoid redundancy and potential conflicts.
// The state of isScheduledMovementActive, currentScheduleStartTime, and currentScheduleStopTime
// is already correctly determined by the preceding block.

  // (Repetitive block removed)

  if (settingsMode) {
    // If in settings mode, ensure display activity is recorded so it doesn't turn off,
    // and displaySettings itself handles screen updates.
    // If settingsMode can be entered/exited by touch, readTouch() should call recordDisplayActivity().
    // If settingsMode is toggled by other means (e.g. WebSocket command), that path should also call recordDisplayActivity().
    // For now, assuming settingsMode implies active interaction.
    if(isDisplayOffByInactivity) { // If it was off, turn it on to show settings
        recordDisplayActivity(); // This will turn on and call updateDisplay - but we want displaySettings
        displaySettings(); // So call displaySettings again if it was just woken up for settings.
    } else {
        displaySettings();
    }
  } else {
    if (!isDisplayOffByInactivity) { // Only update display if it's supposed to be on
        updateDisplay();
    }
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

  // OLED Inactivity Check
  if (!isDisplayOffByInactivity && WiFi.status() == WL_CONNECTED && (millis() - lastDisplayActivityTime > DISPLAY_INACTIVITY_TIMEOUT)) {
    if (!randomMotionActive && !isScheduledMovementActive && !streaming) { // Only turn off if no critical activity is ongoing
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        isDisplayOffByInactivity = true;
        Serial.println("Display turned OFF due to inactivity.");
    } else {
        // If critical activity is ongoing, just reset the activity timer as if there was interaction
        // This ensures the display stays on during these activities without needing explicit recordDisplayActivity() calls every second.
        lastDisplayActivityTime = millis();
        // Serial.println("Display inactivity timeout reached, but critical activity ongoing. Resetting timer."); // Optional debug
    }
  }
}