<?php
require_once __DIR__ . '/vendor/autoload.php';

use MyApp\Database;

$newPassword = 'password123';
$adminUsername = 'admin';

$passwordHash = password_hash($newPassword, PASSWORD_DEFAULT);

$database = new Database();
$db = $database->getConnection();

if ($db) {
    try {
        $stmt = $db->prepare("UPDATE admins SET password = :password WHERE username = :username");
        $stmt->bindParam(':password', $passwordHash);
        $stmt->bindParam(':username', $adminUsername);

        if ($stmt->execute()) {
            if ($stmt->rowCount() > 0) {
                echo "Admin password has been successfully reset.\n";
                echo "Username: " . $adminUsername . "\n";
                echo "New Password: " . $newPassword . "\n";
            } else {
                echo "Error: Could not find an admin user with the username '" . $adminUsername . "'.\n";
            }
        } else {
            echo "Error: Failed to execute the update statement.\n";
        }
    } catch (PDOException $e) {
        echo "Database Error: " . $e->getMessage() . "\n";
    }
} else {
    echo "Error: Could not connect to the database. Please check your config.php settings.\n";
}
?>
