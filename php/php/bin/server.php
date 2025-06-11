<?php
// Location: php/php/bin/server.php
use Ratchet\Server\IoServer;
use Ratchet\Http\HttpServer;
use Ratchet\WebSocket\WsServer;
use MyApp\WebSocketHandler;

require dirname(__DIR__) . '/vendor/autoload.php';

$wsServer = new WsServer(
    new WebSocketHandler(),
    ['arduino'] // Explicitly allow 'arduino' subprotocol
);
// $wsServer->setStrictSubProtocolCheck(false); // Another option if just allowing any, but explicit is better

$server = IoServer::factory(
    new HttpServer($wsServer),
    8080 // Port to listen on
);

echo "Starting WebSocket server on port 8080...\n";
$server->run();
?>
