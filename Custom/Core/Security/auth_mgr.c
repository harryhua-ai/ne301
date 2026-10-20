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
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include "cmsis_os2.h"

/* ==================== Internal Data Structures ==================== */

typedef struct {
    uint32_t magic_number;
    aicam_bool_t is_initialized;
    aicam_bool_t credential_trusted;
    aicam_bool_t has_credential_record;
    aicam_bool_t legacy_upgrade_attempted;
    auth_mgr_config_t config;
    auth_session_t sessions[AUTH_MAX_SESSIONS];
    uint32_t session_count;
    uint32_t next_session_id;
    void *mutex;
} auth_mgr_state_t;

/* ==================== Global Variables ==================== */

static auth_mgr_state_t g_auth_mgr = {0};

/* ==================== Thread Safety Macros ==================== */
#define AUTH_MUTEX_CREATE()         osMutexNew(NULL)
#define AUTH_MUTEX_DESTROY(mutex)   osMutexDelete(mutex)
#define AUTH_MUTEX_LOCK(mutex)      osMutexAcquire(mutex, osWaitForever)
#define AUTH_MUTEX_UNLOCK(mutex)    osMutexRelease(mutex)


/* ==================== Internal Function Declarations ==================== */

static auth_session_t* auth_mgr_find_session(uint32_t session_id);
static auth_session_t* auth_mgr_allocate_session(void);
static void auth_mgr_cleanup_expired_sessions(void);
static uint32_t auth_mgr_generate_session_id(void);
static uint64_t auth_mgr_get_timestamp(void);
static aicam_bool_t auth_mgr_verify_locked(const char *password);
static void auth_mgr_sync_credential_state(const char *password);
static void auth_mgr_upgrade_legacy_credential(void);
static int auth_mgr_fill_salt(uint8_t *salt, size_t salt_len);
static int auth_mgr_constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len);

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

    char config_credential[AUTH_CREDENTIAL_RECORD_MAX];
    aicam_result_t config_result = json_config_get_device_credential(config_credential, sizeof(config_credential));
    if (config_result == AICAM_OK &&
        strncmp(config_credential, AUTH_CREDENTIAL_RECORD_PREFIX, strlen(AUTH_CREDENTIAL_RECORD_PREFIX)) == 0 &&
        auth_mgr_credential_is_record(config_credential) != AICAM_TRUE)
    {
        LOG_CORE_ERROR("Admin credential looks like a record but failed to parse, fail-closed: login and password change disabled, no default fallback");
        config_result = AICAM_ERROR;
    }
    if (config_result == AICAM_OK && auth_mgr_credential_is_record(config_credential) == AICAM_TRUE) {
        strncpy(g_auth_mgr.config.admin_credential, config_credential, sizeof(g_auth_mgr.config.admin_credential) - 1);
        g_auth_mgr.config.admin_credential[sizeof(g_auth_mgr.config.admin_credential) - 1] = '\0';
        memset(g_auth_mgr.config.admin_password, 0, sizeof(g_auth_mgr.config.admin_password));
        g_auth_mgr.has_credential_record = AICAM_TRUE;
        g_auth_mgr.credential_trusted = AICAM_TRUE;
        LOG_CORE_INFO("Admin credential record loaded from configuration manager");
    } else if (config_result == AICAM_OK) {
        strncpy(g_auth_mgr.config.admin_password, config_credential, sizeof(g_auth_mgr.config.admin_password) - 1);
        g_auth_mgr.config.admin_password[sizeof(g_auth_mgr.config.admin_password) - 1] = '\0';
        memset(g_auth_mgr.config.admin_credential, 0, sizeof(g_auth_mgr.config.admin_credential));
        g_auth_mgr.has_credential_record = AICAM_FALSE;
        g_auth_mgr.credential_trusted = AICAM_TRUE;
        LOG_CORE_INFO("Legacy admin credential loaded from configuration manager, record upgrade pending first successful authentication");
    } else {
        memset(g_auth_mgr.config.admin_password, 0, sizeof(g_auth_mgr.config.admin_password));
        memset(g_auth_mgr.config.admin_credential, 0, sizeof(g_auth_mgr.config.admin_credential));
        g_auth_mgr.has_credential_record = AICAM_FALSE;
        g_auth_mgr.credential_trusted = AICAM_FALSE;
        LOG_CORE_ERROR("Admin credential not loadable from config manager(rc=%d), auth fail-closed: login and password change disabled, public default not used", config_result);
    }
    memset(config_credential, 0, sizeof(config_credential));
    // Create mutex for thread safety
    g_auth_mgr.mutex = AUTH_MUTEX_CREATE();
    if (!g_auth_mgr.mutex) {
        LOG_CORE_ERROR("Failed to create authentication manager mutex");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Initialize session ID counter
    g_auth_mgr.next_session_id = AUTH_SESSION_ID_MIN;

    LOG_CORE_INFO("Authentication manager mutex created successfully");

    g_auth_mgr.is_initialized = AICAM_TRUE;

    LOG_CORE_INFO("Authentication Manager initialized successfully");
    LOG_CORE_INFO("Admin username: '%s'", AUTH_ADMIN_USERNAME);
    if (g_auth_mgr.credential_trusted == AICAM_TRUE && g_auth_mgr.has_credential_record != AICAM_TRUE) {
        auth_mgr_upgrade_legacy_credential();
    }

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
    if (!password) {
        return AICAM_FALSE;
    }

    if (g_auth_mgr.is_initialized != AICAM_TRUE || g_auth_mgr.credential_trusted != AICAM_TRUE) {
        return AICAM_FALSE;
    }

    if (strlen(password) > AUTH_MAX_PASSWORD_LEN) {
        return AICAM_FALSE;
    }

    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);

    aicam_bool_t result = auth_mgr_verify_locked(password);
    aicam_bool_t legacy_verified = (result == AICAM_TRUE && g_auth_mgr.has_credential_record != AICAM_TRUE);

    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);

    if (legacy_verified == AICAM_TRUE) {
        auth_mgr_upgrade_legacy_credential();
    }

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

    if (!auth_mgr_verify_locked(password)) {
        AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
        LOG_CORE_WARN("Login failed: invalid password for admin");
        return AUTH_RESULT_INVALID_CREDENTIALS;
    }
    aicam_bool_t legacy_verified = (g_auth_mgr.has_credential_record != AICAM_TRUE);
    
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

    if (legacy_verified == AICAM_TRUE) {
        auth_mgr_upgrade_legacy_credential();
    }

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

    if (g_auth_mgr.credential_trusted != AICAM_TRUE) {
        LOG_CORE_ERROR("Password change refused: session credential untrusted, fail-closed");
        return AUTH_RESULT_INTERNAL_ERROR;
    }

    aicam_result_t config_result = json_config_set_device_password(password);
    if (config_result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to persist admin credential(rc=%d), change not applied, previous credential kept", config_result);
        if (json_config_nvs_credential_undecided() == AICAM_TRUE) {
            AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
            memset(g_auth_mgr.config.admin_password, 0, sizeof(g_auth_mgr.config.admin_password));
            memset(g_auth_mgr.config.admin_credential, 0, sizeof(g_auth_mgr.config.admin_credential));
            g_auth_mgr.has_credential_record = AICAM_FALSE;
            g_auth_mgr.credential_trusted = AICAM_FALSE;
            for (uint32_t i = 0; i < g_auth_mgr.session_count; i++) {
                if (g_auth_mgr.sessions[i].is_active) {
                    g_auth_mgr.sessions[i].is_active = AICAM_FALSE;
                }
            }
            AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
            LOG_CORE_ERROR("Credential state undecided, auth manager fail-closed, all sessions revoked until reset-to-default");
        }
        return AUTH_RESULT_INTERNAL_ERROR;
    }

    auth_mgr_sync_credential_state(password);
    auth_mgr_revoke_all_sessions();

    LOG_CORE_INFO("Password saved to configuration manager successfully");

    LOG_CORE_INFO("Admin password changed successfully, active sessions revoked");

    return AUTH_RESULT_SUCCESS;
}

void auth_mgr_revoke_all_sessions(void)
{
    if (!g_auth_mgr.is_initialized) {
        return;
    }

    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);

    for (uint32_t i = 0; i < g_auth_mgr.session_count; i++) {
        if (g_auth_mgr.sessions[i].is_active) {
            g_auth_mgr.sessions[i].is_active = AICAM_FALSE;
            LOG_CORE_INFO("Session %u revoked after credential change", g_auth_mgr.sessions[i].session_id);
        }
    }

    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);
}

static void auth_mgr_sync_credential_state(const char *password)
{
    char record[AUTH_CREDENTIAL_RECORD_MAX];
    aicam_result_t credential_result = json_config_get_device_credential(record, sizeof(record));

    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);

    if (password != NULL) {
        strncpy(g_auth_mgr.config.admin_password, password, sizeof(g_auth_mgr.config.admin_password) - 1);
        g_auth_mgr.config.admin_password[sizeof(g_auth_mgr.config.admin_password) - 1] = '\0';
    }

    if (credential_result == AICAM_OK && auth_mgr_credential_is_record(record) == AICAM_TRUE) {
        strncpy(g_auth_mgr.config.admin_credential, record, sizeof(g_auth_mgr.config.admin_credential) - 1);
        g_auth_mgr.config.admin_credential[sizeof(g_auth_mgr.config.admin_credential) - 1] = '\0';
        g_auth_mgr.has_credential_record = AICAM_TRUE;
    }

    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);

    memset(record, 0, sizeof(record));
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

static aicam_bool_t auth_mgr_verify_locked(const char *password)
{
    if (g_auth_mgr.has_credential_record == AICAM_TRUE) {
        return auth_mgr_credential_record_verify(g_auth_mgr.config.admin_credential, password);
    }

    if (g_auth_mgr.config.admin_password[0] == '\0') {
        return AICAM_FALSE;
    }

    size_t password_len = strlen(password);
    size_t stored_len = strlen(g_auth_mgr.config.admin_password);
    if (password_len != stored_len) {
        return AICAM_FALSE;
    }

    return (auth_mgr_constant_time_compare((const uint8_t *)password,
                                           (const uint8_t *)g_auth_mgr.config.admin_password,
                                           stored_len) == 0) ? AICAM_TRUE : AICAM_FALSE;
}

static void auth_mgr_upgrade_legacy_credential(void)
{
    if (!g_auth_mgr.is_initialized) {
        return;
    }

    if (g_auth_mgr.legacy_upgrade_attempted == AICAM_TRUE ||
        g_auth_mgr.has_credential_record == AICAM_TRUE ||
        g_auth_mgr.credential_trusted != AICAM_TRUE) {
        return;
    }

    char password[AUTH_MAX_PASSWORD_LEN + 1];

    AUTH_MUTEX_LOCK(g_auth_mgr.mutex);
    g_auth_mgr.legacy_upgrade_attempted = AICAM_TRUE;
    strncpy(password, g_auth_mgr.config.admin_password, sizeof(password) - 1);
    password[sizeof(password) - 1] = '\0';
    AUTH_MUTEX_UNLOCK(g_auth_mgr.mutex);

    if (password[0] == '\0') {
        memset(password, 0, sizeof(password));
        return;
    }

    if (json_config_set_device_password(password) != AICAM_OK) {
        LOG_CORE_WARN("Legacy admin credential record upgrade deferred, verification keeps using the legacy credential");
        memset(password, 0, sizeof(password));
        return;
    }

    auth_mgr_sync_credential_state(password);
    memset(password, 0, sizeof(password));

    LOG_CORE_INFO("Legacy admin credential upgraded to salted record after verified NVS persistence");
}

aicam_bool_t auth_mgr_default_password_active(void)
{
    if (g_auth_mgr.is_initialized != AICAM_TRUE || g_auth_mgr.credential_trusted != AICAM_TRUE) {
        return AICAM_FALSE;
    }

    return auth_mgr_verify_password(AUTH_DEFAULT_PASSWORD);
}

auth_verdict_t auth_mgr_authorize(const char *username, const char *password)
{
    auth_verdict_t verdict = {AICAM_FALSE, AICAM_FALSE, AICAM_FALSE};

    if (!username || !password) {
        return verdict;
    }

    if (g_auth_mgr.is_initialized != AICAM_TRUE || g_auth_mgr.credential_trusted != AICAM_TRUE) {
        return verdict;
    }

    if (username[0] == '\0' || password[0] == '\0') {
        return verdict;
    }

    if (strlen(password) > AUTH_MAX_PASSWORD_LEN) {
        return verdict;
    }

    if (strcmp(username, AUTH_ADMIN_USERNAME) != 0) {
        return verdict;
    }

    if (auth_mgr_verify_password(password) != AICAM_TRUE) {
        return verdict;
    }

    verdict.authenticated = AICAM_TRUE;
    verdict.default_password_active = auth_mgr_default_password_active();
    verdict.high_privilege_allowed = (verdict.default_password_active != AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE;

    return verdict;
}

static int auth_mgr_constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len)
{
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff;
}

static int auth_mgr_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void auth_mgr_bytes_to_hex(const uint8_t *data, size_t len, char *out)
{
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex_chars[data[i] >> 4];
        out[i * 2 + 1] = hex_chars[data[i] & 0x0F];
    }
}

static aicam_bool_t auth_mgr_hex_to_bytes(const char *hex, uint8_t *out, size_t out_len)
{
    for (size_t i = 0; i < out_len; i++) {
        int hi = auth_mgr_hex_nibble(hex[i * 2]);
        int lo = auth_mgr_hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return AICAM_FALSE;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return AICAM_TRUE;
}

static int auth_mgr_fill_salt(uint8_t *salt, size_t salt_len)
{
    uint8_t pool[AUTH_CREDENTIAL_KEY_LEN];
    uint8_t digest[AUTH_CREDENTIAL_KEY_LEN];
    int sha_rc;
    static uint32_t salt_counter = 0x5A17A17Au;
    uint32_t tick = (uint32_t)osKernelGetTickCount();
    uintptr_t address_entropy = (uintptr_t)(void *)&g_auth_mgr;

    memset(pool, 0, sizeof(pool));
    memcpy(pool, &tick, sizeof(tick));
    salt_counter++;
    memcpy(pool + sizeof(tick), &salt_counter, sizeof(salt_counter));
    memcpy(pool + sizeof(tick) + sizeof(salt_counter), &address_entropy, sizeof(address_entropy));
    for (size_t i = 0; i < sizeof(pool); i++) {
        pool[i] = (uint8_t)(pool[i] ^ (uint8_t)(i * 31u + salt_counter));
    }

    sha_rc = mbedtls_sha256(pool, sizeof(pool), digest, 0);
    if (sha_rc != 0) {
        memset(pool, 0, sizeof(pool));
        memset(digest, 0, sizeof(digest));
        memset(salt, 0, salt_len);
        return sha_rc;
    }
    memcpy(salt, digest, salt_len < sizeof(digest) ? salt_len : sizeof(digest));

    memset(pool, 0, sizeof(pool));
    memset(digest, 0, sizeof(digest));
    return 0;
}

aicam_bool_t auth_mgr_credential_is_record(const char *value)
{
    if (!value) {
        return AICAM_FALSE;
    }

    size_t prefix_len = strlen(AUTH_CREDENTIAL_RECORD_PREFIX);
    size_t expected_len = prefix_len
                          + AUTH_CREDENTIAL_SALT_LEN * 2
                          + 1
                          + AUTH_CREDENTIAL_KEY_LEN * 2;
    if (strlen(value) != expected_len) {
        return AICAM_FALSE;
    }

    if (strncmp(value, AUTH_CREDENTIAL_RECORD_PREFIX, prefix_len) != 0) {
        return AICAM_FALSE;
    }

    const char *salt_hex = value + prefix_len;
    const char *separator = salt_hex + AUTH_CREDENTIAL_SALT_LEN * 2;
    const char *key_hex = separator + 1;

    if (*separator != ':') {
        return AICAM_FALSE;
    }

    uint8_t ignored_salt[AUTH_CREDENTIAL_SALT_LEN];
    uint8_t ignored_key[AUTH_CREDENTIAL_KEY_LEN];
    aicam_bool_t parseable = (auth_mgr_hex_to_bytes(salt_hex, ignored_salt, sizeof(ignored_salt)) == AICAM_TRUE &&
                              auth_mgr_hex_to_bytes(key_hex, ignored_key, sizeof(ignored_key)) == AICAM_TRUE) ?
                             AICAM_TRUE : AICAM_FALSE;
    memset(ignored_salt, 0, sizeof(ignored_salt));
    memset(ignored_key, 0, sizeof(ignored_key));
    return parseable;
}

static int auth_mgr_pbkdf2_hmac_sha256(const char *password,
                                       const uint8_t *salt,
                                       size_t salt_len,
                                       uint32_t iterations,
                                       uint8_t *dk,
                                       size_t dk_len)
{
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    int hmac_rc;
    uint8_t u[AUTH_CREDENTIAL_KEY_LEN];
    uint8_t t[AUTH_CREDENTIAL_KEY_LEN];
    uint8_t salt_block[AUTH_CREDENTIAL_SALT_LEN + 4];
    uint32_t block_index = 1;
    size_t copied = 0;

    if (!md_info) {
        return -1;
    }

    while (copied < dk_len) {
        size_t block_len = dk_len - copied;
        if (block_len > AUTH_CREDENTIAL_KEY_LEN) {
            block_len = AUTH_CREDENTIAL_KEY_LEN;
        }

        memcpy(salt_block, salt, salt_len);
        salt_block[salt_len] = (uint8_t)(block_index >> 24);
        salt_block[salt_len + 1] = (uint8_t)(block_index >> 16);
        salt_block[salt_len + 2] = (uint8_t)(block_index >> 8);
        salt_block[salt_len + 3] = (uint8_t)(block_index);

        hmac_rc = mbedtls_md_hmac(md_info, (const unsigned char *)password, strlen(password),
                                  salt_block, sizeof(salt_block), u);
        if (hmac_rc != 0) {
            memset(u, 0, sizeof(u));
            memset(t, 0, sizeof(t));
            memset(salt_block, 0, sizeof(salt_block));
            return hmac_rc;
        }
        memcpy(t, u, sizeof(t));

        for (uint32_t i = 1; i < iterations; i++) {
            hmac_rc = mbedtls_md_hmac(md_info, (const unsigned char *)password, strlen(password),
                                      u, sizeof(u), u);
            if (hmac_rc != 0) {
                memset(u, 0, sizeof(u));
                memset(t, 0, sizeof(t));
                memset(salt_block, 0, sizeof(salt_block));
                return hmac_rc;
            }
            for (size_t j = 0; j < sizeof(t); j++) {
                t[j] = (uint8_t)(t[j] ^ u[j]);
            }
        }

        memcpy(dk + copied, t, block_len);
        copied += block_len;
        block_index++;
    }

    memset(u, 0, sizeof(u));
    memset(t, 0, sizeof(t));
    memset(salt_block, 0, sizeof(salt_block));
    return 0;
}

aicam_result_t auth_mgr_credential_record_encode(const char *password,
                                                 char *record,
                                                 size_t record_size)
{
    if (!password || !record || record_size < AUTH_CREDENTIAL_RECORD_MAX) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (password[0] == '\0' || strlen(password) > AUTH_MAX_PASSWORD_LEN) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    uint8_t salt[AUTH_CREDENTIAL_SALT_LEN];
    uint8_t derived_key[AUTH_CREDENTIAL_KEY_LEN];
    int pbkdf2_rc;
    int salt_rc;
    size_t prefix_len = strlen(AUTH_CREDENTIAL_RECORD_PREFIX);

    memset(record, 0, record_size);
    salt_rc = auth_mgr_fill_salt(salt, sizeof(salt));
    if (salt_rc != 0) {
        memset(salt, 0, sizeof(salt));
        LOG_CORE_ERROR("Admin credential encode failed: salt digest rc=%d, no record produced", salt_rc);
        return AICAM_ERROR;
    }

    pbkdf2_rc = auth_mgr_pbkdf2_hmac_sha256(password, salt, sizeof(salt),
                                            AUTH_PBKDF2_ITERATIONS,
                                            derived_key, sizeof(derived_key));
    if (pbkdf2_rc != 0) {
        memset(salt, 0, sizeof(salt));
        LOG_CORE_ERROR("Admin credential encode failed: PBKDF2 rc=%d, no record produced", pbkdf2_rc);
        return AICAM_ERROR;
    }

    memcpy(record, AUTH_CREDENTIAL_RECORD_PREFIX, prefix_len);
    auth_mgr_bytes_to_hex(salt, sizeof(salt), record + prefix_len);
    record[prefix_len + AUTH_CREDENTIAL_SALT_LEN * 2] = ':';
    auth_mgr_bytes_to_hex(derived_key, sizeof(derived_key),
                          record + prefix_len + AUTH_CREDENTIAL_SALT_LEN * 2 + 1);
    record[AUTH_CREDENTIAL_RECORD_MAX - 1] = '\0';

    memset(salt, 0, sizeof(salt));
    memset(derived_key, 0, sizeof(derived_key));
    return AICAM_OK;
}

aicam_bool_t auth_mgr_credential_record_verify(const char *record,
                                               const char *password)
{
    if (!record || !password) {
        return AICAM_FALSE;
    }

    if (auth_mgr_credential_is_record(record) != AICAM_TRUE) {
        return AICAM_FALSE;
    }

    size_t prefix_len = strlen(AUTH_CREDENTIAL_RECORD_PREFIX);
    const char *salt_hex = record + prefix_len;
    const char *key_hex = salt_hex + AUTH_CREDENTIAL_SALT_LEN * 2 + 1;

    uint8_t salt[AUTH_CREDENTIAL_SALT_LEN];
    uint8_t stored_key[AUTH_CREDENTIAL_KEY_LEN];
    uint8_t computed_key[AUTH_CREDENTIAL_KEY_LEN];

    if (auth_mgr_hex_to_bytes(salt_hex, salt, sizeof(salt)) != AICAM_TRUE ||
        auth_mgr_hex_to_bytes(key_hex, stored_key, sizeof(stored_key)) != AICAM_TRUE) {
        return AICAM_FALSE;
    }

    if (auth_mgr_pbkdf2_hmac_sha256(password, salt, sizeof(salt),
                                    AUTH_PBKDF2_ITERATIONS,
                                    computed_key, sizeof(computed_key)) != 0) {
        memset(salt, 0, sizeof(salt));
        memset(stored_key, 0, sizeof(stored_key));
        memset(computed_key, 0, sizeof(computed_key));
        return AICAM_FALSE;
    }

    aicam_bool_t result = (auth_mgr_constant_time_compare(computed_key, stored_key, sizeof(stored_key)) == 0) ?
                          AICAM_TRUE : AICAM_FALSE;

    memset(salt, 0, sizeof(salt));
    memset(stored_key, 0, sizeof(stored_key));
    memset(computed_key, 0, sizeof(computed_key));
    return result;
}

static uint64_t auth_mgr_get_timestamp(void)
{
    // In real implementation, use system tick or RTC
    static uint64_t timestamp = 0;
    return ++timestamp * 1000;  // Simulate milliseconds
} 
