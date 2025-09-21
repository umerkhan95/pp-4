-- Enhanced Database Schema for PP4 Smart Pet Care System
-- Includes new tables for advanced features: telemetry, monitoring, security, and analytics

-- Enable foreign key constraints
SET FOREIGN_KEY_CHECKS = 1;

-- User profiles table for extended user information
CREATE TABLE IF NOT EXISTS user_profiles (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    subscription_tier ENUM('free', 'premium', 'enterprise') DEFAULT 'free',
    max_devices INT DEFAULT 3,
    permissions JSON,
    timezone VARCHAR(50) DEFAULT 'UTC',
    language VARCHAR(10) DEFAULT 'en',
    notification_preferences JSON,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    INDEX idx_user_profiles_user_id (user_id)
);

-- Device sessions table for enhanced authentication
CREATE TABLE IF NOT EXISTS device_sessions (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    refresh_token VARCHAR(512) NOT NULL,
    device_name VARCHAR(100),
    device_type VARCHAR(50),
    ip_address VARCHAR(45),
    user_agent TEXT,
    is_active BOOLEAN DEFAULT TRUE,
    is_current BOOLEAN DEFAULT FALSE,
    expires_at TIMESTAMP NOT NULL,
    last_used TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    logged_out_at TIMESTAMP NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    UNIQUE KEY unique_refresh_token (refresh_token),
    INDEX idx_device_sessions_user_id (user_id),
    INDEX idx_device_sessions_expires_at (expires_at)
);

-- Login attempts table for security monitoring
CREATE TABLE IF NOT EXISTS login_attempts (
    id INT AUTO_INCREMENT PRIMARY KEY,
    email VARCHAR(255) NOT NULL,
    ip_address VARCHAR(45) NOT NULL,
    user_agent TEXT,
    success BOOLEAN DEFAULT FALSE,
    attempted_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_login_attempts_email (email),
    INDEX idx_login_attempts_ip (ip_address),
    INDEX idx_login_attempts_attempted_at (attempted_at)
);

-- Device telemetry table for real-time monitoring
CREATE TABLE IF NOT EXISTS device_telemetry (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    wifi_signal INT,
    battery_level DECIMAL(5,2),
    memory_usage DECIMAL(5,2),
    cpu_usage DECIMAL(5,2),
    temperature DECIMAL(5,2),
    humidity DECIMAL(5,2),
    water_level DECIMAL(5,2),
    pump1_status BOOLEAN,
    pump2_status BOOLEAN,
    pump1_runtime INT DEFAULT 0,
    pump2_runtime INT DEFAULT 0,
    error_count INT DEFAULT 0,
    uptime BIGINT,
    firmware_version VARCHAR(20),
    free_heap INT,
    wifi_reconnects INT DEFAULT 0,
    last_error TEXT,
    FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE CASCADE,
    INDEX idx_telemetry_device_timestamp (device_id, timestamp),
    INDEX idx_telemetry_timestamp (timestamp)
);

-- Device alerts table for monitoring and notifications
CREATE TABLE IF NOT EXISTS device_alerts (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,
    alert_type VARCHAR(50) NOT NULL,
    severity ENUM('info', 'warning', 'critical') DEFAULT 'info',
    message TEXT NOT NULL,
    alert_value DECIMAL(10,2),
    acknowledged BOOLEAN DEFAULT FALSE,
    acknowledged_by INT,
    acknowledged_at TIMESTAMP NULL,
    resolved BOOLEAN DEFAULT FALSE,
    resolved_at TIMESTAMP NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE CASCADE,
    FOREIGN KEY (acknowledged_by) REFERENCES users(id) ON DELETE SET NULL,
    INDEX idx_alerts_device_id (device_id),
    INDEX idx_alerts_severity (severity),
    INDEX idx_alerts_created_at (created_at)
);

-- Device health history for trend analysis
CREATE TABLE IF NOT EXISTS device_health_history (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,
    health_score INT NOT NULL CHECK (health_score >= 0 AND health_score <= 100),
    recorded_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE CASCADE,
    INDEX idx_health_device_recorded (device_id, recorded_at)
);

-- OTA update management
CREATE TABLE IF NOT EXISTS ota_updates (
    id INT AUTO_INCREMENT PRIMARY KEY,
    version VARCHAR(20) NOT NULL,
    description TEXT,
    firmware_url VARCHAR(500) NOT NULL,
    sha256_hash VARCHAR(64) NOT NULL,
    signature TEXT,
    file_size BIGINT NOT NULL,
    is_delta BOOLEAN DEFAULT FALSE,
    base_version VARCHAR(20),
    target_hardware VARCHAR(50) DEFAULT 'ESP32',
    is_forced BOOLEAN DEFAULT FALSE,
    is_active BOOLEAN DEFAULT TRUE,
    release_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY unique_version (version),
    INDEX idx_ota_active_version (is_active, version)
);

-- Device OTA status tracking
CREATE TABLE IF NOT EXISTS device_ota_status (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,
    update_id INT NOT NULL,
    status ENUM('pending', 'downloading', 'installing', 'completed', 'failed', 'cancelled') DEFAULT 'pending',
    progress INT DEFAULT 0,
    error_message TEXT,
    started_at TIMESTAMP NULL,
    completed_at TIMESTAMP NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE CASCADE,
    FOREIGN KEY (update_id) REFERENCES ota_updates(id) ON DELETE CASCADE,
    INDEX idx_device_ota_device_id (device_id),
    INDEX idx_device_ota_status (status)
);

-- API usage analytics
CREATE TABLE IF NOT EXISTS api_usage_logs (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    user_id INT,
    device_id VARCHAR(50),
    endpoint VARCHAR(100) NOT NULL,
    method VARCHAR(10) NOT NULL,
    ip_address VARCHAR(45),
    user_agent TEXT,
    response_code INT,
    response_time_ms INT,
    request_size INT,
    response_size INT,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE SET NULL,
    FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE SET NULL,
    INDEX idx_api_logs_user_timestamp (user_id, timestamp),
    INDEX idx_api_logs_endpoint (endpoint),
    INDEX idx_api_logs_timestamp (timestamp)
);

-- Notification queue for push notifications
CREATE TABLE IF NOT EXISTS notification_queue (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    device_id VARCHAR(50),
    type VARCHAR(50) NOT NULL,
    title VARCHAR(200) NOT NULL,
    message TEXT NOT NULL,
    data JSON,
    priority ENUM('low', 'normal', 'high', 'urgent') DEFAULT 'normal',
    status ENUM('pending', 'sent', 'failed', 'cancelled') DEFAULT 'pending',
    scheduled_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    sent_at TIMESTAMP NULL,
    error_message TEXT,
    retry_count INT DEFAULT 0,
    max_retries INT DEFAULT 3,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE CASCADE,
    INDEX idx_notifications_user_status (user_id, status),
    INDEX idx_notifications_scheduled (scheduled_at, status)
);

-- Device maintenance schedules
CREATE TABLE IF NOT EXISTS device_maintenance (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,
    maintenance_type ENUM('cleaning', 'filter_change', 'pump_service', 'general_inspection') NOT NULL,
    description TEXT,
    scheduled_date DATE NOT NULL,
    completed_date DATE NULL,
    completed_by INT NULL,
    notes TEXT,
    next_due_date DATE,
    is_recurring BOOLEAN DEFAULT FALSE,
    recurrence_interval_days INT,
    status ENUM('scheduled', 'overdue', 'completed', 'cancelled') DEFAULT 'scheduled',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE CASCADE,
    FOREIGN KEY (completed_by) REFERENCES users(id) ON DELETE SET NULL,
    INDEX idx_maintenance_device_date (device_id, scheduled_date),
    INDEX idx_maintenance_status (status)
);

-- Usage analytics and statistics
CREATE TABLE IF NOT EXISTS usage_analytics (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,
    date DATE NOT NULL,
    pump1_activations INT DEFAULT 0,
    pump2_activations INT DEFAULT 0,
    total_runtime_minutes INT DEFAULT 0,
    water_consumed_ml INT DEFAULT 0,
    cleaning_cycles INT DEFAULT 0,
    error_events INT DEFAULT 0,
    uptime_percentage DECIMAL(5,2) DEFAULT 100.00,
    avg_temperature DECIMAL(5,2),
    avg_humidity DECIMAL(5,2),
    min_battery_level DECIMAL(5,2),
    max_battery_level DECIMAL(5,2),
    wifi_disconnections INT DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE CASCADE,
    UNIQUE KEY unique_device_date (device_id, date),
    INDEX idx_analytics_date (date)
);

-- Enhanced device settings with more configuration options
ALTER TABLE device_settings ADD COLUMN IF NOT EXISTS auto_cleaning_enabled BOOLEAN DEFAULT TRUE;
ALTER TABLE device_settings ADD COLUMN IF NOT EXISTS cleaning_intensity ENUM('low', 'medium', 'high') DEFAULT 'medium';
ALTER TABLE device_settings ADD COLUMN IF NOT EXISTS water_level_threshold INT DEFAULT 20;
ALTER TABLE device_settings ADD COLUMN IF NOT EXISTS temperature_alert_threshold DECIMAL(5,2) DEFAULT 40.0;
ALTER TABLE device_settings ADD COLUMN IF NOT EXISTS maintenance_reminder_days INT DEFAULT 30;
ALTER TABLE device_settings ADD COLUMN IF NOT EXISTS energy_saving_mode BOOLEAN DEFAULT FALSE;
ALTER TABLE device_settings ADD COLUMN IF NOT EXISTS night_mode_enabled BOOLEAN DEFAULT TRUE;
ALTER TABLE device_settings ADD COLUMN IF NOT EXISTS night_mode_start_time TIME DEFAULT '22:00:00';
ALTER TABLE device_settings ADD COLUMN IF NOT EXISTS night_mode_end_time TIME DEFAULT '06:00:00';

-- Enhanced device status with more monitoring fields
ALTER TABLE device_status ADD COLUMN IF NOT EXISTS firmware_version VARCHAR(20);
ALTER TABLE device_status ADD COLUMN IF NOT EXISTS wifi_signal INT;
ALTER TABLE device_status ADD COLUMN IF NOT EXISTS battery_level DECIMAL(5,2);
ALTER TABLE device_status ADD COLUMN IF NOT EXISTS error_count INT DEFAULT 0;
ALTER TABLE device_status ADD COLUMN IF NOT EXISTS last_error TEXT;
ALTER TABLE device_status ADD COLUMN IF NOT EXISTS uptime BIGINT;
ALTER TABLE device_status ADD COLUMN IF NOT EXISTS memory_usage DECIMAL(5,2);
ALTER TABLE device_status ADD COLUMN IF NOT EXISTS cpu_usage DECIMAL(5,2);

-- Add user preferences for enhanced features
ALTER TABLE users ADD COLUMN IF NOT EXISTS role ENUM('user', 'premium', 'admin', 'super_admin') DEFAULT 'user';
ALTER TABLE users ADD COLUMN IF NOT EXISTS is_active BOOLEAN DEFAULT TRUE;
ALTER TABLE users ADD COLUMN IF NOT EXISTS mfa_enabled BOOLEAN DEFAULT FALSE;
ALTER TABLE users ADD COLUMN IF NOT EXISTS mfa_secret VARCHAR(32);
ALTER TABLE users ADD COLUMN IF NOT EXISTS last_login TIMESTAMP NULL;
ALTER TABLE users ADD COLUMN IF NOT EXISTS email_verified BOOLEAN DEFAULT FALSE;
ALTER TABLE users ADD COLUMN IF NOT EXISTS email_verified_at TIMESTAMP NULL;

-- Create indexes for better performance
CREATE INDEX IF NOT EXISTS idx_users_role ON users(role);
CREATE INDEX IF NOT EXISTS idx_users_is_active ON users(is_active);
CREATE INDEX IF NOT EXISTS idx_users_last_login ON users(last_login);

-- Create views for common queries
CREATE OR REPLACE VIEW device_health_summary AS
SELECT 
    d.id as device_id,
    d.device_name,
    d.user_id,
    ds.status,
    ds.battery_level,
    ds.wifi_signal,
    ds.water_level,
    ds.temperature,
    ds.last_seen,
    COALESCE(dhh.health_score, 100) as current_health_score,
    COUNT(da.id) as active_alerts
FROM devices d
LEFT JOIN device_status ds ON d.id = ds.device_id
LEFT JOIN (
    SELECT device_id, health_score,
           ROW_NUMBER() OVER (PARTITION BY device_id ORDER BY recorded_at DESC) as rn
    FROM device_health_history
) dhh ON d.id = dhh.device_id AND dhh.rn = 1
LEFT JOIN device_alerts da ON d.id = da.device_id AND da.resolved = FALSE
GROUP BY d.id, d.device_name, d.user_id, ds.status, ds.battery_level, 
         ds.wifi_signal, ds.water_level, ds.temperature, ds.last_seen, dhh.health_score;

-- Create view for user dashboard statistics
CREATE OR REPLACE VIEW user_dashboard_stats AS
SELECT 
    u.id as user_id,
    COUNT(DISTINCT d.id) as total_devices,
    COUNT(DISTINCT CASE WHEN ds.last_seen > DATE_SUB(NOW(), INTERVAL 5 MINUTE) THEN d.id END) as online_devices,
    COUNT(DISTINCT CASE WHEN da.severity = 'critical' AND da.resolved = FALSE THEN da.device_id END) as critical_alerts,
    COUNT(DISTINCT CASE WHEN da.severity = 'warning' AND da.resolved = FALSE THEN da.device_id END) as warning_alerts,
    AVG(CASE WHEN dhh.rn = 1 THEN dhh.health_score END) as avg_health_score
FROM users u
LEFT JOIN devices d ON u.id = d.user_id
LEFT JOIN device_status ds ON d.id = ds.device_id
LEFT JOIN device_alerts da ON d.id = da.device_id
LEFT JOIN (
    SELECT device_id, health_score,
           ROW_NUMBER() OVER (PARTITION BY device_id ORDER BY recorded_at DESC) as rn
    FROM device_health_history
) dhh ON d.id = dhh.device_id
GROUP BY u.id;

-- Insert default OTA update record
INSERT IGNORE INTO ota_updates (version, description, firmware_url, sha256_hash, file_size, target_hardware, is_active)
VALUES ('1.0.0', 'Initial firmware release', 'https://api.porchpotty.codefied.co/firmware/pp4_v1.0.0.bin', 
        'placeholder_hash', 1048576, 'ESP32', TRUE);

-- Insert default user profiles for existing users
INSERT IGNORE INTO user_profiles (user_id, subscription_tier, max_devices, permissions, notification_preferences)
SELECT id, 'free', 3, '["device:view", "device:control"]', '{"email": true, "push": true, "sms": false}'
FROM users 
WHERE id NOT IN (SELECT user_id FROM user_profiles);

-- Create stored procedures for common operations

DELIMITER //

-- Procedure to clean up old telemetry data
CREATE PROCEDURE IF NOT EXISTS CleanupOldTelemetry(IN retention_days INT)
BEGIN
    DELETE FROM device_telemetry 
    WHERE timestamp < DATE_SUB(NOW(), INTERVAL retention_days DAY);
    
    DELETE FROM api_usage_logs 
    WHERE timestamp < DATE_SUB(NOW(), INTERVAL retention_days DAY);
    
    DELETE FROM login_attempts 
    WHERE attempted_at < DATE_SUB(NOW(), INTERVAL 30 DAY);
END //

-- Procedure to calculate daily usage analytics
CREATE PROCEDURE IF NOT EXISTS CalculateDailyAnalytics(IN target_date DATE)
BEGIN
    INSERT INTO usage_analytics (
        device_id, date, pump1_activations, pump2_activations, 
        total_runtime_minutes, error_events, uptime_percentage,
        avg_temperature, avg_humidity, min_battery_level, max_battery_level
    )
    SELECT 
        device_id,
        DATE(timestamp) as date,
        SUM(CASE WHEN pump1_status = TRUE THEN 1 ELSE 0 END) as pump1_activations,
        SUM(CASE WHEN pump2_status = TRUE THEN 1 ELSE 0 END) as pump2_activations,
        SUM(pump1_runtime + pump2_runtime) / 60 as total_runtime_minutes,
        MAX(error_count) as error_events,
        100.0 as uptime_percentage, -- Simplified calculation
        AVG(temperature) as avg_temperature,
        AVG(humidity) as avg_humidity,
        MIN(battery_level) as min_battery_level,
        MAX(battery_level) as max_battery_level
    FROM device_telemetry
    WHERE DATE(timestamp) = target_date
    GROUP BY device_id, DATE(timestamp)
    ON DUPLICATE KEY UPDATE
        pump1_activations = VALUES(pump1_activations),
        pump2_activations = VALUES(pump2_activations),
        total_runtime_minutes = VALUES(total_runtime_minutes),
        error_events = VALUES(error_events),
        avg_temperature = VALUES(avg_temperature),
        avg_humidity = VALUES(avg_humidity),
        min_battery_level = VALUES(min_battery_level),
        max_battery_level = VALUES(max_battery_level);
END //

DELIMITER ;

-- Create events for automated maintenance
SET GLOBAL event_scheduler = ON;

-- Event to clean up old data daily
CREATE EVENT IF NOT EXISTS cleanup_old_data
ON SCHEDULE EVERY 1 DAY
STARTS CURRENT_TIMESTAMP
DO
  CALL CleanupOldTelemetry(90);

-- Event to calculate daily analytics
CREATE EVENT IF NOT EXISTS calculate_daily_analytics
ON SCHEDULE EVERY 1 DAY
STARTS CURRENT_TIMESTAMP + INTERVAL 1 HOUR
DO
  CALL CalculateDailyAnalytics(CURDATE() - INTERVAL 1 DAY);

-- Grant necessary permissions (adjust as needed for your setup)
-- GRANT SELECT, INSERT, UPDATE, DELETE ON pp4_database.* TO 'pp4_user'@'%';
-- GRANT EXECUTE ON PROCEDURE pp4_database.CleanupOldTelemetry TO 'pp4_user'@'%';
-- GRANT EXECUTE ON PROCEDURE pp4_database.CalculateDailyAnalytics TO 'pp4_user'@'%';

-- Final optimization
ANALYZE TABLE device_telemetry, device_alerts, device_health_history, api_usage_logs;
