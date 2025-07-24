<?php
session_start();
if (!isset($_SESSION['user_id'])) {
    header("Location: auth/login.php");
    exit();
}
require_once __DIR__ . '/../php_backend/db/database.php';

$database = new Database();
$db = $database->getConnection();

$user_id = $_SESSION['user_id'];

if (isset($_GET['id'])) {
    $device_id_to_delete = $_GET['id'];

    // Ensure the user owns this device before deleting
    $query = "DELETE FROM devices WHERE id = :id AND user_id = :user_id";
    $stmt = $db->prepare($query);
    $stmt->bindParam(':id', $device_id_to_delete);
    $stmt->bindParam(':user_id', $user_id);

    if ($stmt->execute()) {
        header("Location: devices.php?message=Device deleted successfully.");
    } else {
        header("Location: devices.php?error=Failed to delete device.");
    }
} else {
    header("Location: devices.php");
}
?>
