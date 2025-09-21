<?php
require 'db.php';
require 'jwt_config.php';

$headers = getallheaders();
$auth = $headers['Authorization'] ?? '';
$token = str_replace('Bearer ', '', $auth);

try {
    $decoded = validateJWT($token);
    $user_id = $decoded->user_id;

    // Get device_id from query string
    $device_id = $_GET['device_id'] ?? '';
    if (empty($device_id)) {
        throw new Exception("Missing device_id");
    }

    // Optional: validate that device belongs to user
    $checkStmt = $conn->prepare("SELECT id FROM devices WHERE id = ? AND user_id = ?");
    $checkStmt->execute([$device_id, $user_id]);
    if ($checkStmt->rowCount() == 0) {
        throw new Exception("Device not found or does not belong to user");
    }

    // Fetch schedule
    $stmt = $conn->prepare("SELECT time_slot FROM sprinkler_schedule WHERE device_id = ?");
    $stmt->execute([$device_id]);
    $schedule = $stmt->fetchAll(PDO::FETCH_COLUMN);

    echo json_encode(["schedule" => $schedule]);

} catch (Exception $e) {
    http_response_code(400);
    echo json_encode(["error" => $e->getMessage()]);
}
?>
