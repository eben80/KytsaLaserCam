<?php
session_start();
if (!isset($_SESSION['is_admin']) || !$_SESSION['is_admin']) {
    header("Location: auth/login.php");
    exit();
}
require_once __DIR__ . '/../php_backend/db/database.php';

$database = new Database();
$db = $database->getConnection();

// Get all unique device IDs from wifi_history and their current registration status
$query = "
    SELECT
        wh.device_id,
        u.email
    FROM (SELECT DISTINCT device_id FROM wifi_history) wh
    LEFT JOIN devices d ON wh.device_id = d.device_id
    LEFT JOIN users u ON d.user_id = u.id
    ORDER BY wh.device_id
";
$stmt = $db->prepare($query);
$stmt->execute();
$devices = $stmt->fetchAll(PDO::FETCH_ASSOC);

?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Admin - All Device History</title>
    <link rel="stylesheet" href="style.css">
</head>
<body>
    <div class="container">
        <div class="controls">
            <h2>All Device History</h2>
            <p><a href="index.php">Back to Control Panel</a></p>

            <table>
                <thead>
                    <tr>
                        <th>Device ID</th>
                        <th>Currently Registered To</th>
                    </tr>
                </thead>
                <tbody>
                    <?php foreach ($devices as $device): ?>
                        <tr>
                            <td><?php echo htmlspecialchars($device['device_id']); ?></td>
                            <td><?php echo $device['email'] ? htmlspecialchars($device['email']) : '<i>Unregistered</i>'; ?></td>
                        </tr>
                    <?php endforeach; ?>
                </tbody>
            </table>
        </div>
    </div>
</body>
</html>
