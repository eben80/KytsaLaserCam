<?php
session_start();
require_once '../../php/php/db/database.php';

if ($_SERVER["REQUEST_METHOD"] == "POST") {
    $database = new Database();
    $db = $database->getConnection();

    $email = $_POST['email'];
    $password = $_POST['password'];

    $query = "SELECT id, email, password FROM users WHERE email = :email";

    $stmt = $db->prepare($query);

    $email = htmlspecialchars(strip_tags($email));
    $stmt->bindParam(':email', $email);
    $stmt->execute();

    $num = $stmt->rowCount();

    if ($num > 0) {
        $row = $stmt->fetch(PDO::FETCH_ASSOC);
        $id = $row['id'];
        $password2 = $row['password'];

        if (password_verify($password, $password2)) {
            $_SESSION['user_id'] = $id;
            header("Location: ../index.php");
        } else {
            header("Location: login.php?error=Invalid password.");
        }
    } else {
        header("Location: login.php?error=Invalid email.");
    }
}
?>
