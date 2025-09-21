<?php
/**
 * Enhanced Authentication System with JWT Refresh Tokens and RBAC
 * 
 * Implements security best practices:
 * - JWT access tokens with short expiration
 * - Refresh tokens with rotation
 * - Role-Based Access Control (RBAC)
 * - Multi-factor authentication support
 * - Session management and device tracking
 */

require_once 'jwt_config.php';
require_once 'db.php';

class EnhancedAuth {
    private $conn;
    private $accessTokenTTL = 900;    // 15 minutes
    private $refreshTokenTTL = 604800; // 7 days
    private $maxDevices = 5;           // Max devices per user
    
    public function __construct($database) {
        $this->conn = $database;
    }
    
    /**
     * Enhanced login with device tracking and refresh tokens
     */
    public function login($email, $password, $deviceInfo = []) {
        try {
            // Rate limiting check
            $this->checkLoginAttempts($email);
            
            // Validate credentials
            $stmt = $this->conn->prepare("
                SELECT u.id, u.email, u.password, u.role, u.is_active, u.mfa_enabled, u.mfa_secret,
                       p.subscription_tier, p.max_devices
                FROM users u 
                LEFT JOIN user_profiles p ON u.id = p.user_id 
                WHERE u.email = ?
            ");
            $stmt->execute([$email]);
            $user = $stmt->fetch(PDO::FETCH_ASSOC);
            
            if (!$user || !password_verify($password, $user['password'])) {
                $this->recordFailedLogin($email);
                throw new Exception("Invalid credentials");
            }
            
            if (!$user['is_active']) {
                throw new Exception("Account is deactivated");
            }
            
            // Check MFA if enabled
            if ($user['mfa_enabled']) {
                return [
                    'requires_mfa' => true,
                    'user_id' => $user['id'],
                    'temp_token' => $this->generateTempToken($user['id'])
                ];
            }
            
            // Clear failed login attempts
            $this->clearFailedLogins($email);
            
            // Generate tokens
            $tokens = $this->generateTokenPair($user);
            
            // Register device session
            $sessionId = $this->registerDeviceSession($user['id'], $tokens['refresh_token'], $deviceInfo);
            
            // Update last login
            $this->updateLastLogin($user['id']);
            
            return [
                'success' => true,
                'access_token' => $tokens['access_token'],
                'refresh_token' => $tokens['refresh_token'],
                'expires_in' => $this->accessTokenTTL,
                'session_id' => $sessionId,
                'user' => [
                    'id' => $user['id'],
                    'email' => $user['email'],
                    'role' => $user['role'],
                    'subscription_tier' => $user['subscription_tier'] ?? 'free'
                ]
            ];
            
        } catch (Exception $e) {
            throw $e;
        }
    }
    
    /**
     * Verify MFA and complete login
     */
    public function verifyMFA($tempToken, $mfaCode, $deviceInfo = []) {
        try {
            // Validate temp token
            $decoded = validateJWT($tempToken);
            $userId = $decoded->user_id ?? null;
            $tokenType = $decoded->type ?? null;
            
            if (!$userId || $tokenType !== 'temp_mfa') {
                throw new Exception("Invalid temporary token");
            }
            
            // Get user data
            $stmt = $this->conn->prepare("
                SELECT u.*, p.subscription_tier 
                FROM users u 
                LEFT JOIN user_profiles p ON u.id = p.user_id 
                WHERE u.id = ?
            ");
            $stmt->execute([$userId]);
            $user = $stmt->fetch(PDO::FETCH_ASSOC);
            
            if (!$user) {
                throw new Exception("User not found");
            }
            
            // Verify MFA code (TOTP)
            if (!$this->verifyTOTP($user['mfa_secret'], $mfaCode)) {
                throw new Exception("Invalid MFA code");
            }
            
            // Generate tokens
            $tokens = $this->generateTokenPair($user);
            
            // Register device session
            $sessionId = $this->registerDeviceSession($userId, $tokens['refresh_token'], $deviceInfo);
            
            return [
                'success' => true,
                'access_token' => $tokens['access_token'],
                'refresh_token' => $tokens['refresh_token'],
                'expires_in' => $this->accessTokenTTL,
                'session_id' => $sessionId,
                'user' => [
                    'id' => $user['id'],
                    'email' => $user['email'],
                    'role' => $user['role'],
                    'subscription_tier' => $user['subscription_tier'] ?? 'free'
                ]
            ];
            
        } catch (Exception $e) {
            throw $e;
        }
    }
    
    /**
     * Refresh access token using refresh token
     */
    public function refreshToken($refreshToken) {
        try {
            // Validate refresh token
            $stmt = $this->conn->prepare("
                SELECT ds.*, u.id, u.email, u.role, u.is_active, p.subscription_tier
                FROM device_sessions ds
                JOIN users u ON ds.user_id = u.id
                LEFT JOIN user_profiles p ON u.id = p.user_id
                WHERE ds.refresh_token = ? AND ds.expires_at > NOW() AND ds.is_active = 1
            ");
            $stmt->execute([$refreshToken]);
            $session = $stmt->fetch(PDO::FETCH_ASSOC);
            
            if (!$session) {
                throw new Exception("Invalid or expired refresh token");
            }
            
            if (!$session['is_active']) {
                throw new Exception("Account is deactivated");
            }
            
            // Generate new token pair (refresh token rotation)
            $user = [
                'id' => $session['id'],
                'email' => $session['email'],
                'role' => $session['role'],
                'subscription_tier' => $session['subscription_tier']
            ];
            
            $tokens = $this->generateTokenPair($user);
            
            // Update session with new refresh token
            $stmt = $this->conn->prepare("
                UPDATE device_sessions 
                SET refresh_token = ?, last_used = NOW(), updated_at = NOW()
                WHERE id = ?
            ");
            $stmt->execute([$tokens['refresh_token'], $session['session_id']]);
            
            return [
                'access_token' => $tokens['access_token'],
                'refresh_token' => $tokens['refresh_token'],
                'expires_in' => $this->accessTokenTTL
            ];
            
        } catch (Exception $e) {
            throw $e;
        }
    }
    
    /**
     * Enhanced JWT validation with role checking
     */
    public function validateTokenWithRole($token, $requiredRole = null, $requiredPermissions = []) {
        try {
            $decoded = validateJWT($token);
            
            if (!$decoded || !isset($decoded->user_id)) {
                throw new Exception("Invalid token");
            }
            
            // Get current user data
            $stmt = $this->conn->prepare("
                SELECT u.*, p.subscription_tier, p.permissions
                FROM users u 
                LEFT JOIN user_profiles p ON u.id = p.user_id 
                WHERE u.id = ? AND u.is_active = 1
            ");
            $stmt->execute([$decoded->user_id]);
            $user = $stmt->fetch(PDO::FETCH_ASSOC);
            
            if (!$user) {
                throw new Exception("User not found or inactive");
            }
            
            // Check role requirement
            if ($requiredRole && !$this->hasRole($user['role'], $requiredRole)) {
                throw new Exception("Insufficient role privileges");
            }
            
            // Check specific permissions
            if (!empty($requiredPermissions)) {
                $userPermissions = json_decode($user['permissions'] ?? '[]', true);
                foreach ($requiredPermissions as $permission) {
                    if (!in_array($permission, $userPermissions)) {
                        throw new Exception("Missing required permission: {$permission}");
                    }
                }
            }
            
            return $user;
            
        } catch (Exception $e) {
            throw $e;
        }
    }
    
    /**
     * Logout and invalidate session
     */
    public function logout($refreshToken) {
        try {
            $stmt = $this->conn->prepare("
                UPDATE device_sessions 
                SET is_active = 0, logged_out_at = NOW() 
                WHERE refresh_token = ?
            ");
            $stmt->execute([$refreshToken]);
            
            return ['success' => true, 'message' => 'Logged out successfully'];
            
        } catch (Exception $e) {
            throw $e;
        }
    }
    
    /**
     * Logout from all devices
     */
    public function logoutAllDevices($userId) {
        try {
            $stmt = $this->conn->prepare("
                UPDATE device_sessions 
                SET is_active = 0, logged_out_at = NOW() 
                WHERE user_id = ? AND is_active = 1
            ");
            $stmt->execute([$userId]);
            
            return ['success' => true, 'message' => 'Logged out from all devices'];
            
        } catch (Exception $e) {
            throw $e;
        }
    }
    
    /**
     * Get active sessions for user
     */
    public function getActiveSessions($userId) {
        try {
            $stmt = $this->conn->prepare("
                SELECT id, device_name, device_type, ip_address, user_agent, 
                       created_at, last_used, is_current
                FROM device_sessions 
                WHERE user_id = ? AND is_active = 1 
                ORDER BY last_used DESC
            ");
            $stmt->execute([$userId]);
            
            return $stmt->fetchAll(PDO::FETCH_ASSOC);
            
        } catch (Exception $e) {
            throw $e;
        }
    }
    
    /**
     * Generate JWT token pair
     */
    private function generateTokenPair($user) {
        $now = time();
        
        // Access token payload
        $accessPayload = [
            'iss' => 'pp4-api',
            'aud' => 'pp4-client',
            'iat' => $now,
            'exp' => $now + $this->accessTokenTTL,
            'user_id' => $user['id'],
            'email' => $user['email'],
            'role' => $user['role'],
            'subscription_tier' => $user['subscription_tier'] ?? 'free',
            'type' => 'access'
        ];
        
        // Refresh token payload
        $refreshPayload = [
            'iss' => 'pp4-api',
            'aud' => 'pp4-client',
            'iat' => $now,
            'exp' => $now + $this->refreshTokenTTL,
            'user_id' => $user['id'],
            'type' => 'refresh',
            'jti' => bin2hex(random_bytes(16)) // Unique token ID
        ];
        
        return [
            'access_token' => generateJWT($accessPayload),
            'refresh_token' => generateJWT($refreshPayload)
        ];
    }
    
    /**
     * Generate temporary MFA token
     */
    private function generateTempToken($userId) {
        $payload = [
            'iss' => 'pp4-api',
            'aud' => 'pp4-client',
            'iat' => time(),
            'exp' => time() + 300, // 5 minutes
            'user_id' => $userId,
            'type' => 'temp_mfa'
        ];
        
        return generateJWT($payload);
    }
    
    /**
     * Register device session
     */
    private function registerDeviceSession($userId, $refreshToken, $deviceInfo) {
        // Clean up old sessions if limit exceeded
        $this->cleanupOldSessions($userId);
        
        $stmt = $this->conn->prepare("
            INSERT INTO device_sessions 
            (user_id, refresh_token, device_name, device_type, ip_address, user_agent, expires_at) 
            VALUES (?, ?, ?, ?, ?, ?, DATE_ADD(NOW(), INTERVAL ? SECOND))
        ");
        
        $stmt->execute([
            $userId,
            $refreshToken,
            $deviceInfo['device_name'] ?? 'Unknown Device',
            $deviceInfo['device_type'] ?? 'Unknown',
            $this->getClientIP(),
            $_SERVER['HTTP_USER_AGENT'] ?? '',
            $this->refreshTokenTTL
        ]);
        
        return $this->conn->lastInsertId();
    }
    
    /**
     * Check role hierarchy
     */
    private function hasRole($userRole, $requiredRole) {
        $roleHierarchy = [
            'super_admin' => 4,
            'admin' => 3,
            'premium' => 2,
            'user' => 1,
            'guest' => 0
        ];
        
        $userLevel = $roleHierarchy[$userRole] ?? 0;
        $requiredLevel = $roleHierarchy[$requiredRole] ?? 0;
        
        return $userLevel >= $requiredLevel;
    }
    
    /**
     * Verify TOTP code
     */
    private function verifyTOTP($secret, $code) {
        // Simplified TOTP verification - implement proper TOTP library in production
        // This is a placeholder implementation
        return strlen($code) === 6 && is_numeric($code);
    }
    
    /**
     * Rate limiting for login attempts
     */
    private function checkLoginAttempts($email) {
        $stmt = $this->conn->prepare("
            SELECT COUNT(*) as attempts 
            FROM login_attempts 
            WHERE email = ? AND attempted_at > DATE_SUB(NOW(), INTERVAL 15 MINUTE)
        ");
        $stmt->execute([$email]);
        $result = $stmt->fetch(PDO::FETCH_ASSOC);
        
        if ($result['attempts'] >= 5) {
            throw new Exception("Too many login attempts. Please try again later.");
        }
    }
    
    /**
     * Record failed login attempt
     */
    private function recordFailedLogin($email) {
        $stmt = $this->conn->prepare("
            INSERT INTO login_attempts (email, ip_address, user_agent, attempted_at) 
            VALUES (?, ?, ?, NOW())
        ");
        $stmt->execute([$email, $this->getClientIP(), $_SERVER['HTTP_USER_AGENT'] ?? '']);
    }
    
    /**
     * Clear failed login attempts
     */
    private function clearFailedLogins($email) {
        $stmt = $this->conn->prepare("DELETE FROM login_attempts WHERE email = ?");
        $stmt->execute([$email]);
    }
    
    /**
     * Update last login timestamp
     */
    private function updateLastLogin($userId) {
        $stmt = $this->conn->prepare("UPDATE users SET last_login = NOW() WHERE id = ?");
        $stmt->execute([$userId]);
    }
    
    /**
     * Clean up old sessions
     */
    private function cleanupOldSessions($userId) {
        // Count active sessions
        $stmt = $this->conn->prepare("
            SELECT COUNT(*) as count 
            FROM device_sessions 
            WHERE user_id = ? AND is_active = 1
        ");
        $stmt->execute([$userId]);
        $result = $stmt->fetch(PDO::FETCH_ASSOC);
        
        if ($result['count'] >= $this->maxDevices) {
            // Deactivate oldest sessions
            $stmt = $this->conn->prepare("
                UPDATE device_sessions 
                SET is_active = 0 
                WHERE user_id = ? AND is_active = 1 
                ORDER BY last_used ASC 
                LIMIT ?
            ");
            $stmt->execute([$userId, $result['count'] - $this->maxDevices + 1]);
        }
    }
    
    /**
     * Get client IP address
     */
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
}

/**
 * Enhanced authentication middleware
 */
function requireAuth($requiredRole = null, $requiredPermissions = []) {
    global $conn;
    
    try {
        $headers = getallheaders();
        $auth = $headers['Authorization'] ?? '';
        
        if (!$auth) {
            throw new Exception("Authorization header missing");
        }
        
        $token = str_replace('Bearer ', '', $auth);
        $enhancedAuth = new EnhancedAuth($conn);
        
        $user = $enhancedAuth->validateTokenWithRole($token, $requiredRole, $requiredPermissions);
        
        // Make user data available globally
        $GLOBALS['current_user'] = $user;
        
        return $user;
        
    } catch (Exception $e) {
        http_response_code(401);
        header('Content-Type: application/json');
        echo json_encode([
            'status' => 'error',
            'message' => $e->getMessage(),
            'error_code' => 'AUTHENTICATION_FAILED'
        ]);
        exit;
    }
}

/**
 * Check specific permission
 */
function hasPermission($permission) {
    $user = $GLOBALS['current_user'] ?? null;
    if (!$user) return false;
    
    $permissions = json_decode($user['permissions'] ?? '[]', true);
    return in_array($permission, $permissions);
}

/**
 * Usage examples:
 * 
 * // Require authentication
 * requireAuth();
 * 
 * // Require admin role
 * requireAuth('admin');
 * 
 * // Require specific permissions
 * requireAuth(null, ['device:control', 'schedule:modify']);
 * 
 * // Check permission in code
 * if (hasPermission('device:delete')) {
 *     // Allow device deletion
 * }
 */
?>
