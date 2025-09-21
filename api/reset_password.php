<?php
require 'db.php';

header('Content-Type: application/json');

$data = json_decode(file_get_contents("php://input"));
$email = trim($data->email ?? '');
$otp = trim($data->otp ?? '');
$new_password = trim($data->new_password ?? '');

if (!$email || !$otp || !$new_password) {
    echo json_encode(["status" => "failed", "message" => "Email, OTP, and new password are required"]);
    return;
}
$stmt = $conn->prepare("SELECT * FROM user_otps WHERE email = ? AND otp = ? AND expires_at > NOW()");
$stmt->execute([$email, $otp]);

if ($stmt->rowCount() === 0) {
    echo json_encode(["status" => "failed", "message" => "Invalid or expired OTP"]);
    return;
}

$hashedPassword = password_hash($new_password, PASSWORD_BCRYPT);

$update = $conn->prepare("UPDATE users SET password = ? WHERE email = ?");
$update->execute([$hashedPassword, $email]);

$conn->prepare("DELETE FROM user_otps WHERE email = ?")->execute([$email]);

echo json_encode(["status" => "success", "message" => "Password reset successful"]);
?>
