/**
 * @file auth_mgr.c
 * @brief Authentication Manager Implementation
 * @details Simple single admin authentication system implementation
 */

#include "auth_mgr.h"
#include "debug.h"
#include "json_config_mgr.h"
#include <string.h>
#include <stdlib.h>



/* ==================== Internal Data Structures ==================== */

typedef struct {
    uint32_t magic_number;
    aicam_bool_t is_initialized;
    auth_mgr_config_t config;
    uint8_t admin_password_hash[AUTH_PASSWORD_HASH_LEN];
    auth_session_t sessions[AUTH_MAX_SESSIONS];
    uint32_t session_count;
    uint32_t next_session_id;
    void *mutex;
} auth_mgr_state_t;

/* ==================== Global Variables ==================== */

static auth_mgr_state_t g_auth_mgr = {0};

/* ==================== Thread Safety Macros ==================== */
#include "cmsis_os2.h"
#define AUTH_MUTEX_CREATE()         osMutexNew(NULL)
#define AUTH_MUTEX_DESTROY(mutex)   osMutexDelete(mutex)
#define AUTH_MUTEX_LOCK(mutex)      osMutexAcquire(mutex, osWaitForever)
#define AUTH_MUTEX_UNLOCK(mutex)    osMutexRelease(mutex)


/* ==================== Internal Function Declarations ==================== */

static auth_session_t* auth_mgr_find_session(uint32_t session_id);
static auth_session_t* auth_mgr_allocate_session(void);
static void auth_mgr_cleanup_expired_sessions(void);
static uint32_t auth_mgr_generate_session_id(void);
static void auth_mgr_hash_password(const char *password, uint8_t *hash);
static uint64_t auth_mgr_get_timestamp(void);

/* ==================== Public API Implementation ==================== */

aicam_result_t auth_mgr_init()
{
    if (g_auth_mgr.is_initialized) {
        LOG_CORE_WARN("Authentication manager already initialized");
        return AICAM_OK;
    }
    
    LOG_CORE_INFO("Initializing Authentication Manager");
    
    // Initialize state structure
    memset(&g_auth_mgr, 0, sizeof(auth_mgr_state_t));
    g_auth_mgr.magic_number = AUTH_MGR_MAGIC_NUMBER;
    
    // Set configuration and load password from json_config_mgr
    auth_mgr_config_t default_config = AUTH_MGR_CONFIG_DEFAULT();
    memcpy(&g_auth_mgr.config, &default_config, sizeof(auth_mgr_config_t));
    
    // Load admin password from configuration manager
    char config_password[AUTH_MAX_PASSWORD_LEN + 1];
    aicam_result_t config_result = json_config_get_device_password(config_password, sizeof(config_password));
    if (config_result == AICAM_OK) {
        strncpy(g_auth_mgr.config.admin_password, config_password, sizeof(g_auth_mgr.config.admin_password) - 1);
        g_auth_mgr.config.admin_password[sizeof(g_auth_mgr.config.admin_password) - 1] = '\0';
        LOG_CORE_INFO("Admin password loaded from configuration manager");
    } else {
        LOG_CORE_WARN("Failed to load password from config manager, using default: %d", config_result);
    }

    
    // Create mutex for thread safety
    g_auth_mgr.mutex = AUTH_MUTEX_CREATE();
    if (!g_auth_mgr.mutex) {
        LOG_CORE_ERROR("Failed to create authentication manager mutex");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Initialize session ID counter
    g_auth_mgr.next_session_id = AUTH_SESSION_ID_MIN;

    LOG_CORE_INFO("Authentication manager mutex created successfully");
    
    // Initialize admin password hash
    auth_mgr_hash_password(g_auth_mgr.config.admin_password, g_auth_mgr.admin_password_hash);
    
    g_auth_mgr.is_initialized = AICAM_TRUE;
    
    LOG_CORE_INFO("Authentication Manager initialized successfully");
    LOG_CORE_INFO("Admin username: '%s'", AUTH_ADMIN_USERNAME);
    LOG_CORE_INFO("Admin password: '%s'", g_auth_mgr.config.admin_password);
    
    return AICAM_OK;
}

aicam_result_t auth_mgr_deinit(void)
{
    if (!g_auth_mgr.is_initialized) {
        return AICAM_OK;
    }
    
    LOG_CORE_INFO("Deinitializing Authentication Manager");
    
    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
    
    // Logout all active sessions
    for (uint32_t i = 0; i < g_auth_mgr.session_count; i++) {
        if (g_auth_mgr.sessions[i].is_active) {
            g_auth_mgr.sessions[i].is_active = AICAM_FALSE;
        }
    }
    
    g_auth_mgr.is_initialized = AICAM_FALSE;
    
    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
    
    // Destroy mutex
    AUTH_MUTEX_DESTROY(g_auth_mgr.mutex);
    
    // Clear sensitive data
    memset(&g_auth_mgr, 0, sizeof(auth_mgr_state_t));
    
    LOG_CORE_INFO("Authentication Manager deinitialized");
    return AICAM_OK;
}

aicam_bool_t auth_mgr_verify_password(const char *password)
{
    uint8_t computed_hash[AUTH_PASSWORD_HASH_LEN];
    auth_mgr_hash_password(password, computed_hash);
    
    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);

    aicam_bool_t result = (memcmp(computed_hash, g_auth_mgr.admin_password_hash, AUTH_PASSWORD_HASH_LEN) == 0) ? 
           AICAM_TRUE : AICAM_FALSE;
    
    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
    
    return result;
}

auth_result_t auth_mgr_login(const char *username, 
                            const char *password,
                            uint32_t *session_id)
{
    if (!g_auth_mgr.is_initialized) {
        return AUTH_RESULT_INTERNAL_ERROR;
    }
    
    if (!username || !password || !session_id) {
        return AUTH_RESULT_INVALID_PARAM;
    }
    
    if (strlen(password) > AUTH_MAX_PASSWORD_LEN) {
        return AUTH_RESULT_INVALID_PARAM;
    }
    
    // Check if username is "admin"
    if (strcmp(username, AUTH_ADMIN_USERNAME) != 0) {
        LOG_CORE_WARN("Login failed: invalid username '%s'", username);
        return AUTH_RESULT_INVALID_CREDENTIALS;
    }
    
    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
    
    // Cleanup expired sessions first
    auth_mgr_cleanup_expired_sessions();
    
    // Verify password
    if (!auth_mgr_verify_password(password)) {
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
        LOG_CORE_WARN("Login failed: invalid password for admin");
        return AUTH_RESULT_INVALID_CREDENTIALS;
    }
    
    // Check if maximum sessions reached
    uint32_t active_sessions = 0;
    for (uint32_t i = 0; i < g_auth_mgr.session_count; i++) {
        if (g_auth_mgr.sessions[i].is_active) {
            active_sessions++;
        }
    }
    
    if (active_sessions >= AUTH_MAX_SESSIONS) {
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
        LOG_CORE_ERROR("Login failed: maximum sessions reached");
        return AUTH_RESULT_MAX_SESSIONS_REACHED;
    }
    
    // Create new session
    auth_session_t *session = auth_mgr_allocate_session();
    if (!session) {
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
        LOG_CORE_ERROR("Login failed: cannot allocate session");
        return AUTH_RESULT_INTERNAL_ERROR;
    }
    
    // Initialize session
    uint64_t current_time = auth_mgr_get_timestamp();
    session->session_id = auth_mgr_generate_session_id();
    session->login_time = current_time;
    session->last_activity = current_time;
    session->expires_at = current_time + g_auth_mgr.config.session_timeout_ms;
    session->is_active = AICAM_TRUE;
    
    *session_id = session->session_id;
    
    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
    
    LOG_CORE_INFO("Admin logged in successfully (session: %u)", *session_id);
    
    return AUTH_RESULT_SUCCESS;
}

auth_result_t auth_mgr_logout(uint32_t session_id)
{
    if (!g_auth_mgr.is_initialized) {
        return AUTH_RESULT_INTERNAL_ERROR;
    }
    
    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
    
    auth_session_t *session = auth_mgr_find_session(session_id);
    if (!session || !session->is_active) {
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
        return AUTH_RESULT_SESSION_NOT_FOUND;
    }
    
    LOG_CORE_INFO("Admin logging out (session: %u)", session_id);
    
    // Deactivate session
    session->is_active = AICAM_FALSE;
    
    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
    
    return AUTH_RESULT_SUCCESS;
}

auth_result_t auth_mgr_validate_session(uint32_t session_id, auth_session_t *session)
{
    if (!g_auth_mgr.is_initialized) {
        return AUTH_RESULT_INTERNAL_ERROR;
    }
    
    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
    
    // Cleanup expired sessions
    auth_mgr_cleanup_expired_sessions();
    
    auth_session_t *found_session = auth_mgr_find_session(session_id);
    if (!found_session || !found_session->is_active) {
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
        return AUTH_RESULT_SESSION_NOT_FOUND;
    }
    
    // Check if session has expired
    uint64_t current_time = auth_mgr_get_timestamp();
    if (g_auth_mgr.config.enable_session_timeout && 
        current_time > found_session->expires_at) {
        found_session->is_active = AICAM_FALSE;
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
        LOG_CORE_INFO("Session %u expired", session_id);
        return AUTH_RESULT_SESSION_EXPIRED;
    }
    
    // Update last activity and extend expiration
    found_session->last_activity = current_time;
    found_session->expires_at = current_time + g_auth_mgr.config.session_timeout_ms;
    
    // Copy session info if requested
    if (session) {
        memcpy(session, found_session, sizeof(auth_session_t));
    }
    
    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
    
    return AUTH_RESULT_SUCCESS;
}

auth_result_t auth_mgr_change_password(const char *password)
{
    if (!g_auth_mgr.is_initialized) {
        return AUTH_RESULT_INTERNAL_ERROR;
    }
    
    if (!password) {
        return AUTH_RESULT_INVALID_PARAM;
    }
    
    if (strlen(password) > AUTH_MAX_PASSWORD_LEN) {
        return AUTH_RESULT_INVALID_PARAM;
    }
    
    // Validate session
    // auth_result_t result = auth_mgr_validate_session(session_id, NULL);
    // if (result != AUTH_RESULT_SUCCESS) {
    //     return result;
    // }
    
    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
    
    // Update password in memory
    strncpy(g_auth_mgr.config.admin_password, password, sizeof(g_auth_mgr.config.admin_password) - 1);
    g_auth_mgr.config.admin_password[sizeof(g_auth_mgr.config.admin_password) - 1] = '\0';
    
    // Update password hash
    auth_mgr_hash_password(password, g_auth_mgr.admin_password_hash);
    
    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
    
    // Save password to configuration manager for persistence
    aicam_result_t config_result = json_config_set_device_password(password);
    if (config_result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to save password to configuration manager: %d", config_result);
        // Note: We don't return error here as the password is already updated in memory
        // The system will still work, but the password won't persist across reboots
    } else {
        LOG_CORE_INFO("Password saved to configuration manager successfully");
    }
    
    LOG_CORE_INFO("Admin password changed successfully");
    
    return AUTH_RESULT_SUCCESS;
}

auth_result_t auth_mgr_get_password(char *password_buffer, size_t buffer_size)
{
    if (!g_auth_mgr.is_initialized) {
        return AUTH_RESULT_INTERNAL_ERROR;
    }
    
    if (!password_buffer || buffer_size == 0) {
        return AUTH_RESULT_INVALID_PARAM;
    }
    
    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
    
    size_t password_len = strlen(g_auth_mgr.config.admin_password);
    if (password_len >= buffer_size) {
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
        return AUTH_RESULT_INVALID_PARAM;
    }
    
    strncpy(password_buffer, g_auth_mgr.config.admin_password, buffer_size - 1);
    password_buffer[buffer_size - 1] = '\0';
    
    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
    
    return AUTH_RESULT_SUCCESS;
}

/* ==================== Utility Functions Implementation ==================== */

const char* auth_result_to_string(auth_result_t result)
{
    switch (result) {
        case AUTH_RESULT_SUCCESS:               return "Success";
        case AUTH_RESULT_INVALID_CREDENTIALS:   return "Invalid credentials";
        case AUTH_RESULT_SESSION_EXPIRED:       return "Session expired";
        case AUTH_RESULT_SESSION_NOT_FOUND:     return "Session not found";
        case AUTH_RESULT_MAX_SESSIONS_REACHED:  return "Maximum sessions reached";
        case AUTH_RESULT_INVALID_PARAM:         return "Invalid parameter";
        case AUTH_RESULT_INTERNAL_ERROR:        return "Internal error";
        default:                               return "Unknown error";
    }
}

/* ==================== Internal Functions Implementation ==================== */

static auth_session_t* auth_mgr_find_session(uint32_t session_id)
{
    for (uint32_t i = 0; i < g_auth_mgr.session_count; i++) {
        if (g_auth_mgr.sessions[i].session_id == session_id && 
            g_auth_mgr.sessions[i].is_active) {
            return &g_auth_mgr.sessions[i];
        }
    }
    return NULL;
}

static auth_session_t* auth_mgr_allocate_session(void)
{
    // First, try to find an inactive session slot
    for (uint32_t i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (!g_auth_mgr.sessions[i].is_active) {
            if (i >= g_auth_mgr.session_count) {
                g_auth_mgr.session_count = i + 1;
            }
            memset(&g_auth_mgr.sessions[i], 0, sizeof(auth_session_t));
            return &g_auth_mgr.sessions[i];
        }
    }
    
    return NULL;  // No available session slots
}

static void auth_mgr_cleanup_expired_sessions(void)
{
    if (!g_auth_mgr.config.enable_session_timeout) {
        return;
    }
    
    uint64_t current_time = auth_mgr_get_timestamp();
    
    for (uint32_t i = 0; i < g_auth_mgr.session_count; i++) {
        auth_session_t *session = &g_auth_mgr.sessions[i];
        if (session->is_active && current_time > session->expires_at) {
            LOG_CORE_INFO("Session %u expired", session->session_id);
            session->is_active = AICAM_FALSE;
        }
    }
}

static uint32_t auth_mgr_generate_session_id(void)
{
    uint32_t session_id = g_auth_mgr.next_session_id++;
    
    if (g_auth_mgr.next_session_id > AUTH_SESSION_ID_MAX) {
        g_auth_mgr.next_session_id = AUTH_SESSION_ID_MIN;
    }
    
    // Ensure uniqueness
    while (auth_mgr_find_session(session_id)) {
        session_id = g_auth_mgr.next_session_id++;
        if (g_auth_mgr.next_session_id > AUTH_SESSION_ID_MAX) {
            g_auth_mgr.next_session_id = AUTH_SESSION_ID_MIN;
        }
    }
    
    return session_id;
}

static void auth_mgr_hash_password(const char *password, uint8_t *hash)
{
    // Simple hash implementation
    memset(hash, 0, AUTH_PASSWORD_HASH_LEN);
    
    size_t len = strlen(password);
    uint32_t hash_val = 0x12345678;
    
    for (size_t i = 0; i < len; i++) {
        hash_val = hash_val * 33 + (uint8_t)password[i];
        hash_val ^= (hash_val >> 16);
    }
    
    // Spread hash value across the hash array
    for (int i = 0; i < AUTH_PASSWORD_HASH_LEN; i += 4) {
        hash[i] = (uint8_t)(hash_val >> 24);
        hash[i + 1] = (uint8_t)(hash_val >> 16);
        hash[i + 2] = (uint8_t)(hash_val >> 8);
        hash[i + 3] = (uint8_t)(hash_val);
        hash_val = hash_val * 1664525 + 1013904223;  // LCG
    }
}

static uint64_t auth_mgr_get_timestamp(void)
{
    // In real implementation, use system tick or RTC
    static uint64_t timestamp = 0;
    return ++timestamp * 1000;  // Simulate milliseconds
} 
