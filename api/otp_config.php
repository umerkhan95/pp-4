<?php
function generateOTP($length = 6) {
    return str_pad(random_int(0, 999999), $length, '0', STR_PAD_LEFT);
}

function sendOTP($email, $otp) {
    // Send email logic here, e.g., using PHPMailer
    // Mail subject/body
    $subject = "Your OTP Code";
    $message = "Your OTP code is: $otp";
    $headers = "From: no-reply@example.com";

    return mail($email, $subject, $message, $headers);  // Update this with PHPMailer if needed
}
?>
