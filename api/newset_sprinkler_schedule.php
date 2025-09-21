<?php
require 'db.php';
require 'jwt_config.php';
header('Content-Type: application/json');

try {
    // Validate token
    $headers = getallheaders();
    $auth = $headers['Authorization'] ?? '';
    if (!$auth) {
        throw new Exception("Authorization header missing");
    }

    $token = str_replace('Bearer ', '', $auth);
    $decoded = validateJWT($token);
    $user_id = $decoded->user_id ?? null;

    if (!$user_id) {
        echo json_encode(["status" => "failed", "message" => "Invalid token payload"]);
        exit;
    }

    // Validate JSON input
    $data = json_decode(file_get_contents("php://input"));
    if (!$data || !isset($data->device_id) || !isset($data->times) || !is_array($data->times)) {
        echo json_encode(["status" => "failed", "message" => "Missing or invalid 'device_id' or 'times'"]);
        exit;
    }

    $device_id = $data->device_id;
    $times = $data->times;
    $timezone = $data->timezone ?? 'Asia/Karachi';

    if (!in_array($timezone, timezone_identifiers_list())) {
        throw new Exception("Invalid timezone: " . $timezone);
    }

    // Validate device belongs to user
    $checkStmt = $conn->prepare("SELECT id FROM devices WHERE id = ? AND user_id = ?");
    $checkStmt->execute([$device_id, $user_id]);
    if ($checkStmt->rowCount() == 0) {
        throw new Exception("Device not found or does not belong to user");
    }

    // Delete previous schedule
    $conn->prepare("DELETE FROM sprinkler_schedule WHERE device_id = ?")->execute([$device_id]);

    // Simple fixed SQL based on your exact table structure - using backticks for reserved keywords
    $sql = "INSERT INTO sprinkler_schedule (`device_id`, `time_slot`, `local_time`, `utc_time`, `timezone`, `utc_offset`) VALUES (?, ?, ?, ?, ?, ?)";
    $stmt = $conn->prepare($sql);

    $inserted_schedules = [];

    foreach ($times as $time) {
        if (!is_string($time) || !preg_match('/^([0-1][0-9]|2[0-3]):[0-5][0-9]$/', $time)) {
            throw new Exception("Invalid time format: " . $time);
        }

        // Create datetime objects for timezone conversion
        $local_datetime = new DateTime($time, new DateTimeZone($timezone));
        $utc_datetime = clone $local_datetime;
        $utc_datetime->setTimezone(new DateTimeZone('UTC'));

        $local_time = $local_datetime->format('H:i');
        $utc_time = $utc_datetime->format('H:i');
        $utc_offset = $local_datetime->format('P');

        // Execute with exact parameters
        $stmt->execute([
            $device_id,
            $time,
            $local_time,
            $utc_time,
            $timezone,
            $utc_offset
        ]);

        $inserted_schedules[] = [
            'time_slot' => $time,
            'local_time' => $local_time,
            'utc_time' => $utc_time,
            'timezone' => $timezone,
            'utc_offset' => $utc_offset
        ];
    }

    // Fetch the updated schedule - using backticks for reserved keywords
    $fetchStmt = $conn->prepare("
        SELECT `time_slot`, `local_time`, `utc_time`, `timezone`, `utc_offset`, `created_at`
        FROM sprinkler_schedule 
        WHERE `device_id` = ? 
        ORDER BY `time_slot`
    ");
    $fetchStmt->execute([$device_id]);
    $schedule = $fetchStmt->fetchAll(PDO::FETCH_ASSOC);

    echo json_encode([
        "status" => "success",
        "message" => "Schedule updated successfully",
        "data" => [
            "device_id" => $device_id,
            "timezone" => $timezone,
            "schedule_count" => count($schedule),
            "schedule" => $schedule
        ]
    ]);

} catch (Exception $e) {
    http_response_code(400);
    echo json_encode([
        "status" => "failed",
        "error" => $e->getMessage(),
        "timestamp" => date('Y-m-d H:i:s')
    ]);
}
?>