<?php
require 'db.php';
require 'jwt_config.php';

header('Content-Type: application/json');

$headers = getallheaders();
$auth = $headers['Authorization'] ?? '';
$token = str_replace('Bearer ', '', $auth);

$decoded = validateJWT($token);
$user_id = $decoded->user_id ?? null;

if (!$user_id) {
    echo json_encode(["status" => "failed", "message" => "Invalid or missing token"]);
    exit;
}

$data = json_decode(file_get_contents("php://input"));
$device_id = $data->device_id ?? '';
$status = $data->status ?? '';
$pump1 = $data->pump1_status ?? 0;
$pump2 = $data->pump2_status ?? 0;
$water = $data->water_level ?? 0;

// Input validation
if (!$device_id || $status === '') {
    echo json_encode(["status" => "failed", "message" => "Missing required parameters"]);
    exit;
}

// Optional: Verify that the device belongs to this user
$stmt = $conn->prepare("SELECT id FROM devices WHERE id = ? AND user_id = ?");
$stmt->execute([$device_id, $user_id]);
if ($stmt->rowCount() === 0) {
    echo json_encode(["status" => "failed", "message" => "Unauthorized device access"]);
    exit;
}

// Insert/update device status
$stmt = $conn->prepare("
    REPLACE INTO device_status (device_id, status, pump1_status, pump2_status, water_level) 
    VALUES (?, ?, ?, ?, ?)
");
$stmt->execute([$device_id, $status, $pump1, $pump2, $water]);

// exit inserted info
echo json_encode([
    "status" => "success",
    "data" => [
        "device_id" => $device_id,
        "status" => $status,
        "pump1_status" => $pump1,
        "pump2_status" => $pump2,
        "water_level" => $water
    ]
]);
?>
