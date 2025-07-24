<?php
use Ratchet\Server\IoServer;
use Ratchet\Http\HttpServer;
use Ratchet\WebSocket\WsServer;
use MyApp\WebSocketHandler;
use Ratchet\Session\SessionProvider;
use Symfony\Component\HttpFoundation\Session\Storage\Handler;

require dirname(__DIR__) . '/vendor/autoload.php';

// Manually create a session handler
$sessionHandler = new Handler\PdoSessionHandler(
    (new \MyApp\Database())->getConnection(),
    ['db_table' => 'sessions']
);

$wsServer = new WsServer(
    new SessionProvider(
        new WebSocketHandler(),
        $sessionHandler
    ),
    ['arduino'] // Allowed subprotocols
);

$server = IoServer::factory(
    new HttpServer($wsServer),
    8080
);

echo "Starting WebSocket server on port 8080...\n";
$server->run();
?>

