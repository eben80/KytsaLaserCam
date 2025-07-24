<?php
use Ratchet\Server\IoServer;
use Ratchet\Http\HttpServer;
use Ratchet\WebSocket\WsServer;
use Ratchet\Session\SessionProvider;
use MyApp\WebSocketHandler;
use Symfony\Component\HttpFoundation\Session\Storage\Handler\PdoSessionHandler;
use MyApp\Database;

require dirname(__DIR__) . '/vendor/autoload.php';

$pdo = (new Database())->getConnection();

$server = IoServer::factory(
    new HttpServer(
        new WsServer(
            new SessionProvider(
                new WebSocketHandler(),
                new PdoSessionHandler($pdo, ['db_table' => 'sessions'])
            )
        )
    ),
    8080
);

echo "Starting WebSocket server on port 8080...\n";
$server->run();
?>
