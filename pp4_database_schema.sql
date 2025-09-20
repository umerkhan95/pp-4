-- =====================================================
-- PP4 Smart Pet Care System Database Schema
-- Version: 2.0 (Enhanced with device settings)
-- Date: 2025-09-20
-- =====================================================

-- Create database
CREATE DATABASE IF NOT EXISTS u552717391_porchpotty 
CHARACTER SET utf8mb4 
COLLATE utf8mb4_unicode_ci;

USE u552717391_porchpotty;

-- =====================================================
-- 1. USERS TABLE
-- =====================================================
CREATE TABLE users (
    id INT AUTO_INCREMENT PRIMARY KEY,
    email VARCHAR(255) UNIQUE NOT NULL,
    password VARCHAR(255) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_email (email)
) ENGINE=InnoDB;

-- =====================================================
-- 2. DEVICES TABLE
-- =====================================================
CREATE TABLE devices (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    device_name VARCHAR(255) NOT NULL DEFAULT 'Pet Toilet Device',
    mac_address VARCHAR(17) NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    INDEX idx_user_id (user_id),
    INDEX idx_mac_address (mac_address)
) ENGINE=InnoDB;

-- =====================================================
-- 3. DEVICE STATUS TABLE
-- =====================================================
CREATE TABLE device_status (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_id INT NOT NULL,
    status ENUM('online', 'offline') DEFAULT 'offline',
    pump1_status TINYINT(1) DEFAULT 0 COMMENT 'Sprinkler pump (0=off, 1=on)',
    pump2_status TINYINT(1) DEFAULT 0 COMMENT 'Filter pump (0=off, 1=on)',
    water_level INT DEFAULT 0 COMMENT 'Water level percentage (0-100)',
    temperature DECIMAL(5,2) NULL COMMENT 'Temperature in Celsius',
    last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE CASCADE,
    UNIQUE KEY unique_device_status (device_id),
    INDEX idx_status (status),
    INDEX idx_last_seen (last_seen)
) ENGINE=InnoDB;

-- =====================================================
-- 4. DEVICE SETTINGS TABLE (Enhanced)
-- =====================================================
CREATE TABLE device_settings (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_id INT NOT NULL,
    duration_sprinkler INT DEFAULT 1 COMMENT 'Sprinkler duration in minutes',
    cycles_sprinkler INT DEFAULT 4 COMMENT 'Number of cycles per day',
    start_hour INT DEFAULT 8 COMMENT 'Start hour (0-23)',
    start_minute INT DEFAULT 0 COMMENT 'Start minute (0-59)',
    active_days JSON COMMENT 'Array of 7 integers for weekdays [Sun,Mon,Tue,Wed,Thu,Fri,Sat]',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE CASCADE,
    UNIQUE KEY unique_device_settings (device_id),
    INDEX idx_device_id (device_id)
) ENGINE=InnoDB;

-- =====================================================
-- 5. SPRINKLER SCHEDULE TABLE
-- =====================================================
CREATE TABLE sprinkler_schedule (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_id INT NOT NULL,
    time_slot TIME NOT NULL COMMENT 'Schedule time in HH:MM format',
    local_time TIME NULL COMMENT 'Local time for the device',
    utc_time TIME NULL COMMENT 'UTC equivalent time',
    timezone VARCHAR(50) DEFAULT 'UTC' COMMENT 'Timezone identifier',
    utc_offset VARCHAR(10) NULL COMMENT 'UTC offset (e.g., +05:00)',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE CASCADE,
    INDEX idx_device_id (device_id),
    INDEX idx_time_slot (time_slot),
    INDEX idx_utc_time (utc_time)
) ENGINE=InnoDB;

-- =====================================================
-- 6. DOG SETTINGS TABLE
-- =====================================================
CREATE TABLE dog_settings (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    dog_size ENUM('Small', 'Medium', 'Large') DEFAULT 'Medium',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    UNIQUE KEY unique_user_dog_settings (user_id)
) ENGINE=InnoDB;

-- =====================================================
-- 7. USER OTPS TABLE (For password reset)
-- =====================================================
CREATE TABLE user_otps (
    id INT AUTO_INCREMENT PRIMARY KEY,
    email VARCHAR(255) NOT NULL,
    otp VARCHAR(6) NOT NULL,
    expires_at TIMESTAMP NOT NULL,
    used TINYINT(1) DEFAULT 0 COMMENT '0=unused, 1=used',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_email (email),
    INDEX idx_otp (otp),
    INDEX idx_expires_at (expires_at)
) ENGINE=InnoDB;

-- =====================================================
-- 8. DEVICE LOGS TABLE (Optional - for monitoring)
-- =====================================================
CREATE TABLE device_logs (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_id INT NOT NULL,
    action VARCHAR(100) NOT NULL COMMENT 'Action performed (pump_on, pump_off, schedule_run, etc.)',
    details JSON NULL COMMENT 'Additional details about the action',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE CASCADE,
    INDEX idx_device_id (device_id),
    INDEX idx_action (action),
    INDEX idx_created_at (created_at)
) ENGINE=InnoDB;

-- =====================================================
-- SAMPLE DATA INSERTION
-- =====================================================

-- Insert sample user
INSERT INTO users (email, password) VALUES 
('admin@porchpotty.com', '$2y$10$92IXUNpkjO0rOQ5byMi.Ye4oKoEa3Ro9llC/.og/at2.uheWG/igi'), -- password: password
('test@example.com', '$2y$10$92IXUNpkjO0rOQ5byMi.Ye4oKoEa3Ro9llC/.og/at2.uheWG/igi'); -- password: password

-- Insert sample device
INSERT INTO devices (user_id, device_name, mac_address) VALUES 
(1, 'Living Room Pet Toilet', 'AA:BB:CC:DD:EE:FF'),
(2, 'Backyard Pet Station', '11:22:33:44:55:66');

-- Insert device status
INSERT INTO device_status (device_id, status, pump1_status, pump2_status, water_level) VALUES 
(1, 'online', 0, 0, 85),
(2, 'offline', 0, 0, 60);

-- Insert device settings with enhanced parameters
INSERT INTO device_settings (device_id, duration_sprinkler, cycles_sprinkler, start_hour, start_minute, active_days) VALUES 
(1, 1, 4, 8, 0, '[1,1,1,1,1,1,1]'),
(2, 2, 3, 7, 30, '[1,1,1,1,1,0,0]');

-- Insert sample sprinkler schedule
INSERT INTO sprinkler_schedule (device_id, time_slot, local_time, utc_time, timezone, utc_offset) VALUES 
(1, '08:00:00', '08:00:00', '03:00:00', 'Asia/Karachi', '+05:00'),
(1, '14:00:00', '14:00:00', '09:00:00', 'Asia/Karachi', '+05:00'),
(1, '20:00:00', '20:00:00', '15:00:00', 'Asia/Karachi', '+05:00'),
(2, '07:30:00', '07:30:00', '02:30:00', 'Asia/Karachi', '+05:00'),
(2, '19:30:00', '19:30:00', '14:30:00', 'Asia/Karachi', '+05:00');

-- Insert dog settings
INSERT INTO dog_settings (user_id, dog_size) VALUES 
(1, 'Medium'),
(2, 'Large');

-- =====================================================
-- USEFUL QUERIES FOR TESTING
-- =====================================================

-- Get complete device information with settings
/*
SELECT 
    d.id as device_id,
    d.device_name,
    ds.status,
    ds.pump1_status,
    ds.pump2_status,
    ds.water_level,
    dst.duration_sprinkler,
    dst.cycles_sprinkler,
    dst.start_hour,
    dst.start_minute,
    dst.active_days,
    dg.dog_size
FROM devices d
LEFT JOIN device_status ds ON d.id = ds.device_id
LEFT JOIN device_settings dst ON d.id = dst.device_id
LEFT JOIN dog_settings dg ON d.user_id = dg.user_id
WHERE d.user_id = 1;
*/

-- Get device schedule with timezone info
/*
SELECT 
    ss.device_id,
    ss.time_slot,
    ss.timezone,
    ss.utc_offset,
    dst.duration_sprinkler,
    dst.cycles_sprinkler,
    dst.active_days
FROM sprinkler_schedule ss
LEFT JOIN device_settings dst ON ss.device_id = dst.device_id
WHERE ss.device_id = 1
ORDER BY ss.time_slot;
*/

-- Clean up expired OTPs (run periodically)
/*
DELETE FROM user_otps WHERE expires_at < NOW() OR used = 1;
*/

-- =====================================================
-- INDEXES FOR PERFORMANCE
-- =====================================================

-- Additional composite indexes for common queries
CREATE INDEX idx_device_status_lookup ON device_status(device_id, status, last_seen);
CREATE INDEX idx_schedule_device_time ON sprinkler_schedule(device_id, time_slot);
CREATE INDEX idx_otp_email_valid ON user_otps(email, expires_at, used);

-- =====================================================
-- TRIGGERS FOR AUTO-CLEANUP
-- =====================================================

-- Auto-delete expired OTPs (optional)
DELIMITER //
CREATE EVENT IF NOT EXISTS cleanup_expired_otps
ON SCHEDULE EVERY 1 HOUR
DO
BEGIN
    DELETE FROM user_otps WHERE expires_at < NOW() OR used = 1;
END //
DELIMITER ;

-- =====================================================
-- VIEWS FOR EASY ACCESS
-- =====================================================

-- View for complete device information
CREATE VIEW device_complete_info AS
SELECT 
    d.id as device_id,
    d.device_name,
    d.mac_address,
    u.email as owner_email,
    ds.status,
    ds.pump1_status,
    ds.pump2_status,
    ds.water_level,
    ds.temperature,
    ds.last_seen,
    dst.duration_sprinkler,
    dst.cycles_sprinkler,
    dst.start_hour,
    dst.start_minute,
    dst.active_days,
    dg.dog_size,
    COUNT(ss.id) as schedule_count
FROM devices d
LEFT JOIN users u ON d.user_id = u.id
LEFT JOIN device_status ds ON d.id = ds.device_id
LEFT JOIN device_settings dst ON d.id = dst.device_id
LEFT JOIN dog_settings dg ON d.user_id = dg.user_id
LEFT JOIN sprinkler_schedule ss ON d.id = ss.device_id
GROUP BY d.id;

-- =====================================================
-- GRANT PERMISSIONS (Adjust as needed)
-- =====================================================

-- Create API user with limited permissions
-- CREATE USER 'pp4_api'@'localhost' IDENTIFIED BY 'secure_password_here';
-- GRANT SELECT, INSERT, UPDATE, DELETE ON u552717391_porchpotty.* TO 'pp4_api'@'localhost';
-- FLUSH PRIVILEGES;

-- =====================================================
-- END OF SCHEMA
-- =====================================================
