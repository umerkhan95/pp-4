<?php
require 'db.php';
require 'jwt_config.php';

header('Content-Type: application/json');

$data = json_decode(file_get_contents("php://input"));
$email = trim($data->email ?? '');
$password = trim($data->password ?? '');

if (empty($email) || empty($password)) {
    echo json_encode(["status" => "failed", "message" => "Email and password are required"]);
    exit;
}

if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
    echo json_encode(["status" => "failed", "message" => "Invalid email format"]);
    exit;
}

$stmt = $conn->prepare("SELECT * FROM users WHERE email = ?");
$stmt->execute([$email]);
$user = $stmt->fetch(PDO::FETCH_ASSOC);

if ($user && password_verify($password, $user['password'])) {
    $token = createJWT($user['id']);

    $stmt = $conn->prepare("SELECT COUNT(*) FROM devices WHERE user_id = ?");
    $stmt->execute([$user['id']]);
    $has_device = $stmt->fetchColumn() > 0;

    $stmt = $conn->prepare("SELECT COUNT(*) FROM sprinkler_schedule WHERE device_id = (SELECT id FROM devices WHERE user_id = ? LIMIT 1)");
    $stmt->execute([$user['id']]);
    $has_sprinkler = $stmt->fetchColumn() > 0;

    $stmt = $conn->prepare("SELECT COUNT(*) FROM dog_settings WHERE user_id = ?");
    $stmt->execute([$user['id']]);
    $has_dog_size = $stmt->fetchColumn() > 0;


    $stmt = $conn->prepare("SELECT MAX(id) FROM devices WHERE user_id = ?");
    $stmt->execute([$user['id']]);
    $device_id = $stmt->fetchColumn();

    echo json_encode(["status" => "success", "user" => array_merge($user, ["has_device" => $has_device], ["has_sprinkler" => $has_sprinkler], ["has_dog" => $has_dog_size], ["device_id" => $device_id]), "token" => $token]);
} else {
    echo json_encode(["status" => "failed", "message" => "Invalid credentials"]);
}