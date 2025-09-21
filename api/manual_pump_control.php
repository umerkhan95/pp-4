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
    return;
}

$data = json_decode(file_get_contents("php://input"));
$device_id = $data->device_id ?? '';
$pump1 = $data->pump1_status ?? null;
$pump2 = $data->pump2_status ?? null;

if (!$device_id && $pump1 === null && $pump2 === null) {
    echo json_encode(["status" => "failed", "message" => "Missing parameters"]);
    return;
}

$stmt = $conn->prepare("SELECT id FROM devices WHERE id = ? AND user_id = ?");
$stmt->execute([$device_id, $user_id]);
if ($stmt->rowCount() === 0) {
    echo json_encode(["status" => "failed", "message" => "Unauthorized device access"]);
    return;
}

$stmt = $conn->prepare("SELECT status, water_level FROM device_status WHERE device_id = ?");
$stmt->execute([$device_id]);
$existing = $stmt->fetch(PDO::FETCH_ASSOC);

$status = $existing['status'] ?? 'manual';
$water = $existing['water_level'] ?? 0;

$stmt = $conn->prepare("
    REPLACE INTO device_status (device_id, status, pump1_status, pump2_status, water_level) 
    VALUES (?, ?, ?, ?, ?)
");
$stmt->execute([$device_id, $status, $pump1, $pump2, $water]);

echo json_encode([
    "status" => "success",
    "message" => "Manual pump control updated",
    "data" => [
        "device_id" => $device_id,
        "status" => $status,
        "pump1_status" => $pump1,
        "pump2_status" => $pump2,
        "water_level" => $water
    ]
]);
?>