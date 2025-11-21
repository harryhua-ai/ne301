/**
 * @file timer_mgr.h
 * @brief Timer Management System Header File
 */

#ifndef TIMER_MGR_H
#define TIMER_MGR_H

#include "aicam_types.h"
#include "cmsis_os2.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TIMER_MGR_MAX_TIMERS 10
#define TIMER_MGR_MAX_NAME_LENGTH 32

/* ==================== Data Type Definitions ==================== */

/**
 * @brief Timer handle type
 */
typedef osTimerId_t timer_handle_t;

/**
 * @brief Timer callback function pointer type
 * @param handle Handle of the timer that triggered this callback
 * @param user_data User-defined data pointer passed when creating the timer
 */
typedef void (*timer_callback_t)(timer_handle_t handle, void* user_data);

/**
 * @brief Timer type enumeration
 */
typedef enum {
    TIMER_TYPE_ONE_SHOT = 0,  // One-shot timer
    TIMER_TYPE_PERIODIC = 1   // Periodic timer
} timer_type_e;

/**
 * @brief Timer state enumeration
 */
typedef enum {
    TIMER_STATE_INACTIVE = 0,  // Inactive
    TIMER_STATE_ACTIVE = 1,    // Active state
    TIMER_STATE_EXPIRED = 2    // Expired (only applicable to one-shot timers)
} timer_state_e;

/**
 * @brief Timer information structure
 */
typedef struct {
    timer_handle_t handle;      // Timer handle
    char name[TIMER_MGR_MAX_NAME_LENGTH]; // Timer name
    uint32_t period_ms;         // Timer period (milliseconds)
    timer_type_e type;          // Timer type
    timer_state_e state;        // Timer state
    timer_callback_t callback;  // Callback function
    void* user_data;           // User data
    uint32_t create_time;      // Creation time
    uint32_t last_trigger_time; // Last trigger time
    uint32_t trigger_count;    // Trigger count
} timer_info_t;

/**
 * @brief Timer statistics information
 */
typedef struct {
    uint32_t total_timers;      // Total timer count
    uint32_t active_timers;     // Active timer count
    uint32_t one_shot_timers;   // One-shot timer count
    uint32_t periodic_timers;   // Periodic timer count
    uint32_t total_triggers;    // Total trigger count
    uint32_t max_callback_time; // Maximum callback execution time (us)
    uint32_t avg_callback_time; // Average callback execution time (us)
} timer_mgr_stats_t;

/* ==================== Interface Function Declarations ==================== */

/**
 * @brief Initialize timer management service
 * @note  This service depends on CMSIS-RTOS2 kernel, but does not require explicit initialization itself
 * @return aicam_result_t Operation result
 */
aicam_result_t timer_mgr_init(void);

/**
 * @brief Deinitialize timer management service
 * @return aicam_result_t Operation result
 */
aicam_result_t timer_mgr_deinit(void);

/**
 * @brief Create a software timer
 * @param name          Descriptive name of the timer (for debugging)
 * @param period_ms     Timer period (unit: milliseconds)
 * @param type          Timer type (one-shot or periodic)
 * @param callback      Callback function to execute when timer expires
 * @param user_data     User-defined data passed to callback function
 * @return timer_handle_t Returns timer handle on success, NULL on failure
 */
timer_handle_t timer_mgr_create(const char* name, 
                                uint32_t period_ms, 
                                timer_type_e type,
                                timer_callback_t callback, 
                                void* user_data);

/**
 * @brief Start or restart a timer
 * @param handle      Handle returned by timer_mgr_create
 * @return aicam_result_t Operation result
 */
aicam_result_t timer_mgr_start(timer_handle_t handle);

/**
 * @brief Stop a timer
 * @param handle      Handle returned by timer_mgr_create
 * @return aicam_result_t Operation result
 */
aicam_result_t timer_mgr_stop(timer_handle_t handle);

/**
 * @brief Reset a timer (make it restart counting)
 * @param handle      Handle returned by timer_mgr_create
 * @return aicam_result_t Operation result
 */
aicam_result_t timer_mgr_reset(timer_handle_t handle);

/**
 * @brief Delete a timer and free its resources
 * @param handle      Handle returned by timer_mgr_create
 * @return aicam_result_t Operation result
 */
aicam_result_t timer_mgr_delete(timer_handle_t handle);

/**
 * @brief Modify timer period
 * @param handle      Timer handle
 * @param new_period_ms New period time (milliseconds)
 * @return aicam_result_t Operation result
 */
aicam_result_t timer_mgr_change_period(timer_handle_t handle, uint32_t new_period_ms);

/**
 * @brief Get timer state
 * @param handle      Timer handle
 * @return timer_state_e Timer state
 */
timer_state_e timer_mgr_get_state(timer_handle_t handle);

/**
 * @brief Check if timer is active
 * @param handle      Timer handle
 * @return bool true indicates active, false indicates inactive
 */
bool timer_mgr_is_active(timer_handle_t handle);

/**
 * @brief Get timer remaining time
 * @param handle      Timer handle
 * @return uint32_t Remaining time (milliseconds), 0 indicates expired or invalid
 */
uint32_t timer_mgr_get_remaining_time(timer_handle_t handle);

/**
 * @brief Get timer information
 * @param handle      Timer handle
 * @param info        Timer information structure pointer (output parameter)
 * @return aicam_result_t Operation result
 */
aicam_result_t timer_mgr_get_info(timer_handle_t handle, timer_info_t* info);

/**
 * @brief Get timer manager statistics
 * @param stats       Statistics structure pointer (output parameter)
 * @return aicam_result_t Operation result
 */
aicam_result_t timer_mgr_get_stats(timer_mgr_stats_t* stats);

/**
 * @brief Reset statistics
 * @return aicam_result_t Operation result
 */
aicam_result_t timer_mgr_reset_stats(void);

/**
 * @brief List all timers
 * @param timer_list  Timer information array (output parameter)
 * @param max_count   Maximum array capacity
 * @param actual_count Actual number of timers returned (output parameter)
 * @return aicam_result_t Operation result
 */
aicam_result_t timer_mgr_list_timers(timer_info_t* timer_list, 
                                     uint32_t max_count, 
                                     uint32_t* actual_count);

/**
 * @brief Find timer by name
 * @param name        Timer name
 * @return timer_handle_t Found timer handle, returns NULL if not found
 */
timer_handle_t timer_mgr_find_by_name(const char* name);

/**
 * @brief Stop all timers
 * @return aicam_result_t Operation result
 */
aicam_result_t timer_mgr_stop_all(void);

/**
 * @brief Delete all timers
 * @return aicam_result_t Operation result
 */
aicam_result_t timer_mgr_delete_all(void);

/* ==================== Convenience Macro Definitions ==================== */

/**
 * @brief Create one-shot timer macro
 */
#define TIMER_CREATE_ONE_SHOT(name, period_ms, callback, user_data) \
    timer_mgr_create(name, period_ms, TIMER_TYPE_ONE_SHOT, callback, user_data)

/**
 * @brief Create periodic timer macro
 */
#define TIMER_CREATE_PERIODIC(name, period_ms, callback, user_data) \
    timer_mgr_create(name, period_ms, TIMER_TYPE_PERIODIC, callback, user_data)

/* ==================== Predefined Timer Period Constants ==================== */

#define TIMER_PERIOD_1MS        1
#define TIMER_PERIOD_10MS       10
#define TIMER_PERIOD_50MS       50
#define TIMER_PERIOD_100MS      100
#define TIMER_PERIOD_500MS      500
#define TIMER_PERIOD_1SEC       1000
#define TIMER_PERIOD_5SEC       5000
#define TIMER_PERIOD_10SEC      10000
#define TIMER_PERIOD_30SEC      30000
#define TIMER_PERIOD_1MIN       60000

#ifdef __cplusplus
}
#endif

#endif // TIMER_MGR_H 