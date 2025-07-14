/**
 * @file script.js
 * @brief Client-side JavaScript for the Kytsa Laser Control web interface.
 *
 * This script handles WebSocket communication with the backend server,
 * manages UI elements for device selection, control, configuration,
 * and status display. It includes logic for live video streaming,
 * timer scheduling ("Kytsa Workouts"), servo limit adjustments,
 * and other device-specific commands.
 */
document.addEventListener('DOMContentLoaded', () => {
    // DOM Element References
    /** @type {HTMLElement | null} Displays WebSocket connection status. */
    const wsStatusEl = document.getElementById('ws-status');
    /** @type {HTMLSelectElement | null} Dropdown for selecting the target ESP32 device. */
    const deviceSelectEl = document.getElementById('deviceSelect');
    /** @type {HTMLElement | null} Displays the ID of the currently selected device. */
    const selectedDeviceIdEl = document.getElementById('selected-device-id');
    /** @type {HTMLElement | null} Preformatted text area for displaying raw device status JSON. */
    const deviceStatusEl = document.getElementById('deviceStatus');
    /** @type {HTMLImageElement | null} Image element for displaying the MJPEG video stream. */
    const streamImgEl = document.getElementById('stream');
    /** @type {HTMLElement | null} Div element for displaying text when the stream is offline. */
    const streamPlaceholderTextEl = document.getElementById('stream-placeholder-text');
    /** @type {HTMLElement | null} Span indicating which device the main controls are targeting. */
    const controlTargetDeviceEl = document.getElementById('control-target-device');
    /** @type {HTMLElement | null} Span indicating which device the status display is for. */
    const statusTargetDeviceEl = document.getElementById('status-target-device');

    // X-Axis Dual Range Slider Elements
    /** @type {HTMLInputElement | null} Range input for X-axis minimum limit. */
    const servoXMinRangeEl = document.getElementById('servoXMinRange');
    /** @type {HTMLInputElement | null} Range input for X-axis maximum limit. */
    const servoXMaxRangeEl = document.getElementById('servoXMaxRange');
    /** @type {HTMLElement | null} Span visually representing the selected range on X-axis slider. */
    const servoXRangeSelectedEl = document.getElementById('servoXRangeSelected');
    /** @type {HTMLInputElement | null} Number input for X-axis minimum limit. */
    const servoXMinValueEl = document.getElementById('servoXMinValue');
    /** @type {HTMLInputElement | null} Number input for X-axis maximum limit. */
    const servoXMaxValueEl = document.getElementById('servoXMaxValue');

    // Y-Axis Dual Range Slider Elements
    /** @type {HTMLInputElement | null} Range input for Y-axis minimum limit. */
    const servoYMinRangeEl = document.getElementById('servoYMinRange');
    /** @type {HTMLInputElement | null} Range input for Y-axis maximum limit. */
    const servoYMaxRangeEl = document.getElementById('servoYMaxRange');
    /** @type {HTMLElement | null} Span visually representing the selected range on Y-axis slider. */
    const servoYRangeSelectedEl = document.getElementById('servoYRangeSelected');
    /** @type {HTMLInputElement | null} Number input for Y-axis minimum limit. */
    const servoYMinValueEl = document.getElementById('servoYMinValue');
    /** @type {HTMLInputElement | null} Number input for Y-axis maximum limit. */
    const servoYMaxValueEl = document.getElementById('servoYMaxValue');

    /** @type {HTMLButtonElement | null} Button to toggle random motion on the ESP32. */
    const randomMotionToggleBtn = document.getElementById('randomMotionToggle');

    // Timer controls
    /** @type {HTMLInputElement | null} Time input for setting a workout start time. */
    const timerStartTimeEl = document.getElementById('timerStartTime');
    /** @type {HTMLInputElement | null} Time input for setting a workout end time. */
    const timerEndTimeEl = document.getElementById('timerEndTime');
    /** @type {HTMLButtonElement | null} Button to add a new workout timer. */
    const addTimerBtn = document.getElementById('addTimerBtn');
    /** @type {HTMLElement | null} Div element to display the list of configured timers. */
    const timerListEl = document.getElementById('timerList');

    // CAM control buttons (consolidated)
    /** @type {HTMLButtonElement | null} Button to toggle the ESP32-CAM video stream. */
    const toggleCamStreamBtn = document.getElementById('toggleCamStreamBtn');
    /** @type {HTMLButtonElement | null} Button to toggle the ESP32-CAM LED. */
    const toggleCamLedBtn = document.getElementById('toggleCamLedBtn');

    // Collapsible section elements
    /** @type {HTMLButtonElement | null} Button to toggle visibility of the device configuration section. */
    const toggleDeviceConfigBtn = document.getElementById('toggleDeviceConfigBtn');
    /** @type {HTMLElement | null} Div container for device configuration controls. */
    const deviceConfigContent = document.getElementById('deviceConfigContent');
    /** @type {HTMLButtonElement | null} Button to toggle visibility of the device status section. */
    const toggleDeviceStatusBtn = document.getElementById('toggleDeviceStatusBtn');
    /** @type {HTMLElement | null} Div container for displaying device status. */
    const deviceStatusContent = document.getElementById('deviceStatusContent');

    // Velocity Dual Range Slider Elements
    /** @type {HTMLInputElement | null} Range input for minimum velocity/interval. */
    const velMinRangeEl = document.getElementById('velMinRange');
    /** @type {HTMLInputElement | null} Range input for maximum velocity/interval. */
    const velMaxRangeEl = document.getElementById('velMaxRange');
    /** @type {HTMLElement | null} Span visually representing the selected velocity/interval range. */
    const velRangeSelectedEl = document.getElementById('velRangeSelected');
    /** @type {HTMLInputElement | null} Number input for minimum velocity/interval. */
    const velMinValueEl = document.getElementById('velMinValue');
    /** @type {HTMLInputElement | null} Number input for maximum velocity/interval. */
    const velMaxValueEl = document.getElementById('velMaxValue');

    // Timezone and NTP Selectors
    /** @type {HTMLSelectElement | null} Dropdown for selecting the device's timezone. */
    const timezoneSelectEl = document.getElementById('timezoneSelect');
    /** @type {HTMLSelectElement | null} Dropdown for selecting the NTP update interval. */
    const ntpIntervalSelectEl = document.getElementById('ntpIntervalSelect');

    // Firmware Update Elements
    const checkForUpdateBtn = document.getElementById('checkForUpdateBtn');
    const performUpdateBtn = document.getElementById('performUpdateBtn');
    const updateStatusEl = document.getElementById('updateStatus');

    // Status Indicator Elements
    const wifiStrengthIndicatorEl = document.getElementById('wifiStrengthIndicator');
    const laserStatusIndicatorEl = document.getElementById('laserStatusIndicator');
    // const workoutStatusIndicatorEl = document.getElementById('workoutStatusIndicator'); // Removed
    const manualMovementIndicatorEl = document.getElementById('manualMovementIndicator');
    const scheduledWorkoutIndicatorEl = document.getElementById('scheduledWorkoutIndicator');


    /** @type {Array<HTMLElement|null>} Array of all major control elements, used for batch enabling/disabling. */
    const allControls = [
        servoXMinRangeEl, servoXMaxRangeEl, servoXMinValueEl, servoXMaxValueEl,
        servoYMinRangeEl, servoYMaxRangeEl, servoYMinValueEl, servoYMaxValueEl,
        velMinRangeEl, velMaxRangeEl, velMinValueEl, velMaxValueEl, // Velocity controls
        timezoneSelectEl, ntpIntervalSelectEl, // New selectors
        timerStartTimeEl, timerEndTimeEl, addTimerBtn,
        randomMotionToggleBtn, toggleCamStreamBtn, toggleCamLedBtn,
        checkForUpdateBtn, performUpdateBtn // Firmware update buttons
    ];

    const RANGE_MIN_DIFFERENCE = 10; // Minimum difference between min and max thumbs of a range slider

    // --- Global State Variables ---
    /**
     * The main WebSocket connection instance.
     * @type {WebSocket | null}
     */
    let socket = null;
    /**
     * The device ID of the currently selected ESP32 device from the dropdown.
     * @type {string | null}
     */
    let selectedDeviceId = null;
    /**
     * Stores the last known state/configuration for each device, keyed by deviceId.
     * Example: `deviceStates['D4627C931744'] = { min_x: 10, random_motion_active: false, ... }`
     * @type {Object<string, Object>}
     */
    let deviceStates = {};

    // --- Control Logic Variables ---
    /**
     * Timeout ID for automatically turning off the laser after manual servo adjustment.
     * @type {number | null}
     */
    let laserOffTimeoutId = null;
    /**
     * Delay in milliseconds before automatically turning off the laser after X/Y adjustment.
     * @const {number}
     */
    const LASER_OFF_DELAY = 10000; // 10 seconds

    /**
     * Tracks the client-side optimistic state of the CAM stream. Updated by status messages.
     * @type {boolean}
     */
    let isStreamActive = false;
    /**
     * Timeout ID to prevent rapid toggling of the CAM stream.
     * @type {number | null}
     */
    let streamToggleTimeoutId = null;
    /**
     * Tracks the client-side optimistic state of the CAM LED. Updated by status messages.
     * @type {boolean}
     */
    let isCamLedActive = false;
    /**
     * Timeout ID to prevent rapid toggling of the CAM LED.
     * @type {number | null}
     */
    let ledToggleTimeoutId = null;
    /**
     * Cooldown period in milliseconds for CAM control toggles.
     * @const {number}
     */
    const CAM_TOGGLE_TIMEOUT = 2000; // 2 seconds

    /**
     * Enables or disables all interactive control elements on the page.
     * Also updates the text indicating which device is currently being controlled.
     * @param {boolean} disabled - True to disable controls, false to enable them.
     */
    function setControlsDisabled(disabled) {
        allControls.forEach(control => {
            if (control) control.disabled = disabled;
        });
        if (controlTargetDeviceEl) {
            controlTargetDeviceEl.textContent = disabled ? 'No Target' : (selectedDeviceId || 'Unknown Target');
        }
    }

    /**
     * Establishes and manages the WebSocket connection to the server.
     * Sets up event handlers for `onopen`, `onmessage`, `onclose`, and `onerror`.
     * Includes logic for attempting reconnection on close or error.
     */
    function connectWebSocket() {
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        // Assumes the WebSocket server is accessible via a path on the same host as the web UI,
        // potentially proxied by a web server like Nginx.
        const wsUrl = `${protocol}//${window.location.host}/ws`;
        console.log(`Attempting to connect to WebSocket: ${wsUrl}`);

        if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) {
            console.log("WebSocket connection attempt already in progress or open.");
            return;
        }
        socket = new WebSocket(wsUrl);

        /**
         * Handles the WebSocket connection opening.
         * Updates UI status, sends client initialization message.
         */
        socket.onopen = () => {
            console.log('[DEBUG] WebSocket opened.');
            if (wsStatusEl) {
                wsStatusEl.textContent = 'Connected';
                wsStatusEl.className = 'connected';
            }
            console.log('WebSocket connected'); // Original log, can be kept or removed
            socket.send(JSON.stringify({ type: 'webClientInit' }));
            // Controls remain disabled until a device is selected
            setControlsDisabled(true);
        };

        /**
         * Handles messages received from the WebSocket server.
         * Parses the JSON message and routes it based on `message.type`.
         * @param {MessageEvent} event - The message event from the WebSocket.
         */
        socket.onmessage = (event) => {
            console.log('[DEBUG] ONMESSAGE: Raw data received from server:', event.data); // Ensure this is active
            let message;
            try {
                message = JSON.parse(event.data);
                console.log('[DEBUG] ONMESSAGE: Parsed message:', message); // Ensure this is active
            } catch (e) {
                console.error('[ERROR] ONMESSAGE: Failed to parse JSON message from server:', event.data, e);
                return;
            }

            switch (message.type) {
                case 'deviceList':
                    updateDeviceList(message.devices);
                    break;
                case 'deviceConnected':
                    addDeviceToList(message.deviceId);
                    break;
                case 'deviceDisconnected':
                    removeDeviceFromList(message.deviceId);
                    break;
                case 'statusUpdate':
                    if (!deviceStates[message.deviceId]) deviceStates[message.deviceId] = {};
                    Object.assign(deviceStates[message.deviceId], message.data);

                    if (message.deviceId === selectedDeviceId) {
                        updateDeviceStatusDisplay(message.data);
                        updateUIToggleStates(message.data);
                        updateStatusIndicators(message.data); // Update new status indicators

                        // Update CAM states from statusUpdate
                        if (message.data.esp32cam_streaming !== undefined) {
                            isStreamActive = message.data.esp32cam_streaming;
                            if (toggleCamStreamBtn) toggleCamStreamBtn.textContent = isStreamActive ? 'Stop CAM Stream' : 'Start CAM Stream';
                            // Update stream image source based on actual streaming state
                            if (isStreamActive && streamImgEl && streamPlaceholderTextEl) {
                                streamImgEl.src = `https://ebski.co/stream?t=${new Date().getTime()}`;
                                streamImgEl.style.display = 'block';
                                streamPlaceholderTextEl.style.display = 'none';
                            } else if (streamImgEl && streamPlaceholderTextEl) {
                                streamImgEl.src = "#";
                                streamImgEl.style.display = 'none';
                                streamPlaceholderTextEl.style.display = 'block'; // Or 'flex' if using flex for centering
                            }
                        }
                        if (message.data.cam_led_active !== undefined) {
                            isCamLedActive = message.data.cam_led_active;
                            if (toggleCamLedBtn) toggleCamLedBtn.textContent = isCamLedActive ? 'Turn CAM LED OFF' : 'Turn CAM LED ON';
                        }

                        // Servo limits are now fixed in HTML, no need to update from statusUpdate
                    }
                    break;
                case 'systemConfig':
                    console.log('[DEBUG] SYSTEM_CONFIG_HANDLER: Matched message.type === "systemConfig".');
                    console.log('[DEBUG] SYSTEM_CONFIG_HANDLER: Full message object for systemConfig:', JSON.stringify(message, null, 2));

                    // ADD THIS CHECK:
                    if (message.deviceId !== selectedDeviceId) {
                        console.log(`[DEBUG] SYSTEM_CONFIG_HANDLER: Ignoring systemConfig for device ${message.deviceId} as current selected device is ${selectedDeviceId}.`);
                        break; // Exit this case if deviceId doesn't match
                    }
                    // END OF ADDED CHECK

                    // Existing logic (now only runs if deviceId matches)
                    if (message.config) {
                        console.log('[DEBUG] SYSTEM_CONFIG_HANDLER: message.config object exists for selected device:', JSON.stringify(message.config, null, 2));

                        // Servo slider min/max values are now fixed in the HTML.
                        // We still need to ensure the current value is within these fixed bounds
                        // if a systemConfig message were to suggest a value outside them,
                        // though typically systemConfig would provide the current position which should be valid.
                        // The primary role here is to update the slider's *value* and display.
                        // Also, update the dual range sliders from systemConfig.

                        // Update X-Axis from systemConfig
                        // The primary role here is to update the slider's *value* and display.
                        // Also, update the dual range sliders from systemConfig.

                        // Update X-Axis from systemConfig
                        if (message.config.min_x !== undefined && message.config.max_x !== undefined) { // Changed to snake_case
                            const xElements = {
                                minRangeEl: servoXMinRangeEl, maxRangeEl: servoXMaxRangeEl,
                                rangeSelectedEl: servoXRangeSelectedEl,
                                minValueEl: servoXMinValueEl, maxValueEl: servoXMaxValueEl
                            };
                            updateDualRangeSliderUI(xElements, parseInt(message.config.min_x), parseInt(message.config.max_x), 0, 180);
                        }
                        // Update X position slider value - REMOVED as slider is gone
                        // if (servoXSliderEl && servoXValueEl && message.config.servoX_pos !== undefined) {
                        //     servoXSliderEl.value = message.config.servoX_pos;
                        //     servoXValueEl.textContent = servoXSliderEl.value;
                        // } else if (servoXSliderEl && servoXValueEl) {
                        //      servoXValueEl.textContent = servoXSliderEl.value;
                        // }

                        // Update Y-Axis from systemConfig
                        if (message.config.min_y !== undefined && message.config.max_y !== undefined) { // Changed to snake_case
                            const yElements = {
                                minRangeEl: servoYMinRangeEl, maxRangeEl: servoYMaxRangeEl,
                                rangeSelectedEl: servoYRangeSelectedEl,
                                minValueEl: servoYMinValueEl, maxValueEl: servoYMaxValueEl
                            };
                            updateDualRangeSliderUI(yElements, parseInt(message.config.min_y), parseInt(message.config.max_y), 45, 135); // Y-axis specific range
                        }
                        // Update Y position slider value - REMOVED as slider is gone

                        // Update Velocity limits from systemConfig
                        if (message.config.min_vel !== undefined && message.config.max_vel !== undefined && velMinRangeEl) { // Use snake_case
                            const velElements = {
                                minRangeEl: velMinRangeEl, maxRangeEl: velMaxRangeEl,
                                rangeSelectedEl: velRangeSelectedEl,
                                minValueEl: velMinValueEl, maxValueEl: velMaxValueEl
                            };
                            updateDualRangeSliderUI(velElements, parseInt(message.config.min_vel), parseInt(message.config.max_vel), 0, 3000);
                        }

                        // Update Timezone from systemConfig
                        if (message.config.timezone_posix !== undefined && timezoneSelectEl) { // Expect 'timezone_posix' (string)
                            timezoneSelectEl.value = message.config.timezone_posix;
                        }

                        // Store firmware version and update UI
                        if (message.config.firmware_version !== undefined) {
                            if (!deviceStates[selectedDeviceId]) {
                                deviceStates[selectedDeviceId] = {};
                            }
                            deviceStates[selectedDeviceId].firmware_version = message.config.firmware_version;
                            updateFirmwareStatusUI(message.config.firmware_version, '?'); // Update UI with current version
                        }
                        // Old integer offset handling removed/commented if any:
                        // if (message.config.timezone !== undefined && timezoneSelectEl) {
                        //     timezoneSelectEl.value = message.config.timezone;
                        // }

                        // Update NTP Interval from systemConfig
                        if (message.config.ntp_interval !== undefined && ntpIntervalSelectEl) { // Expect 'ntp_interval' (milliseconds)
                            ntpIntervalSelectEl.value = message.config.ntp_interval / 1000; // Convert ms to seconds for select value
                        }

                        // Update CAM button states if present in systemConfig.config
                        // ESP32's sendSystemConfig currently does not include these.
                        // if (message.config.hasOwnProperty('cam_led_active')) {
                        //     isCamLedActive = message.config.cam_led_active;
                        //     if(toggleCamLedBtn) toggleCamLedBtn.textContent = isCamLedActive ? 'Turn CAM LED OFF' : 'Turn CAM LED ON';
                        // }
                        // if (message.config.hasOwnProperty('esp32cam_streaming')) {
                        //     isStreamActive = message.config.esp32cam_streaming;
                        //     if(toggleCamStreamBtn) toggleCamStreamBtn.textContent = isStreamActive ? 'Stop CAM Stream' : 'Start CAM Stream';
                        //     if (isStreamActive && streamImgEl && streamPlaceholderTextEl) {
                        //        streamImgEl.src = `https://ebski.co/stream?t=${new Date().getTime()}`;
                        //        streamImgEl.style.display = 'block';
                        //        streamPlaceholderTextEl.style.display = 'none';
                        //     } else if (streamImgEl && streamPlaceholderTextEl) {
                        //        streamImgEl.src = "#";
                        //        streamImgEl.style.display = 'none';
                        //        streamPlaceholderTextEl.style.display = 'block'; // or 'flex'
                        //     }
                        // }

                        if (message.config.hasOwnProperty('timers')) {
                            console.log('[DEBUG] SYSTEM_CONFIG_HANDLER: message.config.timers exists. Value:', JSON.stringify(message.config.timers, null, 2));
                            console.log('[DEBUG] SYSTEM_CONFIG_HANDLER: Type of timers:', typeof message.config.timers, 'Is Array?', Array.isArray(message.config.timers));
                            displayTimers(message.config.timers || []);
                        } else {
                            console.warn('[WARN] SYSTEM_CONFIG_HANDLER: message.config does NOT have "timers" property for selected device. Displaying empty list.');
                            displayTimers([]);
                        }
                    } else {
                        console.error('[ERROR] SYSTEM_CONFIG_HANDLER: message.config object is missing in systemConfig message for selected device. Cannot process.');
                        displayTimers([]);
                    }
                    break;
                case 'timerList': // Message type for timer updates
                case 'scheduleUpdate': // ESP32 might send this after add/delete timer
                     if (message.deviceId === selectedDeviceId && message.timers) {
                        console.log('[DEBUG] timerList or scheduleUpdate received. Timers:', message.timers);
                        displayTimers(message.timers || []); // Pass empty array if null/undefined
                    }
                    break;
                case 'ota_status':
                    if (updateStatusEl && message.deviceId === selectedDeviceId) {
                        updateStatusEl.textContent = `Status: ${message.status}`;
                    }
                    break;
                case 'error':
                    alert(`Error from server: ${message.message || 'Unknown error'}`);
                    break;
                default:
                    console.log("Received unhandled message type: ", message.type);
                    break;
            }
        };

        /**
         * Handles the WebSocket connection closing.
         * Updates UI status, disables controls, and schedules a reconnection attempt.
         * @param {CloseEvent} event - The close event from the WebSocket.
         */
        socket.onclose = (event) => {
            if (wsStatusEl) {
                wsStatusEl.textContent = 'Disconnected';
                wsStatusEl.className = 'disconnected';
            }
            console.log('[DEBUG] WebSocket closed. Code:', event.code, 'Reason:', event.reason);
            setControlsDisabled(true);
            // Clear CAM toggle timeouts
            if (streamToggleTimeoutId) { clearTimeout(streamToggleTimeoutId); streamToggleTimeoutId = null; }
            if (ledToggleTimeoutId) { clearTimeout(ledToggleTimeoutId); ledToggleTimeoutId = null; }
            // Reset button states as they are part of allControls and will be disabled by setControlsDisabled
            // Text content will be reset on next device selection or by status update
            if (toggleCamStreamBtn) toggleCamStreamBtn.textContent = 'Start CAM Stream';
            if (toggleCamLedBtn) toggleCamLedBtn.textContent = 'Turn CAM LED ON';
            isStreamActive = false;
            isCamLedActive = false;
            if (streamImgEl && streamPlaceholderTextEl) {
                streamImgEl.src = "#";
                streamImgEl.style.display = 'none';
                streamPlaceholderTextEl.style.display = 'block'; // Or 'flex'
            }

            // Laser safety: If WebSocket closes and laser timer was active, clear it.
            // turnLaserOff() already has a check for selectedDeviceId, so it won't send command if no device.
            if (laserOffTimeoutId) {
                clearTimeout(laserOffTimeoutId); // Just clear, don't try to send command as socket is closed.
                laserOffTimeoutId = null;
            }

            // Avoid rapid reconnection loops if server is truly down
            setTimeout(connectWebSocket, 5000 + Math.random() * 1000);
        };

        /**
         * Handles WebSocket errors.
         * Logs the error, updates UI status, and disables controls.
         * Relies on `onclose` to handle reconnection.
         * @param {Event} error - The error event from the WebSocket.
         */
        socket.onerror = (error) => {
            console.log('[DEBUG] WebSocket error:', error);
            // console.error('WebSocket error:', error); // Original log
            if (wsStatusEl) {
                wsStatusEl.textContent = 'Error';
                wsStatusEl.className = 'disconnected'; // Visually treat as disconnected
            }
            setControlsDisabled(true);
            // onclose will likely be called after onerror, which handles reconnection
        };
    }

    /**
     * Populates the device selection dropdown with a list of available devices.
     * @param {string[]} devices - An array of device IDs.
     */
    function updateDeviceList(devices) {
        const currentSelectedVal = deviceSelectEl.value;
        deviceSelectEl.innerHTML = '<option value="">-- Select a Device --</option>';
        devices.forEach(deviceId => {
            const option = document.createElement('option');
            option.value = deviceId;
            option.textContent = deviceId;
            deviceSelectEl.appendChild(option);
        });
        // Try to reselect previous device if it's still in the list
        if (devices.includes(currentSelectedVal)) {
            deviceSelectEl.value = currentSelectedVal;
        }
        // Manually trigger change to update UI based on (possibly new) selection
        deviceSelectEl.dispatchEvent(new Event('change'));
    }

    /**
     * Adds a single device to the device selection dropdown if not already present.
     * @param {string} deviceId - The ID of the device to add.
     */
    function addDeviceToList(deviceId) {
        if (!Array.from(deviceSelectEl.options).some(opt => opt.value === deviceId)) {
            const option = document.createElement('option');
            option.value = deviceId;
            option.textContent = deviceId;
            deviceSelectEl.appendChild(option);
        }
    }

    /**
     * Removes a device from the device selection dropdown.
     * If the removed device was the currently selected one, it resets the selection.
     * @param {string} deviceId - The ID of the device to remove.
     */
    function removeDeviceFromList(deviceId) {
        const currentSelectedVal = deviceSelectEl.value;
        Array.from(deviceSelectEl.options).forEach(option => {
            if (option.value === deviceId) {
                option.remove();
            }
        });
        // If the removed device was selected, reset selection
        if (currentSelectedVal === deviceId) {
            deviceSelectEl.value = "";
            deviceSelectEl.dispatchEvent(new Event('change'));
        }
    }

    /**
     * Updates the text content of toggle buttons (Laser, Relay, Random Motion)
     * to reflect their current state (e.g., "Laser (ON)").
     * @param {Object} [stateData={}] - An object containing the current state of the device.
     *                                 Expected keys: `laser_active`, `relay_active`, `random_motion_active`.
     */
    function updateUIToggleStates(stateData = {}) { // Default to empty object if no state
        // Laser and Relay buttons are removed, so their logic is gone.
        if (randomMotionToggleBtn) randomMotionToggleBtn.textContent = `Random Motion (${stateData.random_motion_active ? "ON" : "OFF"})`;
    }

    /**
     * Updates the firmware status UI section.
     * @param {number | string} deviceVersion - The current version on the device.
     * @param {number | string | null} serverVersion - The latest version on the server. Can be null or '?' if unknown.
     */
    function updateFirmwareStatusUI(deviceVersion, serverVersion = null) {
        if (!updateStatusEl) return;

        let serverVersionText = (serverVersion === null || serverVersion === '?') ? 'v?' : `v${serverVersion}`;
        updateStatusEl.textContent = `Current: v${deviceVersion} | Available: ${serverVersionText}`;

        if (serverVersion !== null && serverVersion > deviceVersion) {
            if (performUpdateBtn) performUpdateBtn.disabled = false;
        } else {
            if (performUpdateBtn) performUpdateBtn.disabled = true;
        }
    }

    /**
     * Converts total minutes into a HH:MM formatted string.
     * @param {number} totalMinutes - The total minutes from midnight.
     * @returns {string} The time in HH:MM format.
     */
    function minutesToTime(totalMinutes) {
        if (typeof totalMinutes !== 'number' || isNaN(totalMinutes)) {
            return 'N/A';
        }
        const hours = Math.floor(totalMinutes / 60);
        const minutes = totalMinutes % 60;
        return `${String(hours).padStart(2, '0')}:${String(minutes).padStart(2, '0')}`;
    }

    /**
     * Displays the list of timers and sets up delete buttons.
     * @param {Array<Object>} timers - Array of timer objects, e.g., [{startTimeMinutes: 600, stopTimeMinutes: 720}, ...]
     */
    function displayTimers(timers) {
            // ADD THIS LOG:
            console.log('[DEBUG] DISPLAY_TIMERS: Called with timers argument:', JSON.stringify(timers, null, 2), 'Is Array?', Array.isArray(timers));

        if (!timerListEl) return;
        timerListEl.innerHTML = ''; // Clear existing timers

        if (!timers || !Array.isArray(timers) || timers.length === 0) { // Made condition more robust
            timerListEl.innerHTML = '<p>No timers currently configured.</p>';
            return;
        }

        timers.forEach((timer, index) => {
            const startTimeFormatted = minutesToTime(timer.startTimeMinutes);
            const endTimeFormatted = minutesToTime(timer.stopTimeMinutes);
            const timerDiv = document.createElement('div');
            timerDiv.className = 'timer-entry';
            timerDiv.innerHTML = `
                <span>Start: ${startTimeFormatted}, End: ${endTimeFormatted}</span>
                <button class="deleteTimerBtn" data-timer-index="${index}">Delete</button>
            `;
            timerListEl.appendChild(timerDiv);
        });

        // Add event listeners to new delete buttons
        document.querySelectorAll('.deleteTimerBtn').forEach(button => {
            button.addEventListener('click', (event) => {
                const timerIndex = event.target.getAttribute('data-timer-index');
                if (timerIndex !== null) {
                    sendCommand({ command: 'deleteTimer', timerIndex: parseInt(timerIndex) });
                    // Optionally, re-request system config or expect a timerList update
                    // sendCommand({ command: 'getSystemConfig' });
                }
            });
        });
    }

    /**
     * Updates the device status display area and servo sliders with new data.
     * @param {Object} [data={}] - An object containing status data for the selected device.
     *                           Expected keys: `servoX_pos`, `servoY_pos`, and others for raw display.
     */
    function updateDeviceStatusDisplay(data = {}) { // Default to empty object
        if (deviceStatusEl) deviceStatusEl.textContent = JSON.stringify(data, null, 2);
        // servoXSliderEl, servoXValueEl, servoYSliderEl, servoYValueEl related logic removed
        // as these elements no longer exist for displaying current position directly.
        // The raw status display will still show these values if they come from the backend.
    }

    /**
     * Sends a command object to the currently selected ESP32 device via WebSocket.
     * @param {Object} commandData - The command payload to send. This will be merged with
     *                             `type: 'command'` and `targetDeviceId`.
     */
    function sendCommand(commandData) {
        if (!selectedDeviceId) {
            alert('Please select a device first.');
            return;
        }
        if (socket && socket.readyState === WebSocket.OPEN) {
            // Generic message construction (special addTimer handling removed)
            const messageToSend = {
                type: 'command',
                targetDeviceId: selectedDeviceId,
                ...commandData
            };

            const messagePayloadString = JSON.stringify(messageToSend);
            console.log('[DEBUG] sendCommand: Sending WebSocket message (stringified):', messagePayloadString);
            socket.send(messagePayloadString);
        } else {
            alert('WebSocket not connected. Please wait or try refreshing.');
        }
    }

    if (deviceSelectEl) {
        deviceSelectEl.addEventListener('change', () => {
            selectedDeviceId = deviceSelectEl.value || null;
            if (selectedDeviceIdEl) selectedDeviceIdEl.textContent = selectedDeviceId || 'None';
            if (statusTargetDeviceEl) statusTargetDeviceEl.textContent = selectedDeviceId || 'No Target';

            if (selectedDeviceId) {
                setControlsDisabled(false);
                // Request full system config for the selected device
                console.log('[DEBUG] Device selected. Sending "getSystemConfig" command.');
                sendCommand({ command: 'getSystemConfig' });

                // Initial placeholder display until config arrives
                const currentDeviceState = deviceStates[selectedDeviceId] || {};
                updateDeviceStatusDisplay(currentDeviceState); // Shows basic status if available
                updateUIToggleStates(currentDeviceState); // For random motion toggle
                if (timerListEl) timerListEl.innerHTML = '<p>Loading timers...</p>'; // Will be updated by systemConfig

                // Default button texts - will be updated by systemConfig or statusUpdate
                if (toggleCamStreamBtn) toggleCamStreamBtn.textContent = 'Start CAM Stream';
                if (toggleCamLedBtn) toggleCamLedBtn.textContent = 'Turn CAM LED ON';
                // Stream image will be set by systemConfig/statusUpdate. Initial src set here for immediate feedback.
                // if (streamImgEl) streamImgEl.src = `https://ebski.co/stream?t=${new Date().getTime()}`; // This might be too soon if stream isn't active
                // Set initial stream placeholder state for selected device (stream likely off)
                if (streamImgEl && streamPlaceholderTextEl) {
                    streamImgEl.style.display = 'none';
                    streamPlaceholderTextEl.style.display = 'block'; // or 'flex'
                }
            } else {
                setControlsDisabled(true);
                if (deviceStatusEl) deviceStatusEl.textContent = 'Waiting for updates...';
                updateUIToggleStates({}); // Clears random motion toggle text
                if (streamImgEl && streamPlaceholderTextEl) {
                     streamImgEl.src = "#";
                     streamImgEl.style.display = 'none';
                     streamPlaceholderTextEl.style.display = 'block'; // or 'flex'
                }
                if (timerListEl) timerListEl.innerHTML = ''; // Clear timers

                // Laser safety: If a device is deselected and laser timer was active, turn laser off
                if (laserOffTimeoutId) {
                    turnLaserOff();
                }

                // Clear CAM toggle timeouts and reset states
                if (streamToggleTimeoutId) { clearTimeout(streamToggleTimeoutId); streamToggleTimeoutId = null; }
                if (ledToggleTimeoutId) { clearTimeout(ledToggleTimeoutId); ledToggleTimeoutId = null; }
                if (toggleCamStreamBtn) {
                    // toggleCamStreamBtn.disabled = true; // Done by setControlsDisabled
                    toggleCamStreamBtn.textContent = 'Start CAM Stream';
                }
                if (toggleCamLedBtn) {
                    // toggleCamLedBtn.disabled = true; // Done by setControlsDisabled
                    toggleCamLedBtn.textContent = 'Turn CAM LED ON';
                }
                isStreamActive = false;
                isCamLedActive = false;
            }
        });
    }

    // Old servoXSliderEl and servoYSliderEl event listeners are removed as elements are gone.

    function turnLaserOff() {
        if (selectedDeviceId) { // Only send if a device is selected
            console.log('[DEBUG] Laser auto-off timer expired. Turning laser OFF.');
            sendCommand({ command: 'LASER_OFF' }); // Assuming this is the command
        }
        if (laserOffTimeoutId) {
            clearTimeout(laserOffTimeoutId);
            laserOffTimeoutId = null;
        }
    }

    function turnLaserOnWithTimeout() {
        if (!selectedDeviceId) return; // Don't try to turn laser on if no device selected

        console.log('[DEBUG] X/Y adjustment detected. Turning laser ON and resetting OFF timer.');
        sendCommand({ command: 'LASER_ON' }); // Assuming this is the command

        if (laserOffTimeoutId) {
            clearTimeout(laserOffTimeoutId);
        }
        laserOffTimeoutId = setTimeout(turnLaserOff, LASER_OFF_DELAY);
    }

    // Timer management
    if (addTimerBtn) {
        addTimerBtn.addEventListener('click', () => {
            const startTimeValue = timerStartTimeEl.value;
            const endTimeValue = timerEndTimeEl.value;

            console.log('[DEBUG] ADD_TIMER_BTN: Clicked. Start input:', startTimeValue, 'End input:', endTimeValue);

            if (!selectedDeviceId) { // Check if a device is selected
                alert('Please select a device first before adding a timer.');
                console.warn('[WARN] ADD_TIMER_BTN: No device selected. Command not sent.');
                return;
            }

            if (!startTimeValue || !endTimeValue) {
                alert('Please select both a start and end time for the timer.');
                console.warn('[WARN] ADD_TIMER_BTN: Empty start/end time. Command not sent.');
                return;
            }

            // New command object structure for addTimer as per user suggestion
            const addTimerValueCommandObject = {
                type: "command",
                targetDeviceId: selectedDeviceId, // Ensure selectedDeviceId is accessible
                command: "addTimer_value",       // New command name
                value: startTimeValue + ";" + endTimeValue // Use "value" as the key for the data string
            };

            if (socket && socket.readyState === WebSocket.OPEN) {
                const messagePayloadString = JSON.stringify(addTimerValueCommandObject);

                // Update log message to reflect the new structure
                console.log('[DEBUG] ADD_TIMER_BTN: Preparing to send stringified (addTimer_value with "value" key) payload:', messagePayloadString);
                console.log('[DEBUG] ADD_TIMER_BTN: WebSocket readyState before send:', socket.readyState);
                console.log('[DEBUG] ADD_TIMER_BTN: WebSocket bufferedAmount before send:', socket.bufferedAmount);

                try {
                    socket.send(messagePayloadString);
                    console.log('[DEBUG] ADD_TIMER_BTN: socket.send() EXECUTED for addTimer_value command.');
                } catch (e) {
                    console.error('[ERROR] ADD_TIMER_BTN: socket.send() FAILED with exception:', e);
                    alert('Failed to send addTimer_value command. Check console for errors.');
                }

                // Note: ESP32 should send back updated timer list via 'systemConfig' or 'scheduleUpdate'
                // after processing the addTimer_value command. Client relies on this update.

            } else {
                alert('WebSocket not connected or not open. Cannot send addTimer_value command.');
                console.error('[ERROR] ADD_TIMER_BTN: WebSocket not connected or not open. Current readyState:', socket ? socket.readyState : 'socket is null');
            }

            // Optional: Clear input fields after sending
            // timerStartTimeEl.value = '';
            // timerEndTimeEl.value = '';
            // ESP32 should send back updated timer list via 'systemConfig' or 'timerList'
        });
    }

    // Removed old CAM button event listeners (camStreamStartBtn, camStreamStopBtn, camLedOnBtn, camLedOffBtn)

    if (toggleCamStreamBtn) {
        toggleCamStreamBtn.addEventListener('click', () => {
            if (streamToggleTimeoutId) {
                console.log("Stream toggle cooling down");
                return;
            }
            if (isStreamActive) {
                sendCommand({ command: 'STOP_STREAM' });
                toggleCamStreamBtn.textContent = 'Start CAM Stream'; // Optimistic
                if (streamImgEl && streamPlaceholderTextEl) {
                    streamImgEl.src = "#";
                    streamImgEl.style.display = 'none';
                    streamPlaceholderTextEl.style.display = 'block'; // or 'flex'
                }
            } else {
                sendCommand({ command: 'START_STREAM' });
                toggleCamStreamBtn.textContent = 'Stop CAM Stream'; // Optimistic
                if (streamImgEl && streamPlaceholderTextEl) {
                    streamImgEl.src = `https://ebski.co/stream?t=${new Date().getTime()}`;
                    streamImgEl.style.display = 'block';
                    streamPlaceholderTextEl.style.display = 'none';
                }
            }
            isStreamActive = !isStreamActive; // Optimistic toggle
            toggleCamStreamBtn.disabled = true;
            streamToggleTimeoutId = setTimeout(() => {
                toggleCamStreamBtn.disabled = false;
                streamToggleTimeoutId = null;
                // Re-check actual state after timeout if status hasn't updated it
                // This might involve a getSystemConfig or relying on next statusUpdate
                // For now, the statusUpdate handler should correct the text if needed.
            }, CAM_TOGGLE_TIMEOUT);
        });
    }

    if (toggleCamLedBtn) {
        toggleCamLedBtn.addEventListener('click', () => {
            if (ledToggleTimeoutId) {
                console.log("LED toggle cooling down");
                return;
            }
            if (isCamLedActive) {
                sendCommand({ command: 'CAM_LED_OFF' });
                toggleCamLedBtn.textContent = 'Turn CAM LED ON'; // Optimistic
            } else {
                sendCommand({ command: 'CAM_LED_ON' });
                toggleCamLedBtn.textContent = 'Turn CAM LED OFF'; // Optimistic
            }
            isCamLedActive = !isCamLedActive; // Optimistic toggle
            toggleCamLedBtn.disabled = true;
            ledToggleTimeoutId = setTimeout(() => {
                toggleCamLedBtn.disabled = false;
                ledToggleTimeoutId = null;
            }, CAM_TOGGLE_TIMEOUT);
        });
    }

    if (randomMotionToggleBtn) {
        randomMotionToggleBtn.addEventListener('click', () => {
            const currentDeviceState = deviceStates[selectedDeviceId] || {};
            const newRandomMotionState = !currentDeviceState.random_motion_active;
            sendCommand({ command: 'RANDOM_MOTION_TOGGLE' });
            if (deviceStates[selectedDeviceId]) deviceStates[selectedDeviceId].random_motion_active = newRandomMotionState;
            updateUIToggleStates({ random_motion_active: newRandomMotionState });
        });
    }

    // Firmware Update Logic
    if (checkForUpdateBtn) {
        checkForUpdateBtn.addEventListener('click', async () => {
            if (!selectedDeviceId) {
                alert("Please select a device first.");
                return;
            }
            if (updateStatusEl) updateStatusEl.textContent = "Checking for updates...";

            // Fetch server version
            let serverVersion = -1;
            try {
                const response = await fetch('https://ebski.co/firmware/firmware.version');
                if (!response.ok) {
                    throw new Error(`Server returned status: ${response.status}`);
                }
                const versionText = await response.text();
                serverVersion = parseInt(versionText.trim());
            } catch (error) {
                if (updateStatusEl) updateStatusEl.textContent = `Error fetching server version: ${error.message}`;
                return;
            }

            // Get device version from its state
            const deviceState = deviceStates[selectedDeviceId] || {};
            const deviceVersion = deviceState.firmware_version;

            if (!deviceVersion) {
                 if (updateStatusEl) updateStatusEl.textContent = "Could not determine device version. It may not have reported yet.";
                 return;
            }

            // Update the UI with both versions
            updateFirmwareStatusUI(deviceVersion, serverVersion);
        });
    }

    if (performUpdateBtn) {
        performUpdateBtn.addEventListener('click', () => {
            if (!selectedDeviceId) {
                alert("Please select a device first.");
                return;
            }
            if (updateStatusEl) updateStatusEl.textContent = "Update command sent. Device will now update and reboot...";
            sendCommand({ command: 'http_ota_update' });
            performUpdateBtn.disabled = true; // Disable after clicking
        });
    }

    // Initialize
    setControlsDisabled(true);
    updateUIToggleStates({}); // Initialize button texts
    initializeStatusIndicators(); // Initialize indicators to default state


    /**
     * Updates the UI for a dual-range slider (both range inputs and number inputs)
     * and the corresponding main position slider's limits.
     * @param {object} elements - Object containing DOM elements for one axis.
     * @param {HTMLInputElement} elements.minRangeEl - Min range slider.
     * @param {HTMLInputElement} elements.maxRangeEl - Max range slider.
     * @param {HTMLSpanElement} elements.rangeSelectedEl - Span for visual selection.
     * @param {HTMLInputElement} elements.minValueEl - Min number input.
     * @param {HTMLInputElement} elements.maxValueEl - Max number input.
     * @param {number} newMin - The new minimum value for the range.
     * @param {number} newMax - The new maximum value for the range.
     * @param {number} overallSliderMin - The absolute minimum value this slider can represent.
     * @param {number} overallSliderMax - The absolute maximum value this slider can represent.
     */
    function updateDualRangeSliderUI(elements, newMin, newMax, overallSliderMin, overallSliderMax) {
        // Ensure overallSliderMin and overallSliderMax are numbers, default if not
        overallSliderMin = typeof overallSliderMin === 'number' ? overallSliderMin : 0;
        overallSliderMax = typeof overallSliderMax === 'number' ? overallSliderMax : 180; // Default to X/Y like range

        newMin = Math.max(overallSliderMin, Math.min(newMin, overallSliderMax - RANGE_MIN_DIFFERENCE));
        newMax = Math.min(overallSliderMax, Math.max(newMax, overallSliderMin + RANGE_MIN_DIFFERENCE));

        if (newMax - newMin < RANGE_MIN_DIFFERENCE) {
            // This logic might need refinement based on which input triggered the call,
            // but for now, it tries to maintain the difference.
            // If called from min thumb/input, adjust max. If from max, adjust min.
            // This simple check might not always be correct if called programmatically.
            if (document.activeElement && (document.activeElement === elements.minRangeEl || document.activeElement === elements.minValueEl) ) {
                 newMax = newMin + RANGE_MIN_DIFFERENCE;
            } else if (document.activeElement && (document.activeElement === elements.maxRangeEl || document.activeElement === elements.maxValueEl)) {
                 newMin = newMax - RANGE_MIN_DIFFERENCE;
            } else {
                // Default: if min changed more recently or is the one causing issue, adjust max
                // This is a heuristic. A more robust way would be to know which value is "leading".
                 newMax = newMin + RANGE_MIN_DIFFERENCE;
            }
            // Re-clamp after adjustment
            newMin = Math.max(overallSliderMin, Math.min(newMin, overallSliderMax - RANGE_MIN_DIFFERENCE));
            newMax = Math.min(overallSliderMax, Math.max(newMax, newMin + RANGE_MIN_DIFFERENCE)); // Ensure max is at least min + diff
        }

        elements.minRangeEl.value = newMin;
        elements.minValueEl.value = newMin;
        elements.maxRangeEl.value = newMax;
        elements.maxValueEl.value = newMax;

        const minPercent = (newMin / overallSliderMax) * 100; // Use overallSliderMax
        const maxPercent = (newMax / overallSliderMax) * 100; // Use overallSliderMax
        elements.rangeSelectedEl.style.left = `${minPercent}%`;
        elements.rangeSelectedEl.style.right = `${100 - maxPercent}%`;

        // Logic for updating positionSliderEl and positionValueEl has been removed.
    }


    // Setup X-Axis Dual Range Slider
    if (servoXMinRangeEl && servoXMaxRangeEl && servoXMinValueEl && servoXMaxValueEl && servoXRangeSelectedEl) { // servoXSliderEl and servoXValueEl removed
        const xElements = {
            minRangeEl: servoXMinRangeEl, maxRangeEl: servoXMaxRangeEl,
            rangeSelectedEl: servoXRangeSelectedEl,
            minValueEl: servoXMinValueEl, maxValueEl: servoXMaxValueEl
            // positionSliderEl and positionValueEl removed
        };

        [servoXMinRangeEl, servoXMaxRangeEl].forEach(input => {
            input.addEventListener('input', (e) => {
                let minVal = parseInt(servoXMinRangeEl.value);
                let maxVal = parseInt(servoXMaxRangeEl.value);

                if (maxVal - minVal < RANGE_MIN_DIFFERENCE) {
                    if (e.target.id === 'servoXMinRange') {
                        minVal = maxVal - RANGE_MIN_DIFFERENCE;
                         servoXMinRangeEl.value = minVal; // Adjust the input that would violate
                    } else {
                        maxVal = minVal + RANGE_MIN_DIFFERENCE;
                        servoXMaxRangeEl.value = maxVal; // Adjust
                    }
                }
                updateDualRangeSliderUI(xElements, minVal, maxVal, 0, 180);
                turnLaserOnWithTimeout(); // Call laser function on input
            });

            input.addEventListener('change', (e) => {
                const minVal = parseInt(servoXMinRangeEl.value);
                const maxVal = parseInt(servoXMaxRangeEl.value);
                // Send limit commands
                sendCommand({ command: 'setServoLimit', axis: 'x', limit_type: 'min', value: minVal });
                sendCommand({ command: 'setServoLimit', axis: 'x', limit_type: 'max', value: maxVal });

                // Move servo to the thumb that was adjusted
                if (e.target.id === servoXMinRangeEl.id) {
                    sendCommand({ command: 'servoX', value: minVal });
                } else if (e.target.id === servoXMaxRangeEl.id) {
                    sendCommand({ command: 'servoX', value: maxVal });
                }
            });
        });

        [servoXMinValueEl, servoXMaxValueEl].forEach(input => {
            input.addEventListener('change', (e) => {
                let minVal = parseInt(servoXMinValueEl.value);
                let maxVal = parseInt(servoXMaxValueEl.value);

                if (isNaN(minVal) || isNaN(maxVal)) return;
                minVal = Math.max(0, Math.min(minVal, 180 - RANGE_MIN_DIFFERENCE));
                maxVal = Math.min(180, Math.max(maxVal, 0 + RANGE_MIN_DIFFERENCE));

                if (maxVal - minVal < RANGE_MIN_DIFFERENCE) {
                    if (e.target.id === servoXMinValueEl.id) {
                        minVal = maxVal - RANGE_MIN_DIFFERENCE;
                    } else {
                        maxVal = minVal + RANGE_MIN_DIFFERENCE;
                    }
                    minVal = Math.max(0, Math.min(minVal, 180 - RANGE_MIN_DIFFERENCE));
                    maxVal = Math.min(180, Math.max(maxVal, 0 + RANGE_MIN_DIFFERENCE));
                }

                updateDualRangeSliderUI(xElements, minVal, maxVal, 0, 180);
                // Send limit commands
                sendCommand({ command: 'setServoLimit', axis: 'x', limit_type: 'min', value: minVal });
                sendCommand({ command: 'setServoLimit', axis: 'x', limit_type: 'max', value: maxVal });

                // Move servo to the value of the input that was changed
                if (e.target.id === servoXMinValueEl.id) {
                    sendCommand({ command: 'servoX', value: minVal });
                } else if (e.target.id === servoXMaxValueEl.id) {
                    sendCommand({ command: 'servoX', value: maxVal });
                }
                turnLaserOnWithTimeout(); // Call laser function on change
            });
        });
        // Initial UI setup for X from its default HTML values - REMOVED
        // Sliders will be initialized by systemConfig message after device selection.
    }

    // Setup Y-Axis Dual Range Slider
    if (servoYMinRangeEl && servoYMaxRangeEl && servoYMinValueEl && servoYMaxValueEl && servoYRangeSelectedEl) { // servoYSliderEl and servoYValueEl removed from condition
        const yElements = {
            minRangeEl: servoYMinRangeEl, maxRangeEl: servoYMaxRangeEl,
            rangeSelectedEl: servoYRangeSelectedEl,
            minValueEl: servoYMinValueEl, maxValueEl: servoYMaxValueEl
            // positionSliderEl and positionValueEl removed
        };

        [servoYMinRangeEl, servoYMaxRangeEl].forEach(input => {
            input.addEventListener('input', (e) => {
                let minVal = parseInt(servoYMinRangeEl.value);
                let maxVal = parseInt(servoYMaxRangeEl.value);

                if (maxVal - minVal < RANGE_MIN_DIFFERENCE) {
                    if (e.target.id === 'servoYMinRange') {
                        minVal = maxVal - RANGE_MIN_DIFFERENCE;
                        servoYMinRangeEl.value = minVal;
                    } else {
                        maxVal = minVal + RANGE_MIN_DIFFERENCE;
                        servoYMaxRangeEl.value = maxVal;
                    }
                }
                // For Y, use the specific 45-135 range for UI updates.
                updateDualRangeSliderUI(yElements, minVal, maxVal, 45, 135);
                turnLaserOnWithTimeout(); // Call laser function on input
            });

            input.addEventListener('change', (e) => {
                const minVal = parseInt(servoYMinRangeEl.value);
                const maxVal = parseInt(servoYMaxRangeEl.value);
                // Send limit commands
                sendCommand({ command: 'setServoLimit', axis: 'y', limit_type: 'min', value: minVal });
                sendCommand({ command: 'setServoLimit', axis: 'y', limit_type: 'max', value: maxVal });

                // Move servo to the thumb that was adjusted
                if (e.target.id === servoYMinRangeEl.id) {
                    sendCommand({ command: 'servoY', value: minVal });
                } else if (e.target.id === servoYMaxRangeEl.id) {
                    sendCommand({ command: 'servoY', value: maxVal });
                }
            });
        });

        [servoYMinValueEl, servoYMaxValueEl].forEach(input => {
            input.addEventListener('change', (e) => {
                let minVal = parseInt(servoYMinValueEl.value);
                let maxVal = parseInt(servoYMaxValueEl.value);

                if (isNaN(minVal) || isNaN(maxVal)) return;
                // Clamp to the Y-axis specific range 45-135
                minVal = Math.max(45, Math.min(minVal, 135 - RANGE_MIN_DIFFERENCE));
                maxVal = Math.min(135, Math.max(maxVal, 45 + RANGE_MIN_DIFFERENCE));

                if (maxVal - minVal < RANGE_MIN_DIFFERENCE) {
                    if (e.target.id === servoYMinValueEl.id) {
                        minVal = maxVal - RANGE_MIN_DIFFERENCE;
                    } else {
                        maxVal = minVal + RANGE_MIN_DIFFERENCE;
                    }
                    // Re-clamp after adjustment
                    minVal = Math.max(45, Math.min(minVal, 135 - RANGE_MIN_DIFFERENCE));
                    maxVal = Math.min(135, Math.max(maxVal, 45 + RANGE_MIN_DIFFERENCE));
                }

                updateDualRangeSliderUI(yElements, minVal, maxVal, 45, 135); // Y-axis specific range
                // Send limit commands
                sendCommand({ command: 'setServoLimit', axis: 'y', limit_type: 'min', value: minVal });
                sendCommand({ command: 'setServoLimit', axis: 'y', limit_type: 'max', value: maxVal });

                // Move servo to the value of the input that was changed
                if (e.target.id === servoYMinValueEl.id) {
                    sendCommand({ command: 'servoY', value: minVal });
                } else if (e.target.id === servoYMaxValueEl.id) {
                    sendCommand({ command: 'servoY', value: maxVal });
                }
                turnLaserOnWithTimeout(); // Call laser function on change
            });
        });
        // Initial UI setup for Y from its default HTML values - REMOVED
        // Sliders will be initialized by systemConfig message after device selection.
    }

    // Setup Velocity Dual Range Slider
    if (velMinRangeEl && velMaxRangeEl && velMinValueEl && velMaxValueEl && velRangeSelectedEl) {
        const velElements = {
            minRangeEl: velMinRangeEl, maxRangeEl: velMaxRangeEl,
            rangeSelectedEl: velRangeSelectedEl,
            minValueEl: velMinValueEl, maxValueEl: velMaxValueEl
        };
        const VEL_OVERALL_MIN = 0;
        const VEL_OVERALL_MAX = 3000;

        [velMinRangeEl, velMaxRangeEl].forEach(input => {
            input.addEventListener('input', (e) => {
                let minVal = parseInt(velMinRangeEl.value);
                let maxVal = parseInt(velMaxRangeEl.value);

                if (maxVal - minVal < RANGE_MIN_DIFFERENCE) { // Using global RANGE_MIN_DIFFERENCE, consider if Vel needs its own
                    if (e.target.id === velMinRangeEl.id) {
                        minVal = maxVal - RANGE_MIN_DIFFERENCE;
                        velMinRangeEl.value = minVal;
                    } else {
                        maxVal = minVal + RANGE_MIN_DIFFERENCE;
                        velMaxRangeEl.value = maxVal;
                    }
                }
                updateDualRangeSliderUI(velElements, minVal, maxVal, VEL_OVERALL_MIN, VEL_OVERALL_MAX);
            });

            input.addEventListener('change', (e) => {
                const minVal = parseInt(velMinRangeEl.value);
                const maxVal = parseInt(velMaxRangeEl.value);
                sendCommand({ command: 'setPreference', key: 'min_vel', value: minVal }); // Use snake_case
                sendCommand({ command: 'setPreference', key: 'max_vel', value: maxVal }); // Use snake_case
                // No direct servo movement for velocity changes
            });
        });

        [velMinValueEl, velMaxValueEl].forEach(input => {
            input.addEventListener('change', (e) => {
                let minVal = parseInt(velMinValueEl.value);
                let maxVal = parseInt(velMaxValueEl.value);

                if (isNaN(minVal) || isNaN(maxVal)) return;
                minVal = Math.max(VEL_OVERALL_MIN, Math.min(minVal, VEL_OVERALL_MAX - RANGE_MIN_DIFFERENCE));
                maxVal = Math.min(VEL_OVERALL_MAX, Math.max(maxVal, VEL_OVERALL_MIN + RANGE_MIN_DIFFERENCE));

                if (maxVal - minVal < RANGE_MIN_DIFFERENCE) {
                    if (e.target.id === velMinValueEl.id) {
                        minVal = maxVal - RANGE_MIN_DIFFERENCE;
                    } else {
                        maxVal = minVal + RANGE_MIN_DIFFERENCE;
                    }
                    minVal = Math.max(VEL_OVERALL_MIN, Math.min(minVal, VEL_OVERALL_MAX - RANGE_MIN_DIFFERENCE));
                    maxVal = Math.min(VEL_OVERALL_MAX, Math.max(maxVal, VEL_OVERALL_MIN + RANGE_MIN_DIFFERENCE));
                }

                updateDualRangeSliderUI(velElements, minVal, maxVal, VEL_OVERALL_MIN, VEL_OVERALL_MAX);
                sendCommand({ command: 'setPreference', key: 'min_vel', value: minVal }); // Use snake_case
                sendCommand({ command: 'setPreference', key: 'max_vel', value: maxVal }); // Use snake_case
            });
        });
        // Initial UI setup for Velocity from its default HTML values
        updateDualRangeSliderUI(velElements, parseInt(velMinRangeEl.value), parseInt(velMaxRangeEl.value), VEL_OVERALL_MIN, VEL_OVERALL_MAX);
    }


    /**
     * Sets up a collapsible section with localStorage persistence for its state.
     * @param {HTMLElement} toggleBtn - The button element that triggers the collapse/expand.
     * @param {HTMLElement} contentEl - The content element to be shown/hidden.
     * @param {string} localStorageKey - The key used to store the state in localStorage.
     */
    function setupCollapsibleSection(toggleBtn, contentEl, localStorageKey) {
        if (!toggleBtn || !contentEl) {
            console.warn(`Collapsible section setup skipped: Button or content element not found for key ${localStorageKey}`);
            return;
        }

        // Load saved state
        const savedState = localStorage.getItem(localStorageKey);
        if (savedState === 'expanded') {
            contentEl.style.display = 'block';
            toggleBtn.textContent = '-';
        } else {
            // Default to collapsed if not explicitly expanded or if key doesn't exist
            contentEl.style.display = 'none';
            toggleBtn.textContent = '+';
        }

        toggleBtn.addEventListener('click', () => {
            const isHidden = contentEl.style.display === 'none';
            if (isHidden) {
                contentEl.style.display = 'block';
                localStorage.setItem(localStorageKey, 'expanded');
                toggleBtn.textContent = '-';
            } else {
                contentEl.style.display = 'none';
                localStorage.setItem(localStorageKey, 'collapsed');
                toggleBtn.textContent = '+';
            }
        });
    }

    // Setup collapsible sections
    setupCollapsibleSection(toggleDeviceConfigBtn, deviceConfigContent, 'deviceConfigState'); // Updated IDs and key
    setupCollapsibleSection(toggleDeviceStatusBtn, deviceStatusContent, 'deviceStatusState');


    function populateTimezoneSelector() {
        if (!timezoneSelectEl) return;
        const timezones = [
            { name: "UTC", value: "UTC0" },
            { name: "London (GMT/BST)", value: "GMT0BST,M3.5.0/1,M10.5.0/2" },
            { name: "Berlin (CET/CEST)", value: "CET-1CEST,M3.5.0/2,M10.5.0/3" },
            { name: "New York (EST/EDT)", value: "EST5EDT,M3.2.0/2,M11.1.0/2" },
            { name: "Chicago (CST/CDT)", value: "CST6CDT,M3.2.0/2,M11.1.0/2" },
            { name: "Denver (MST/MDT)", value: "MST7MDT,M3.2.0/2,M11.1.0/2" },
            { name: "Los Angeles (PST/PDT)", value: "PST8PDT,M3.2.0/2,M11.1.0/2" },
            { name: "Tokyo (JST)", value: "JST-9" },
            { name: "Sydney (AEST/AEDT)", value: "AEST-10AEDT,M10.1.0/2,M4.1.0/3" }
            // Add more as needed, from a reliable source for POSIX TZ strings
        ];
        timezoneSelectEl.innerHTML = ''; // Clear loading/existing options
        timezones.forEach(tz => {
            const option = document.createElement('option');
            option.value = tz.value; // POSIX TZ String
            option.textContent = tz.name;
            timezoneSelectEl.appendChild(option);
        });
    }

    function populateNtpIntervalSelector() {
        if (!ntpIntervalSelectEl) return;
        const intervals = [
            { name: "1 Hour", value: 3600 },
            { name: "3 Hours", value: 10800 },
            { name: "6 Hours", value: 21600 },
            { name: "12 Hours", value: 43200 },
            { name: "24 Hours", value: 86400 },
        ];
        ntpIntervalSelectEl.innerHTML = ''; // Clear loading/existing options
        intervals.forEach(interval => {
            const option = document.createElement('option');
            option.value = interval.value;
            option.textContent = interval.name;
            ntpIntervalSelectEl.appendChild(option);
        });
    }

    populateTimezoneSelector();
    populateNtpIntervalSelector();

    if(timezoneSelectEl) {
        timezoneSelectEl.addEventListener('change', () => {
            if (!selectedDeviceId) {
                alert('Please select a device first.');
                const lastKnownState = deviceStates[selectedDeviceId];
                // Revert to last known POSIX string or default to 'UTC0'
                if (lastKnownState && lastKnownState.timezone_posix !== undefined) {
                    timezoneSelectEl.value = lastKnownState.timezone_posix;
                } else {
                    timezoneSelectEl.value = 'UTC0'; // Default POSIX string
                }
                return;
            }
            // Send POSIX string with key 'timezone_posix'
            sendCommand({ command: 'setPreference', key: 'timezone_posix', value: timezoneSelectEl.value });
        });
    }

    if(ntpIntervalSelectEl) {
        ntpIntervalSelectEl.addEventListener('change', () => {
            if (!selectedDeviceId) {
                alert('Please select a device first.');
                const lastKnownState = deviceStates[selectedDeviceId];
                if (lastKnownState && lastKnownState.ntp_interval !== undefined) {
                    ntpIntervalSelectEl.value = lastKnownState.ntp_interval / 1000; // Convert ms to s for select value
                } else {
                    ntpIntervalSelectEl.value = '3600'; // Default to 1 hour (3600s)
                }
                return;
            }
            // ESP32 expects preference key "ntp_interval" and value in milliseconds
            const valueInSeconds = parseInt(ntpIntervalSelectEl.value);
            sendCommand({ command: 'setPreference', key: 'ntp_interval', value: valueInSeconds * 1000 });
        });
    }

    connectWebSocket(); // Start WebSocket connection on page load

    /**
     * Initializes the status indicators to their default (inactive) state.
     */
    function initializeStatusIndicators() {
        if (wifiStrengthIndicatorEl) {
            const bars = wifiStrengthIndicatorEl.querySelectorAll('.wifi-bar');
            bars.forEach(bar => {
                bar.className = 'wifi-bar'; // Reset to default class
            });
        }
        if (laserStatusIndicatorEl) {
            laserStatusIndicatorEl.className = 'status-indicator status-light'; // Reset to default (Grey)
        }
        if (manualMovementIndicatorEl) {
            manualMovementIndicatorEl.className = 'status-indicator status-light'; // Reset to default (Grey)
        }
        if (scheduledWorkoutIndicatorEl) {
            scheduledWorkoutIndicatorEl.className = 'status-indicator status-light'; // Reset to default (Grey)
        }
    }

    /**
     * Updates the graphical status indicators based on the received data.
     * @param {object} data - The status data object from the ESP32.
     *                        Expected keys: wifi_rssi, laser_active, random_motion_active, is_scheduled_movement_active.
     */
    function updateStatusIndicators(data = {}) {
        // WiFi Strength Indicator (logic remains the same)
        if (wifiStrengthIndicatorEl && data.wifi_rssi !== undefined) {
            const rssi = parseInt(data.wifi_rssi);
            const bars = wifiStrengthIndicatorEl.querySelectorAll('.wifi-bar');
            bars.forEach(bar => bar.className = 'wifi-bar'); // Reset bars

            let numActiveBars = 0;
            if (rssi >= -55) numActiveBars = 4;
            else if (rssi >= -67) numActiveBars = 3;
            else if (rssi >= -80) numActiveBars = 2;
            else if (rssi < -80 && rssi !== 0) numActiveBars = 1;

            for (let i = 0; i < bars.length; i++) {
                if (i < numActiveBars) {
                    bars[i].classList.add('active');
                } else {
                    bars[i].classList.remove('active');
                }
            }
        }

        // Laser Status Indicator (Red for ON, Grey for OFF)
        if (laserStatusIndicatorEl && data.laser_active !== undefined) {
            if (data.laser_active) {
                laserStatusIndicatorEl.classList.add('laser-on'); // Red
                laserStatusIndicatorEl.classList.remove('active'); // Ensure no green
            } else {
                laserStatusIndicatorEl.classList.remove('laser-on'); // Grey
                laserStatusIndicatorEl.classList.remove('active');
            }
        }

        // Manual Movement Indicator (Green for ACTIVE, Grey for INACTIVE)
        if (manualMovementIndicatorEl && data.random_motion_active !== undefined) {
            if (data.random_motion_active) {
                manualMovementIndicatorEl.classList.add('active'); // Green
                manualMovementIndicatorEl.classList.remove('laser-on'); // Ensure no red
            } else {
                manualMovementIndicatorEl.classList.remove('active'); // Grey
                manualMovementIndicatorEl.classList.remove('laser-on');
            }
        }

        // Scheduled Workout Indicator (Green for ACTIVE, Grey for INACTIVE)
        if (scheduledWorkoutIndicatorEl && data.is_scheduled_movement_active !== undefined) {
            if (data.is_scheduled_movement_active) {
                scheduledWorkoutIndicatorEl.classList.add('active'); // Green
                scheduledWorkoutIndicatorEl.classList.remove('laser-on'); // Ensure no red
            } else {
                scheduledWorkoutIndicatorEl.classList.remove('active'); // Grey
                scheduledWorkoutIndicatorEl.classList.remove('laser-on');
            }
        }
    }
});
