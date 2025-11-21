/**
 * @file netif_init_manager.h
 * @brief Network Interface Initialization Manager
 * @details Manages asynchronous initialization of network interfaces to reduce boot time
 */

#ifndef NETIF_INIT_MANAGER_H
#define NETIF_INIT_MANAGER_H

#include "aicam_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Network Interface Initialization States ==================== */

/**
 * @brief Network interface initialization state
 */
typedef enum {
    NETIF_INIT_STATE_IDLE,              // Not initialized
    NETIF_INIT_STATE_INITIALIZING,      // Initialization in progress
    NETIF_INIT_STATE_READY,             // Initialized and ready
    NETIF_INIT_STATE_FAILED             // Initialization failed
} netif_init_state_t;

/**
 * @brief Network interface initialization priority
 */
typedef enum {
    NETIF_INIT_PRIORITY_HIGH,           // High priority (e.g., WiFi AP)
    NETIF_INIT_PRIORITY_NORMAL,         // Normal priority (e.g., WiFi STA)
    NETIF_INIT_PRIORITY_LOW             // Low priority (e.g., 4G, Ethernet)
} netif_init_priority_t;

/**
 * @brief Network interface initialization callback
 * @param if_name Interface name
 * @param result Initialization result (AICAM_OK or error code)
 */
typedef void (*netif_init_callback_t)(const char *if_name, aicam_result_t result);

/**
 * @brief Network interface initialization configuration
 */
typedef struct {
    const char *if_name;                // Interface name (e.g., "wlan0", "ap0")
    netif_init_state_t state;           // Current state
    netif_init_priority_t priority;     // Initialization priority
    aicam_bool_t auto_up;               // Auto bring up interface after init
    aicam_bool_t async;                 // Use asynchronous initialization
    uint32_t init_time_ms;              // Initialization time (milliseconds)
    netif_init_callback_t callback;     // Completion callback
} netif_init_config_t;

/* ==================== Initialization Manager API ==================== */

/**
 * @brief Initialize the network interface initialization manager framework
 * @details This is a lightweight initialization that sets up the manager
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t netif_init_manager_framework_init(void);

/**
 * @brief Deinitialize the network interface initialization manager
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t netif_init_manager_deinit(void);

/**
 * @brief Register a network interface initialization configuration
 * @param config Initialization configuration
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t netif_init_manager_register(const netif_init_config_t *config);

/**
 * @brief Unregister a network interface
 * @param if_name Interface name
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t netif_init_manager_unregister(const char *if_name);

/**
 * @brief Asynchronously initialize a network interface
 * @details Returns immediately, initialization happens in background
 * @param if_name Interface name
 * @return AICAM_OK if initialization started, error code otherwise
 */
aicam_result_t netif_init_manager_init_async(const char *if_name);

/**
 * @brief Synchronously initialize a network interface (blocking)
 * @param if_name Interface name
 * @param timeout_ms Timeout in milliseconds
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t netif_init_manager_init_sync(const char *if_name, uint32_t timeout_ms);

/**
 * @brief Initialize all registered network interfaces
 * @param async Use asynchronous initialization for all interfaces
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t netif_init_manager_init_all(aicam_bool_t async);

/**
 * @brief Get initialization state of a network interface
 * @param if_name Interface name
 * @return Current initialization state
 */
netif_init_state_t netif_init_manager_get_state(const char *if_name);

/**
 * @brief Wait for a network interface to become ready
 * @param if_name Interface name
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @return AICAM_OK if ready, AICAM_ERROR_TIMEOUT if timeout, error code otherwise
 */
aicam_result_t netif_init_manager_wait_ready(const char *if_name, uint32_t timeout_ms);

/**
 * @brief Check if a network interface is ready
 * @param if_name Interface name
 * @return AICAM_TRUE if ready, AICAM_FALSE otherwise
 */
aicam_bool_t netif_init_manager_is_ready(const char *if_name);

/**
 * @brief Get initialization time of a network interface
 * @param if_name Interface name
 * @return Initialization time in milliseconds (0 if not initialized)
 */
uint32_t netif_init_manager_get_init_time(const char *if_name);

/**
 * @brief Get number of registered network interfaces
 * @return Number of registered interfaces
 */
uint32_t netif_init_manager_get_count(void);

/**
 * @brief Get list of registered network interface names
 * @param names Array to store interface names
 * @param max_count Maximum number of names to return
 * @param actual_count Actual number of interfaces (output)
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t netif_init_manager_get_list(const char **names, uint32_t max_count, uint32_t *actual_count);

#ifdef __cplusplus
}
#endif

#endif /* NETIF_INIT_MANAGER_H */

