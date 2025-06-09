# Deployment Guide: ESP32 WAN Stream Project

This guide provides step-by-step instructions for deploying the ESP32 WAN Stream project to an AWS EC2 instance (or similar Linux server).

## Prerequisites

Before you begin, ensure your server has the following installed:

1.  **Nginx:** Web server to handle HTTP/S requests and proxy WebSockets.
2.  **PHP:** For the WebSocket server and MJPEG script. Version 8.3 FPM & CLI are assumed based on current Nginx config (`php8.3-fpm`).
    *   Required PHP extensions: `php-redis` (for `mjpeg_server.php`).
3.  **Composer:** PHP dependency manager (to install Ratchet for WebSockets).
4.  **Redis Server:** In-memory data store used by `mjpeg_server.php` to cache the latest camera frame.
5.  **Git:** For cloning the repository.
6.  **Certbot (Recommended):** For SSL certificates if not already configured for `ebski.co`.

## Deployment Steps

### 1. Clone the Repository

Clone this repository to a suitable location on your server (e.g., your home directory or `/srv/`).

```bash
git clone <repository_url> esp32_wan_stream
cd esp32_wan_stream
```

### 2. Deploy Web Interface Files

The web interface files are located in the `kytsa_web_interface/` directory of this repository. They need to be copied to the web server's document root for the `/kytsa/` path.

*   **Target directory on server (as per Nginx config):** `/var/www/ebski.co/kytsa_web_interface/`

```bash
# Ensure the target directory exists
sudo mkdir -p /var/www/ebski.co/kytsa_web_interface/

# Copy the web interface files
sudo cp -R kytsa_web_interface/* /var/www/ebski.co/kytsa_web_interface/

# Set appropriate permissions (example, adjust if needed)
sudo chown -R www-data:www-data /var/www/ebski.co/kytsa_web_interface/
sudo find /var/www/ebski.co/kytsa_web_interface/ -type d -exec chmod 755 {} \;
sudo find /var/www/ebski.co/kytsa_web_interface/ -type f -exec chmod 644 {} \;
```

### 3. Deploy PHP Backend (WebSocket Server & MJPEG Script)

The PHP backend code is in the `php/php/` directory of the repository.

*   **Choose a deployment location for the backend:** This location should *not* be directly web-accessible (except for `mjpeg_server.php` which Nginx handles specifically). A common choice is `/opt/` or a subdirectory within `/var/www/` that isn't the web root.
    *   Example: `/var/www/ebski.co/php_backend/`

```bash
# Create the backend directory
sudo mkdir -p /var/www/ebski.co/php_backend/

# Copy the PHP backend files (src, bin, composer.json, mjpeg_server.php, README.md)
sudo cp -R php/php/* /var/www/ebski.co/php_backend/

# Navigate to the backend directory
cd /var/www/ebski.co/php_backend/

# Install Composer (if not installed globally)
# Option 1: Local install (downloads composer.phar to current directory)
# curl -sS https://getcomposer.org/installer | sudo php -- --install-dir=/usr/local/bin --filename=composer
# (Previous line installs globally, better. If local, use php composer.phar ...)

# Ensure composer is executable and in PATH or call it with php composer.phar
# Assuming composer is globally available:
sudo composer install --no-dev --optimize-autoloader

# Set appropriate permissions
sudo chown -R www-data:www-data /var/www/ebski.co/php_backend/ # Or user running the WebSocket server
# Ensure storage/cache directories used by Ratchet are writable if any (not typical for basic setup)
```
**Note on `mjpeg_server.php`**: The Nginx configuration (`php/nginx_config/ebski.co`) expects `mjpeg_server.php` to be accessible via `$document_root/mjpeg_server.php`.
The `$document_root` is `/var/www/ebski.co/html`.
If you've placed the entire `php/php` content into `/var/www/ebski.co/php_backend/`, then `mjpeg_server.php` is at `/var/www/ebski.co/php_backend/mjpeg_server.php`.
You will need to adjust the Nginx config for the `/receive` and `/stream` locations:
Change:
`fastcgi_param SCRIPT_FILENAME $document_root/mjpeg_server.php;`
To:
`fastcgi_param SCRIPT_FILENAME /var/www/ebski.co/php_backend/mjpeg_server.php;`
This guide will assume you make this Nginx adjustment.

### 4. Configure Nginx

1.  **Copy the Nginx configuration file:**
    The updated Nginx configuration is in `php/nginx_config/ebski.co` in the repository. Copy this file to your Nginx configuration directory.

    ```bash
    sudo cp php/nginx_config/ebski.co /etc/nginx/sites-available/ebski.co
    ```

2.  **Enable the site (if not already enabled):**

    ```bash
    # Remove default if it exists and conflicts
    # sudo rm /etc/nginx/sites-enabled/default

    sudo ln -s /etc/nginx/sites-available/ebski.co /etc/nginx/sites-enabled/ebski.co
    ```

3.  **Important: Adjust `mjpeg_server.php` path in Nginx config:**
    As noted above, if you placed the PHP backend (including `mjpeg_server.php`) in `/var/www/ebski.co/php_backend/`, you MUST update the `fastcgi_param SCRIPT_FILENAME` line in your `/etc/nginx/sites-available/ebski.co` file for BOTH the `location = /receive` and `location = /stream` blocks to point to the correct path:
    `fastcgi_param SCRIPT_FILENAME /var/www/ebski.co/php_backend/mjpeg_server.php;`

4.  **Test Nginx Configuration:**

    ```bash
    sudo nginx -t
    ```
    If the test is successful, proceed to reload Nginx.

5.  **Reload Nginx:**

    ```bash
    sudo systemctl reload nginx
    ```

### 5. Set Up and Run WebSocket Server as a Service (using systemd)

To ensure the PHP WebSocket server runs persistently in the background, it's recommended to set it up as a systemd service.

1.  **Create a systemd service file:**
    An example service file (`websocket_server.service`) is provided in the `examples/systemd/` directory of this repository. You'll need to copy it to `/etc/systemd/system/` and customize it.

    **Example path on server for the PHP backend:** `/var/www/ebski.co/php_backend/`
    **Example `websocket_server.service` content (place in `/etc/systemd/system/websocket_server.service`):**
    ```ini
    [Unit]
    Description=PHP WebSocket Server for Kytsa Project
    After=network.target

    [Service]
    User=www-data        # User that owns php_backend files and Nginx runs as
    Group=www-data       # Group for the user
    WorkingDirectory=/var/www/ebski.co/php_backend/
    ExecStart=/usr/bin/php /var/www/ebski.co/php_backend/bin/server.php
    Restart=always       # Restart service if it crashes
    RestartSec=5         # Time to wait before restart
    StandardOutput=syslog  # Log stdout to syslog
    StandardError=syslog   # Log stderr to syslog
    SyslogIdentifier=php-kytsa-websocket

    [Install]
    WantedBy=multi-user.target
    ```
    *   **Copy the example:** First, ensure the `examples/systemd/websocket_server.service` file is created in the repository (this is covered in the next plan step). Then, on the server:
        ```bash
        # Assuming you are in the cloned repo directory on the server
        sudo cp examples/systemd/websocket_server.service /etc/systemd/system/websocket_server.service
        ```
    *   **Customize:** Edit `/etc/systemd/system/websocket_server.service` if your paths or user/group are different. Ensure the `User` and `Group` have correct permissions for the `WorkingDirectory`.

2.  **Reload systemd, Enable, and Start the Service:**

    ```bash
    sudo systemctl daemon-reload
    sudo systemctl enable websocket_server.service
    sudo systemctl start websocket_server.service
    ```

3.  **Check Service Status:**

    ```bash
    sudo systemctl status websocket_server.service
    journalctl -u websocket_server.service -f # To view logs
    ```

### 6. Final Checks

*   The ESP32DevKitV1 firmware (`src/esp32devkitv1/main.cpp`) has been updated to connect to the WebSocket server using a secure connection (WSS) by default. It is configured for `wss://www.ebski.co/ws` (using host `ebski.co`, port `443`, and path `/ws`, via `webSocket.beginSSL()`). Ensure your Nginx configuration correctly proxies the `/ws` path on your SSL-enabled server (port 443) to the backend PHP WebSocket server, as detailed in the Nginx setup section.
*   Ensure your ESP32CAM firmware is updated with the correct `serverURLReceive` (`https://www.ebski.co/receive`).
*   Test the web interface by navigating to `https://www.ebski.co/kytsa/`.
*   Check Nginx error logs (`/var/log/nginx/error.log` or `/var/www/ebski.co/error.log`) and WebSocket server logs (via `journalctl`) if you encounter issues.

---

This guide provides a comprehensive overview. Specific paths and commands might need minor adjustments based on your exact server setup and distribution.
