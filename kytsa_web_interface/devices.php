<?php
session_start();
if (!isset($_SESSION['user_id'])) {
    header("Location: auth/login.php");
    exit();
}
require_once '../php/php/db/database.php';

$database = new Database();
$db = $database->getConnection();

$user_id = $_SESSION['user_id'];

// Handle device registration
if ($_SERVER["REQUEST_METHOD"] == "POST" && isset($_POST['device_id'])) {
    $device_id = htmlspecialchars(strip_tags($_POST['device_id']));

    // Check if the device is already registered to another user
    $query = "SELECT id FROM devices WHERE device_id = :device_id";
    $stmt = $db->prepare($query);
    $stmt->bindParam(':device_id', $device_id);
    $stmt->execute();

    if ($stmt->rowCount() > 0) {
        $error = "Device is already registered.";
    } else {
        $query = "INSERT INTO devices (device_id, user_id) VALUES (:device_id, :user_id)";
        $stmt = $db->prepare($query);
        $stmt->bindParam(':device_id', $device_id);
        $stmt->bindParam(':user_id', $user_id);

        if ($stmt->execute()) {
            $message = "Device registered successfully.";
        } else {
            $error = "Failed to register device.";
        }
    }
}

// Get the list of registered devices for the current user
$query = "SELECT id, device_id, created_at FROM devices WHERE user_id = :user_id";
$stmt = $db->prepare($query);
$stmt->bindParam(':user_id', $user_id);
$stmt->execute();
$devices = $stmt->fetchAll(PDO::FETCH_ASSOC);

?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Manage Devices</title>
    <link rel="stylesheet" href="style.css">
</head>
<body>
    <div class="container">
        <div class="controls">
            <h2>Manage Your Devices</h2>
            <p><a href="index.php">Back to Control Panel</a></p>

            <h3>Register a New Device</h3>
            <?php if (isset($message)): ?>
                <p style="color: green;"><?php echo $message; ?></p>
            <?php endif; ?>
            <?php if (isset($error)): ?>
                <p style="color: red;"><?php echo $error; ?></p>
            <?php endif; ?>
            <form action="devices.php" method="post">
                <div class="control-group">
                    <label for="device_id">Device ID</label>
                    <input type="text" name="device_id" id="device_id" required>
                </div>
                <button type="submit">Register Device</button>
            </form>

            <h3>Your Registered Devices</h3>
            <?php if (count($devices) > 0): ?>
                <table>
                    <thead>
                        <tr>
                            <th>Device ID</th>
                            <th>Registered On</th>
                            <th>Action</th>
                        </tr>
                    </thead>
                    <tbody>
                        <?php foreach ($devices as $device): ?>
                            <tr>
                                <td><?php echo htmlspecialchars($device['device_id']); ?></td>
                                <td><?php echo htmlspecialchars($device['created_at']); ?></td>
                                <td><a href="delete_device.php?id=<?php echo $device['id']; ?>">Delete</a></td>
                            </tr>
                        <?php endforeach; ?>
                    </tbody>
                </table>
            <?php else: ?>
                <p>You have no registered devices.</p>
            <?php endif; ?>
        </div>
    </div>
</body>
</html>
