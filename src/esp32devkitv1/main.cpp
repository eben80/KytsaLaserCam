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


Preferences preferences;

// NTP Settings
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
unsigned long lastNTPUpdateTime = 0;
long ntpUpdateInterval = 60 * 60 * 1000; // Update every hour
int timeZoneOffset = 2; // Default to CEST (UTC+2) - Changed to int
bool randomMotionActive = false; // Toggled by the web button
bool scheduledMovementOverridden = false; // To temporarily stop scheduled movement
bool isScheduledMovementActive = false; // Tracks if the schedule is currently active
bool inConfiguration = false; // Flag to indicate if in configuration mode
String currentScheduleStartTime = "";
String currentScheduleStopTime = "";

// Define the serial port to use (adjust if needed)
HardwareSerial& serialPort = Serial2; // Use Serial2 (RX2, TX2)

bool wifiConnectedAP = false;
String staSSID;
String staPassword;
String esp32CamIP = "";
bool esp32CamConnected = false;
// Add this global variable to track streaming state
bool streaming = false;


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

WiFiServer server(80);

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
struct ServoMovement {
  int startPos;
  int targetPos;
  unsigned long startTime;
  unsigned long duration;
  bool isMoving;
};

ServoMovement movementX = {0, 0, 0, 0, false}; // Servo X movement tracking
ServoMovement movementY = {0, 0, 0, 0, false}; // Servo Y movement tracking


void RTC_IRAM_ATTR esp_wake_deep_sleep() {
  esp_default_wake_deep_sleep();
  laserActive = true;
  relayActive = true;
  digitalWrite(outputPin, HIGH);
  digitalWrite(relayPin, HIGH);
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
  int hours = formattedTime.substring(0, 2).toInt();
  int minutes = formattedTime.substring(3, 5).toInt();
  return hours * 60 + minutes;
}

String minutesToTime(int totalMinutes) {
  int hours = (totalMinutes / 60) % 24;
  int minutes = totalMinutes % 60;
  return String(hours < 10 ? "0" : "") + String(hours) + ":" + String(minutes < 10 ? "0" : "") + String(minutes);
}

void turnLaserOn() {
  digitalWrite(outputPin, HIGH);
}

void turnLaserOff() {
  digitalWrite(outputPin, LOW);
}

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

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
  Serial.begin(115200);
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

  // Load the number of timers
  numTimeSlots = preferences.getInt("num_timers", 0);
  numTimeSlots = constrain(numTimeSlots, 0, MAX_TIMERS); // Ensure it's within bounds

  Serial.print("Loaded number of timers: ");
  Serial.println(numTimeSlots);

  // Load each timer
  for (int i = 0; i < numTimeSlots; i++) {
    String baseKey = "timer_" + String(i);
    timeSlots[i].startTimeMinutes = preferences.getInt((baseKey + "_start").c_str(), -1);
    timeSlots[i].stopTimeMinutes = preferences.getInt((baseKey + "_stop").c_str(), -1);
    timeSlots[i].active = false; // Initialize as not active
    Serial.printf("Loaded Timer %d: Start=%d, Stop=%d\n", i, timeSlots[i].startTimeMinutes, timeSlots[i].stopTimeMinutes);
  }
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

  server.begin();
}



void saveTimersToPreferences() {
  preferences.begin("servo_config"); // Begin the session here

  preferences.putInt("num_timers", numTimeSlots);
  Serial.printf("Saved num_timers: %d\n", numTimeSlots);
  Serial.printf("Read back num_timers: %d\n", preferences.getInt("num_timers", -99));

  for (int i = 0; i < numTimeSlots; i++) {
    String startKey = "timer_" + String(i) + "_start";
    String stopKey = "timer_" + String(i) + "_stop";

    preferences.putInt(startKey.c_str(), timeSlots[i].startTimeMinutes);
    Serial.printf("Saved %s: %d\n", startKey.c_str(), timeSlots[i].startTimeMinutes);
    Serial.printf("Read back %s: %d\n", startKey.c_str(), preferences.getInt(startKey.c_str(), -99));

    preferences.putInt(stopKey.c_str(), timeSlots[i].stopTimeMinutes);
    Serial.printf("Saved %s: %d\n", stopKey.c_str(), timeSlots[i].stopTimeMinutes);
    Serial.printf("Read back %s: %d\n", stopKey.c_str(), preferences.getInt(stopKey.c_str(), -99));
  }

  preferences.end(); // End the session here
  Serial.println("Timers saved to Preferences and verification read performed.");
}

void addTimeSlot(String startTimeStr, String stopTimeStr) {
  if (numTimeSlots < MAX_TIMERS) {
    int startTimeMinutes = timeToMinutes(startTimeStr);
    int stopTimeMinutes = timeToMinutes(stopTimeStr);
    timeSlots[numTimeSlots].startTimeMinutes = startTimeMinutes;
    timeSlots[numTimeSlots].stopTimeMinutes = stopTimeMinutes;
    numTimeSlots++; // Increment numTimeSlots FIRST
    saveTimersToPreferences(); // Now save with the updated count
  } else {
    Serial.println("Maximum number of timers reached.");
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
void startServoMovement(Servo& servo, ServoMovement &movement, int targetPos, int moveTime) {
  movement.startPos = servo.read();
  movement.targetPos = targetPos;
  movement.startTime = millis();
  movement.duration = moveTime;
  movement.isMoving = true;
}

// Function to update servo position smoothly (non-blocking)
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

void handleClientRequest() {
  WiFiClient client = server.available();
  if (client) {
    unsigned long currentTime = millis();
    unsigned long previousTime = currentTime;
    String currentLine = "";
    String header = "";

    Serial.println("New Client.");
    while (client.connected() && currentTime - previousTime <= timeoutTime) {
      currentTime = millis();
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
        header += c;

        if (c == '\n') {
          if (currentLine.length() == 0) {
            // HTTP Response Header
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // HTML Content - Start
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<style>body { text-align: center; font-family: \"Trebuchet MS\", Arial; } .slider { width: 300px; }</style>");
            client.println("<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/3.3.1/jquery.min.js\"></script>");
            client.println("</head><body><h1>Kytsa Laser</h1>");

            // Random Motion Controls (Visible)
            client.println("<h2>Random Motion</h2>");
            client.println("<button onclick=\"toggleRandom()\" id=\"randomButton\">Toggle Random Movement</button>");

            // Configuration Section - Collapsed by Default
            client.println("<details>");
            client.println("<summary>Configuration</summary>");

            // Servo Control for X and Y
            client.println("<h3>Servo X Control</h3>");
            client.println("<p>Position X: <span id=\"servoPosX\"></span> (Min: <span id=\"minX\">" + String(minX) + "</span> Max: <span id=\"maxX\">" + String(maxX) + "</span>)</p>");
            client.println("<input type=\"range\" min=\"0\" max=\"180\" class=\"slider\" id=\"servoSliderX\" onchange=\"servoX(this.value)\" value=\"" + valueStringX + "\"/>");
            client.println("<button onclick=\"setMinX()\">Set Min X</button> <button onclick=\"setMaxX()\">Set Max X</button>");
            client.println("<button onclick=\"testX()\">Test Servo X</button>");

            client.println("<h3>Servo Y Control</h3>");
            client.println("<p>Position Y: <span id=\"servoPosY\"></span> (Min: <span id=\"minY\">" + String(minY) + "</span> Max: <span id=\"maxY\">" + String(maxY) + "</span>)</p>");
            client.println("<input type=\"range\" min=\"45\" max=\"135\" class=\"slider\" id=\"servoSliderY\" onchange=\"servoY(this.value)\" value=\"" + valueStringY + "\"/>");
            client.println("<button onclick=\"setMinY()\">Set Min Y</button> <button onclick=\"setMaxY()\">Set Max Y</button>");
            client.println("<button onclick=\"testY()\">Test Servo Y</button>");

            // Velocity Controls (minVel and maxVel)
            client.println("<h3>Velocity Controls</h3>");
            client.println("<p>Min Velocity: <input type=\"number\" id=\"minVel\" value=\"" + String(minVel) + "\" /></p>");
            client.println("<p>Max Velocity: <input type=\"number\" id=\"maxVel\" value=\"" + String(maxVel) + "\" /></p>");
            client.println("<button onclick=\"saveVelocities()\">Save Velocities</button>");

            // Timezone Setting
            client.println("<h3>Timezone Setting</h3>");
            client.println("<p>Timezone Offset (hours from UTC): <input type=\"number\" id=\"timezoneOffset\" value=\"" + String(timeZoneOffset) + "\" /></p>");
            client.println("<p>NTP Update Interval (milliseconds): <input type=\"number\" id=\"ntpInterval\" value=\"" + String(ntpUpdateInterval) + "\" /></p>");
            client.println("<button onclick=\"saveTimeSettings()\">Save Time Settings</button>");

            client.println("</details>"); // End of Configuration Section

            // Random Motion Scheduling (Visible)
            client.println("<h2>Random Motion Schedule</h2>");
            client.println("<div id=\"schedule-container\">");
            // Display existing timers
            for (int i = 0; i < numTimeSlots; i++) {
              if (timeSlots[i].startTimeMinutes != -1 && timeSlots[i].stopTimeMinutes != -1) {
                client.printf("<p id=\"timer-%d\">Start: %s, Stop: %s <button onclick=\"deleteTimer(%d)\">Delete</button></p>",
                              i, minutesToTime(timeSlots[i].startTimeMinutes).c_str(), minutesToTime(timeSlots[i].stopTimeMinutes).c_str(), i);
              }
            }
            client.println("</div>");

            // Add new timer (Visible)
            client.println("<h3>Add New Timer</h3>");
            client.println("<p>Start Time: <input type=\"time\" id=\"newStartTime\"></p>");
            client.println("<p>Stop Time: <input type=\"time\" id=\"newStopTime\"></p>");
            client.println("<button onclick=\"addTimerAction()\">Add Timer</button>");

            // Live Stream (Visible)
            client.println("<h2>Live Stream</h2>");
            if (esp32CamConnected) {
              client.println("<button onclick=\"startStream()\">Start Stream</button>");
              client.println("<button onclick=\"stopStream()\">Stop Stream</button>");
              client.println("<button onclick=\"ledOn()\">Turn LED On</button>");
              client.println("<button onclick=\"ledOff()\">Turn LED Off</button>");
              client.println("<div id=\"stream-container\" style=\"transform: rotate(180deg);\">");
              if (streaming) {
                client.println("<img id=\"stream\" src=\"http://" + esp32CamIP + "/?t=" + millis() + "\" width=\"800\" height=\"600\">");
              } else {
                client.println("<p>Stream is currently stopped.</p>");
              }
              client.println("</div>");
            } else {
              client.println("<p>ESP32-CAM not connected or IP not received.</p>");
            }

            // JavaScript Handlers
            client.println("<script>var sliderX = document.getElementById(\"servoSliderX\");");
            client.println("var servoPX = document.getElementById(\"servoPosX\"); servoPX.innerHTML = sliderX.value;");
            client.println("sliderX.oninput = function() { servoPX.innerHTML = this.value; }");
            client.println("function servoX(pos) { $.get('/?valueX=' + pos); }");
            client.println("function setMinX() { $.get('/?setMinX=' + sliderX.value, function() { document.getElementById(\"minX\").innerText = sliderX.value; }); }");
            client.println("function setMaxX() { $.get('/?setMaxX=' + sliderX.value, function() { document.getElementById(\"maxX\").innerText = sliderX.value; }); }");
            client.println("function testX() { $.get('/?testX=1'); }");

            client.println("var sliderY = document.getElementById(\"servoSliderY\");");
            client.println("var servoPY = document.getElementById(\"servoPosY\"); servoPY.innerHTML = sliderY.value;");
            client.println("sliderY.oninput = function() { servoPY.innerHTML = this.value; }");
            client.println("function servoY(pos) { $.get('/?valueY=' + pos); }");
            client.println("function setMinY() { $.get('/?setMinY=' + sliderY.value, function() { document.getElementById(\"minY\").innerText = sliderY.value; }); }");
            client.println("function setMaxY() { $.get('/?setMaxY=' + sliderY.value, function() { document.getElementById(\"maxY\").innerText = sliderY.value; }); }");
            client.println("function testY() { $.get('/?testY=1'); }");

            // Save velocities
            client.println("function saveVelocities() {");
            client.println("  var minVel = document.getElementById('minVel').value;");
            client.println("  var maxVel = document.getElementById('maxVel').value;");
            client.println("  $.get('/?saveVelocities=' + minVel + '&maxVel=' + maxVel);");
            client.println("}");
            // Save Time Settings
            client.println("function saveTimeSettings() {");
            client.println("  var timezoneOffset = document.getElementById('timezoneOffset').value;");
            client.println("  var ntpInterval = document.getElementById('ntpInterval').value;");
            client.println("  $.get('/?saveTimeSettings=' + timezoneOffset + '&ntpInterval=' + ntpInterval);");
            client.println("}");

            client.println("var startStreamButtonEnabled = true;");
            client.println("function startStream() {");
            client.println("  if (startStreamButtonEnabled) {");
            client.println("    startStreamButtonEnabled = false;");
            client.println("    $('#startStreamButton').prop('disabled', true);");
            client.println("    $.get('/?startStream=1', function(data, status){");
            client.println("      if(status == 'success'){");
            client.println("        $('#stream-container').html('<img id=\"stream\" src=\"http://' + '" + esp32CamIP + "' + '/?t=' + new Date().getTime() + '\" width=\"800\" height=\"600\">');");
            client.println("        setTimeout(function() {");
            client.println("          startStreamButtonEnabled = true;");
            client.println("          $('#startStreamButton').prop('disabled', false);");
            client.println("        }, 5000);");
            client.println("      }");
            client.println("    });");
            client.println("  }");
            client.println("}");
            client.println("function stopStream() {");
            client.println("  $.get('/?stopStream=1', function(data, status){");
            client.println("    if(status == 'success'){");
            client.println("      $('#stream-container').empty();");
            client.println("      startStreamButtonEnabled = false;");
            client.println("      $('#startStreamButton').prop('disabled', true);");
            client.println("      setTimeout(function() {");
            client.println("        startStreamButtonEnabled = true;");
            client.println("        $('#startStreamButton').prop('disabled', false);");
            client.println("      }, 5000);");
            client.println("    }");
            client.println("  });");
            client.println("}");
            client.println("function ledOn() { $.get('/?ledOn=1'); }");
            client.println("function ledOff() { $.get('/?ledOff=1'); }");
            client.println("function toggleRandom() {");
            client.println("  $.get('/?toggleRandom=1');");
            client.println("}");
            client.println("function addTimerAction() {");
            client.println("  var startTime = $('#newStartTime').val();");
            client.println("  var stopTime = $('#newStopTime').val();");
            client.println("  if (startTime && stopTime) {");
            client.println("    $.get('/?addTimer=1&startTime=' + startTime + '&stopTime=' + stopTime, function(data, status) {");
            client.println("      if (status == 'success') {");
            client.println("        window.location.reload(); // Refresh the page");
            client.println("      }");
            client.println("    });");
            client.println("  }");
            client.println("}");

            client.println("function deleteTimer(index) {");
            client.println("  $.get('/?deleteTimer=1&index=' + index, function(data, status) {");
            client.println("    if (status == 'success') {");
            client.println("      window.location.reload(); // Refresh the page");
            client.println("    }");
            client.println("  });");
            client.println("}");
            client.println("</script></body></html>");
          }

          // Handle GET Requests
          if (header.indexOf("GET /?toggleRandom=1") >= 0) {
            inConfiguration = false;
            randomMotionActive = !randomMotionActive; // Toggle the manual state
            if (isScheduledMovementActive && randomMotionActive == false) {
              scheduledMovementOverridden = true; // Override if schedule is active and button is pressed to stop
              Serial.println("Scheduled movement overridden by button.");
            } else if (scheduledMovementOverridden && randomMotionActive == true) {
              scheduledMovementOverridden = false; // Re-enable scheduled movement if button is pressed again
              Serial.println("Scheduled movement re-enabled by button.");
            }
            Serial.print("Random Motion Active (Manual): ");
            Serial.println(randomMotionActive);
            Serial.print("Scheduled Movement Overridden: ");
            Serial.println(scheduledMovementOverridden);
          } else if (header.indexOf("GET /?valueX=") >= 0) {
            inConfiguration = true;
            turnLaserOn();
            int pos = header.indexOf('=') + 1;
            valueStringX = header.substring(pos);
            myservoX.write(valueStringX.toInt());
          } else if (header.indexOf("GET /?valueY=") >= 0) {
            inConfiguration = true;
            turnLaserOn();
            int pos = header.indexOf('=') + 1;
            valueStringY = header.substring(pos);
            myservoY.write(valueStringY.toInt());
          } else if (header.indexOf("GET /?setMinX=") >= 0) {
            int pos = header.indexOf('=') + 1;
            preferences.putInt("min_x", header.substring(pos).toInt());
            minX = preferences.getInt("min_x", 0); // Update current value
          } else if (header.indexOf("GET /?setMaxX=") >= 0) {
            int pos = header.indexOf('=') + 1;
            preferences.putInt("max_x", header.substring(pos).toInt());
            maxX = preferences.getInt("max_x", 180); // Update current value
          } else if (header.indexOf("GET /?setMinY=") >= 0) {
            int pos = header.indexOf('=') + 1;
            preferences.putInt("min_y", header.substring(pos).toInt());
            minY = preferences.getInt("min_y", 45); // Update current value
          } else if (header.indexOf("GET /?setMaxY=") >= 0) {
            int pos = header.indexOf('=') + 1;
            preferences.putInt("max_y", header.substring(pos).toInt());
            maxY = preferences.getInt("max_y", 135); // Update current value
          } else if (header.indexOf("GET /?testX=1") >= 0) {
            testServo(myservoX, preferences.getInt("min_x", 0), preferences.getInt("max_x", 180));
          } else if (header.indexOf("GET /?testY=1") >= 0) {
            testServo(myservoY, preferences.getInt("min_y", 45), preferences.getInt("max_y", 135));
          } else if (header.indexOf("GET /?saveVelocities=") >= 0) {
            int minPos = header.indexOf('=') + 1;
            int maxPosAmp = header.indexOf('&');
            if (minPos > 0 && maxPosAmp > minPos) {
              String minVelStr = header.substring(minPos, maxPosAmp);
              int minVelValue = minVelStr.toInt();
              int maxVelPos = header.indexOf("maxVel=") + 7;
              if (maxVelPos > 6) {
                String maxVelStr = header.substring(maxVelPos);
                int maxVelValue = maxVelStr.toInt();
                Serial.print("Saving Min Velocity (Preferences): ");
                Serial.println(minVelValue);
                Serial.print("Saving Max Velocity (Preferences): ");
                Serial.println(maxVelValue);
                preferences.putInt("min_vel", minVelValue);
                preferences.putInt("max_vel", maxVelValue);
                minVel = minVelValue; // Update current value
                maxVel = maxVelValue; // Update current value
              }
            }
          } else if (header.indexOf("GET /?saveTimeSettings=") >= 0) {
            int tzPos = header.indexOf('=') + 1;
            int intervalPosAmp = header.indexOf('&');
            if (tzPos > 0 && intervalPosAmp > tzPos) {
              String timezoneStr = header.substring(tzPos, intervalPosAmp);
              int timezoneValue = timezoneStr.toInt();
              int intervalPos = header.indexOf("ntpInterval=") + 12;
              if (intervalPos > 11) {
                String intervalStr = header.substring(intervalPos);
                long intervalValue = intervalStr.toInt();

                Serial.print("Saving Timezone (Preferences): ");
                Serial.println(timezoneValue);
                Serial.print("Saving NTP Interval (Preferences): ");
                Serial.println(intervalValue);

                preferences.putInt("timezone", timezoneValue); // save
                preferences.putLong("ntp_interval", intervalValue); //save

                timeZoneOffset = timezoneValue; //update
                ntpUpdateInterval = intervalValue; // update

                timeClient.setTimeOffset(timeZoneOffset * 3600); //update
              }
            }
          } else if (header.indexOf("GET /?startStream=1") >= 0 && esp32CamConnected && !streaming) {
            Serial2.println("START_STREAM");
            Serial.println("Sent command: START_STREAM");
            streaming = true;
          } else if (header.indexOf("GET /?stopStream=1") >= 0 && streaming) {
            Serial2.println("STOP_STREAM");
            Serial.println("Sent command: STOP_STREAM");
            streaming = false;
          } else if (header.indexOf("GET /?ledOn=1") >= 0 && esp32CamConnected) {
            Serial2.println("LED_ON");
            Serial.println("Sent command: LED_ON");
          } else if (header.indexOf("GET /?ledOff=1") >= 0 && esp32CamConnected) {
            Serial2.println("LED_OFF");
            Serial.println("Sent command: LED_OFF");
          } else if (header.indexOf("GET /?addTimer=1") >= 0) {
            int startTimePos = header.indexOf("startTime=") + 10;
            int stopTimePos = header.indexOf("&stopTime=") + 10;
            if (startTimePos > 9 && stopTimePos > 9) {
              String startTimeStr = header.substring(startTimePos, header.indexOf('&', startTimePos));
              String stopTimeStr = header.substring(stopTimePos);
              addTimeSlot(startTimeStr, stopTimeStr);
              preferences.end(); // Ensure data is written to flash
              delay(10);        // Small delay to allow write operation
            }
            // No direct HTML response here, AJAX will handle it
          } else if (header.indexOf("GET /?deleteTimer=1") >= 0) {
            int indexPos = header.indexOf("index=") + 6;
            if (indexPos > 5) {
              int indexToDelete = header.substring(indexPos).toInt();
              deleteTimeSlot(indexToDelete);
              preferences.end(); // Ensure data is written to flash
              delay(10);        // Small delay to allow write operation
            }
            // No direct HTML response here, AJAX will handle it
          }

          // End the response
          client.println();
          client.stop();
          Serial.println("Client Disconnected.");
          break;
        }
      }
    }
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

void loop() {
  ArduinoOTA.handle();
  readTouch();

  bool shouldMoveRandomlyThisCycle = false;
  String scheduledStartTime = ""; // Local variables to store the times
  String scheduledStopTime = "";

  if (timeClient.isTimeSet()) {
    String currentTime = getFormattedTime();
    int currentMinutes = timeToMinutes(currentTime);

    for (int i = 0; i < numTimeSlots; i++) {
      if (timeSlots[i].startTimeMinutes != -1 && timeSlots[i].stopTimeMinutes != -1) {
        if (timeSlots[i].startTimeMinutes < timeSlots[i].stopTimeMinutes) {
          if (currentMinutes >= timeSlots[i].startTimeMinutes && currentMinutes < timeSlots[i].stopTimeMinutes) {
            shouldMoveRandomlyThisCycle = true;
            scheduledStartTime = minutesToTime(timeSlots[i].startTimeMinutes);
            scheduledStopTime = minutesToTime(timeSlots[i].stopTimeMinutes);
            break;
          }
        } else { // Handle cases where the stop time is on the next day (e.g., 22:00 - 02:00)
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
    currentScheduleStartTime = scheduledStartTime; // Update the global start time
    currentScheduleStopTime = scheduledStopTime;   // Update the global stop time
    turnLaserOn();
  } else {
    currentScheduleStartTime = ""; // Clear the global start time when no schedule is active
    currentScheduleStopTime = "";   // Clear the global stop time when no schedule is active
  }

  // Call random movement if the schedule says it should AND it's not overridden, OR if the button is toggled ON
  if ((shouldMoveRandomlyThisCycle && !scheduledMovementOverridden) || randomMotionActive) {
    moveServosRandomlyNonBlocking(); // Call the non-blocking random movement function
    turnLaserOn();
  } else {
    if (!inConfiguration) {  turnLaserOff();}
  }

  updateServoMovement(myservoX, movementX); // Update X servo movement
  updateServoMovement(myservoY, movementY); // Update Y servo movement

  if (settingsMode) {
    displaySettings();
  } else {
    updateDisplay();
  }

  handleClientRequest();

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