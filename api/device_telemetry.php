<?php
/**
 * Device Telemetry and Monitoring System
 * 
 * Implements real-time device monitoring with:
 * - Real-time telemetry data collection
 * - Device health monitoring
 * - Alert system for anomalies
 * - Performance analytics
 * - Predictive maintenance indicators
 */

require_once 'db.php';
require_once 'enhanced_auth.php';
require_once 'cache_manager.php';
require_once 'rate_limiter.php';

header('Content-Type: application/json');

// Apply rate limiting
applyRateLimit('telemetry', true);

try {
    // Authenticate user
    $user = requireAuth();
    $userId = $user['id'];
    
    $method = $_SERVER['REQUEST_METHOD'];
    
    switch ($method) {
        case 'POST':
            handleTelemetryUpdate($userId);
            break;
        case 'GET':
            handleTelemetryQuery($userId);
            break;
        default:
            throw new Exception("Method not allowed");
    }
    
} catch (Exception $e) {
    http_response_code(400);
    echo json_encode([
        'status' => 'error',
        'message' => $e->getMessage(),
        'timestamp' => date('Y-m-d H:i:s')
    ]);
}

/**
 * Handle telemetry data updates from ESP32 devices
 */
function handleTelemetryUpdate($userId) {
    global $conn;
    
    $data = json_decode(file_get_contents("php://input"), true);
    
    if (!$data || !isset($data['device_id'])) {
        throw new Exception("Missing device_id in telemetry data");
    }
    
    $deviceId = $data['device_id'];
    
    // Verify device ownership
    $stmt = $conn->prepare("SELECT id FROM devices WHERE id = ? AND user_id = ?");
    $stmt->execute([$deviceId, $userId]);
    if ($stmt->rowCount() == 0) {
        throw new Exception("Device not found or access denied");
    }
    
    // Extract telemetry data
    $telemetryData = [
        'device_id' => $deviceId,
        'timestamp' => $data['timestamp'] ?? date('Y-m-d H:i:s'),
        'wifi_signal' => $data['wifi_signal'] ?? null,
        'battery_level' => $data['battery_level'] ?? null,
        'memory_usage' => $data['memory_usage'] ?? null,
        'cpu_usage' => $data['cpu_usage'] ?? null,
        'temperature' => $data['temperature'] ?? null,
        'humidity' => $data['humidity'] ?? null,
        'water_level' => $data['water_level'] ?? null,
        'pump1_status' => $data['pump1_status'] ?? null,
        'pump2_status' => $data['pump2_status'] ?? null,
        'pump1_runtime' => $data['pump1_runtime'] ?? null,
        'pump2_runtime' => $data['pump2_runtime'] ?? null,
        'error_count' => $data['error_count'] ?? 0,
        'uptime' => $data['uptime'] ?? null,
        'firmware_version' => $data['firmware_version'] ?? null,
        'free_heap' => $data['free_heap'] ?? null,
        'wifi_reconnects' => $data['wifi_reconnects'] ?? 0,
        'last_error' => $data['last_error'] ?? null
    ];
    
    // Store telemetry data
    storeTelemetryData($telemetryData);
    
    // Update device status
    updateDeviceStatus($deviceId, $telemetryData);
    
    // Check for alerts
    $alerts = checkDeviceAlerts($deviceId, $telemetryData);
    
    // Analyze device health
    $healthScore = calculateDeviceHealth($deviceId, $telemetryData);
    
    // Cache latest telemetry
    $cacheManager = new CacheManager();
    $deviceCache = new DeviceCache($cacheManager);
    $deviceCache->cacheDeviceStatus($deviceId, $telemetryData, 60);
    
    echo json_encode([
        'status' => 'success',
        'message' => 'Telemetry data received',
        'health_score' => $healthScore,
        'alerts' => $alerts,
        'timestamp' => date('Y-m-d H:i:s')
    ]);
}

/**
 * Handle telemetry data queries
 */
function handleTelemetryQuery($userId) {
    global $conn;
    
    $deviceId = $_GET['device_id'] ?? '';
    $timeRange = $_GET['time_range'] ?? '1h'; // 1h, 6h, 24h, 7d, 30d
    $metrics = $_GET['metrics'] ?? 'all';
    
    if (empty($deviceId)) {
        throw new Exception("Missing device_id parameter");
    }
    
    // Verify device ownership
    $stmt = $conn->prepare("SELECT id FROM devices WHERE id = ? AND user_id = ?");
    $stmt->execute([$deviceId, $userId]);
    if ($stmt->rowCount() == 0) {
        throw new Exception("Device not found or access denied");
    }
    
    // Check cache first
    $cacheManager = new CacheManager();
    $cacheKey = "telemetry:{$deviceId}:{$timeRange}:{$metrics}";
    
    $cachedData = $cacheManager->get($cacheKey);
    if ($cachedData !== null) {
        echo $cachedData;
        return;
    }
    
    // Get time range
    $timeCondition = getTimeRangeCondition($timeRange);
    
    // Get telemetry data
    $telemetryData = getTelemetryData($deviceId, $timeCondition, $metrics);
    
    // Get device health trends
    $healthTrends = getDeviceHealthTrends($deviceId, $timeCondition);
    
    // Get alerts for the period
    $alerts = getDeviceAlerts($deviceId, $timeCondition);
    
    // Calculate statistics
    $statistics = calculateTelemetryStatistics($telemetryData);
    
    $response = [
        'status' => 'success',
        'device_id' => $deviceId,
        'time_range' => $timeRange,
        'data' => $telemetryData,
        'health_trends' => $healthTrends,
        'alerts' => $alerts,
        'statistics' => $statistics,
        'timestamp' => date('Y-m-d H:i:s')
    ];
    
    $responseJson = json_encode($response);
    
    // Cache the response
    $cacheManager->set($cacheKey, $responseJson, 300, ['telemetry', "device:{$deviceId}"]);
    
    echo $responseJson;
}

/**
 * Store telemetry data in database
 */
function storeTelemetryData($data) {
    global $conn;
    
    $stmt = $conn->prepare("
        INSERT INTO device_telemetry (
            device_id, timestamp, wifi_signal, battery_level, memory_usage, 
            cpu_usage, temperature, humidity, water_level, pump1_status, 
            pump2_status, pump1_runtime, pump2_runtime, error_count, 
            uptime, firmware_version, free_heap, wifi_reconnects, last_error
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ");
    
    $stmt->execute([
        $data['device_id'],
        $data['timestamp'],
        $data['wifi_signal'],
        $data['battery_level'],
        $data['memory_usage'],
        $data['cpu_usage'],
        $data['temperature'],
        $data['humidity'],
        $data['water_level'],
        $data['pump1_status'],
        $data['pump2_status'],
        $data['pump1_runtime'],
        $data['pump2_runtime'],
        $data['error_count'],
        $data['uptime'],
        $data['firmware_version'],
        $data['free_heap'],
        $data['wifi_reconnects'],
        $data['last_error']
    ]);
}

/**
 * Update device status table
 */
function updateDeviceStatus($deviceId, $data) {
    global $conn;
    
    $stmt = $conn->prepare("
        UPDATE device_status SET 
            wifi_signal = ?, battery_level = ?, water_level = ?, 
            pump1_status = ?, pump2_status = ?, temperature = ?, 
            humidity = ?, last_seen = NOW(), firmware_version = ?
        WHERE device_id = ?
    ");
    
    $stmt->execute([
        $data['wifi_signal'],
        $data['battery_level'],
        $data['water_level'],
        $data['pump1_status'],
        $data['pump2_status'],
        $data['temperature'],
        $data['humidity'],
        $data['firmware_version'],
        $deviceId
    ]);
}

/**
 * Check for device alerts and anomalies
 */
function checkDeviceAlerts($deviceId, $data) {
    $alerts = [];
    
    // Low battery alert
    if ($data['battery_level'] !== null && $data['battery_level'] < 20) {
        $alerts[] = [
            'type' => 'low_battery',
            'severity' => 'warning',
            'message' => "Battery level is low: {$data['battery_level']}%",
            'value' => $data['battery_level']
        ];
    }
    
    // Critical battery alert
    if ($data['battery_level'] !== null && $data['battery_level'] < 10) {
        $alerts[] = [
            'type' => 'critical_battery',
            'severity' => 'critical',
            'message' => "Battery level is critically low: {$data['battery_level']}%",
            'value' => $data['battery_level']
        ];
    }
    
    // Low water level alert
    if ($data['water_level'] !== null && $data['water_level'] < 20) {
        $alerts[] = [
            'type' => 'low_water',
            'severity' => 'warning',
            'message' => "Water level is low: {$data['water_level']}%",
            'value' => $data['water_level']
        ];
    }
    
    // High temperature alert
    if ($data['temperature'] !== null && $data['temperature'] > 40) {
        $alerts[] = [
            'type' => 'high_temperature',
            'severity' => 'warning',
            'message' => "Device temperature is high: {$data['temperature']}°C",
            'value' => $data['temperature']
        ];
    }
    
    // Memory usage alert
    if ($data['memory_usage'] !== null && $data['memory_usage'] > 85) {
        $alerts[] = [
            'type' => 'high_memory_usage',
            'severity' => 'warning',
            'message' => "Memory usage is high: {$data['memory_usage']}%",
            'value' => $data['memory_usage']
        ];
    }
    
    // WiFi signal alert
    if ($data['wifi_signal'] !== null && $data['wifi_signal'] < -80) {
        $alerts[] = [
            'type' => 'weak_wifi_signal',
            'severity' => 'info',
            'message' => "WiFi signal is weak: {$data['wifi_signal']} dBm",
            'value' => $data['wifi_signal']
        ];
    }
    
    // Error count alert
    if ($data['error_count'] !== null && $data['error_count'] > 5) {
        $alerts[] = [
            'type' => 'high_error_count',
            'severity' => 'warning',
            'message' => "High error count detected: {$data['error_count']} errors",
            'value' => $data['error_count']
        ];
    }
    
    // Store alerts in database
    if (!empty($alerts)) {
        storeDeviceAlerts($deviceId, $alerts);
    }
    
    return $alerts;
}

/**
 * Calculate device health score (0-100)
 */
function calculateDeviceHealth($deviceId, $data) {
    $healthScore = 100;
    
    // Battery health (20% weight)
    if ($data['battery_level'] !== null) {
        if ($data['battery_level'] < 10) {
            $healthScore -= 20;
        } elseif ($data['battery_level'] < 20) {
            $healthScore -= 10;
        } elseif ($data['battery_level'] < 30) {
            $healthScore -= 5;
        }
    }
    
    // WiFi signal health (15% weight)
    if ($data['wifi_signal'] !== null) {
        if ($data['wifi_signal'] < -90) {
            $healthScore -= 15;
        } elseif ($data['wifi_signal'] < -80) {
            $healthScore -= 10;
        } elseif ($data['wifi_signal'] < -70) {
            $healthScore -= 5;
        }
    }
    
    // Memory usage health (15% weight)
    if ($data['memory_usage'] !== null) {
        if ($data['memory_usage'] > 90) {
            $healthScore -= 15;
        } elseif ($data['memory_usage'] > 80) {
            $healthScore -= 10;
        } elseif ($data['memory_usage'] > 70) {
            $healthScore -= 5;
        }
    }
    
    // Temperature health (10% weight)
    if ($data['temperature'] !== null) {
        if ($data['temperature'] > 50) {
            $healthScore -= 10;
        } elseif ($data['temperature'] > 40) {
            $healthScore -= 5;
        }
    }
    
    // Error count health (20% weight)
    if ($data['error_count'] !== null) {
        if ($data['error_count'] > 10) {
            $healthScore -= 20;
        } elseif ($data['error_count'] > 5) {
            $healthScore -= 10;
        } elseif ($data['error_count'] > 2) {
            $healthScore -= 5;
        }
    }
    
    // Water level health (10% weight)
    if ($data['water_level'] !== null) {
        if ($data['water_level'] < 10) {
            $healthScore -= 10;
        } elseif ($data['water_level'] < 20) {
            $healthScore -= 5;
        }
    }
    
    // WiFi reconnects health (10% weight)
    if ($data['wifi_reconnects'] !== null) {
        if ($data['wifi_reconnects'] > 10) {
            $healthScore -= 10;
        } elseif ($data['wifi_reconnects'] > 5) {
            $healthScore -= 5;
        }
    }
    
    $healthScore = max(0, min(100, $healthScore));
    
    // Store health score
    storeDeviceHealthScore($deviceId, $healthScore);
    
    return $healthScore;
}

/**
 * Store device alerts
 */
function storeDeviceAlerts($deviceId, $alerts) {
    global $conn;
    
    foreach ($alerts as $alert) {
        $stmt = $conn->prepare("
            INSERT INTO device_alerts (device_id, alert_type, severity, message, alert_value, created_at)
            VALUES (?, ?, ?, ?, ?, NOW())
        ");
        
        $stmt->execute([
            $deviceId,
            $alert['type'],
            $alert['severity'],
            $alert['message'],
            $alert['value'] ?? null
        ]);
    }
}

/**
 * Store device health score
 */
function storeDeviceHealthScore($deviceId, $healthScore) {
    global $conn;
    
    $stmt = $conn->prepare("
        INSERT INTO device_health_history (device_id, health_score, recorded_at)
        VALUES (?, ?, NOW())
    ");
    
    $stmt->execute([$deviceId, $healthScore]);
}

/**
 * Get time range condition for SQL queries
 */
function getTimeRangeCondition($timeRange) {
    switch ($timeRange) {
        case '1h':
            return "timestamp >= DATE_SUB(NOW(), INTERVAL 1 HOUR)";
        case '6h':
            return "timestamp >= DATE_SUB(NOW(), INTERVAL 6 HOUR)";
        case '24h':
            return "timestamp >= DATE_SUB(NOW(), INTERVAL 24 HOUR)";
        case '7d':
            return "timestamp >= DATE_SUB(NOW(), INTERVAL 7 DAY)";
        case '30d':
            return "timestamp >= DATE_SUB(NOW(), INTERVAL 30 DAY)";
        default:
            return "timestamp >= DATE_SUB(NOW(), INTERVAL 1 HOUR)";
    }
}

/**
 * Get telemetry data from database
 */
function getTelemetryData($deviceId, $timeCondition, $metrics) {
    global $conn;
    
    $selectFields = $metrics === 'all' ? '*' : $metrics;
    
    $stmt = $conn->prepare("
        SELECT {$selectFields}
        FROM device_telemetry 
        WHERE device_id = ? AND {$timeCondition}
        ORDER BY timestamp DESC
        LIMIT 1000
    ");
    
    $stmt->execute([$deviceId]);
    return $stmt->fetchAll(PDO::FETCH_ASSOC);
}

/**
 * Get device health trends
 */
function getDeviceHealthTrends($deviceId, $timeCondition) {
    global $conn;
    
    $stmt = $conn->prepare("
        SELECT health_score, recorded_at
        FROM device_health_history 
        WHERE device_id = ? AND recorded_at >= DATE_SUB(NOW(), INTERVAL 24 HOUR)
        ORDER BY recorded_at DESC
        LIMIT 100
    ");
    
    $stmt->execute([$deviceId]);
    return $stmt->fetchAll(PDO::FETCH_ASSOC);
}

/**
 * Get device alerts
 */
function getDeviceAlerts($deviceId, $timeCondition) {
    global $conn;
    
    $stmt = $conn->prepare("
        SELECT alert_type, severity, message, alert_value, created_at
        FROM device_alerts 
        WHERE device_id = ? AND created_at >= DATE_SUB(NOW(), INTERVAL 24 HOUR)
        ORDER BY created_at DESC
        LIMIT 50
    ");
    
    $stmt->execute([$deviceId]);
    return $stmt->fetchAll(PDO::FETCH_ASSOC);
}

/**
 * Calculate telemetry statistics
 */
function calculateTelemetryStatistics($telemetryData) {
    if (empty($telemetryData)) {
        return [];
    }
    
    $stats = [];
    $numericFields = ['wifi_signal', 'battery_level', 'memory_usage', 'cpu_usage', 
                     'temperature', 'humidity', 'water_level', 'error_count'];
    
    foreach ($numericFields as $field) {
        $values = array_filter(array_column($telemetryData, $field), function($v) {
            return $v !== null && is_numeric($v);
        });
        
        if (!empty($values)) {
            $stats[$field] = [
                'min' => min($values),
                'max' => max($values),
                'avg' => round(array_sum($values) / count($values), 2),
                'count' => count($values)
            ];
        }
    }
    
    return $stats;
}
?>
