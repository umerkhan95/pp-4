<?php
require 'db.php';
require 'jwt_config.php';

header('Content-Type: application/json');

$headers = getallheaders();
$auth = $headers['Authorization'] ?? null;

if (!$auth || !str_starts_with($auth, 'Bearer ')) {
    http_response_code(401);
    echo json_encode(["status" => "failed", "message" => "Authorization header missing or malformed"]);
    return;
}

$token = str_replace('Bearer ', '', $auth);
$decoded = validateJWT($token);

$user_id = $decoded->user_id ?? null;

$data = json_decode(file_get_contents("php://input"));
$device_name = trim($data->device_name ?? '');

if (!$user_id || !$device_name) {
    echo json_encode(["status" => "failed", "message" => "Missing user_id or device_name"]);
    return;
}

$stmt = $conn->prepare("INSERT INTO devices (user_id, device_name) VALUES (?, ?)");
$stmt->execute([$user_id, $device_name]);

$stmt = $conn->prepare("SELECT id FROM devices WHERE user_id = ? ORDER BY id DESC LIMIT 1");
$stmt->execute([$user_id]);
$device_id = $stmt->fetchColumn();

$stmt = $conn->prepare("INSERT INTO device_status (device_id, status, pump1_status, pump2_status, water_level) VALUES (?, ?, 0, 0, 0)");
$stmt->execute([$device_id, 'online']);

echo json_encode([
    "status" => "success",
    "message" => "Device registered successfully",
    "data" => [
        "device_id" => $device_id,
        "user_id" => $user_id,
        "device_name" => $device_name
    ]
]);
?>