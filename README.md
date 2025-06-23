# Kytsa Laser Control

Kytsa Laser Control is a comprehensive remote-controlled laser toy system designed to entertain pets (like Kytsa the cat!). It combines an ESP32 DevKit as the main controller, an ESP32-CAM for live video streaming, and a user-friendly web interface for real-time interaction and configuration.

## Key Features

*   **Remote Servo Control:** Precisely control X and Y axis servos to direct the laser pointer.
*   **Live Video Stream:** View the play area in real-time through an integrated ESP32-CAM feed in the web UI.
*   **Kytsa Workouts (Scheduled Movements):** Configure multiple timer slots for automated random laser movements, ensuring your pet stays engaged even when you're busy.
*   **Configurable Limits & Speed:**
    *   Set min/max operational limits for both X (Left-Right) and Y (Up-Down) servo axes.
    *   Adjust the interval between random movements ("How Fast") to customize play intensity.
*   **Laser & Relay Control:** Manually toggle the laser and an auxiliary relay (e.g., for other toys or lights) via the web UI.
*   **System Configuration:**
    *   Selectable timezones with POSIX string support for accurate local time display and scheduling.
    *   Configurable NTP update intervals.
*   **Responsive Web Interface:** Access and control the system from desktop or mobile devices.
    *   **Graphical Status Indicators:** Real-time visual feedback for WiFi signal strength, laser activity (Red for ON, Grey for OFF), manual workout status (Green for Active), and scheduled workout status (Green for Active).
    *   **Improved Stream Placeholder:** Clear "Stream Offline" text displayed when the video stream is not active, replacing the default broken image icon.
*   **Multi-Device Support:** The web interface can list and target multiple ESP32 laser units.
*   **OLED Display Management:**
    *   Onboard OLED screen shows current time, IP address, and device status.
    *   **Burn-in Prevention:** Display automatically turns off after a period of inactivity and wakes up on touch or relevant command.
*   **OTA Updates:** Firmware supports Over-The-Air updates for easier maintenance.

## System Components

*   **ESP32 DevKit V1:** Acts as the main brain, handling:
    *   WiFi connectivity and WebSocket communication.
    *   Servo control for X and Y axes.
    *   Laser and relay activation.
    *   NTP time synchronization and timezone management.
    *   Storing and executing scheduled "Workouts".
    *   Communication with the ESP32-CAM.
    *   Serving device status and configuration to the web UI.
*   **ESP32-CAM:** Dedicated to capturing and streaming video. It receives commands from the DevKit to start/stop streaming and toggle its onboard LED.
*   **Web Interface (`kytsa_web_interface`):** An HTML, CSS, and JavaScript single-page application that provides:
    *   Device discovery and selection.
    *   Display of the live video stream.
    *   Controls for all device features.
    *   Configuration panels for servo limits, movement speed, timers, and system settings.
*   **WebSocket Server:** A PHP-based server (hosted externally, e.g., on `ebski.co`) that facilitates communication between the web interface and the ESP32 devices. ESP32 devices connect to this server, and the web UI also connects to it to send commands and receive updates.

## Getting Started

### Prerequisites

*   **PlatformIO IDE:** Recommended for building and uploading firmware (usually as an extension in VS Code).
*   **Git:** For cloning the repository.

### Hardware Requirements

*   ESP32 DevKit V1 (or compatible ESP32 board with sufficient pins).
*   ESP32-CAM module (ensure it's a model that can be flashed and has a camera).
*   2x Servo Motors (e.g., SG90 or MG90S) for X and Y axes.
*   Laser Diode Module (low power, pet-safe).
*   Optional: Relay module for controlling an additional device.
*   Appropriate power supply for all components.
*   Mounting/Enclosure for the components.

### Firmware Installation (ESP32 DevKit)

1.  **Clone the Repository:**
    ```bash
    git clone <repository_url>
    cd <repository_directory>
    ```
2.  **Open with PlatformIO:** Open the cloned repository folder in VS Code with the PlatformIO extension installed.
3.  **Select Environment:** The primary environment for the DevKit is likely `esp32devkitv1`.
4.  **Build & Upload:** Use PlatformIO's "Build" and "Upload" options to compile and flash the firmware to your ESP32 DevKit board.

### WiFi Configuration

*   **First Boot / Reset:** On its first boot (or after its WiFi settings are reset), the ESP32 DevKit will start a WiFi Access Point (AP) named "**MiauAP**".
*   **Connect to AP:** Connect your computer or smartphone to the "MiauAP" WiFi network.
*   **Configure:** Once connected, a captive portal should automatically open in your browser. If not, navigate to `192.168.4.1`.
*   Follow the on-screen instructions to select your home WiFi network (SSID) and enter its password.
*   The ESP32 will then connect to your network and also send these credentials to the ESP32-CAM over serial.

### Accessing the Web Interface

*   **Option 1 (Local IP - if known):** Once the ESP32 DevKit is connected to your WiFi, it will print its IP address to the Serial Monitor. You might be able to access a basic UI directly from the ESP32 if it hosts one (though the primary UI is centralized).
*   **Option 2 (Centralized UI):** The main web interface is typically accessed via a central server, for example, `http://kytsa.ebski.co/` (replace with the actual URL if different). This interface communicates with your ESP32(s) via the WebSocket server.

## Web Interface Overview

The web interface allows you to:

*   **Select Device:** If multiple Kytsa Laser units are online, you can choose which one to control.
*   **Status Indicators:** At-a-glance view of WiFi strength, Laser status, Manual Workout activity, and Scheduled Workout activity.
*   **View Live Stream:** See the video feed from the selected device's ESP32-CAM. Shows "Stream Offline" when not active.
*   **Make Kytsa run...:** Toggle random automated laser movement.
*   **Live Streaming Controls:** Start/stop the video stream and toggle the ESP32-CAM's LED.
*   **Kytsa Workouts:** Add, view, and delete scheduled time slots for automated play sessions.
*   **Configure Gym (Device Configuration):**
    *   **Left-Right Limits (X-Axis):** Set the minimum and maximum angle for the horizontal servo.
    *   **Up-Down Limits (Y-Axis):** Set the minimum and maximum angle for the vertical servo (typically 45-135 degrees).
    *   **How Fast (Interval):** Configure the minimum and maximum delay (in milliseconds) between random movements.
    *   **Timezone:** Set the local timezone for the device.
    *   **NTP Update Interval:** Configure how often the device syncs its time with an NTP server.
*   **Device Status:** View raw status information and logs from the selected device.

## ESP32-CAM Communication

The ESP32 DevKit communicates with the ESP32-CAM over a serial connection (typically `Serial2` on the DevKit).
*   On boot, the DevKit sends the configured WiFi credentials to the CAM.
*   The DevKit can send commands to the CAM to start/stop video streaming and turn its LED on/off.
*   The CAM sends its IP address back to the DevKit once connected to WiFi.

## License

(License information to be added here - e.g., MIT, GPL, Apache 2.0, or Unlicensed/Proprietary)

---

*This README provides a general overview. More detailed documentation on specific components or setup steps may be available in other files within this repository or linked externally.*
