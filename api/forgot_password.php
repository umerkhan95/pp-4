<?php
require 'db.php';
require 'otp_config.php';

header('Content-Type: application/json');

$data = json_decode(file_get_contents("php://input"));
$email = trim($data->email ?? '');

// Validate email
if (!$email || !filter_var($email, FILTER_VALIDATE_EMAIL)) {
    echo json_encode(["status" => "failed", "message" => "Valid email is required"]);
    return;
}

// Check if user exists
$stmt = $conn->prepare("SELECT * FROM users WHERE email = ?");
$stmt->execute([$email]);

if ($stmt->rowCount() === 0) {
    echo json_encode(["status" => "failed", "message" => "Email not registered"]);
    return;
}

// Generate and store OTP
$otp = generateOTP();
$expires_at = date('Y-m-d H:i:s', strtotime('+5 minutes'));

$stmt = $conn->prepare("REPLACE INTO user_otps (email, otp, expires_at) VALUES (?, ?, ?)");
$stmt->execute([$email, $otp, $expires_at]);

// Send OTP
sendOTP($email, $otp);

// Respond
echo json_encode(["status" => "success", "message" => "OTP sent to your email"]);
?>
