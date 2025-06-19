document.addEventListener('DOMContentLoaded', () => {
    const wsStatusEl = document.getElementById('ws-status');
    const deviceSelectEl = document.getElementById('deviceSelect');
    const selectedDeviceIdEl = document.getElementById('selected-device-id');
    const deviceStatusEl = document.getElementById('deviceStatus');
    const streamImgEl = document.getElementById('stream');
    const controlTargetDeviceEl = document.getElementById('control-target-device');
    const statusTargetDeviceEl = document.getElementById('status-target-device');

    const servoXSliderEl = document.getElementById('servoX');
    const servoXValueEl = document.getElementById('servoXValue');
    const servoYSliderEl = document.getElementById('servoY');
    const servoYValueEl = document.getElementById('servoYValue');

    // Axis configuration buttons
    const setXMinBtn = document.getElementById('setXMinBtn');
    const setXMaxBtn = document.getElementById('setXMaxBtn');
    const setYMinBtn = document.getElementById('setYMinBtn');
    const setYMaxBtn = document.getElementById('setYMaxBtn');

    const randomMotionToggleBtn = document.getElementById('randomMotionToggle');

    // Timer controls
    const timerStartTimeEl = document.getElementById('timerStartTime');
    const timerEndTimeEl = document.getElementById('timerEndTime');
    const addTimerBtn = document.getElementById('addTimerBtn');
    const timerListEl = document.getElementById('timerList');

    const camStreamStartBtn = document.getElementById('camStreamStart');
    const camStreamStopBtn = document.getElementById('camStreamStop');
    const camLedOnBtn = document.getElementById('camLedOn');
    const camLedOffBtn = document.getElementById('camLedOff');

    const allControls = [
        servoXSliderEl, servoYSliderEl,
        setXMinBtn, setXMaxBtn, setYMinBtn, setYMaxBtn, // New axis buttons
        timerStartTimeEl, timerEndTimeEl, addTimerBtn, // New timer controls
        randomMotionToggleBtn, camStreamStartBtn, camStreamStopBtn,
        camLedOnBtn, camLedOffBtn
    ];

    /** @type {WebSocket | null} The main WebSocket connection instance. */
    let socket;
    /** @type {string | null} The device ID of the currently selected ESP32 device. */
    let selectedDeviceId = null;
    /** @type {Object<string, Object>} Stores the last known state for each device. deviceId -> { stateKey: stateValue }. */
    let deviceStates = {}; // { deviceId: { key: value } }

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
            if (wsStatusEl) {
                wsStatusEl.textContent = 'Connected';
                wsStatusEl.className = 'connected';
            }
            console.log('WebSocket connected');
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
            console.log('Message from server:', event.data);
            let message;
            try {
                message = JSON.parse(event.data);
            } catch (e) {
                console.error('Failed to parse JSON message from server:', event.data, e);
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
                        updateUIToggleStates(message.data); // Handles random motion, could update servo limits if included
                        // If statusUpdate includes servo limits, update them (optional redundancy)
                        if (message.data.minX !== undefined) servoXSliderEl.min = message.data.minX;
                        if (message.data.maxX !== undefined) servoXSliderEl.max = message.data.maxX;
                        if (message.data.minY !== undefined) servoYSliderEl.min = message.data.minY;
                        if (message.data.maxY !== undefined) servoYSliderEl.max = message.data.maxY;
                    }
                    break;
                case 'systemConfig': // New message type for initial config
                    if (message.deviceId === selectedDeviceId) {
                        console.log("Received systemConfig:", message.config);
                        const config = message.config;
                        if (config.minX !== undefined) servoXSliderEl.min = config.minX;
                        if (config.maxX !== undefined) servoXSliderEl.max = config.maxX;
                        if (config.minY !== undefined) servoYSliderEl.min = config.minY;
                        if (config.maxY !== undefined) servoYSliderEl.max = config.maxY;

                        // Update slider values if they are outside new limits
                        if (parseInt(servoXSliderEl.value) < config.minX) servoXSliderEl.value = config.minX;
                        if (parseInt(servoXSliderEl.value) > config.maxX) servoXSliderEl.value = config.maxX;
                        if (servoXValueEl) servoXValueEl.textContent = servoXSliderEl.value;

                        if (parseInt(servoYSliderEl.value) < config.minY) servoYSliderEl.value = config.minY;
                        if (parseInt(servoYSliderEl.value) > config.maxY) servoYSliderEl.value = config.maxY;
                        if (servoYValueEl) servoYValueEl.textContent = servoYSliderEl.value;

                        if (config.timers) {
                            displayTimers(config.timers);
                        }
                    }
                    break;
                case 'timerList': // Message type for timer updates
                case 'scheduleUpdate': // ESP32 might send this after add/delete timer
                     if (message.deviceId === selectedDeviceId && message.timers) {
                        displayTimers(message.timers);
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
            console.log('WebSocket disconnected. Reason:', event.reason, 'Code:', event.code);
            setControlsDisabled(true);
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
            console.error('WebSocket error:', error);
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
    function displayTimers(timers = []) {
        if (!timerListEl) return;
        timerListEl.innerHTML = ''; // Clear existing timers

        if (!timers || timers.length === 0) {
            timerListEl.innerHTML = '<p>No timers scheduled.</p>';
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
        if (servoXSliderEl && data.servoX_pos !== undefined) {
            servoXSliderEl.value = data.servoX_pos;
            if (servoXValueEl) servoXValueEl.textContent = data.servoX_pos;
        }
        if (servoYSliderEl && data.servoY_pos !== undefined) {
            servoYSliderEl.value = data.servoY_pos;
            if (servoYValueEl) servoYValueEl.textContent = data.servoY_pos;
        }
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
            const message = {
                type: 'command',
                targetDeviceId: selectedDeviceId,
                ...commandData
            };
            socket.send(JSON.stringify(message));
            console.log('Sent command:', message);
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
                sendCommand({ command: 'getSystemConfig' });

                // Initial placeholder display until config arrives
                const currentDeviceState = deviceStates[selectedDeviceId] || {};
                updateDeviceStatusDisplay(currentDeviceState); // Shows basic status if available
                updateUIToggleStates(currentDeviceState); // For random motion toggle
                if (timerListEl) timerListEl.innerHTML = '<p>Loading timers...</p>';


                if (streamImgEl) streamImgEl.src = `https://ebski.co/stream?t=${new Date().getTime()}`;
            } else {
                setControlsDisabled(true);
                if (deviceStatusEl) deviceStatusEl.textContent = 'Waiting for updates...';
                updateUIToggleStates({});
                if (streamImgEl) streamImgEl.src = "#";
                if (timerListEl) timerListEl.innerHTML = ''; // Clear timers when no device selected
            }
        });
    }

    if (servoXSliderEl) {
        servoXSliderEl.addEventListener('input', () => { if (servoXValueEl) servoXValueEl.textContent = servoXSliderEl.value; });
        servoXSliderEl.addEventListener('change', () => sendCommand({ command: 'servoX', value: parseInt(servoXSliderEl.value) }));
    }
    if (servoYSliderEl) {
        servoYSliderEl.addEventListener('input', () => { if (servoYValueEl) servoYValueEl.textContent = servoYSliderEl.value; });
        servoYSliderEl.addEventListener('change', () => sendCommand({ command: 'servoY', value: parseInt(servoYSliderEl.value) }));
    }

    // Axis limit setting buttons
    if (setXMinBtn) {
        setXMinBtn.addEventListener('click', () => {
            sendCommand({ command: 'setServoLimit', axis: 'x', limit_type: 'min', value: parseInt(servoXSliderEl.value) });
        });
    }
    if (setXMaxBtn) {
        setXMaxBtn.addEventListener('click', () => {
            sendCommand({ command: 'setServoLimit', axis: 'x', limit_type: 'max', value: parseInt(servoXSliderEl.value) });
        });
    }
    if (setYMinBtn) {
        setYMinBtn.addEventListener('click', () => {
            sendCommand({ command: 'setServoLimit', axis: 'y', limit_type: 'min', value: parseInt(servoYSliderEl.value) });
        });
    }
    if (setYMaxBtn) {
        setYMaxBtn.addEventListener('click', () => {
            sendCommand({ command: 'setServoLimit', axis: 'y', limit_type: 'max', value: parseInt(servoYSliderEl.value) });
        });
    }

    // Timer management
    if (addTimerBtn) {
        addTimerBtn.addEventListener('click', () => {
            const startTime = timerStartTimeEl.value;
            const endTime = timerEndTimeEl.value;
            if (!startTime || !endTime) {
                alert('Please select both a start and end time for the timer.');
                return;
            }
            // Basic validation: end time after start time (can be more complex if spanning midnight)
            // For now, sending to ESP32 for more robust validation.
            sendCommand({ command: 'addTimer', startTime: startTime, endTime: endTime });
            // Clear input fields after attempting to add
            // timerStartTimeEl.value = '';
            // timerEndTimeEl.value = '';
            // ESP32 should send back updated timer list via 'systemConfig' or 'timerList'
        });
    }

    // Removed laserToggleBtn and relayToggleBtn event listeners

    if (randomMotionToggleBtn) {
        randomMotionToggleBtn.addEventListener('click', () => {
             // Toggle based on current known state to provide immediate UI feedback (optional)
            const currentDeviceState = deviceStates[selectedDeviceId] || {};
            const newRandomMotionState = !currentDeviceState.random_motion_active;
            sendCommand({ command: 'RANDOM_MOTION_TOGGLE' }); // ESP32 handles actual toggle
            // Optimistically update UI, will be corrected by statusUpdate if needed
            if (deviceStates[selectedDeviceId]) deviceStates[selectedDeviceId].random_motion_active = newRandomMotionState;
            updateUIToggleStates({ random_motion_active: newRandomMotionState });
        });
    }
    if (camStreamStartBtn) {
        camStreamStartBtn.addEventListener('click', () => {
            sendCommand({ command: 'START_STREAM' });
            // src is already set in deviceSelect 'change' handler, but this ensures it if called independently
            if (streamImgEl) streamImgEl.src = `https://ebski.co/stream?t=${new Date().getTime()}`;
        });
    }
    if (camStreamStopBtn) {
        camStreamStopBtn.addEventListener('click', () => {
            sendCommand({ command: 'STOP_STREAM' });
            if (streamImgEl) streamImgEl.src = "#"; // Set to placeholder or hash
        });
    }
    if (camLedOnBtn) {
        camLedOnBtn.addEventListener('click', () => sendCommand({ command: 'CAM_LED_ON' }));
    }
    if (camLedOffBtn) {
        camLedOffBtn.addEventListener('click', () => sendCommand({ command: 'CAM_LED_OFF' }));
    }

    // Initialize
    setControlsDisabled(true);
    updateUIToggleStates({}); // Initialize button texts
    connectWebSocket(); // Start WebSocket connection on page load
});
