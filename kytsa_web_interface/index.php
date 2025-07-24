<?php
session_start();
if (!isset($_SESSION['user_id'])) {
    header("Location: auth/login.php");
    exit();
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Kytsa Laser Control</title>
    <link rel="stylesheet" href="style.css">
</head>
<body>
    <h1>Kytsa Laser Control</h1>
    <p><a href="devices.php">Manage Devices</a> | <a href="auth/logout.php">Logout</a></p>

    <div>
        <label for="deviceSelect">Select Device:</label>
        <select id="deviceSelect"><option value="">Loading devices...</option></select>
        <p>WebSocket Status: <span id="ws-status">Disconnected</span></p>
        <p>Selected Device ID: <span id="selected-device-id">None</span></p>
    </div>

    <div id="statusIndicatorsContainer">
        <div id="wifiStrengthIndicator" class="status-indicator wifi-indicator">
            <span class="wifi-bar"></span>
            <span class="wifi-bar"></span>
            <span class="wifi-bar"></span>
            <span class="wifi-bar"></span>
        </div>
        <div class="indicator-group">
            <span class="indicator-label">Laser:</span>
            <div id="laserStatusIndicator" class="status-indicator status-light" title="Laser Status"></div>
        </div>
        <div class="indicator-group">
            <span class="indicator-label">Workout: Manual</span>
            <div id="manualMovementIndicator" class="status-indicator status-light" title="Manual Movement Active"></div>
        </div>
        <div class="indicator-group">
            <span class="indicator-label">Workout: Scheduled</span>
            <div id="scheduledWorkoutIndicator" class="status-indicator status-light" title="Scheduled Workout Active"></div>
        </div>
    </div>

    <div class="container">
        <div class="controls">
            <h2>Controls (<span id="control-target-device">No Target</span>)</h2>

            <div class="control-group">
                <h3>Make Kytsa run...</h3>
                <button id="randomMotionToggle" disabled>Toggle Random Motion</button><br>
            </div>

            <div class="control-group">
                <h3>Live Streaming</h3>
                <button id="toggleCamStreamBtn" disabled>Toggle CAM Stream</button><br>
                <button id="toggleCamLedBtn" disabled>Toggle CAM LED</button><br>
            </div>

            <div class="timer-controls control-group"> <!-- Assuming timer-controls can also be a control-group or styled similarly -->
                <h3>Kytsa Workouts</h3> <!-- Changed from H2 to H3 for consistency -->
                <label for="timerStartTime">Start Time:</label>
                <input type="time" id="timerStartTime" disabled><br>
                <label for="timerEndTime">End Time:</label>
                <input type="time" id="timerEndTime" disabled><br>
                <button id="addTimerBtn" disabled>Add Timer</button>
                <h4>Scheduled Timers</h4> <!-- Changed from H3 to H4 as Timer Config is now H3 -->
                <div id="timerList">
                    <!-- Timer entries will be added here by script.js -->
                </div>
            </div>

            <div class="control-group">
                <h3><button id="toggleDeviceConfigBtn" class="toggle-btn">+/-</button> Configure Gym</h3>
                <div id="deviceConfigContent" style="display: none;">
                    <h4>Left-Right Limits</h4>
                    <div class="range-slider-container">
                        <div class="range-slider">
                            <span class="range-selected" id="servoXRangeSelected"></span>
                        </div>
                        <div class="range-input">
                            <input type="range" id="servoXMinRange" min="0" max="180" value="0" step="1" disabled>
                            <input type="range" id="servoXMaxRange" min="0" max="180" value="180" step="1" disabled>
                        </div>
                        <div class="range-values">
                            <label for="servoXMinValue">Min:</label>
                            <input type="number" id="servoXMinValue" value="0" min="0" max="180" step="1" disabled>
                            <label for="servoXMaxValue">Max:</label>
                            <input type="number" id="servoXMaxValue" value="180" min="0" max="180" step="1" disabled>
                        </div>
                    </div>
                    <!-- Servo X Position Slider Removed -->

                    <hr> <!-- Separator -->

                    <h4>Up-Down Limits</h4>
                    <div class="range-slider-container">
                        <div class="range-slider">
                            <span class="range-selected" id="servoYRangeSelected"></span>
                        </div>
                        <div class="range-input">
                            <input type="range" id="servoYMinRange" min="45" max="135" value="45" step="1" disabled>
                            <input type="range" id="servoYMaxRange" min="45" max="135" value="135" step="1" disabled>
                        </div>
                        <div class="range-values">
                            <label for="servoYMinValue">Min:</label>
                            <input type="number" id="servoYMinValue" value="45" min="45" max="135" step="1" disabled>
                            <label for="servoYMaxValue">Max:</label>
                            <input type="number" id="servoYMaxValue" value="135" min="45" max="135" step="1" disabled>
                        </div>
                    </div>
                    <!-- Servo Y Position Slider Removed -->

                    <hr> <!-- Separator -->

                    <h4>How Fast (0-3000)</h4>
                    <div class="range-slider-container">
                        <div class="range-slider">
                            <span class="range-selected" id="velRangeSelected"></span>
                        </div>
                        <div class="range-input">
                            <input type="range" id="velMinRange" min="0" max="3000" value="800" step="10" disabled>
                            <input type="range" id="velMaxRange" min="0" max="3000" value="2000" step="10" disabled>
                        </div>
                        <div class="range-values">
                            <label for="velMinValue">Min:</label>
                            <input type="number" id="velMinValue" value="800" min="0" max="3000" step="10" disabled>
                            <label for="velMaxValue">Max:</label>
                            <input type="number" id="velMaxValue" value="2000" min="0" max="3000" step="10" disabled>
                        </div>
                    </div>

                    <hr> <!-- Separator -->

                    <div class="config-item">
                        <label for="timezoneSelect">Timezone:</label>
                        <select id="timezoneSelect" disabled><option value="">Loading...</option></select>
                    </div>

                    <div class="config-item">
                        <label for="ntpIntervalSelect">NTP Update Interval:</label>
                        <select id="ntpIntervalSelect" disabled><option value="">Loading...</option></select>
                    </div>

                    <hr> <!-- Separator -->

                    <h4>Firmware Update</h4>
                    <div class="config-item">
                        <p id="updateStatus">Current: v?.?.? | Available: v?.?.?</p>
                        <button id="checkForUpdateBtn" disabled>Check for Update</button>
                        <button id="performUpdateBtn" disabled>Perform Update</button>
                    </div>

                </div>
            </div>

            <div class="control-group"> <!-- Changed from status-section -->
                <h3><button id="toggleDeviceStatusBtn" class="toggle-btn">+/-</button> Device Status (<span id="status-target-device">No Target</span>)</h3>
                <div id="deviceStatusContent" style="display: none;">
                    <pre id="deviceStatus">Waiting for updates...</pre>
                </div>
            </div>
        </div>

        <div class="stream-section">
            <h2>Live Stream</h2>
            <div id="stream-container">
                <img id="stream" src="#" alt="Live Stream" style="display:none; transform: rotate(180deg);">
                <div id="stream-placeholder-text">
                    Stream Offline
                </div>
            </div>
        </div>
    </div>

    <?php if (isset($_SESSION['is_admin']) && $_SESSION['is_admin']): ?>
    <div class="container" style="margin-top: 20px;">
        <div class="controls">
            <h3><button id="toggleDeviceHistoryBtn" class="toggle-btn">+/-</button> Device History</h3>
            <div id="deviceHistoryContent" style="display: none;">
                <!-- WiFi history will be loaded here -->
            </div>
        </div>
    </div>
    <?php endif; ?>

    <script src="script.js"></script>
</body>
</html>
