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

    const laserToggleBtn = document.getElementById('laserToggle');
    const relayToggleBtn = document.getElementById('relayToggle');
    const randomMotionToggleBtn = document.getElementById('randomMotionToggle');

    const camStreamStartBtn = document.getElementById('camStreamStart');
    const camStreamStopBtn = document.getElementById('camStreamStop');
    const camLedOnBtn = document.getElementById('camLedOn');
    const camLedOffBtn = document.getElementById('camLedOff');

    const allControls = [
        servoXSliderEl, servoYSliderEl, laserToggleBtn, relayToggleBtn,
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
                        updateUIToggleStates(message.data);
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
        if (laserToggleBtn) laserToggleBtn.textContent = `Laser (${stateData.laser_active ? "ON" : "OFF"})`;
        if (relayToggleBtn) relayToggleBtn.textContent = `Relay (${stateData.relay_active ? "ON" : "OFF"})`;
        if (randomMotionToggleBtn) randomMotionToggleBtn.textContent = `Random Motion (${stateData.random_motion_active ? "ON" : "OFF"})`;
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
                const currentDeviceState = deviceStates[selectedDeviceId] || {};
                updateDeviceStatusDisplay(currentDeviceState);
                updateUIToggleStates(currentDeviceState);
                if (streamImgEl) streamImgEl.src = `https://ebski.co/stream?t=${new Date().getTime()}`;
            } else {
                setControlsDisabled(true);
                if (deviceStatusEl) deviceStatusEl.textContent = 'Waiting for updates...';
                updateUIToggleStates({});
                if (streamImgEl) streamImgEl.src = "#";
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
    if (laserToggleBtn) {
        laserToggleBtn.addEventListener('click', () => {
            const currentState = deviceStates[selectedDeviceId] ? deviceStates[selectedDeviceId].laser_active : false;
            sendCommand({ command: currentState ? 'LASER_OFF' : 'LASER_ON' });
        });
    }
    if (relayToggleBtn) {
        relayToggleBtn.addEventListener('click', () => {
            const currentState = deviceStates[selectedDeviceId] ? deviceStates[selectedDeviceId].relay_active : false;
            sendCommand({ command: currentState ? 'RELAY_OFF' : 'RELAY_ON' });
        });
    }
    if (randomMotionToggleBtn) {
        randomMotionToggleBtn.addEventListener('click', () => sendCommand({ command: 'RANDOM_MOTION_TOGGLE' }));
    }
    if (camStreamStartBtn) {
        camStreamStartBtn.addEventListener('click', () => {
            sendCommand({ command: 'START_STREAM' });
            if (streamImgEl) streamImgEl.src = `https://ebski.co/stream?t=${new Date().getTime()}`;
        });
    }
    if (camStreamStopBtn) {
        camStreamStopBtn.addEventListener('click', () => {
            sendCommand({ command: 'STOP_STREAM' });
            if (streamImgEl) streamImgEl.src = "#";
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
