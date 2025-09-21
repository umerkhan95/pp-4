<?php
/**
 * Rate Limiter Class for API Security
 * 
 * Implements sliding window rate limiting with Redis backend
 * Supports multiple rate limit tiers and IP-based tracking
 */

class RateLimiter {
    private $redis;
    private $defaultLimits;
    private $premiumLimits;
    
    public function __construct() {
        // Initialize Redis connection
        $this->redis = new Redis();
        $this->redis->connect('127.0.0.1', 6379);
        
        // Define rate limits (requests per minute)
        $this->defaultLimits = [
            'login' => 5,           // 5 login attempts per minute
            'api_call' => 60,       // 60 API calls per minute
            'ota_check' => 10,      // 10 OTA checks per minute
            'device_status' => 120, // 120 status updates per minute
            'global' => 200         // 200 total requests per minute
        ];
        
        $this->premiumLimits = [
            'login' => 10,
            'api_call' => 300,
            'ota_check' => 50,
            'device_status' => 600,
            'global' => 1000
        ];
    }
    
    /**
     * Check if request is within rate limit
     * 
     * @param string $identifier - IP address or user ID
     * @param string $action - Type of action (login, api_call, etc.)
     * @param bool $isPremium - Whether user has premium tier
     * @return array - ['allowed' => bool, 'remaining' => int, 'reset_time' => int]
     */
    public function checkRateLimit($identifier, $action = 'api_call', $isPremium = false) {
        $limits = $isPremium ? $this->premiumLimits : $this->defaultLimits;
        $limit = $limits[$action] ?? $limits['api_call'];
        
        $key = "rate_limit:{$action}:{$identifier}";
        $window = 60; // 1 minute window
        $now = time();
        
        // Remove expired entries
        $this->redis->zRemRangeByScore($key, 0, $now - $window);
        
        // Count current requests
        $current = $this->redis->zCard($key);
        
        if ($current >= $limit) {
            // Get reset time (when oldest entry expires)
            $oldest = $this->redis->zRange($key, 0, 0, ['withscores' => true]);
            $resetTime = !empty($oldest) ? (int)array_values($oldest)[0] + $window : $now + $window;
            
            return [
                'allowed' => false,
                'remaining' => 0,
                'reset_time' => $resetTime,
                'limit' => $limit
            ];
        }
        
        // Add current request
        $this->redis->zAdd($key, $now, uniqid());
        $this->redis->expire($key, $window);
        
        return [
            'allowed' => true,
            'remaining' => $limit - $current - 1,
            'reset_time' => $now + $window,
            'limit' => $limit
        ];
    }
    
    /**
     * Get rate limit status without incrementing counter
     */
    public function getRateLimitStatus($identifier, $action = 'api_call', $isPremium = false) {
        $limits = $isPremium ? $this->premiumLimits : $this->defaultLimits;
        $limit = $limits[$action] ?? $limits['api_call'];
        
        $key = "rate_limit:{$action}:{$identifier}";
        $window = 60;
        $now = time();
        
        // Remove expired entries
        $this->redis->zRemRangeByScore($key, 0, $now - $window);
        
        // Count current requests
        $current = $this->redis->zCard($key);
        
        $oldest = $this->redis->zRange($key, 0, 0, ['withscores' => true]);
        $resetTime = !empty($oldest) ? (int)array_values($oldest)[0] + $window : $now + $window;
        
        return [
            'remaining' => max(0, $limit - $current),
            'reset_time' => $resetTime,
            'limit' => $limit
        ];
    }
    
    /**
     * Block IP address for specified duration
     */
    public function blockIP($ip, $duration = 3600) {
        $key = "blocked_ip:{$ip}";
        $this->redis->setex($key, $duration, time());
    }
    
    /**
     * Check if IP is blocked
     */
    public function isIPBlocked($ip) {
        $key = "blocked_ip:{$ip}";
        return $this->redis->exists($key);
    }
    
    /**
     * Implement progressive penalties for repeated violations
     */
    public function applyPenalty($identifier, $action) {
        $penaltyKey = "penalty:{$action}:{$identifier}";
        $violations = $this->redis->incr($penaltyKey);
        $this->redis->expire($penaltyKey, 3600); // Reset penalties after 1 hour
        
        // Progressive penalties
        if ($violations >= 5) {
            // Block for 1 hour after 5 violations
            $this->blockIP($identifier, 3600);
            return 3600;
        } elseif ($violations >= 3) {
            // Block for 15 minutes after 3 violations
            $this->blockIP($identifier, 900);
            return 900;
        } elseif ($violations >= 2) {
            // Block for 5 minutes after 2 violations
            $this->blockIP($identifier, 300);
            return 300;
        }
        
        return 0;
    }
    
    /**
     * Get client identifier (IP + User-Agent hash for better tracking)
     */
    public static function getClientIdentifier() {
        $ip = self::getClientIP();
        $userAgent = $_SERVER['HTTP_USER_AGENT'] ?? '';
        return $ip . ':' . substr(md5($userAgent), 0, 8);
    }
    
    /**
     * Get real client IP address
     */
    public static function getClientIP() {
        $ipKeys = [
            'HTTP_CF_CONNECTING_IP',     // Cloudflare
            'HTTP_CLIENT_IP',            // Proxy
            'HTTP_X_FORWARDED_FOR',      // Load balancer/proxy
            'HTTP_X_FORWARDED',          // Proxy
            'HTTP_X_CLUSTER_CLIENT_IP',  // Cluster
            'HTTP_FORWARDED_FOR',        // Proxy
            'HTTP_FORWARDED',            // Proxy
            'REMOTE_ADDR'                // Standard
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
    
    /**
     * Add rate limit headers to response
     */
    public function addRateLimitHeaders($rateLimitInfo) {
        header("X-RateLimit-Limit: " . $rateLimitInfo['limit']);
        header("X-RateLimit-Remaining: " . $rateLimitInfo['remaining']);
        header("X-RateLimit-Reset: " . $rateLimitInfo['reset_time']);
        
        if (!$rateLimitInfo['allowed']) {
            header("Retry-After: " . ($rateLimitInfo['reset_time'] - time()));
        }
    }
}

/**
 * Rate Limiting Middleware Function
 */
function applyRateLimit($action = 'api_call', $requireAuth = true) {
    $rateLimiter = new RateLimiter();
    $clientId = RateLimiter::getClientIdentifier();
    
    // Check if IP is blocked
    if ($rateLimiter->isIPBlocked($clientId)) {
        http_response_code(429);
        header('Content-Type: application/json');
        echo json_encode([
            'status' => 'error',
            'message' => 'IP address is temporarily blocked due to rate limit violations',
            'error_code' => 'IP_BLOCKED'
        ]);
        exit;
    }
    
    // Determine if user is premium (if authenticated)
    $isPremium = false;
    if ($requireAuth) {
        try {
            $headers = getallheaders();
            $auth = $headers['Authorization'] ?? '';
            if ($auth) {
                $token = str_replace('Bearer ', '', $auth);
                // Check if user has premium subscription
                // This would typically involve decoding JWT and checking user tier
                $isPremium = false; // Placeholder
            }
        } catch (Exception $e) {
            // Continue with default limits if auth check fails
        }
    }
    
    // Check rate limit
    $rateLimitInfo = $rateLimiter->checkRateLimit($clientId, $action, $isPremium);
    
    // Add headers
    $rateLimiter->addRateLimitHeaders($rateLimitInfo);
    
    if (!$rateLimitInfo['allowed']) {
        // Apply penalty for repeated violations
        $penaltyDuration = $rateLimiter->applyPenalty($clientId, $action);
        
        http_response_code(429);
        header('Content-Type: application/json');
        echo json_encode([
            'status' => 'error',
            'message' => 'Rate limit exceeded. Please try again later.',
            'error_code' => 'RATE_LIMIT_EXCEEDED',
            'retry_after' => $rateLimitInfo['reset_time'] - time(),
            'penalty_duration' => $penaltyDuration
        ]);
        exit;
    }
}

/**
 * Usage example in API endpoints:
 * 
 * // At the top of your API endpoint files:
 * require_once 'rate_limiter.php';
 * 
 * // Apply rate limiting
 * applyRateLimit('login', false); // For login endpoint
 * applyRateLimit('api_call', true); // For authenticated endpoints
 * applyRateLimit('ota_check', true); // For OTA update checks
 */
?>
