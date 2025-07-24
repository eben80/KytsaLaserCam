<?php
session_start();
require_once 'db/database.php';

// Configuration
$boundary = 'frame';
$frameRate = 0.05;

// Redis Configuration
$redisHost = '127.0.0.1';
$redisPort = 6379;
$redisKeyPrefix = 'stream:';

// Handle POST request to receive a frame
if ($_SERVER['REQUEST_METHOD'] === 'POST' && strpos($_SERVER['REQUEST_URI'], '/receive') === 0) {
    if (!isset($_GET['device_id'])) {
        http_response_code(400);
        echo "Device ID is missing.";
        exit();
    }
    $deviceId = $_GET['device_id'];
    $redisKey = $redisKeyPrefix . $deviceId;

    $rawFrameData = file_get_contents('php://input');
    if ($rawFrameData) {
        try {
            $redis = new Redis();
            $redis->connect($redisHost, $redisPort);
            $redis->set($redisKey, $rawFrameData);
            $redis->close();
            http_response_code(200);
        } catch (RedisException $e) {
            http_response_code(500);
            error_log("Redis connection or write failed: " . $e->getMessage());
        }
    } else {
        http_response_code(400);
        echo "No data received.";
    }
    exit();
}

// Handle GET request to serve the MJPEG stream
if ($_SERVER['REQUEST_METHOD'] === 'GET' && strpos($_SERVER['REQUEST_URI'], '/stream') === 0) {
    if (!isset($_GET['device_id'])) {
        http_response_code(400);
        echo "Device ID is missing.";
        exit();
    }
    $deviceId = $_GET['device_id'];
    $redisKey = $redisKeyPrefix . $deviceId;

    // --- Security Check ---
    $isAuthorized = false;
    if (isset($_SESSION['user_id'])) {
        $userId = $_SESSION['user_id'];
        $db = (new Database())->getConnection();
        $stmt = $db->prepare("SELECT id FROM devices WHERE user_id = :user_id AND device_id = :device_id");
        $stmt->bindParam(':user_id', $userId);
        $stmt->bindParam(':device_id', $deviceId);
        $stmt->execute();
        if ($stmt->rowCount() > 0) {
            $isAuthorized = true;
        }
    }

    if (!$isAuthorized && isset($_SESSION['admin_id'])) {
        // Admins are always authorized
        $isAuthorized = true;
    }

    if (!$isAuthorized) {
        http_response_code(403);
        echo "Forbidden: You do not have access to this device's stream.";
        exit();
    }
    // --- End Security Check ---

    header('Content-Type: multipart/x-mixed-replace; boundary=' . $boundary);
    header('Cache-Control: no-cache');
    header('Pragma: no-cache');

    while (true) {
        $latestFrame = null;
        try {
            $redis = new Redis();
            $redis->connect($redisHost, $redisPort);
            $latestFrame = $redis->get($redisKey);
            $redis->close();
        } catch (RedisException $e) {
            error_log("Redis read failed: " . $e->getMessage());
        }

        if ($latestFrame) {
            echo "--" . $boundary . "\r\n";
            echo "Content-Type: image/jpeg\r\n";
            echo "Content-Length: " . strlen($latestFrame) . "\r\n";
            echo "\r\n";
            echo $latestFrame;
            echo "\r\n";
        }

        ob_flush();
        flush();

        usleep($frameRate * 1000000);
    }
}

http_response_code(404);
echo "Not Found.";
?>
