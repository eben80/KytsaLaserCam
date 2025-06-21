# Kytsa Laser Control - System Architecture

This document outlines the system architecture of the Kytsa Laser Control project, detailing its components, their responsibilities, and how they interact.

## Core Components

The system is comprised of the following main components:

1.  **ESP32 DevKit V1 (Main Controller)**
2.  **ESP32-CAM (Video Streamer)**
3.  **Web Interface (Client-Side UI)**
4.  **WebSocket Server (Backend Mediator)**
5.  **NVS (Non-Volatile Storage on ESP32 DevKit)**

---

### 1. ESP32 DevKit V1 (Main Controller)

*   **Hardware:** An ESP32 DevKit V1 board (or equivalent).
*   **Firmware:** Custom C++ code located in `src/esp32devkitv1/main.cpp`, built using PlatformIO.
*   **Responsibilities:**
    *   **WiFi Connectivity:** Connects to the local WiFi network using credentials configured via WiFiManager.
    *   **WebSocket Client:** Establishes and maintains a WebSocket connection to the central WebSocket Server.
    *   **Servo Control:** Directly controls two servo motors for X (pan/left-right) and Y (tilt/up-down) axes based on commands received.
    *   **Laser & Relay Output:** Toggles a laser diode module and an auxiliary relay module.
    *   **NTP Time Synchronization:** Fetches current time from NTP servers and manages local time based on the configured timezone.
    *   **Preference Management:** Stores and retrieves settings (servo limits, velocity intervals, timezone, NTP interval, timer schedules) from Non-Volatile Storage (NVS).
    *   **Scheduled Automation ("Kytsa Workouts"):** Executes pre-configured random movement schedules based on the current time.
    *   **Serial Communication with ESP32-CAM:** Manages the ESP32-CAM by sending commands and receiving status information.
    *   **Status Reporting:** Sends periodic status updates (`statusUpdate`) and full configuration (`systemConfig`) to the WebSocket server for the web interface.
    *   **Command Processing:** Receives JSON-formatted commands from the WebSocket server (originating from the web UI) and acts upon them (e.g., move servo, set preference, add timer).

---

### 2. ESP32-CAM (Video Streamer)

*   **Hardware:** An ESP32-CAM module with an attached camera.
*   **Firmware:** Assumed to be a standard ESP32-CAM MJPEG streaming firmware (e.g., CameraWebServer example from Arduino ESP32 core, or a customized version). This firmware is typically flashed separately to the ESP32-CAM.
*   **Responsibilities:**
    *   **WiFi Connectivity:** Connects to the local WiFi network using credentials provided by the ESP32 DevKit over serial.
    *   **Video Capture:** Captures video frames from the attached camera.
    *   **MJPEG Streaming:** Serves an MJPEG (Motion JPEG) video stream over HTTP on its local IP address (typically on port 80 or 81).
    *   **Command Reception (Serial):** Listens for commands from the ESP32 DevKit via a serial connection. Supported commands include:
        *   Receiving WiFi credentials.
        *   Starting/stopping the video stream.
        *   Turning its onboard LED on/off.
    *   **Status Reporting (Serial):** Sends its IP address back to the DevKit upon successful WiFi connection.

---

### 3. Web Interface (Client-Side UI)

*   **Location:** Hosted within the `kytsa_web_interface/` directory, consisting of `index.html`, `style.css`, and `script.js`.
*   **Technology:** HTML, CSS, JavaScript (Single Page Application).
*   **Responsibilities:**
    *   **User Interaction:** Provides all controls and displays for the user.
    *   **WebSocket Client:** Establishes a WebSocket connection to the central WebSocket Server.
    *   **Device Discovery & Selection:** Lists available ESP32 DevKit devices (as reported by the server) and allows the user to select one to control.
    *   **Command Transmission:** Sends user-initiated commands (e.g., move servo, toggle laser, save settings) as JSON messages to the WebSocket server, targeted at the selected device.
    *   **Status & Configuration Display:** Receives `statusUpdate` and `systemConfig` messages from the WebSocket server (originating from the selected ESP32 DevKit) and updates the UI accordingly (e.g., populating servo limit sliders, displaying current timezone, listing timers).
    *   **Video Stream Display:** Embeds and displays the MJPEG video stream. The `<img>` tag's `src` attribute points to a URL that provides the stream (currently `https://ebski.co/stream`, which implies a proxy on the server that relays the stream from the ESP32-CAM's local IP).

---

### 4. WebSocket Server

*   **Technology:** PHP (specific implementation details are external to this repository, assumed to be hosted on `ebski.co`).
*   **Responsibilities (Conceptual):**
    *   **Mediator:** Acts as a central communication hub between potentially multiple Web Interface clients and multiple ESP32 DevKit devices.
    *   **Device Registration:** ESP32 DevKits connect and "pair" by sending their unique `deviceId`. The server maintains a list of connected devices.
    *   **Client Initialization:** When a Web Interface connects, the server sends it the current list of active ESP32 devices.
    *   **Message Routing:**
        *   Routes commands from a Web Interface client to the specified `targetDeviceId`.
        *   Broadcasts or routes status updates and system configurations from an ESP32 DevKit to interested Web Interface clients (typically those that have selected that device).
        *   Manages device connection/disconnection events and informs Web Interface clients.
    *   **Stream Proxying (Implied):** The stream URL `https://ebski.co/stream` suggests the WebSocket server (or another service on `ebski.co`) might be proxying the MJPEG stream from the ESP32-CAM's local IP address to make it accessible publicly/easily. This avoids direct exposure of local device IPs.

---

### 5. NVS (Non-Volatile Storage on ESP32 DevKit)

*   **Technology:** ESP32's built-in Non-Volatile Storage system.
*   **Usage:** The ESP32 DevKit firmware uses NVS via the `Preferences.h` library to persistently store:
    *   Servo X/Y axis minimum and maximum limits.
    *   `minVel` and `maxVel` values (used for random movement intervals).
    *   The POSIX timezone string (`tz_posix`).
    *   The NTP update interval (`ntp_interval`).
    *   Scheduled timer slots for "Kytsa Workouts" (`timer_X_start`, `timer_X_stop`, `num_timers`).
    *   These preferences are loaded during `setup()` and can be updated via commands from the web interface.

---

## Key Interactions & Data Flows

1.  **Initial Setup & Pairing:**
    *   ESP32 DevKit boots, loads preferences from NVS.
    *   If WiFi credentials are not set, enters WiFiManager AP mode ("MiauAP"). User connects and configures.
    *   DevKit connects to WiFi, then sends credentials to ESP32-CAM via Serial.
    *   ESP32-CAM connects to WiFi and reports its IP to DevKit via Serial.
    *   DevKit connects to WebSocket Server, sends `pairing` message with its `deviceId`.
    *   Web Interface connects to WebSocket Server, sends `webClientInit`, receives list of active devices.

2.  **User Selects Device in Web UI:**
    *   Web UI sends `getSystemConfig` command to the selected ESP32 DevKit (via WebSocket Server).
    *   DevKit responds with its full `systemConfig` (servo limits, timers, timezone, etc.).
    *   Web UI populates all configuration controls with these values.

3.  **User Sends a Command (e.g., Move Servo, Set Preference):**
    *   Web UI -> WebSocket Server -> ESP32 DevKit (JSON command message).
    *   ESP32 DevKit processes command (e.g., writes to servo, saves preference to NVS).
    *   ESP32 DevKit may send an updated `systemConfig` or `statusUpdate` back to the UI.

4.  **Status Updates:**
    *   ESP32 DevKit periodically sends `statusUpdate` messages (uptime, current servo positions, etc.) to the WebSocket Server, which forwards them to the interested Web UI.

5.  **Video Streaming:**
    *   User clicks "Start CAM Stream" in Web UI.
    *   Web UI -> WebSocket Server -> ESP32 DevKit (command: `START_STREAM`).
    *   ESP32 DevKit -> ESP32-CAM (Serial command: `START_STREAM`).
    *   ESP32-CAM starts its MJPEG HTTP server.
    *   Web UI's `<img>` tag `src` is set to the stream URL (e.g., proxied via `ebski.co/stream`, which in turn requests from `http://<ESP32_CAM_IP>/stream`).

6.  **Scheduled Workouts:**
    *   User adds a timer via Web UI.
    *   Web UI -> WebSocket Server -> ESP32 DevKit (command: `addTimer_value`).
    *   ESP32 DevKit saves timer to NVS and updates its internal schedule.
    *   ESP32 DevKit's main loop checks current time against scheduled slots. If a workout is active, it initiates random servo movements.

---

This architecture allows for a decoupled system where the ESP32 devices handle hardware control and core logic, the web server facilitates communication, and the web interface provides a rich user experience.
