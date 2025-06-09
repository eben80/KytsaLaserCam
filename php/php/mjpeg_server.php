<?php
// mjpeg_server.php

// Configuration
$boundary = 'frame';
$frameRate = 0.05; // Try this for faster server-side streaming
// $frameFilePath = '/tmp/latest_camera_frame.jpg'; // REMOVE OR COMMENT OUT THIS LINE

// Redis Configuration
$redisHost = '127.0.0.1';
$redisPort = 6379;
$redisKey = 'latest_camera_frame'; // Key to store the frame in Redis

// Handle POST request to receive a frame
if ($_SERVER['REQUEST_METHOD'] === 'POST' && $_SERVER['REQUEST_URI'] === '/receive') {
    $rawFrameData = file_get_contents('php://input');
    if ($rawFrameData) {
        try {
            $redis = new Redis();
            $redis->connect($redisHost, $redisPort);
            $redis->set($redisKey, $rawFrameData); // Store in Redis
            $redis->close(); // Close connection

            http_response_code(200);
        } catch (RedisException $e) {
            http_response_code(500);
            echo "Redis connection or write failed: " . $e->getMessage();
        }
    } else {
        http_response_code(400);
        echo "No data received.";
    }
    exit();
}

// Handle GET request to serve the MJPEG stream
if ($_SERVER['REQUEST_METHOD'] === 'GET' && $_SERVER['REQUEST_URI'] === '/stream') {
    header('Content-Type: multipart/x-mixed-replace; boundary=' . $boundary);
    header('Cache-Control: no-cache');
    // header('Connection: close'); // Remove or comment out this
    header('Pragma: no-cache');

    // This is crucial for Nginx to not buffer the stream for fastcgi
    // Make sure you have `fastcgi_buffering off;` in your Nginx config for /stream
    // And also `fastcgi_no_cache 1;` and `fastcgi_keep_conn on;` if not already.

    while (true) {
        $latestFrame = null;
        try {
            $redis = new Redis();
            $redis->connect($redisHost, $redisPort);
            $latestFrame = $redis->get($redisKey); // Read from Redis
            $redis->close();
        } catch (RedisException $e) {
            // Log Redis error, but don't crash stream
            error_log("Redis read failed: " . $e->getMessage());
            // Optionally send a placeholder image or simply continue loop
        }

        if ($latestFrame) {
            echo "--" . $boundary . "\r\n";
            echo "Content-Type: image/jpeg\r\n";
            echo "Content-Length: " . strlen($latestFrame) . "\r\n";
            echo "\r\n";
            echo $latestFrame;
            echo "\r\n";
        } else {
            // Optional: Send a placeholder image if no frame is available yet
            // For example, a small gray JPEG
            // $placeholder = base64_decode('/9j/4AAQSkZJRgABAQEAYABgAAD/2wBDAAIBAQE... (your placeholder base64)');
            // echo "--" . $boundary . "\r\n";
            // echo "Content-Type: image/jpeg\r\n";
            // echo "Content-Length: " . strlen($placeholder) . "\r\n";
            // echo "\r\n";
            // echo $placeholder;
            // echo "\r\n";
        }

        // Flush output buffer to send the frame immediately
        ob_flush();
        flush();

        usleep($frameRate * 1000000); // Sleep for the desired frame rate
    }
}

// If neither /receive (POST) nor /stream (GET)
http_response_code(404);
echo "Not Found.";
?>
