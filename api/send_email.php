<?php
use PHPMailer\PHPMailer\PHPMailer;
use PHPMailer\PHPMailer\Exception;

require_once 'PHPMailer/PHPMailer.php';
require_once 'PHPMailer/SMTP.php';
require_once 'PHPMailer/Exception.php';

function sendOTPEmail($to, $otp) {
    $mail = new PHPMailer(true);

    try {
        // Server settings
        $mail->isSMTP();
        $mail->Host = 'mail.pixelstechnologies.com'; // e.g., smtp.gmail.com or your hosting SMTP
        $mail->SMTPAuth = true;
        $mail->Username = 'pp@pixelstechnologies.com';  // your SMTP username
        $mail->Password = 'c[G,ax_$+nb#';    // your SMTP password
        $mail->SMTPSecure = 'tls';
        $mail->Port = 587;

        // Email content
        $mail->setFrom('pp@pixelstechnologies.com', 'porch potty');
        $mail->addAddress($to);
        $mail->isHTML(true);
        $mail->Subject = 'Your OTP Code';
        $mail->Body    = "Your OTP is: <b>$otp</b>. It is valid for 5 minutes.";

        $mail->send();
        return true;
    } catch (Exception $e) {
        return false;
    }
}
