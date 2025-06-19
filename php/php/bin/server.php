<?php
use Ratchet\Server\IoServer;
use Ratchet\Http\HttpServer;
use Ratchet\WebSocket\WsServer;
use MyApp\WebSocketHandler;

require dirname(__DIR__) . '/vendor/autoload.php';

$wsServer = new WsServer(
    new WebSocketHandler(),
    ['arduino'] // Allowed subprotocols
);

$server = IoServer::factory(
    new HttpServer($wsServer),
    8080
);

echo "Starting WebSocket server on port 8080...\n";
$server->run();
?>

