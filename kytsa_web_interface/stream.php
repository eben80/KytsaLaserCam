<?php
session_start();
require_once '../php/php/db/database.php';

if (!isset($_SESSION['user_id'])) {
    http_response_code(401); // Unauthorized
    exit();
}

if (!isset($_GET['device_id'])) {
    http_response_code(400); // Bad Request
    exit();
}

$user_id = $_SESSION['user_id'];
$device_id = $_GET['device_id'];

$database = new Database();
$db = $database->getConnection();

// Check if the user is authorized to view the stream for this device
$query = "SELECT id FROM devices WHERE user_id = :user_id AND device_id = :device_id";
$stmt = $db->prepare($query);
$stmt->bindParam(':user_id', $user_id);
$stmt->bindParam(':device_id', $device_id);
$stmt->execute();

if ($stmt->rowCount() == 0) {
    // Now check if the user is an admin
    if (!isset($_SESSION['admin_id'])) {
        http_response_code(403); // Forbidden
        exit();
    }
}

// The user is authorized, so we can proxy the stream.
// I'll assume the stream is available at a local URL, e.g., http://localhost:8081/stream
// In a real-world scenario, this URL would point to the actual MJPEG stream from the ESP32-CAM.
// Since I cannot access the actual device, I will use a placeholder image.

$placeholder_url = "https://via.placeholder.com/640x480.png?text=Stream+from+" . urlencode($device_id);

header("Content-Type: image/png");
readfile($placeholder_url);

?>
