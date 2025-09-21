<?php
/**
 * Comprehensive Logging and Monitoring System
 * 
 * Implements structured logging with:
 * - Multiple log levels and channels
 * - Real-time log streaming
 * - Log aggregation and analysis
 * - Performance monitoring
 * - Security event tracking
 * - Automated alerting
 */

class LoggingSystem {
    private $logLevels = [
        'DEBUG' => 0,
        'INFO' => 1,
        'WARNING' => 2,
        'ERROR' => 3,
        'CRITICAL' => 4
    ];
    
    private $logChannels = [
        'api' => '/var/log/pp4/api.log',
        'security' => '/var/log/pp4/security.log',
        'device' => '/var/log/pp4/device.log',
        'performance' => '/var/log/pp4/performance.log',
        'error' => '/var/log/pp4/error.log',
        'audit' => '/var/log/pp4/audit.log'
    ];
    
    private $conn;
    private $minLogLevel;
    private $enableDatabaseLogging;
    private $enableRealTimeStreaming;
    
    public function __construct($database, $minLogLevel = 'INFO') {
        $this->conn = $database;
        $this->minLogLevel = $this->logLevels[$minLogLevel] ?? 1;
        $this->enableDatabaseLogging = true;
        $this->enableRealTimeStreaming = false;
        
        // Ensure log directories exist
        $this->ensureLogDirectories();
    }
    
    /**
     * Log a message with context
     */
    public function log($level, $message, $context = [], $channel = 'api') {
        $levelValue = $this->logLevels[$level] ?? 1;
        
        if ($levelValue < $this->minLogLevel) {
            return; // Skip logging if below minimum level
        }
        
        $logEntry = $this->formatLogEntry($level, $message, $context, $channel);
        
        // Write to file
        $this->writeToFile($logEntry, $channel);
        
        // Write to database if enabled
        if ($this->enableDatabaseLogging) {
            $this->writeToDatabase($level, $message, $context, $channel);
        }
        
        // Stream in real-time if enabled
        if ($this->enableRealTimeStreaming) {
            $this->streamRealTime($logEntry);
        }
        
        // Check for alert conditions
        $this->checkAlertConditions($level, $message, $context, $channel);
    }
    
    /**
     * Convenience methods for different log levels
     */
    public function debug($message, $context = [], $channel = 'api') {
        $this->log('DEBUG', $message, $context, $channel);
    }
    
    public function info($message, $context = [], $channel = 'api') {
        $this->log('INFO', $message, $context, $channel);
    }
    
    public function warning($message, $context = [], $channel = 'api') {
        $this->log('WARNING', $message, $context, $channel);
    }
    
    public function error($message, $context = [], $channel = 'error') {
        $this->log('ERROR', $message, $context, $channel);
    }
    
    public function critical($message, $context = [], $channel = 'error') {
        $this->log('CRITICAL', $message, $context, $channel);
    }
    
    /**
     * Security-specific logging methods
     */
    public function logSecurityEvent($event, $details = []) {
        $context = array_merge([
            'ip_address' => $this->getClientIP(),
            'user_agent' => $_SERVER['HTTP_USER_AGENT'] ?? '',
            'timestamp' => date('Y-m-d H:i:s'),
            'session_id' => session_id()
        ], $details);
        
        $this->log('WARNING', "Security event: {$event}", $context, 'security');
    }
    
    public function logAPIAccess($endpoint, $method, $responseCode, $responseTime, $userId = null) {
        $context = [
            'endpoint' => $endpoint,
            'method' => $method,
            'response_code' => $responseCode,
            'response_time_ms' => $responseTime,
            'user_id' => $userId,
            'ip_address' => $this->getClientIP(),
            'user_agent' => $_SERVER['HTTP_USER_AGENT'] ?? '',
            'request_size' => $_SERVER['CONTENT_LENGTH'] ?? 0
        ];
        
        $level = $responseCode >= 500 ? 'ERROR' : ($responseCode >= 400 ? 'WARNING' : 'INFO');
        $this->log($level, "API access: {$method} {$endpoint}", $context, 'api');
    }
    
    public function logDeviceEvent($deviceId, $event, $details = []) {
        $context = array_merge([
            'device_id' => $deviceId,
            'event_type' => $event,
            'timestamp' => date('Y-m-d H:i:s')
        ], $details);
        
        $this->log('INFO', "Device event: {$event}", $context, 'device');
    }
    
    public function logPerformanceMetric($metric, $value, $context = []) {
        $logContext = array_merge([
            'metric' => $metric,
            'value' => $value,
            'timestamp' => microtime(true)
        ], $context);
        
        $this->log('INFO', "Performance metric: {$metric} = {$value}", $logContext, 'performance');
    }
    
    /**
     * Format log entry as structured JSON
     */
    private function formatLogEntry($level, $message, $context, $channel) {
        $entry = [
            'timestamp' => date('c'),
            'level' => $level,
            'channel' => $channel,
            'message' => $message,
            'context' => $context,
            'extra' => [
                'memory_usage' => memory_get_usage(true),
                'peak_memory' => memory_get_peak_usage(true),
                'execution_time' => microtime(true) - $_SERVER['REQUEST_TIME_FLOAT'],
                'request_id' => $this->getRequestId()
            ]
        ];
        
        return json_encode($entry, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE) . "\n";
    }
    
    /**
     * Write log entry to file
     */
    private function writeToFile($logEntry, $channel) {
        $logFile = $this->logChannels[$channel] ?? $this->logChannels['api'];
        
        // Use file locking to prevent corruption
        file_put_contents($logFile, $logEntry, FILE_APPEND | LOCK_EX);
    }
    
    /**
     * Write log entry to database
     */
    private function writeToDatabase($level, $message, $context, $channel) {
        try {
            $stmt = $this->conn->prepare("
                INSERT INTO system_logs (level, channel, message, context, ip_address, user_agent, created_at)
                VALUES (?, ?, ?, ?, ?, ?, NOW())
            ");
            
            $stmt->execute([
                $level,
                $channel,
                $message,
                json_encode($context),
                $this->getClientIP(),
                $_SERVER['HTTP_USER_AGENT'] ?? ''
            ]);
        } catch (Exception $e) {
            // Fallback to file logging if database fails
            error_log("Failed to write to database log: " . $e->getMessage());
        }
    }
    
    /**
     * Stream logs in real-time (WebSocket or SSE)
     */
    private function streamRealTime($logEntry) {
        // Implement real-time streaming (WebSocket/SSE)
        // This would typically push to a message queue or WebSocket server
    }
    
    /**
     * Check for conditions that should trigger alerts
     */
    private function checkAlertConditions($level, $message, $context, $channel) {
        // Critical errors always trigger alerts
        if ($level === 'CRITICAL') {
            $this->triggerAlert('critical_error', $message, $context);
        }
        
        // Security events
        if ($channel === 'security') {
            $this->triggerAlert('security_event', $message, $context);
        }
        
        // High error rate detection
        if ($level === 'ERROR') {
            $this->checkErrorRate();
        }
        
        // Performance degradation
        if ($channel === 'performance' && isset($context['value'])) {
            $this->checkPerformanceThresholds($context['metric'], $context['value']);
        }
    }
    
    /**
     * Trigger an alert
     */
    private function triggerAlert($alertType, $message, $context) {
        try {
            $stmt = $this->conn->prepare("
                INSERT INTO system_alerts (alert_type, message, context, severity, status, created_at)
                VALUES (?, ?, ?, ?, 'active', NOW())
            ");
            
            $severity = $this->determineAlertSeverity($alertType, $context);
            
            $stmt->execute([
                $alertType,
                $message,
                json_encode($context),
                $severity
            ]);
            
            // Send notification (email, Slack, etc.)
            $this->sendAlertNotification($alertType, $message, $severity);
            
        } catch (Exception $e) {
            error_log("Failed to trigger alert: " . $e->getMessage());
        }
    }
    
    /**
     * Check error rate for anomaly detection
     */
    private function checkErrorRate() {
        try {
            // Count errors in the last 5 minutes
            $stmt = $this->conn->prepare("
                SELECT COUNT(*) as error_count
                FROM system_logs 
                WHERE level IN ('ERROR', 'CRITICAL') 
                AND created_at > DATE_SUB(NOW(), INTERVAL 5 MINUTE)
            ");
            $stmt->execute();
            $result = $stmt->fetch(PDO::FETCH_ASSOC);
            
            if ($result['error_count'] > 10) { // Threshold: 10 errors in 5 minutes
                $this->triggerAlert('high_error_rate', 
                    "High error rate detected: {$result['error_count']} errors in 5 minutes", 
                    ['error_count' => $result['error_count']]
                );
            }
        } catch (Exception $e) {
            error_log("Failed to check error rate: " . $e->getMessage());
        }
    }
    
    /**
     * Check performance thresholds
     */
    private function checkPerformanceThresholds($metric, $value) {
        $thresholds = [
            'response_time' => 5000, // 5 seconds
            'memory_usage' => 90,    // 90%
            'cpu_usage' => 80,       // 80%
            'disk_usage' => 85       // 85%
        ];
        
        if (isset($thresholds[$metric]) && $value > $thresholds[$metric]) {
            $this->triggerAlert('performance_threshold', 
                "Performance threshold exceeded: {$metric} = {$value}", 
                ['metric' => $metric, 'value' => $value, 'threshold' => $thresholds[$metric]]
            );
        }
    }
    
    /**
     * Determine alert severity
     */
    private function determineAlertSeverity($alertType, $context) {
        $severityMap = [
            'critical_error' => 'critical',
            'security_event' => 'high',
            'high_error_rate' => 'high',
            'performance_threshold' => 'medium'
        ];
        
        return $severityMap[$alertType] ?? 'low';
    }
    
    /**
     * Send alert notification
     */
    private function sendAlertNotification($alertType, $message, $severity) {
        // Implement notification sending (email, Slack, SMS, etc.)
        // This would typically use a notification service
        
        // For now, just log the alert
        error_log("ALERT [{$severity}] {$alertType}: {$message}");
    }
    
    /**
     * Get logs with filtering and pagination
     */
    public function getLogs($filters = [], $limit = 100, $offset = 0) {
        $whereConditions = [];
        $params = [];
        
        if (!empty($filters['level'])) {
            $whereConditions[] = "level = ?";
            $params[] = $filters['level'];
        }
        
        if (!empty($filters['channel'])) {
            $whereConditions[] = "channel = ?";
            $params[] = $filters['channel'];
        }
        
        if (!empty($filters['start_date'])) {
            $whereConditions[] = "created_at >= ?";
            $params[] = $filters['start_date'];
        }
        
        if (!empty($filters['end_date'])) {
            $whereConditions[] = "created_at <= ?";
            $params[] = $filters['end_date'];
        }
        
        if (!empty($filters['search'])) {
            $whereConditions[] = "message LIKE ?";
            $params[] = '%' . $filters['search'] . '%';
        }
        
        $whereClause = !empty($whereConditions) ? 'WHERE ' . implode(' AND ', $whereConditions) : '';
        
        $stmt = $this->conn->prepare("
            SELECT * FROM system_logs 
            {$whereClause}
            ORDER BY created_at DESC 
            LIMIT ? OFFSET ?
        ");
        
        $params[] = $limit;
        $params[] = $offset;
        
        $stmt->execute($params);
        return $stmt->fetchAll(PDO::FETCH_ASSOC);
    }
    
    /**
     * Get log statistics
     */
    public function getLogStatistics($timeRange = '24h') {
        $interval = $this->getIntervalFromRange($timeRange);
        
        $stmt = $this->conn->prepare("
            SELECT 
                level,
                channel,
                COUNT(*) as count,
                DATE_FORMAT(created_at, '%Y-%m-%d %H:00:00') as hour_bucket
            FROM system_logs 
            WHERE created_at > DATE_SUB(NOW(), INTERVAL {$interval})
            GROUP BY level, channel, hour_bucket
            ORDER BY hour_bucket DESC
        ");
        
        $stmt->execute();
        return $stmt->fetchAll(PDO::FETCH_ASSOC);
    }
    
    /**
     * Utility methods
     */
    private function ensureLogDirectories() {
        foreach ($this->logChannels as $channel => $logFile) {
            $directory = dirname($logFile);
            if (!is_dir($directory)) {
                mkdir($directory, 0755, true);
            }
        }
    }
    
    private function getClientIP() {
        $ipKeys = [
            'HTTP_CF_CONNECTING_IP',
            'HTTP_CLIENT_IP',
            'HTTP_X_FORWARDED_FOR',
            'HTTP_X_FORWARDED',
            'HTTP_X_CLUSTER_CLIENT_IP',
            'HTTP_FORWARDED_FOR',
            'HTTP_FORWARDED',
            'REMOTE_ADDR'
        ];
        
        foreach ($ipKeys as $key) {
            if (!empty($_SERVER[$key])) {
                $ips = explode(',', $_SERVER[$key]);
                $ip = trim($ips[0]);
                if (filter_var($ip, FILTER_VALIDATE_IP, FILTER_FLAG_NO_PRIV_RANGE | FILTER_FLAG_NO_RES_RANGE)) {
                    return $ip;
                }
            }
        }
        
        return $_SERVER['REMOTE_ADDR'] ?? '0.0.0.0';
    }
    
    private function getRequestId() {
        static $requestId = null;
        if ($requestId === null) {
            $requestId = uniqid('req_', true);
        }
        return $requestId;
    }
    
    private function getIntervalFromRange($range) {
        $intervals = [
            '1h' => '1 HOUR',
            '6h' => '6 HOUR',
            '24h' => '24 HOUR',
            '7d' => '7 DAY',
            '30d' => '30 DAY'
        ];
        
        return $intervals[$range] ?? '24 HOUR';
    }
}

/**
 * Performance Monitor Class
 */
class PerformanceMonitor {
    private $logger;
    private $startTime;
    private $startMemory;
    
    public function __construct(LoggingSystem $logger) {
        $this->logger = $logger;
        $this->startTime = microtime(true);
        $this->startMemory = memory_get_usage(true);
    }
    
    public function startTimer($name) {
        $this->timers[$name] = microtime(true);
    }
    
    public function endTimer($name) {
        if (isset($this->timers[$name])) {
            $duration = (microtime(true) - $this->timers[$name]) * 1000;
            $this->logger->logPerformanceMetric("timer_{$name}", $duration, ['unit' => 'ms']);
            unset($this->timers[$name]);
            return $duration;
        }
        return null;
    }
    
    public function logMemoryUsage($checkpoint = '') {
        $current = memory_get_usage(true);
        $peak = memory_get_peak_usage(true);
        
        $this->logger->logPerformanceMetric('memory_usage', $current, [
            'checkpoint' => $checkpoint,
            'peak_memory' => $peak,
            'unit' => 'bytes'
        ]);
    }
    
    public function logRequestMetrics() {
        $executionTime = (microtime(true) - $this->startTime) * 1000;
        $memoryUsed = memory_get_usage(true) - $this->startMemory;
        
        $this->logger->logPerformanceMetric('request_execution_time', $executionTime, ['unit' => 'ms']);
        $this->logger->logPerformanceMetric('request_memory_usage', $memoryUsed, ['unit' => 'bytes']);
    }
}

/**
 * Global logging functions
 */
function initializeLogging($database, $logLevel = 'INFO') {
    global $logger, $performanceMonitor;
    
    $logger = new LoggingSystem($database, $logLevel);
    $performanceMonitor = new PerformanceMonitor($logger);
    
    // Set up error and exception handlers
    set_error_handler('logErrorHandler');
    set_exception_handler('logExceptionHandler');
    
    // Register shutdown function for request metrics
    register_shutdown_function('logShutdownHandler');
}

function logErrorHandler($severity, $message, $file, $line) {
    global $logger;
    
    if ($logger) {
        $context = [
            'severity' => $severity,
            'file' => $file,
            'line' => $line,
            'trace' => debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS)
        ];
        
        $level = $severity & (E_ERROR | E_CORE_ERROR | E_COMPILE_ERROR | E_USER_ERROR) ? 'ERROR' : 'WARNING';
        $logger->log($level, $message, $context, 'error');
    }
    
    return false; // Let PHP handle the error normally
}

function logExceptionHandler($exception) {
    global $logger;
    
    if ($logger) {
        $context = [
            'exception_class' => get_class($exception),
            'file' => $exception->getFile(),
            'line' => $exception->getLine(),
            'trace' => $exception->getTraceAsString()
        ];
        
        $logger->critical($exception->getMessage(), $context, 'error');
    }
}

function logShutdownHandler() {
    global $performanceMonitor;
    
    if ($performanceMonitor) {
        $performanceMonitor->logRequestMetrics();
    }
    
    // Check for fatal errors
    $error = error_get_last();
    if ($error && in_array($error['type'], [E_ERROR, E_CORE_ERROR, E_COMPILE_ERROR, E_PARSE])) {
        global $logger;
        if ($logger) {
            $logger->critical("Fatal error: {$error['message']}", [
                'file' => $error['file'],
                'line' => $error['line']
            ], 'error');
        }
    }
}

/**
 * Usage example:
 * 
 * // Initialize logging
 * initializeLogging($conn, 'INFO');
 * 
 * // Use global logger
 * global $logger;
 * $logger->info('User logged in', ['user_id' => 123]);
 * $logger->logAPIAccess('/api/devices', 'GET', 200, 150, 123);
 * $logger->logSecurityEvent('failed_login', ['email' => 'user@example.com']);
 * 
 * // Performance monitoring
 * global $performanceMonitor;
 * $performanceMonitor->startTimer('database_query');
 * // ... database operation
 * $performanceMonitor->endTimer('database_query');
 */
?>
