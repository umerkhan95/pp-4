<?php
/**
 * Cache Manager Class for API Performance Optimization
 * 
 * Implements multi-layer caching with Redis and APCu
 * Supports cache invalidation, compression, and TTL management
 */

class CacheManager {
    private $redis;
    private $useRedis;
    private $useAPCu;
    private $defaultTTL;
    private $compressionThreshold;
    
    public function __construct($redisHost = '127.0.0.1', $redisPort = 6379) {
        $this->defaultTTL = 3600; // 1 hour
        $this->compressionThreshold = 1024; // Compress data larger than 1KB
        
        // Initialize Redis
        try {
            $this->redis = new Redis();
            $this->useRedis = $this->redis->connect($redisHost, $redisPort);
            if ($this->useRedis) {
                $this->redis->select(1); // Use database 1 for cache
            }
        } catch (Exception $e) {
            $this->useRedis = false;
            error_log("Redis connection failed: " . $e->getMessage());
        }
        
        // Check APCu availability
        $this->useAPCu = extension_loaded('apcu') && apcu_enabled();
    }
    
    /**
     * Get cached data with fallback strategy
     * 
     * @param string $key - Cache key
     * @param callable $callback - Function to generate data if not cached
     * @param int $ttl - Time to live in seconds
     * @param array $tags - Cache tags for invalidation
     * @return mixed - Cached or generated data
     */
    public function remember($key, $callback, $ttl = null, $tags = []) {
        $ttl = $ttl ?? $this->defaultTTL;
        
        // Try to get from cache
        $data = $this->get($key);
        
        if ($data !== null) {
            return $data;
        }
        
        // Generate data using callback
        $data = $callback();
        
        // Store in cache
        $this->set($key, $data, $ttl, $tags);
        
        return $data;
    }
    
    /**
     * Get data from cache
     */
    public function get($key) {
        // Try APCu first (fastest)
        if ($this->useAPCu) {
            $data = apcu_fetch($this->prefixKey($key, 'apcu'));
            if ($data !== false) {
                return $this->unserializeData($data);
            }
        }
        
        // Try Redis
        if ($this->useRedis) {
            $data = $this->redis->get($this->prefixKey($key, 'redis'));
            if ($data !== false) {
                $uncompressed = $this->decompress($data);
                return $this->unserializeData($uncompressed);
            }
        }
        
        return null;
    }
    
    /**
     * Set data in cache
     */
    public function set($key, $data, $ttl = null, $tags = []) {
        $ttl = $ttl ?? $this->defaultTTL;
        $serialized = $this->serializeData($data);
        
        // Store in APCu (with shorter TTL for memory efficiency)
        if ($this->useAPCu) {
            $apcuTTL = min($ttl, 300); // Max 5 minutes in APCu
            apcu_store($this->prefixKey($key, 'apcu'), $serialized, $apcuTTL);
        }
        
        // Store in Redis
        if ($this->useRedis) {
            $compressed = $this->compress($serialized);
            $this->redis->setex($this->prefixKey($key, 'redis'), $ttl, $compressed);
            
            // Store tags for invalidation
            if (!empty($tags)) {
                foreach ($tags as $tag) {
                    $this->redis->sAdd("tag:{$tag}", $key);
                    $this->redis->expire("tag:{$tag}", $ttl + 3600); // Keep tags longer
                }
            }
        }
        
        return true;
    }
    
    /**
     * Delete from cache
     */
    public function delete($key) {
        $deleted = false;
        
        if ($this->useAPCu) {
            $deleted = apcu_delete($this->prefixKey($key, 'apcu')) || $deleted;
        }
        
        if ($this->useRedis) {
            $deleted = $this->redis->del($this->prefixKey($key, 'redis')) || $deleted;
        }
        
        return $deleted;
    }
    
    /**
     * Invalidate cache by tags
     */
    public function invalidateByTag($tag) {
        if (!$this->useRedis) {
            return false;
        }
        
        $keys = $this->redis->sMembers("tag:{$tag}");
        
        if (!empty($keys)) {
            foreach ($keys as $key) {
                $this->delete($key);
            }
            $this->redis->del("tag:{$tag}");
        }
        
        return true;
    }
    
    /**
     * Clear all cache
     */
    public function flush() {
        $cleared = false;
        
        if ($this->useAPCu) {
            $cleared = apcu_clear_cache() || $cleared;
        }
        
        if ($this->useRedis) {
            $cleared = $this->redis->flushDB() || $cleared;
        }
        
        return $cleared;
    }
    
    /**
     * Get cache statistics
     */
    public function getStats() {
        $stats = [
            'apcu' => null,
            'redis' => null
        ];
        
        if ($this->useAPCu) {
            $stats['apcu'] = apcu_cache_info();
        }
        
        if ($this->useRedis) {
            $stats['redis'] = $this->redis->info();
        }
        
        return $stats;
    }
    
    /**
     * Compress data if it exceeds threshold
     */
    private function compress($data) {
        if (strlen($data) > $this->compressionThreshold) {
            return gzcompress($data, 6);
        }
        return $data;
    }
    
    /**
     * Decompress data
     */
    private function decompress($data) {
        // Check if data is compressed (gzcompress adds header)
        if (substr($data, 0, 2) === "\x78") {
            $decompressed = gzuncompress($data);
            return $decompressed !== false ? $decompressed : $data;
        }
        return $data;
    }
    
    /**
     * Serialize data for storage
     */
    private function serializeData($data) {
        return serialize($data);
    }
    
    /**
     * Unserialize data from storage
     */
    private function unserializeData($data) {
        return unserialize($data);
    }
    
    /**
     * Add prefix to cache key
     */
    private function prefixKey($key, $type) {
        return "pp4_cache:{$type}:{$key}";
    }
}

/**
 * Device-specific cache functions
 */
class DeviceCache {
    private $cache;
    
    public function __construct(CacheManager $cache) {
        $this->cache = $cache;
    }
    
    /**
     * Cache device status with short TTL
     */
    public function cacheDeviceStatus($deviceId, $status, $ttl = 60) {
        $key = "device_status:{$deviceId}";
        return $this->cache->set($key, $status, $ttl, ['device', "device:{$deviceId}"]);
    }
    
    /**
     * Get cached device status
     */
    public function getDeviceStatus($deviceId) {
        $key = "device_status:{$deviceId}";
        return $this->cache->get($key);
    }
    
    /**
     * Cache sprinkler schedule with longer TTL
     */
    public function cacheSprinklerSchedule($deviceId, $schedule, $ttl = 3600) {
        $key = "sprinkler_schedule:{$deviceId}";
        return $this->cache->set($key, $schedule, $ttl, ['schedule', "device:{$deviceId}"]);
    }
    
    /**
     * Get cached sprinkler schedule
     */
    public function getSprinklerSchedule($deviceId) {
        $key = "sprinkler_schedule:{$deviceId}";
        return $this->cache->get($key);
    }
    
    /**
     * Invalidate all cache for a device
     */
    public function invalidateDevice($deviceId) {
        return $this->cache->invalidateByTag("device:{$deviceId}");
    }
    
    /**
     * Cache user data
     */
    public function cacheUserData($userId, $data, $ttl = 1800) {
        $key = "user_data:{$userId}";
        return $this->cache->set($key, $data, $ttl, ['user', "user:{$userId}"]);
    }
    
    /**
     * Get cached user data
     */
    public function getUserData($userId) {
        $key = "user_data:{$userId}";
        return $this->cache->get($key);
    }
}

/**
 * API Response Cache Middleware
 */
function cacheAPIResponse($cacheKey, $ttl = 300, $tags = []) {
    global $cacheManager;
    
    if (!isset($cacheManager)) {
        $cacheManager = new CacheManager();
    }
    
    // Check if response is cached
    $cachedResponse = $cacheManager->get($cacheKey);
    
    if ($cachedResponse !== null) {
        header('Content-Type: application/json');
        header('X-Cache: HIT');
        header('X-Cache-Key: ' . $cacheKey);
        echo $cachedResponse;
        exit;
    }
    
    // Start output buffering to capture response
    ob_start();
    
    // Register shutdown function to cache the response
    register_shutdown_function(function() use ($cacheManager, $cacheKey, $ttl, $tags) {
        $response = ob_get_contents();
        
        // Only cache successful responses
        $httpCode = http_response_code();
        if ($httpCode >= 200 && $httpCode < 300 && !empty($response)) {
            $cacheManager->set($cacheKey, $response, $ttl, $tags);
        }
    });
    
    header('X-Cache: MISS');
    header('X-Cache-Key: ' . $cacheKey);
}

/**
 * Generate cache key for API requests
 */
function generateCacheKey($endpoint, $params = [], $userId = null) {
    $keyParts = [$endpoint];
    
    if ($userId) {
        $keyParts[] = "user:{$userId}";
    }
    
    if (!empty($params)) {
        ksort($params); // Sort for consistent keys
        $keyParts[] = md5(serialize($params));
    }
    
    return implode(':', $keyParts);
}

/**
 * Usage examples:
 * 
 * // In API endpoints:
 * require_once 'cache_manager.php';
 * 
 * // Cache API response
 * $cacheKey = generateCacheKey('device_status', ['device_id' => $deviceId], $userId);
 * cacheAPIResponse($cacheKey, 60, ['device', "device:{$deviceId}"]);
 * 
 * // Use device cache
 * $deviceCache = new DeviceCache(new CacheManager());
 * $status = $deviceCache->getDeviceStatus($deviceId);
 * if (!$status) {
 *     $status = fetchDeviceStatusFromDB($deviceId);
 *     $deviceCache->cacheDeviceStatus($deviceId, $status);
 * }
 */
?>
