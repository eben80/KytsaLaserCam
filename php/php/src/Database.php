<?php
namespace MyApp;

require_once __DIR__ . '/config.php';

class Database {
    private $conn;

    public function getConnection() {
        $this->conn = null;

        try {
            $this->conn = new \PDO("mysql:host=" . DB_SERVER . ";dbname=" . DB_NAME, DB_USERNAME, DB_PASSWORD);
            $this->conn->exec("set names utf8");
        } catch(\PDOException $exception) {
            echo "Connection error: " . $exception->getMessage();
        }

        return $this->conn;
    }
}
?>
