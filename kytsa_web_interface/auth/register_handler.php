<?php
require_once __DIR__ . '/../../php_backend/db/database.php';

if ($_SERVER["REQUEST_METHOD"] == "POST") {
    $database = new Database();
    $db = $database->getConnection();

    $email = $_POST['email'];
    $password = $_POST['password'];

    // Hash the password for security
    $password_hash = password_hash($password, PASSWORD_BCRYPT);

    $query = "INSERT INTO users (email, password) VALUES (:email, :password)";

    $stmt = $db->prepare($query);

    // Sanitize inputs
    $email = htmlspecialchars(strip_tags($email));

    // Bind the parameters
    $stmt->bindParam(':email', $email);
    $stmt->bindParam(':password', $password_hash);

    if ($stmt->execute()) {
        header("Location: login.php?message=Registration successful. Please login.");
    } else {
        header("Location: register.php?error=Registration failed. Please try again.");
    }
}
?>
