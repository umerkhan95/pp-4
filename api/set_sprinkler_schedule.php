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
    if (!$data || !isset($data->device_id)) {
        echo json_encode(["status" => "failed", "message" => "Missing device_id"]);
        exit;
    }

    // Validate device belongs to user
    $checkStmt = $conn->prepare("SELECT id FROM devices WHERE id = ? AND user_id = ?");
    $checkStmt->execute([$data->device_id, $user_id]);
    if ($checkStmt->rowCount() == 0) {
        throw new Exception("Device not found or does not belong to user");
    }

    $device_id = $data->device_id;
    
    // Extract parameters from request
    $duration_sprinkler = $data->duration_sprinkler ?? 1; // Default 1 minute
    $cycles_sprinkler = $data->cycles_sprinkler ?? 4; // Default 4 cycles
    $active_days = $data->active_days ?? [1,1,1,1,1,1,1]; // Default all days active
    $start_time = $data->start_time ?? [8,0]; // Default 8:00 AM
    $times = $data->times ?? []; // Schedule times array
    $current_time = $data->current_time ?? date('l, F j Y H:i:s'); // Current timestamp
    
    // Validate active_days array
    if (!is_array($active_days) || count($active_days) != 7) {
        throw new Exception("active_days must be an array of 7 integers (0 or 1)");
    }
    
    // Validate start_time array
    if (!is_array($start_time) || count($start_time) != 2) {
        throw new Exception("start_time must be an array of [hour, minute]");
    }
    
    // Validate start_time values
    if ($start_time[0] < 0 || $start_time[0] > 23 || $start_time[1] < 0 || $start_time[1] > 59) {
        throw new Exception("Invalid start_time values");
    }

    // Begin transaction
    $conn->beginTransaction();

    try {
        // Delete previous schedule and settings
        $conn->prepare("DELETE FROM sprinkler_schedule WHERE device_id = ?")->execute([$device_id]);
        
        // Update or insert device settings
        $settingsStmt = $conn->prepare("
            INSERT INTO device_settings (device_id, duration_sprinkler, cycles_sprinkler, start_hour, start_minute, active_days, updated_at) 
            VALUES (?, ?, ?, ?, ?, ?, NOW())
            ON DUPLICATE KEY UPDATE 
            duration_sprinkler = VALUES(duration_sprinkler),
            cycles_sprinkler = VALUES(cycles_sprinkler),
            start_hour = VALUES(start_hour),
            start_minute = VALUES(start_minute),
            active_days = VALUES(active_days),
            updated_at = NOW()
        ");
        
        $active_days_json = json_encode($active_days);
        $settingsStmt->execute([
            $device_id, 
            $duration_sprinkler, 
            $cycles_sprinkler, 
            $start_time[0], 
            $start_time[1], 
            $active_days_json
        ]);

        // Insert schedule times if provided
        if (!empty($times) && is_array($times)) {
            $scheduleStmt = $conn->prepare("INSERT INTO sprinkler_schedule (device_id, time_slot) VALUES (?, ?)");
            foreach ($times as $time) {
                if (!is_string($time) || !preg_match('/^([0-1][0-9]|2[0-3]):[0-5][0-9]$/', $time)) {
                    throw new Exception("Invalid time format: " . $time . ". Use HH:MM format.");
                }
                $scheduleStmt->execute([$device_id, $time]);
            }
        }

        // Commit transaction
        $conn->commit();

        // Fetch updated schedule
        $fetchStmt = $conn->prepare("SELECT time_slot FROM sprinkler_schedule WHERE device_id = ? ORDER BY time_slot");
        $fetchStmt->execute([$device_id]);
        $schedule = $fetchStmt->fetchAll(PDO::FETCH_COLUMN);

        // Prepare response data
        $response_data = [
            "device_id" => $device_id,
            "duration_sprinkler" => $duration_sprinkler,
            "cycles_sprinkler" => $cycles_sprinkler,
            "active_days" => $active_days,
            "start_time" => $start_time,
            "schedule" => $schedule,
            "current_time" => $current_time,
            "updated_at" => date('Y-m-d H:i:s')
        ];

        echo json_encode([
            "status" => "success", 
            "message" => "Schedule and settings updated successfully",
            "data" => $response_data
        ]);

    } catch (Exception $e) {
        $conn->rollback();
        throw $e;
    }

} catch (Exception $e) {
    http_response_code(400);
    echo json_encode([
        "status" => "failed", 
        "error" => $e->getMessage(),
        "timestamp" => date('Y-m-d H:i:s')
    ]);
}
