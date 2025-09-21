<?php
use Firebase\JWT\JWT;
use Firebase\JWT\Key;

require_once __DIR__ . '/vendor/autoload.php'; // composer autoload

$JWT_SECRET = 'your_super_secret_key';

function createJWT($user_id) {
    global $JWT_SECRET;

    $payload = [
        'iss' => 'https://porchpotty.pixelstechnologies.com', // issuer
        'aud' => 'https://porchpotty.pixelstechnologies.com', // audience
        'iat' => time(),
        'exp' => time() + 3600, // 1 hour expiration
        'user_id' => $user_id
    ];

    return JWT::encode($payload, $JWT_SECRET, 'HS256');
}

function validateJWT($token) {
    global $JWT_SECRET;

    return JWT::decode($token, new Key($JWT_SECRET, 'HS256'));
}
