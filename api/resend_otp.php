<?php
require 'db.php';
require 'send_email.php';

$email = $_POST['email'] ?? '';
if (!$email) {
    echo json_encode(["status" => "error", "message" => "Email required"]);
    exit;
}

$otp = rand(100000, 999999);
$expiry = date("Y-m-d H:i:s", strtotime("+5 minutes"));

$stmt = $conn->prepare("UPDATE otps SET otp = ?, expires_at = ? WHERE email = ?");
$stmt->execute([$otp, $expiry, $email]);

if ($stmt->rowCount() > 0 && sendOTPEmail($email, $otp)) {
    echo json_encode(["status" => "success", "message" => "OTP resent"]);
} else {
    echo json_encode(["status" => "error", "message" => "User not found or mail error"]);
}
