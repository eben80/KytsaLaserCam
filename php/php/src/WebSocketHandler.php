<?php
// Location: php/php/src/WebSocketHandler.php
namespace MyApp;

use Ratchet\MessageComponentInterface;
use Ratchet\ConnectionInterface;

/**
 * Handles WebSocket communication for the ESP32 remote control application.
 * Manages connections from ESP32 devices and Web UI clients, facilitating
 * message exchange between them.
 */
class WebSocketHandler implements MessageComponentInterface {
    /** @var \SplObjectStorage<ConnectionInterface, mixed> A collection of all active client connections. */
    protected $clients;
    /** @var array<int, string> An associative array mapping connection resource IDs to ESP32 device IDs. resourceId => deviceId */
    protected $esp32Devices;
    /** @var \SplObjectStorage<ConnectionInterface, mixed> A collection of active Web UI client connections. */
    protected $webUIClients;
    /** @var array<int, int> An associative array mapping connection resource IDs to user IDs. resourceId => userId */
    protected $userConnections;

    /**
     * Constructor. Initializes client storage.
     */
    public function __construct() {
        $this->clients = new \SplObjectStorage;
        $this->esp32Devices = [];
        $this->webUIClients = new \SplObjectStorage;
        $this->userConnections = [];
        echo "WebSocketHandler Instantiated\n";
    }

    /**
     * Called when a new client connection is opened.
     * Adds the new connection to the list of clients.
     *
     * @param ConnectionInterface $conn The new connection object.
     */
    public function onOpen(ConnectionInterface $conn) {
        $this->clients->attach($conn);
        echo "New connection! ({$conn->resourceId})\n";
    }

    /**
     * Called when a message is received from a client.
     * Processes the message based on its type (pairing, webClientInit, statusUpdate, command).
     *
     * @param ConnectionInterface $from The connection from which the message was received.
     * @param string $msg The message received from the client (expected to be JSON).
     */
    public function onMessage(ConnectionInterface $from, $msg) {
        $numRecv = count($this->clients) - 1;
        echo sprintf('Connection %d sending message "%s" to %d other connection%s' . "\n", $from->resourceId, $msg, $numRecv, $numRecv == 1 ? '' : 's');

        $data = json_decode($msg, true);

        if (isset($data['type'])) {
            switch ($data['type']) {
                case 'pairing':
                    if (isset($data['deviceId'])) {
                        $newDeviceId = $data['deviceId'];
                        $newResourceId = $from->resourceId;

                        // Check if this deviceId was already paired with a different connection
                        foreach ($this->esp32Devices as $oldResourceId => $existingDeviceId) {
                            if ($existingDeviceId === $newDeviceId && $oldResourceId !== $newResourceId) {
                                // DeviceId is attempting to pair on a new connection.
                                // Remove the old connection's pairing for this deviceId.
                                unset($this->esp32Devices[$oldResourceId]);
                                echo "Device {$newDeviceId} re-paired. Old connection {$oldResourceId} association removed.\n";
                                // Optional: Could try to close the old connection if it's still in $this->clients
                                // foreach ($this->clients as $client) {
                                //    if ($client->resourceId == $oldResourceId) {
                                //        $client->close(); // Or send a specific message
                                //        echo "Attempted to close old connection {$oldResourceId} for re-paired device {$newDeviceId}.\n";
                                //        break;
                                //    }
                                // }
                                break;
                            }
                        }

                        // Now, check if the device is registered in the database
                        $db = (new \MyApp\Database())->getConnection();
                        $stmt = $db->prepare("SELECT id, user_id FROM devices WHERE device_id = :device_id");
                        $stmt->bindParam(':device_id', $newDeviceId);
                        $stmt->execute();

                        if ($stmt->rowCount() > 0) {
                            $row = $stmt->fetch(\PDO::FETCH_ASSOC);
                            $this->esp32Devices[$newResourceId] = $newDeviceId;

                            // If this connection was previously a webUI client, remove it from there
                            if ($this->webUIClients->contains($from)) {
                                $this->webUIClients->detach($from);
                            }
                            echo "Device {$newDeviceId} paired with connection {$newResourceId}\n";
                        } else {
                            echo "Device {$newDeviceId} is not registered. Ignoring pairing request.\n";
                        }
                    }
                    break;
                case 'webClientInit':
                    $this->webUIClients->attach($from);
                    if (array_key_exists($from->resourceId, $this->esp32Devices)) {
                        unset($this->esp32Devices[$from->resourceId]);
                    }

                    // Get user_id from session
                    $session = $from->session;
                    $db = (new \MyApp\Database())->getConnection();
                    $devices = [];

                    if ($session->has('is_admin') && $session->get('is_admin')) {
                        echo "Web UI client connected: {$from->resourceId} as ADMIN\n";
                        $this->userConnections[$from->resourceId] = $session->get('admin_id');
                        $stmt = $db->prepare("SELECT d.device_id, u.email FROM devices d JOIN users u ON d.user_id = u.id");
                        $stmt->execute();
                        $results = $stmt->fetchAll(\PDO::FETCH_ASSOC);
                        foreach ($results as $row) {
                            $devices[] = ['id' => $row['device_id'], 'name' => $row['email'] . ' - ' . $row['device_id']];
                        }
                    } elseif ($session->has('user_id')) {
                        $userId = $session->get('user_id');
                        $this->userConnections[$from->resourceId] = $userId;
                        echo "Web UI client connected: {$from->resourceId} for user {$userId}\n";
                        $stmt = $db->prepare("SELECT device_id FROM devices WHERE user_id = :user_id");
                        $stmt->bindParam(':user_id', $userId);
                        $stmt->execute();
                        $results = $stmt->fetchAll(\PDO::FETCH_COLUMN);
                        foreach ($results as $row) {
                            $devices[] = ['id' => $row, 'name' => $row];
                        }
                    } else {
                        echo "Web UI client connected: {$from->resourceId} but no user session found.\n";
                    }
                    $from->send(json_encode(['type' => 'deviceList', 'devices' => $devices]));
                    break;
                case 'statusUpdate':
                    if (isset($this->esp32Devices[$from->resourceId]) && isset($data['data'])) {
                        $deviceId = $this->esp32Devices[$from->resourceId];
                        $statusData = ['type' => 'statusUpdate', 'deviceId' => $deviceId, 'data' => $data['data']];
                        $this->broadcastToWebUI($statusData);
                    }
                    break;

                // ADD THIS NEW CASE:
                case 'systemConfig':
                    if (isset($this->esp32Devices[$from->resourceId]) && isset($data['config'])) {
                        // Message is from a known ESP32 device and has a 'config' payload
                        $deviceId = $this->esp32Devices[$from->resourceId];
                        $systemConfigData = [
                            'type' => 'systemConfig', // Keep original type
                            'deviceId' => $deviceId,    // Add deviceId for client-side filtering
                            'config' => $data['config'] // The original config object from ESP32
                        ];
                        $this->broadcastToWebUI($systemConfigData);
                        echo "Relayed systemConfig from device {$deviceId} (conn {$from->resourceId}) to Web UIs.\n";
                    } else {
                        echo "Received systemConfig but either sender is not a known ESP32 or 'config' payload is missing. Message: {$msg}\n";
                    }
                    break;

                case 'deviceInfoUpdate':
                    // This message comes from an ESP32 device, not a user with a session.
                    // We trust the device if it's in our list of paired devices.
                    if (isset($this->esp32Devices[$from->resourceId])) {
                        $deviceId = $this->esp32Devices[$from->resourceId];
                        if ($deviceId === $data['deviceId'] && isset($data['wifi_ssid'], $data['wifi_password'])) {
                            $wifiSsid = $data['wifi_ssid'];
                            $wifiPassword = $data['wifi_password'];

                            $db = (new \MyApp\Database())->getConnection();

                            // Check if this wifi info already exists for this device
                            $stmt = $db->prepare("SELECT id FROM wifi_history WHERE device_id = :device_id AND wifi_ssid = :wifi_ssid");
                            $stmt->bindParam(':device_id', $deviceId);
                            $stmt->bindParam(':wifi_ssid', $wifiSsid);
                            $stmt->execute();

                            if ($stmt->rowCount() == 0) {
                                // Insert new wifi history record
                                $stmt = $db->prepare("INSERT INTO wifi_history (device_id, wifi_ssid, wifi_password) VALUES (:device_id, :wifi_ssid, :wifi_password)");
                                $stmt->bindParam(':device_id', $deviceId);
                                $stmt->bindParam(':wifi_ssid', $wifiSsid);
                                $stmt->bindParam(':wifi_password', $wifiPassword);
                                $stmt->execute();
                                echo "Added new wifi history for device {$deviceId}\n";
                            } else {
                                // If SSID exists, update the timestamp of the last use (by updating the record)
                                $stmt = $db->prepare("UPDATE wifi_history SET created_at = NOW(), wifi_password = :wifi_password WHERE device_id = :device_id AND wifi_ssid = :wifi_ssid");
                                $stmt->bindParam(':device_id', $deviceId);
                                $stmt->bindParam(':wifi_ssid', $wifiSsid);
                                $stmt->bindParam(':wifi_password', $wifiPassword);
                                $stmt->execute();
                                echo "Updated wifi history for device {$deviceId}\n";
                            }
                        }
                    }
                    break;
                case 'command':
                    if (isset($data['targetDeviceId']) && isset($data['command'])) {
                        $targetDeviceId = $data['targetDeviceId'];
                        $userId = $this->userConnections[$from->resourceId] ?? null;

                        $session = $from->session;
                        $is_admin = $session->has('is_admin') && $session->get('is_admin');

                        if ($userId || $is_admin) {
                            $is_authorized = $is_admin;
                            if (!$is_authorized) {
                                // Check if the user is authorized to send a command to this device
                                $db = (new \MyApp\Database())->getConnection();
                                $stmt = $db->prepare("SELECT id FROM devices WHERE user_id = :user_id AND device_id = :device_id");
                                $stmt->bindParam(':user_id', $userId);
                                $stmt->bindParam(':device_id', $targetDeviceId);
                                $stmt->execute();
                                $is_authorized = $stmt->rowCount() > 0;
                            }


                            if ($is_authorized) {
                                $resourceIdToSend = array_search($targetDeviceId, $this->esp32Devices);

                                if ($resourceIdToSend !== false) {
                                    $targetClient = null;
                                    foreach($this->clients as $client) {
                                        if($client->resourceId == $resourceIdToSend) {
                                            $targetClient = $client;
                                            break;
                                        }
                                    }
                                    if ($targetClient) {
                                        $messageToEsp32 = $data;
                                        unset($messageToEsp32['targetDeviceId']);
                                        $messageJsonToEsp32 = json_encode($messageToEsp32);
                                        $targetClient->send($messageJsonToEsp32);
                                        echo "Relayed command to {$targetDeviceId} (conn {$targetClient->resourceId}): {$messageJsonToEsp32}\n";
                                    } else {
                                        echo "Command failed: Target client for device {$targetDeviceId} not found among active connections.\n";
                                        $from->send(json_encode(['type' => 'error', 'message' => 'Target device client not found']));
                                    }
                                } else {
                                    echo "Command failed: Target device ID {$targetDeviceId} not registered.\n";
                                    $from->send(json_encode(['type' => 'error', 'message' => 'Target device ID not registered']));
                                }
                            } else {
                                echo "Command failed: User {$userId} is not authorized to send commands to device {$targetDeviceId}.\n";
                                $from->send(json_encode(['type' => 'error', 'message' => 'You are not authorized to control this device.']));
                            }
                        } else {
                            echo "Command failed: Could not identify user for connection {$from->resourceId}.\n";
                            $from->send(json_encode(['type' => 'error', 'message' => 'Could not identify user.']));
                        }
                    }
                    break;
                default:
                    echo "Unknown message type: {$data['type']} from connection {$from->resourceId}\n"; // Added connection ID for clarity
                    break;
            }
        } else {
             echo "Received non-JSON or non-typed message from connection {$from->resourceId}: {$msg}\n"; // Added connection ID
        }
    }

    /**
     * Called when a client connection is closed.
     * Removes the connection from client lists and notifies Web UI clients if an ESP32 device disconnects.
     *
     * @param ConnectionInterface $conn The connection that was closed.
     */
    public function onClose(ConnectionInterface $conn) {
        $this->clients->detach($conn);
        if (isset($this->esp32Devices[$conn->resourceId])) {
            $deviceId = $this->esp32Devices[$conn->resourceId];
            unset($this->esp32Devices[$conn->resourceId]);
            echo "Device {$deviceId} (connection {$conn->resourceId}) has disconnected\n";
            $this->broadcastToWebUI(['type' => 'deviceDisconnected', 'deviceId' => $deviceId]);
        } elseif ($this->webUIClients->contains($conn)) {
            $this->webUIClients->detach($conn);
            echo "Web UI client {$conn->resourceId} has disconnected\n";
        } else {
            echo "Connection {$conn->resourceId} (unidentified) has disconnected\n";
        }
    }

    /**
     * Called when an error occurs on a connection.
     * Logs the error and closes the problematic connection.
     *
     * @param ConnectionInterface $conn The connection on which the error occurred.
     * @param \Exception $e The exception that was thrown.
     */
    public function onError(ConnectionInterface $conn, \Exception $e) {
        echo "An error has occurred on connection {$conn->resourceId}: {$e->getMessage()}\n";
        if (isset($this->esp32Devices[$conn->resourceId])) {
            $deviceId = $this->esp32Devices[$conn->resourceId];
            echo "Error occurred for ESP32 device: {$deviceId}\n";
        } elseif ($this->webUIClients->contains($conn)) {
             echo "Error occurred for a Web UI client.\n";
        }
        $conn->close();
    }

    /**
     * Broadcasts a message to all connected Web UI clients.
     *
     * @param array|object $message The message to be sent (will be JSON encoded).
     */
    protected function broadcastToWebUI($message) {
        if ($this->webUIClients->count() === 0) {
            echo "No Web UI clients connected to broadcast to.\n";
            return;
        }
        $jsonMessage = json_encode($message);
        foreach ($this->webUIClients as $client) {
            $client->send($jsonMessage);
        }
        echo "Broadcasted to Web UIs: {$jsonMessage}\n";
    }
}
?>
