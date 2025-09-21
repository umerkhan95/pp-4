<?php
require 'db.php';
require 'jwt_config.php';
header('Content-Type: application/json');

try {
    // Validate token
    $headers = getallheaders();
    $auth = $headers['Authorization'] ?? '';
    $token = str_replace('Bearer ', '', $auth);
    $decoded = validateJWT($token);
    $user_id = $decoded->user_id ?? null;
    
    if (!$user_id) {
        throw new Exception("Invalid or missing token");
    }
    
    // Get device_id from query string
    $device_id = $_GET['device_id'] ?? '';
    if (empty($device_id)) {
        throw new Exception("Missing device_id parameter");
    }
    
    // Validate that device belongs to user
    $checkStmt = $conn->prepare("SELECT id FROM devices WHERE id = ? AND user_id = ?");
    $checkStmt->execute([$device_id, $user_id]);
    if ($checkStmt->rowCount() == 0) {
        throw new Exception("Device not found or does not belong to user");
    }
    
    // Get timezone preference from request or use device's saved timezone
    $requested_timezone = $_GET['timezone'] ?? null;
    
    // Fetch device settings (enhanced parameters)
    $settingsStmt = $conn->prepare("
        SELECT 
            duration_sprinkler, 
            cycles_sprinkler, 
            start_hour, 
            start_minute, 
            active_days, 
            updated_at
        FROM device_settings 
        WHERE device_id = ?
    ");
    $settingsStmt->execute([$device_id]);
    $settings = $settingsStmt->fetch(PDO::FETCH_ASSOC);
    
    // Fetch schedule with timezone information
    $stmt = $conn->prepare("
        SELECT 
            time_slot, 
            local_time, 
            utc_time, 
            timezone, 
            utc_offset,
            created_at
        FROM sprinkler_schedule 
        WHERE device_id = ? 
        ORDER BY utc_time ASC
    ");
    $stmt->execute([$device_id]);
    $schedules = $stmt->fetchAll(PDO::FETCH_ASSOC);
    
    if (empty($schedules)) {
        // Prepare response with settings even if no schedule times exist
        $response_data = [
            "device_id" => $device_id,
            "schedule_count" => 0,
            "schedule" => [],
            "detailed_schedule" => []
        ];
        
        // Add enhanced parameters if settings exist
        if ($settings) {
            $response_data["duration_sprinkler"] = (int)$settings['duration_sprinkler'];
            $response_data["cycles_sprinkler"] = (int)$settings['cycles_sprinkler'];
            $response_data["start_time"] = [(int)$settings['start_hour'], (int)$settings['start_minute']];
            $response_data["active_days"] = json_decode($settings['active_days'], true) ?: [1,1,1,1,1,1,1];
            $response_data["settings_updated"] = $settings['updated_at'];
        } else {
            // Default values if no settings found
            $response_data["duration_sprinkler"] = 1;
            $response_data["cycles_sprinkler"] = 4;
            $response_data["start_time"] = [8, 0];
            $response_data["active_days"] = [1,1,1,1,1,1,1];
            $response_data["settings_updated"] = null;
        }
        
        echo json_encode([
            "status" => "success",
            "message" => "No schedule times found, returning device settings",
            "data" => $response_data
        ]);
        exit;
    }
    
    // Get the device's timezone from the first schedule entry
    $device_timezone = $schedules[0]['timezone'];
    $target_timezone = $requested_timezone ?: $device_timezone;
    
    // Convert times to requested timezone if different
    $converted_schedule = [];
    foreach ($schedules as $schedule) {
        $converted_time = $schedule['time_slot'];
        
        // If requested timezone is different from stored timezone, convert
        if ($requested_timezone && $requested_timezone !== $schedule['timezone']) {
            try {
                $utc_datetime = new DateTime($schedule['utc_time'], new DateTimeZone('UTC'));
                $target_datetime = $utc_datetime->setTimezone(new DateTimeZone($requested_timezone));
                $converted_time = $target_datetime->format('H:i');
            } catch (Exception $e) {
                // If conversion fails, use original time
                $converted_time = $schedule['time_slot'];
            }
        }
        
        $converted_schedule[] = [
            'time_slot' => $converted_time,
            'original_time' => $schedule['time_slot'],
            'local_time' => $schedule['local_time'],
            'utc_time' => $schedule['utc_time'],
            'timezone' => $target_timezone,
            'original_timezone' => $schedule['timezone'],
            'utc_offset' => $schedule['utc_offset']
        ];
    }
    
    // Also return simple array for backward compatibility
    $simple_schedule = array_column($converted_schedule, 'time_slot');
    
    // Prepare enhanced response data
    $response_data = [
        "device_id" => $device_id,
        "timezone" => $target_timezone,
        "schedule_count" => count($converted_schedule),
        "schedule" => $simple_schedule, // For backward compatibility
        "detailed_schedule" => $converted_schedule,
        "last_updated" => $schedules[0]['created_at'] ?? null
    ];
    
    // Add enhanced parameters if settings exist
    if ($settings) {
        $response_data["duration_sprinkler"] = (int)$settings['duration_sprinkler'];
        $response_data["cycles_sprinkler"] = (int)$settings['cycles_sprinkler'];
        $response_data["start_time"] = [(int)$settings['start_hour'], (int)$settings['start_minute']];
        $response_data["active_days"] = json_decode($settings['active_days'], true) ?: [1,1,1,1,1,1,1];
        $response_data["settings_updated"] = $settings['updated_at'];
    } else {
        // Default values if no settings found
        $response_data["duration_sprinkler"] = 1;
        $response_data["cycles_sprinkler"] = 4;
        $response_data["start_time"] = [8, 0];
        $response_data["active_days"] = [1,1,1,1,1,1,1];
        $response_data["settings_updated"] = null;
    }
    
    echo json_encode([
        "status" => "success",
        "data" => $response_data
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