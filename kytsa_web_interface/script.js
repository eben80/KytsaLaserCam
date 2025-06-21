document.addEventListener('DOMContentLoaded', () => {
    const wsStatusEl = document.getElementById('ws-status');
    const deviceSelectEl = document.getElementById('deviceSelect');
    const selectedDeviceIdEl = document.getElementById('selected-device-id');
    const deviceStatusEl = document.getElementById('deviceStatus');
    const streamImgEl = document.getElementById('stream');
    const controlTargetDeviceEl = document.getElementById('control-target-device');
    const statusTargetDeviceEl = document.getElementById('status-target-device');

    // X-Axis Dual Range Slider Elements
    const servoXMinRangeEl = document.getElementById('servoXMinRange');
    const servoXMaxRangeEl = document.getElementById('servoXMaxRange');
    const servoXRangeSelectedEl = document.getElementById('servoXRangeSelected');
    const servoXMinValueEl = document.getElementById('servoXMinValue');
    const servoXMaxValueEl = document.getElementById('servoXMaxValue');

    // Y-Axis Dual Range Slider Elements
    const servoYMinRangeEl = document.getElementById('servoYMinRange');
    const servoYMaxRangeEl = document.getElementById('servoYMaxRange');
    const servoYRangeSelectedEl = document.getElementById('servoYRangeSelected');
    const servoYMinValueEl = document.getElementById('servoYMinValue');
    const servoYMaxValueEl = document.getElementById('servoYMaxValue');

    const randomMotionToggleBtn = document.getElementById('randomMotionToggle');

    // Timer controls
    const timerStartTimeEl = document.getElementById('timerStartTime');
    const timerEndTimeEl = document.getElementById('timerEndTime');
    const addTimerBtn = document.getElementById('addTimerBtn');
    const timerListEl = document.getElementById('timerList');

    // CAM control buttons (consolidated)
    const toggleCamStreamBtn = document.getElementById('toggleCamStreamBtn');
    const toggleCamLedBtn = document.getElementById('toggleCamLedBtn');

    // Collapsible section elements
    const toggleServoConfigBtn = document.getElementById('toggleServoConfigBtn');
    const servoConfigContent = document.getElementById('servoConfigContent');
    const toggleDeviceStatusBtn = document.getElementById('toggleDeviceStatusBtn');
    const deviceStatusContent = document.getElementById('deviceStatusContent');

    const allControls = [
        // servoXSliderEl, servoYSliderEl, // Main position sliders REMOVED
        servoXMinRangeEl, servoXMaxRangeEl, servoXMinValueEl, servoXMaxValueEl, // X-axis range controls
        servoYMinRangeEl, servoYMaxRangeEl, servoYMinValueEl, servoYMaxValueEl, // Y-axis range controls
        timerStartTimeEl, timerEndTimeEl, addTimerBtn,
        randomMotionToggleBtn, toggleCamStreamBtn, toggleCamLedBtn
    ];

    const RANGE_MIN_DIFFERENCE = 10; // Minimum difference between min and max thumbs of a range slider

    /** @type {WebSocket | null} The main WebSocket connection instance. */
    let socket;
    /** @type {string | null} The device ID of the currently selected ESP32 device. */
    let selectedDeviceId = null;
    /** @type {Object<string, Object>} Stores the last known state for each device. deviceId -> { stateKey: stateValue }. */
    let deviceStates = {}; // { deviceId: { key: value } }

    // State variables and timeouts for CAM controls
    let isStreamActive = false;
    let streamToggleTimeoutId = null;
    let isCamLedActive = false;
    let ledToggleTimeoutId = null;
    const CAM_TOGGLE_TIMEOUT = 2000; // 2 seconds

    /**
     * Enables or disables all control elements on the page.
     * Also updates the text indicating which device is being controlled.
     * @param {boolean} disabled - True to disable controls, false to enable.
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
     * Establishes a WebSocket connection to the server.
     * Sets up event handlers for open, message, close, and error events.
     * Attempts to reconnect on close or error with a randomized delay.
     */
    function connectWebSocket() {
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        // Assuming Nginx is on the same host, proxying /ws
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

                        // Update CAM states from statusUpdate
                        if (message.data.esp32cam_streaming !== undefined) {
                            isStreamActive = message.data.esp32cam_streaming;
                            if (toggleCamStreamBtn) toggleCamStreamBtn.textContent = isStreamActive ? 'Stop CAM Stream' : 'Start CAM Stream';
                            // Update stream image source based on actual streaming state
                            if (isStreamActive && streamImgEl) {
                                streamImgEl.src = `https://ebski.co/stream?t=${new Date().getTime()}`;
                            } else if (streamImgEl) {
                                streamImgEl.src = "#";
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
                        if (message.config.minX !== undefined && message.config.maxX !== undefined) {
                            const xElements = { // Definition updated: no positionSliderEl, positionValueEl
                                minRangeEl: servoXMinRangeEl, maxRangeEl: servoXMaxRangeEl,
                                rangeSelectedEl: servoXRangeSelectedEl,
                                minValueEl: servoXMinValueEl, maxValueEl: servoXMaxValueEl
                            };
                            updateDualRangeSliderUI(xElements, parseInt(message.config.minX), parseInt(message.config.maxX), 'X');
                        }
                        // Update X position slider value - REMOVED as slider is gone
                        // if (servoXSliderEl && servoXValueEl && message.config.servoX_pos !== undefined) {
                        //     servoXSliderEl.value = message.config.servoX_pos;
                        //     servoXValueEl.textContent = servoXSliderEl.value;
                        // } else if (servoXSliderEl && servoXValueEl) {
                        //      servoXValueEl.textContent = servoXSliderEl.value;
                        // }

                        // Update Y-Axis from systemConfig
                        if (message.config.minY !== undefined && message.config.maxY !== undefined) {
                            const yElements = { // Definition updated: no positionSliderEl, positionValueEl
                                minRangeEl: servoYMinRangeEl, maxRangeEl: servoYMaxRangeEl,
                                rangeSelectedEl: servoYRangeSelectedEl,
                                minValueEl: servoYMinValueEl, maxValueEl: servoYMaxValueEl
                            };
                            updateDualRangeSliderUI(yElements, parseInt(message.config.minY), parseInt(message.config.maxY), 'Y');
                        }
                        // Update Y position slider value - REMOVED as slider is gone
                        // if (servoYSliderEl && servoYValueEl && message.config.servoY_pos !== undefined) {
                        //     servoYSliderEl.value = message.config.servoY_pos;
                        //     servoYValueEl.textContent = servoYSliderEl.value;
                        // } else if (servoYSliderEl && servoYValueEl) {
                        //     servoYValueEl.textContent = servoYSliderEl.value;
                        // }

                        // Update CAM button states if present in systemConfig.config
                        // ESP32's sendSystemConfig currently does not include these.
                        // if (message.config.hasOwnProperty('cam_led_active')) {
                        //     isCamLedActive = message.config.cam_led_active;
                        //     if(toggleCamLedBtn) toggleCamLedBtn.textContent = isCamLedActive ? 'Turn CAM LED OFF' : 'Turn CAM LED ON';
                        // }
                        // if (message.config.hasOwnProperty('esp32cam_streaming')) {
                        //     isStreamActive = message.config.esp32cam_streaming;
                        //     if(toggleCamStreamBtn) toggleCamStreamBtn.textContent = isStreamActive ? 'Stop CAM Stream' : 'Start CAM Stream';
                        //     if (isStreamActive && streamImgEl) {
                        //        streamImgEl.src = `https://ebski.co/stream?t=${new Date().getTime()}`;
                        //     } else if (streamImgEl) {
                        //        streamImgEl.src = "#";
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
            if (streamImgEl) streamImgEl.src = "#";


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
            } else {
                setControlsDisabled(true);
                if (deviceStatusEl) deviceStatusEl.textContent = 'Waiting for updates...';
                updateUIToggleStates({}); // Clears random motion toggle text
                if (streamImgEl) streamImgEl.src = "#";
                if (timerListEl) timerListEl.innerHTML = ''; // Clear timers

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
                if (streamImgEl) streamImgEl.src = "#";
            } else {
                sendCommand({ command: 'START_STREAM' });
                toggleCamStreamBtn.textContent = 'Stop CAM Stream'; // Optimistic
                if (streamImgEl) streamImgEl.src = `https://ebski.co/stream?t=${new Date().getTime()}`;
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

    // Initialize
    setControlsDisabled(true);
    updateUIToggleStates({}); // Initialize button texts


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
     * @param {string} axisPrefix - 'X' or 'Y' for overall slider limits (e.g., 0-180 for X).
     */
    function updateDualRangeSliderUI(elements, newMin, newMax, axisPrefix) {
        const overallMin = (axisPrefix === 'X') ? 0 : 0;
        const overallMax = (axisPrefix === 'X') ? 180 : 180;

        newMin = Math.max(overallMin, Math.min(newMin, overallMax - RANGE_MIN_DIFFERENCE));
        newMax = Math.min(overallMax, Math.max(newMax, overallMin + RANGE_MIN_DIFFERENCE));

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
            newMin = Math.max(overallMin, Math.min(newMin, overallMax - RANGE_MIN_DIFFERENCE));
            newMax = Math.min(overallMax, Math.max(newMax, newMin + RANGE_MIN_DIFFERENCE)); // Ensure max is at least min + diff
        }

        elements.minRangeEl.value = newMin;
        elements.minValueEl.value = newMin;
        elements.maxRangeEl.value = newMax;
        elements.maxValueEl.value = newMax;

        const minPercent = (newMin / overallMax) * 100;
        const maxPercent = (newMax / overallMax) * 100;
        elements.rangeSelectedEl.style.left = `${minPercent}%`;
        elements.rangeSelectedEl.style.right = `${100 - maxPercent}%`;

        // Logic for updating positionSliderEl and positionValueEl has been removed.
    }


    // Setup X-Axis Dual Range Slider
    if (servoXMinRangeEl && servoXMaxRangeEl && servoXMinValueEl && servoXMaxValueEl && servoXRangeSelectedEl && servoXSliderEl && servoXValueEl) {
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
                updateDualRangeSliderUI(xElements, minVal, maxVal, 'X');
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

                updateDualRangeSliderUI(xElements, minVal, maxVal, 'X');
                // Send limit commands
                sendCommand({ command: 'setServoLimit', axis: 'x', limit_type: 'min', value: minVal });
                sendCommand({ command: 'setServoLimit', axis: 'x', limit_type: 'max', value: maxVal });

                // Move servo to the value of the input that was changed
                if (e.target.id === servoXMinValueEl.id) {
                    sendCommand({ command: 'servoX', value: minVal });
                } else if (e.target.id === servoXMaxValueEl.id) {
                    sendCommand({ command: 'servoX', value: maxVal });
                }
            });
        });
        // Initial UI setup for X from its default HTML values
        updateDualRangeSliderUI(xElements, parseInt(servoXMinRangeEl.value), parseInt(servoXMaxRangeEl.value), 'X');
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
                // For Y, let's ensure the values stay broadly within a 0-180 context for the slider mechanism,
                // but the actual effective limits (like 45-135) will be from initial values or systemConfig.
                updateDualRangeSliderUI(yElements, minVal, maxVal, 'Y');
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
                minVal = Math.max(0, Math.min(minVal, 180 - RANGE_MIN_DIFFERENCE));
                maxVal = Math.min(180, Math.max(maxVal, 0 + RANGE_MIN_DIFFERENCE));

                if (maxVal - minVal < RANGE_MIN_DIFFERENCE) {
                    if (e.target.id === servoYMinValueEl.id) {
                        minVal = maxVal - RANGE_MIN_DIFFERENCE;
                    } else {
                        maxVal = minVal + RANGE_MIN_DIFFERENCE;
                    }
                    minVal = Math.max(0, Math.min(minVal, 180 - RANGE_MIN_DIFFERENCE));
                    maxVal = Math.min(180, Math.max(maxVal, 0 + RANGE_MIN_DIFFERENCE));
                }

                updateDualRangeSliderUI(yElements, minVal, maxVal, 'Y');
                // Send limit commands
                sendCommand({ command: 'setServoLimit', axis: 'y', limit_type: 'min', value: minVal });
                sendCommand({ command: 'setServoLimit', axis: 'y', limit_type: 'max', value: maxVal });

                // Move servo to the value of the input that was changed
                if (e.target.id === servoYMinValueEl.id) {
                    sendCommand({ command: 'servoY', value: minVal });
                } else if (e.target.id === servoYMaxValueEl.id) {
                    sendCommand({ command: 'servoY', value: maxVal });
                }
            });
        });
        // Initial UI setup for Y from its default HTML values
        updateDualRangeSliderUI(yElements, parseInt(servoYMinRangeEl.value), parseInt(servoYMaxRangeEl.value), 'Y');
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
    setupCollapsibleSection(toggleServoConfigBtn, servoConfigContent, 'servoConfigState');
    setupCollapsibleSection(toggleDeviceStatusBtn, deviceStatusContent, 'deviceStatusState');

    connectWebSocket(); // Start WebSocket connection on page load
});
