<?php
require 'db.php';
require 'jwt_config.php';

header('Content-Type: application/json');

// Get JWT token
$headers = getallheaders();
$auth = $headers['Authorization'] ?? '';
$token = str_replace('Bearer ', '', $auth);

// Validate token
$decoded = validateJWT($token);
$user_id = $decoded->user_id ?? null;

if (!$user_id) {
    echo json_encode(["status" => "failed", "message" => "Invalid or missing token"]);
    exit;
}

// Get JSON input
$data = json_decode(file_get_contents("php://input"));
$device_id   = $data->device_id ?? '';
$status      = $data->status ?? '';
$water_level = $data->water_level ?? '';

// Validate inputs
if (!$device_id || $status === '' || $water_level === '') {
    echo json_encode(["status" => "failed", "message" => "Missing required parameters"]);
    exit;
}

// Check if device belongs to the user
$stmt = $conn->prepare("SELECT id FROM devices WHERE id = ? AND user_id = ?");
$stmt->execute([$device_id, $user_id]);
if ($stmt->rowCount() === 0) {
    echo json_encode(["status" => "failed", "message" => "Unauthorized device access"]);
    exit;
}

// Update only status and water level
$stmt = $conn->prepare("
    UPDATE device_status 
    SET status = ?, water_level = ?, last_seen = NOW()
    WHERE device_id = ?
");
$stmt->execute([$status, $water_level, $device_id]);

echo json_encode([
    "status" => "success",
    "message" => "Device status updated",
    "data" => [
        "device_id" => $device_id,
        "status" => $status,
        "water_level" => $water_level,
        "last_seen" => date('Y-m-d H:i:s')
    ]
]);
?>
