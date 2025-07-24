<?php
session_start();
header('Content-Type: application/json');

if (!isset($_SESSION['is_admin']) || !$_SESSION['is_admin']) {
    http_response_code(403);
    echo json_encode(['error' => 'Forbidden']);
    exit();
}

if (!isset($_GET['device_id'])) {
    http_response_code(400);
    echo json_encode(['error' => 'Bad Request: Missing device_id']);
    exit();
}

require_once __DIR__ . '/../php_backend/db/database.php';
$database = new Database();
$db = $database->getConnection();

$deviceId = $_GET['device_id'];

$query = "SELECT wifi_ssid, wifi_password, created_at FROM wifi_history WHERE device_id = :device_id ORDER BY created_at DESC";
$stmt = $db->prepare($query);
$stmt->bindParam(':device_id', $deviceId);
$stmt->execute();

$history = $stmt->fetchAll(PDO::FETCH_ASSOC);

echo json_encode($history);
?>
