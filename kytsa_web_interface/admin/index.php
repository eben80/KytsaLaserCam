<?php
session_start();
if (!isset($_SESSION['admin_id'])) {
    header("Location: login.php");
    exit();
}
require_once '../../php/php/db/database.php';

$database = new Database();
$db = $database->getConnection();

// Handle unbinding a device
if (isset($_GET['action']) && $_GET['action'] == 'unbind' && isset($_GET['device_id'])) {
    $device_id_to_unbind = $_GET['device_id'];
    $query = "DELETE FROM devices WHERE id = :id";
    $stmt = $db->prepare($query);
    $stmt->bindParam(':id', $device_id_to_unbind);
    if ($stmt->execute()) {
        $message = "Device unbound successfully.";
    } else {
        $error = "Failed to unbind device.";
    }
}


// Get all users and their devices
$query = "SELECT u.id as user_id, u.email, d.id as device_id, d.device_id as device_name, d.wifi_ssid, d.wifi_password, d.created_at FROM users u LEFT JOIN devices d ON u.id = d.user_id ORDER BY u.email, d.created_at";
$stmt = $db->prepare($query);
$stmt->execute();
$results = $stmt->fetchAll(PDO::FETCH_ASSOC);

$users = [];
foreach ($results as $row) {
    $users[$row['email']]['user_id'] = $row['user_id'];
    if ($row['device_id']) {
        $users[$row['email']]['devices'][] = $row;
    }
}

?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Admin Dashboard</title>
    <link rel="stylesheet" href="../style.css">
</head>
<body>
    <div class="container">
        <div class="controls">
            <h2>Admin Dashboard</h2>
            <p><a href="logout.php">Logout</a></p>
            <?php if (isset($message)): ?>
                <p style="color: green;"><?php echo $message; ?></p>
            <?php endif; ?>
            <?php if (isset($error)): ?>
                <p style="color: red;"><?php echo $error; ?></p>
            <?php endif; ?>

            <?php foreach ($users as $email => $userData): ?>
                <h3>User: <?php echo htmlspecialchars($email); ?></h3>
                <?php if (isset($userData['devices'])): ?>
                    <table>
                        <thead>
                            <tr>
                                <th>Device ID</th>
                                <th>WiFi SSID</th>
                                <th>WiFi Password</th>
                                <th>Registered On</th>
                                <th>Action</th>
                            </tr>
                        </thead>
                        <tbody>
                            <?php foreach ($userData['devices'] as $device): ?>
                                <tr>
                                    <td><?php echo htmlspecialchars($device['device_name']); ?></td>
                                    <td><?php echo htmlspecialchars($device['wifi_ssid']); ?></td>
                                    <td><?php echo htmlspecialchars($device['wifi_password']); ?></td>
                                    <td><?php echo htmlspecialchars($device['created_at']); ?></td>
                                    <td><a href="index.php?action=unbind&device_id=<?php echo $device['device_id']; ?>">Unbind</a></td>
                                </tr>
                            <?php endforeach; ?>
                        </tbody>
                    </table>
                <?php else: ?>
                    <p>No devices registered for this user.</p>
                <?php endif; ?>
            <?php endforeach; ?>
        </div>
    </div>
</body>
</html>
