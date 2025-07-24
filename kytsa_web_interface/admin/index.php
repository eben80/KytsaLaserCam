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


// Get all users and their devices, including wifi history
$query = "
    SELECT
        u.id as user_id,
        u.email,
        d.id as device_id,
        d.device_id as device_name,
        d.created_at,
        wh.wifi_ssid,
        wh.wifi_password,
        wh.created_at as wifi_used_at
    FROM users u
    LEFT JOIN devices d ON u.id = d.user_id
    LEFT JOIN wifi_history wh ON d.device_id = wh.device_id
    ORDER BY u.email, d.device_id, wh.created_at DESC";
$stmt = $db->prepare($query);
$stmt->execute();
$results = $stmt->fetchAll(PDO::FETCH_ASSOC);

$users = [];
foreach ($results as $row) {
    $users[$row['email']]['user_id'] = $row['user_id'];
    if ($row['device_id']) {
        $users[$row['email']]['devices'][$row['device_name']]['created_at'] = $row['created_at'];
        if ($row['wifi_ssid']) {
            $users[$row['email']]['devices'][$row['device_name']]['wifi_history'][] = [
                'wifi_ssid' => $row['wifi_ssid'],
                'wifi_password' => $row['wifi_password'],
                'wifi_used_at' => $row['wifi_used_at']
            ];
        }
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
                    <?php foreach ($userData['devices'] as $deviceName => $deviceData): ?>
                        <h4>Device: <?php echo htmlspecialchars($deviceName); ?> (Registered on: <?php echo $deviceData['created_at']; ?>) <a href="index.php?action=unbind&device_id=<?php echo $deviceName; ?>">Unbind</a></h4>
                        <?php if (isset($deviceData['wifi_history'])): ?>
                            <table>
                                <thead>
                                    <tr>
                                        <th>WiFi SSID</th>
                                        <th>WiFi Password</th>
                                        <th>Last Used</th>
                                    </tr>
                                </thead>
                                <tbody>
                                    <?php foreach ($deviceData['wifi_history'] as $wifi): ?>
                                        <tr>
                                            <td><?php echo htmlspecialchars($wifi['wifi_ssid']); ?></td>
                                            <td><?php echo htmlspecialchars($wifi['wifi_password']); ?></td>
                                            <td><?php echo htmlspecialchars($wifi['wifi_used_at']); ?></td>
                                        </tr>
                                    <?php endforeach; ?>
                                </tbody>
                            </table>
                        <?php else: ?>
                            <p>No WiFi history for this device.</p>
                        <?php endif; ?>
                    <?php endforeach; ?>
                <?php else: ?>
                    <p>No devices registered for this user.</p>
                <?php endif; ?>
            <?php endforeach; ?>
        </div>
    </div>
</body>
</html>
