<?php
require 'db.php';

header('Content-Type: application/json');

$data = json_decode(file_get_contents("php://input"));
$email = trim($data->email ?? null);
$otp = trim($data->otp ?? null);

if (!$email || !$otp) {
    echo json_encode(["status" => "failed", "message" => "Email and OTP are required"]);
    return;
}

$stmt = $conn->prepare("SELECT * FROM user_otps WHERE email = ? AND otp = ? AND expires_at > NOW()");
$stmt->execute([$email, $otp]);

if ($stmt->rowCount() > 0) {
    echo json_encode(["status" => "success", "message" => "OTP verified"]);
} else {
    echo json_encode(["status" => "failed", "message" => "Invalid or expired OTP"]);
}
?>
