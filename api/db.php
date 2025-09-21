<?php
$host = "localhost";
$dbname = "u552717391_porchpotty";
// $username = "pixel_pet";  // change if needed
// $password = "1op8Z^SkUT.3Fbe7";      // change if needed
$username = "u552717391_porchpotty";  // change if needed
$password = "Codefied123!";      // change if needed

try {
    $conn = new PDO("mysql:host=$host;dbname=$dbname;charset=utf8mb4", $username, $password);
    $conn->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
} catch(PDOException $e) {
    die("Connection failed: " . $e->getMessage());
}
?>
