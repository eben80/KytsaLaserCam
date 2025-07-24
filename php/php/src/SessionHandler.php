<?php
namespace MyApp;

class SessionHandler implements \SessionHandlerInterface
{
    private $db;

    public function __construct(Database $db)
    {
        $this->db = $db->getConnection();
    }

    public function open($savePath, $sessionName)
    {
        return true;
    }

    public function close()
    {
        return true;
    }

    public function read($id)
    {
        $stmt = $this->db->prepare("SELECT sess_data FROM sessions WHERE sess_id = :id");
        $stmt->bindParam(':id', $id);
        $stmt->execute();
        if ($row = $stmt->fetch(\PDO::FETCH_ASSOC)) {
            return $row['sess_data'];
        }
        return "";
    }

    public function write($id, $data)
    {
        $stmt = $this->db->prepare("REPLACE INTO sessions (sess_id, sess_data, sess_time) VALUES (:id, :data, :time)");
        $stmt->bindParam(':id', $id);
        $stmt->bindParam(':data', $data);
        $time = time();
        $stmt->bindParam(':time', $time);
        return $stmt->execute();
    }

    public function destroy($id)
    {
        $stmt = $this->db->prepare("DELETE FROM sessions WHERE sess_id = :id");
        $stmt->bindParam(':id', $id);
        return $stmt->execute();
    }

    public function gc($maxlifetime)
    {
        $stmt = $this->db->prepare("DELETE FROM sessions WHERE sess_time < :time");
        $old = time() - $maxlifetime;
        $stmt->bindParam(':time', $old);
        return $stmt->execute();
    }
}
