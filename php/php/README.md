# PHP Backend for WAN Stream Project

This directory contains the PHP scripts for the server-side logic.

## MJPEG Server (`mjpeg_server.php`)
Handles receiving image frames from the ESP32CAM and streaming them as an MJPEG feed.
-   POST to `/receive`: ESP32CAM sends JPEG frames here.
-   GET from `/stream`: Web interface embeds this URL to display the live video.
It uses Redis to store the latest frame. Make sure Redis server is installed and running.

## WebSocket Server (`bin/server.php` and `src/WebSocketHandler.php`)

Handles real-time duplex communication using the Ratchet library.

### Dependencies
Dependencies are managed by Composer (Ratchet library).

### File Structure
- `composer.json`: Defines project dependencies.
- `src/WebSocketHandler.php`: Contains the core WebSocket message handling logic (class `MyApp\WebSocketHandler`).
- `bin/server.php`: Script to run the WebSocket server.
- `vendor/`: Directory where Composer installs libraries (created after `composer install`).

### Setup & Running

1.  **Install Composer:** If you don't have Composer installed, download and install it from [getcomposer.org](https://getcomposer.org/download/).
2.  **Install Dependencies:** Navigate to the `php/php/` directory in your terminal and run:
    ```bash
    composer install
    ```
    This command reads `composer.json`, creates the `vendor/` directory, and installs Ratchet and its dependencies there.
3.  **Run the Server:** From the `php/php/` directory, execute:
    ```bash
    php bin/server.php
    ```
    The server will start and listen for WebSocket connections on port 8080. In a later step, Nginx will be configured to proxy public WebSocket requests (e.g., from `wss://ebski.co/ws`) to this internally running server (`ws://localhost:8080`).

### Communication Protocol (JSON-based)

*   **Client to Server Messages:**
    *   ESP32 for Pairing: `{"type": "pairing", "deviceId": "esp32_unique_id"}`
    *   ESP32 Status Update: `{"type": "statusUpdate", "data": {"key": "value", ...}}` (Note: `deviceId` is implicitly known by the server from the connection it was paired with.)
    *   Web UI for Initialization: `{"type": "webClientInit"}`
    *   Web UI Command for ESP32: `{"type": "command", "targetDeviceId": "esp32_unique_id", "command": "command_name", "value": "some_value"}` (Optional `value` for simple commands)
    *   Web UI Command for ESP32 (complex payload): `{"type": "command", "targetDeviceId": "esp32_unique_id", "command": "complex_command", "payload": {"param1": "val1", ...}}`

*   **Server to Client Messages:**
    *   To ESP32 (Command): `{"command": "command_name", "value": "some_value"}` or `{"command": "complex_command", "payload": {...}}`
    *   To Web UI (List of connected devices after init): `{"type": "deviceList", "devices": ["id1", "id2", ...]}`
    *   To Web UI (New ESP32 connected): `{"type": "deviceConnected", "deviceId": "esp32_unique_id"}`
    *   To Web UI (ESP32 disconnected): `{"type": "deviceDisconnected", "deviceId": "esp32_unique_id"}`
    *   To Web UI (Status update from an ESP32): `{"type": "statusUpdate", "deviceId": "esp32_unique_id", "data": {"key": "value", ...}}`
    *   To Web UI (Error message): `{"type": "error", "message": "Error description"}`
