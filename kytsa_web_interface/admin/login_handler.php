<?php
session_start();
require_once '../../php/php/db/database.php';

if ($_SERVER["REQUEST_METHOD"] == "POST") {
    $database = new Database();
    $db = $database->getConnection();

    $username = $_POST['username'];
    $password = $_POST['password'];

    $query = "SELECT id, username, password FROM admins WHERE username = :username";

    $stmt = $db->prepare($query);

    $username = htmlspecialchars(strip_tags($username));
    $stmt->bindParam(':username', $username);
    $stmt->execute();

    $num = $stmt->rowCount();

    if ($num > 0) {
        $row = $stmt->fetch(PDO::FETCH_ASSOC);
        $id = $row['id'];
        $password2 = $row['password'];

        if (password_verify($password, $password2)) {
            $_SESSION['admin_id'] = $id;
            header("Location: index.php");
        } else {
            header("Location: login.php?error=Invalid password.");
        }
    } else {
        header("Location: login.php?error=Invalid username.");
    }
}
?>
