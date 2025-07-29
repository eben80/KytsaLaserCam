<?php
use Ratchet\Server\IoServer;
use Ratchet\Http\HttpServer;
use Ratchet\WebSocket\WsServer;
use Ratchet\Session\SessionProvider;
use MyApp\WebSocketHandler;
use MyApp\Database;
use MyApp\SessionHandler;

require dirname(__DIR__) . '/vendor/autoload.php';

$db = new Database();
$sessionHandler = new SessionHandler($db);
session_set_save_handler($sessionHandler, true);

$wsServer = new WsServer(new WebSocketHandler());

$server = IoServer::factory(
    new HttpServer(
        new SessionProvider(
            $wsServer,
            $sessionHandler
        )
    ),
    8080
);

echo "Starting WebSocket server on port 8080...\n";
$server->run();
?>
