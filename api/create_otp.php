<?php
require 'db.php';
require 'otp_config.php';

$data = json_decode(file_get_contents("php://input"));
$email = trim($data->email ?? '');

if (!$email) {
    echo json_encode(["error" => "Email is required"]);
    exit;
}

$otp = generateOTP();
$expires_at = date('Y-m-d H:i:s', strtotime('+5 minutes'));

$stmt = $conn->prepare("REPLACE INTO user_otps (email, otp, expires_at) VALUES (?, ?, ?)");
$stmt->execute([$email, $otp, $expires_at]);

sendOTP($email, $otp);

echo json_encode(["status" => "otp_sent"]);
?>
