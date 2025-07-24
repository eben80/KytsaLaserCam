<?php
session_start();
require_once '../../php/php/db/database.php';

if ($_SERVER["REQUEST_METHOD"] == "POST") {
    $database = new Database();
    $db = $database->getConnection();

    $email = $_POST['email'];
    $password = $_POST['password'];

    // Check if it's an admin
    if (strpos($email, '@') === false) { // Admins use usernames, not emails
        $query = "SELECT id, username, password FROM admins WHERE username = :username";
        $stmt = $db->prepare($query);
        $username = htmlspecialchars(strip_tags($email));
        $stmt->bindParam(':username', $username);
        $stmt->execute();

        if ($stmt->rowCount() > 0) {
            $row = $stmt->fetch(PDO::FETCH_ASSOC);
            if (password_verify($password, $row['password'])) {
                $_SESSION['admin_id'] = $row['id'];
                $_SESSION['is_admin'] = true;
                header("Location: ../index.php");
                exit();
            }
        }
    }

    // Check if it's a regular user
    $query = "SELECT id, email, password FROM users WHERE email = :email";
    $stmt = $db->prepare($query);
    $email = htmlspecialchars(strip_tags($email));
    $stmt->bindParam(':email', $email);
    $stmt->execute();

    if ($stmt->rowCount() > 0) {
        $row = $stmt->fetch(PDO::FETCH_ASSOC);
        if (password_verify($password, $row['password'])) {
            $_SESSION['user_id'] = $row['id'];
            $_SESSION['is_admin'] = false;
            header("Location: ../index.php");
            exit();
        }
    }

    header("Location: login.php?error=Invalid credentials.");
}
?>
