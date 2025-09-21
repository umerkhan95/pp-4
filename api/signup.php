<?php
require 'db.php';
require 'jwt_config.php';
error_reporting(E_ALL & ~E_WARNING);

header('Content-Type: application/json');

$data = json_decode(file_get_contents("php://input"));

$email = trim($data->email ?? '');
$passwordRaw = trim($data->password ?? '');

// Input validation
if (empty($email) || empty($passwordRaw)) {
    echo json_encode(["status" => "failed", "message" => "Email and password are required"]);
    exit;
}

if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
    echo json_encode(["status" => "failed", "message" => "Invalid email format"]);
    exit;
}

$password = password_hash($passwordRaw, PASSWORD_DEFAULT);

$stmt = $conn->prepare("SELECT * FROM users WHERE email = ?");
$stmt->execute([$email]);
if ($stmt->rowCount() > 0) {
    echo json_encode(["status" => "failed", "message" => "Email already exists"]);
    exit;
}


$conn->prepare("INSERT INTO users (email, password) VALUES (?, ?)")->execute([$email, $password]);

$stmt = $conn->prepare("SELECT * FROM users WHERE email = ?");
$stmt->execute([$email]);
$user = $stmt->fetch(PDO::FETCH_ASSOC);


$token = createJWT($user['id']);
echo json_encode(["status" => "success", "message" => "User created successfully", "token"=>$token]);
?>