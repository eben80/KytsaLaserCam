# Kytsa Laser Control - System Architecture

This document outlines the system architecture of the Kytsa Laser Control project, detailing its components, their responsibilities, and how they interact.

## Core Components

The system is comprised of the following main components:

1.  **ESP32 DevKit V1 (Main Controller)**
2.  **ESP32-CAM (Video Streamer)**
3.  **Web Interface (Client-Side UI)**
4.  **Backend Server (`ebski.co` - AWS Instance)**
    *   Nginx (Web Server & Reverse Proxy)
    *   PHP-FPM (PHP Processor)
    *   Ratchet WebSocket Server (PHP Application)
    *   MJPEG Relay Server (PHP Application with Redis)
5.  **NVS (Non-Volatile Storage on ESP32 DevKit)**

---

### 1. ESP32 DevKit V1 (Main Controller)

*   **Hardware:** An ESP32 DevKit V1 board (or equivalent).
*   **Firmware:** Custom C++ code located in `src/esp32devkitv1/main.cpp`, built using PlatformIO. It utilizes a custom version of the Links2004 WebSockets client library.
*   **Responsibilities:**
    *   **WiFi Connectivity:** Connects to the local WiFi network using credentials configured via WiFiManager.
    *   **WebSocket Client:** Establishes and maintains a WebSocket connection to the central WebSocket Server (via Nginx proxy at `ebski.co/ws`).
    *   **Servo Control:** Directly controls two servo motors for X (pan/left-right) and Y (tilt/up-down) axes based on commands received.
    *   **Laser & Relay Output:** Toggles a laser diode module and an auxiliary relay module.
    *   **NTP Time Synchronization:** Fetches current time from NTP servers and manages local time based on the configured timezone.
    *   **Preference Management:** Stores and retrieves settings (servo limits, velocity intervals, timezone, NTP interval, timer schedules) from Non-Volatile Storage (NVS).
    *   **Scheduled Automation ("Kytsa Workouts"):** Executes pre-configured random movement schedules based on the current time.
    *   **Serial Communication with ESP32-CAM:** Manages the ESP32-CAM by sending commands and receiving status information.
    *   **Status Reporting:** Sends periodic status updates (`statusUpdate`) and full configuration (`systemConfig`) to the WebSocket server for the web interface.
    *   **Command Processing:** Receives JSON-formatted commands from the WebSocket server (originating from the web UI) and acts upon them.

---

### 2. ESP32-CAM (Video Streamer)

*   **Hardware:** An ESP32-CAM module with an attached camera.
*   **Firmware:** Assumed to be a custom or standard ESP32-CAM firmware capable of capturing JPEGs and POSTing them over HTTP.
*   **Responsibilities:**
    *   **WiFi Connectivity:** Connects to the local WiFi network using credentials provided by the ESP32 DevKit over serial.
    *   **Video Capture:** Captures individual JPEG frames from the attached camera.
    *   **Frame Transmission:** Sends these JPEG frames via HTTP POST requests to a specific endpoint on the backend server (`ebski.co/receive`).
    *   **Command Reception (Serial):** Listens for commands from the ESP32 DevKit via a serial connection (e.g., start/stop streaming commands, LED control, receiving WiFi credentials).
    *   **Status Reporting (Serial):** Sends its IP address back to the DevKit upon successful WiFi connection.

---

### 3. Web Interface (Client-Side UI)

*   **Location:** Served from `ebski.co/kytsa/` (physically located at `/var/www/ebski.co/kytsa_web_interface/` on the server).
*   **Technology:** HTML (`index.html`), CSS (`style.css`), JavaScript (`script.js`) (Single Page Application).
*   **Responsibilities:**
    *   **User Interaction:** Provides all controls and displays for the user.
    *   **WebSocket Client:** Establishes a WebSocket connection to the central WebSocket Server (via Nginx proxy at `ebski.co/ws`).
    *   **Device Discovery & Selection:** Lists available ESP32 DevKit devices (as reported by the server) and allows the user to select one to control.
    *   **Command Transmission:** Sends user-initiated commands as JSON messages to the WebSocket server, targeted at the selected device.
    *   **Status & Configuration Display:** Receives `statusUpdate` and `systemConfig` messages from the WebSocket server and updates the UI.
    *   **Video Stream Display:** Embeds and displays the MJPEG video stream by setting an `<img>` tag's `src` attribute to `ebski.co/stream`.

---

### 4. Backend Server (`ebski.co` - AWS Instance)

This server hosts the Nginx web server, PHP applications for WebSocket and MJPEG handling, and Redis.

#### 4.1. Nginx

*   **Role:** Acts as the primary web server and a reverse proxy.
*   **Configuration:** (`php/nginx_config/ebski.co`)
    *   Serves the static files for the Web Interface from `/kytsa/`.
    *   **WebSocket Proxy:** Proxies requests made to `/ws` to the internal Ratchet PHP WebSocket server running on `localhost:8080`.
    *   **MJPEG Relay Proxy:** Routes requests to `/receive` and `/stream` to the `mjpeg_server.php` script via PHP-FPM. Critically configured with `proxy_buffering off` for these streaming endpoints.
    *   Handles HTTP and HTTPS (SSL via Let's Encrypt).

#### 4.2. Ratchet WebSocket Server (PHP Application)

*   **Entry Point:** `php/php/bin/server.php`
*   **Core Logic:** `php/php/src/WebSocketHandler.php`
*   **Technology:** PHP, using the Ratchet library for WebSocket communication.
*   **Listening Port:** Runs on `localhost:8080` (accessed via Nginx proxy).
*   **Responsibilities:**
    *   Manages WebSocket connections from ESP32 DevKits and Web Interface clients.
    *   Handles `pairing` messages from ESP32s to register them with their `deviceId`.
    *   Handles `webClientInit` messages from Web UIs, sending them the list of connected devices.
    *   Routes `command` messages from a Web UI to the targeted ESP32 DevKit.
    *   Broadcasts `statusUpdate` and `systemConfig` messages received from ESP32 DevKits to all connected Web UI clients.
    *   Manages client lists and notifies Web UIs of device connections/disconnections.
    *   Configured for the "arduino" WebSocket subprotocol.

#### 4.3. MJPEG Relay Server (PHP Application with Redis)

*   **Script:** `php/php/mjpeg_server.php`
*   **Technology:** PHP, Redis (client library assumed, e.g., phpredis).
*   **Redis:** Used as a fast, temporary storage (buffer) for the latest camera frame.
    *   **Host:** `127.0.0.1`
    *   **Port:** `6379`
    *   **Key:** `latest_camera_frame`
*   **Responsibilities:**
    *   **Frame Reception (`/receive` endpoint):**
        *   Receives raw JPEG image data via HTTP POST from an ESP32-CAM.
        *   Stores this frame data into the Redis key `latest_camera_frame`.
    *   **MJPEG Stream Serving (`/stream` endpoint):**
        *   Responds to HTTP GET requests from the Web Interface.
        *   Continuously retrieves the latest frame from the `latest_camera_frame` Redis key.
        *   Sends these frames formatted as an MJPEG stream (`multipart/x-mixed-replace`).
        *   Manages frame rate for serving the stream.

---

### 5. NVS (Non-Volatile Storage on ESP32 DevKit)

*   **Technology:** ESP32's built-in Non-Volatile Storage system, accessed via `Preferences.h`.
*   **Usage:** The ESP32 DevKit firmware uses NVS to persistently store user configurations and device settings, such as servo limits, movement intervals (`minVel`, `maxVel`), timezone string, NTP update interval, and scheduled workout timers.

---

## Communication Flows Summary

1.  **Control & Status (WebSockets):**
    `Web UI <--(WebSocket via Nginx @ /ws)--> Ratchet Server (PHP) <--(WebSocket)--> ESP32 DevKit`

2.  **Video Stream (MJPEG over HTTP, with Redis Relay):**
    *   **Upload:** `ESP32-CAM --(HTTP POST JPEG frame)--> Nginx @ /receive --> mjpeg_server.php --> Redis`
    *   **Viewing:** `Web UI <--(HTTP GET MJPEG)--> Nginx @ /stream --> mjpeg_server.php --> Redis`

3.  **DevKit to CAM (Serial):**
    `ESP32 DevKit --(Serial TX/RX)--> ESP32-CAM`
    *   Commands (WiFi creds, stream control, LED).
    *   Status (CAM IP address).

This updated architecture provides a clearer picture of the backend components and their interactions, especially regarding the WebSocket communication and the MJPEG streaming relay mechanism.
