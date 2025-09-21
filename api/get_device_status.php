<?php
require 'db.php';
require 'jwt_config.php';

header('Content-Type: application/json');

// JWT auth
$headers = getallheaders();
$auth = $headers['Authorization'] ?? '';
$token = str_replace('Bearer ', '', $auth);
$decoded = validateJWT($token);
$user_id = $decoded->user_id ?? null;

if (!$user_id) {
    echo json_encode(["status" => "failed", "message" => "Invalid or missing token"]);
    exit;
}

$device_id = $_GET['device_id'] ?? '';
if (!$device_id) {
    echo json_encode(["status" => "failed", "message" => "Missing device_id"]);
    exit;
}

// Check device belongs to user
$stmt = $conn->prepare("SELECT id FROM devices WHERE id = ? AND user_id = ?");
$stmt->execute([$device_id, $user_id]);
if ($stmt->rowCount() === 0) {
    echo json_encode(["status" => "failed", "message" => "Unauthorized or unknown device"]);
    exit;
}

// Fetch current status
$stmt = $conn->prepare("SELECT * FROM device_status WHERE device_id = ?");
$stmt->execute([$device_id]);
$status = $stmt->fetch(PDO::FETCH_ASSOC);

if ($status) {
    $lastSeen = strtotime($status['last_seen']);
    $currentStatus = $status['status'];
    $isOffline = (time() - $lastSeen) > 300; // 5 minutes

    // Handle offline
    if ($isOffline && $currentStatus !== 'offline') {
        // $update = $conn->prepare("UPDATE device_status SET status = 'offline' WHERE device_id = ?");
        // $update->execute([$device_id]);
        $status['status'] = 'offline';
    }

    // Handle online (if previously offline or empty)
    if (!$isOffline && (empty($currentStatus) || $currentStatus === 'offline')) {
        // $update = $conn->prepare("UPDATE device_status SET status = 'online' WHERE device_id = ?");
        // $update->execute([$device_id]);
        $status['status'] = 'online';
    }

    $status['is_offline'] = $isOffline;

    echo json_encode([
        "status" => "success",
        "data" => $status
    ]);
} else {
    echo json_encode([
        "status" => "failed",
        "message" => "No status found for this device"
    ]);
}
?>
