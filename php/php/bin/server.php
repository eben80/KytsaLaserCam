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

$server = IoServer::factory(
    new HttpServer(
        new WsServer(
            new SessionProvider(
                new WebSocketHandler(),
                $sessionHandler
            )
        )
    ),
    8080
);

echo "Starting WebSocket server on port 8080...\n";
$server->run();
?>
