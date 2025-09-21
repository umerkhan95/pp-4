<?php
require 'db.php';
require 'jwt_config.php';

header('Content-Type: application/json');

// Get Authorization header
$headers = getallheaders();
$auth = $headers['Authorization'] ?? '';

if (!$auth || !str_starts_with($auth, 'Bearer ')) {
    http_response_code(401);
    echo json_encode(["status" => "failed", "message" => "Authorization header missing or malformed"]);
    return;
}

// Extract and validate token
$token = str_replace('Bearer ', '', $auth);
$decoded = validateJWT($token);
$user_id = $decoded->user_id ?? null;

if (!$user_id) {
    echo json_encode(["status" => "failed", "message" => "Invalid token payload"]);
    return;
}

// Parse JSON input
$data = json_decode(file_get_contents("php://input"));
if (!$data || !isset($data->dog_size)) {
    echo json_encode(["status" => "failed", "message" => "Missing dog_size in request body"]);
    return;
}

$dog_size = $data->dog_size;

// Save to database
$stmt = $conn->prepare("REPLACE INTO dog_settings (user_id, dog_size) VALUES (?, ?)");
$stmt->execute([$user_id, $dog_size]);

// Return success response
echo json_encode([
    "status" => "success",
    "message" => "Dog size updated",
    "data" => [
        "user_id" => $user_id,
        "dog_size" => $dog_size
    ]
]);
?>
