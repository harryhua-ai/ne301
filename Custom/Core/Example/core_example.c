/**
 * @file core_example.c
 * @brief Core System Test Example - Config Manager and Event Bus
 * @details Test example for JSON configuration manager and event bus system
 */

#include "aicam_types.h"
#include "aicam_error.h"
#include "json_config_mgr.h"
#include "generic_log.h"
#include "cmsis_os2.h"
#include <stdio.h>
#include "core_example.h"
#include "event_bus.h"
#include <string.h>

/* ==================== Test Configuration ==================== */

#define TEST_TASK_STACK_SIZE    2048
#define TEST_EVENT_TIMEOUT      1000
#define CONFIG_TEST_INTERVAL    5000
#define EVENT_TEST_INTERVAL     3000

/* ==================== Test Event Types ==================== */

typedef enum {
    TEST_EVENT_CONFIG_CHANGED = 1000,
    TEST_EVENT_SYSTEM_STATUS = 1001,
    TEST_EVENT_USER_ACTION = 1002,
    TEST_EVENT_ERROR_OCCURRED = 1003
} test_event_type_t;

/* ==================== Test Data Structures ==================== */

typedef struct {
    uint32_t sequence_id;
    uint32_t timestamp;
    char message[64];
} test_event_data_t;

typedef struct {
    uint32_t config_tests_passed;
    uint32_t config_tests_failed;
    uint32_t event_tests_passed;
    uint32_t event_tests_failed;
    uint32_t total_events_received;
    uint32_t total_configs_loaded;
} test_statistics_t;

/* ==================== Global Variables ==================== */

static test_statistics_t g_test_stats = {0};
static aicam_global_config_t g_test_config = {0};
static osThreadId_t g_config_test_task = NULL;
static osThreadId_t g_event_test_task = NULL;
static osThreadId_t g_monitor_task = NULL;

/* ==================== Event Bus Test Functions ==================== */

/**
 * @brief Event handler for configuration change events
 */
static void on_config_changed_event(const event_t *event)
{
    printf("[EVENT] Config changed event received, type: %u, size: %u\r\n", 
           (unsigned int)event->event_id, (unsigned int)event->payload_size);
    
    if (event->payload_size == sizeof(test_event_data_t)) {
        test_event_data_t* data = (test_event_data_t*)event->payload;
        printf("[EVENT] Config change: seq=%u, msg='%s'\r\n", 
               (unsigned int)data->sequence_id, data->message);
    }
    
    g_test_stats.total_events_received++;
    g_test_stats.event_tests_passed++;
}

/**
 * @brief Event handler for system status events
 */
static void on_system_status_event(const event_t *event)
{
    printf("[EVENT] System status event received, type: %u\r\n", (unsigned int)event->event_id);
    
    g_test_stats.total_events_received++;
    g_test_stats.event_tests_passed++;
}

/**
 * @brief Event handler for user action events
 */
static void on_user_action_event(const event_t *event)
{
    printf("[EVENT] User action event received, type: %u\r\n", (unsigned int)event->event_id);
    
    if (event->payload_size == sizeof(test_event_data_t)) {
        test_event_data_t* data = (test_event_data_t*)event->payload;
        printf("[EVENT] User action: seq=%u, msg='%s'\r\n", 
               (unsigned int)data->sequence_id, data->message);
    }
    
    g_test_stats.total_events_received++;
    g_test_stats.event_tests_passed++;
}

/**
 * @brief Event handler for error events
 */
static void on_error_event(const event_t *event)
{
    printf("[EVENT] Error event received, type: %u\r\n", (unsigned int)event->event_id);
    
    g_test_stats.total_events_received++;
    g_test_stats.event_tests_failed++;  // Count as failed for testing
}

/* ==================== Configuration Test Functions ==================== */

/**
 * @brief Test configuration loading
 */
static aicam_result_t test_config_load(void)
{
    printf("[CONFIG] Testing configuration load...\r\n");
    
    // Test loading from NVS
    aicam_result_t result = json_config_get_config(&g_test_config);
    if (result == AICAM_OK) {
        printf("[CONFIG] ✓ Load from NVS: SUCCESS\r\n");
        printf("[CONFIG]   Device: %s\r\n", g_test_config.device_info.device_name);
        printf("[CONFIG]   AI Confidence: %.2f\r\n", g_test_config.ai_debug.confidence_threshold / 100.0f);
        printf("[CONFIG]   Log Level: %lu\r\n", (unsigned long)g_test_config.log_config.log_level);
        
        g_test_stats.config_tests_passed++;
        g_test_stats.total_configs_loaded++;
        return AICAM_OK;
    } else {
        printf("[CONFIG] ✗ Load from NVS: FAILED (%d)\r\n", result);
        printf("[CONFIG]   Using default configuration\r\n");
        
        // Initialize with default values
        json_config_load_default(&g_test_config);
        g_test_stats.config_tests_failed++;
        return result;
    }
}

/**
 * @brief Test configuration modification
 */
static aicam_result_t test_config_modify(void)
{
    printf("[CONFIG] Testing configuration modify...\r\n");
    
    // Modify some values
    static uint32_t test_counter = 0;
    test_counter++;
    
    // Update device name
    snprintf(g_test_config.device_info.device_name, sizeof(g_test_config.device_info.device_name), 
             "AICAM-TEST-%04lu", test_counter);
    
    // Update AI confidence threshold (stored as 0-100, increment by 5)
    g_test_config.ai_debug.confidence_threshold += 5;
    if (g_test_config.ai_debug.confidence_threshold > 95) {
        g_test_config.ai_debug.confidence_threshold = 50;
    }
    
    
    // Update log level
    g_test_config.log_config.log_level = (g_test_config.log_config.log_level + 1) % 5;
    
    printf("[CONFIG] ✓ Configuration modified:\r\n");
    printf("[CONFIG]   Device: %s\r\n", g_test_config.device_info.device_name);
    printf("[CONFIG]   AI Confidence: %.2f\r\n", g_test_config.ai_debug.confidence_threshold / 100.0f);
    printf("[CONFIG]   Log Level: %lu\r\n", (unsigned long)g_test_config.log_config.log_level);
    
    g_test_stats.config_tests_passed++;
    return AICAM_OK;
}

/**
 * @brief Test configuration saving
 */
static aicam_result_t test_config_save(void)
{
    printf("[CONFIG] Testing configuration save...\r\n");
    
    aicam_result_t result = json_config_set_config(&g_test_config);
    if (result == AICAM_OK) {
        printf("[CONFIG] ✓ Save to NVS: SUCCESS\r\n");
        g_test_stats.config_tests_passed++;
        
        // Publish config changed event
        test_event_data_t event_data = {
            .sequence_id = g_test_stats.total_configs_loaded,
            .timestamp = osKernelGetTickCount()
        };
        strncpy(event_data.message, "Config saved to NVS", sizeof(event_data.message) - 1);
        
        event_bus_publish(TEST_EVENT_CONFIG_CHANGED, &event_data, sizeof(event_data), AICAM_PRIORITY_HIGH);
        
        return AICAM_OK;
    } else {
        printf("[CONFIG] ✗ Save to NVS: FAILED (%d)\r\n", result);
        g_test_stats.config_tests_failed++;
        return result;
    }
}

/**
 * @brief Test configuration validation
 */
static void test_config_validation(void)
{
    printf("[CONFIG] Testing configuration validation...\r\n");
    
    // Create validation options structure
    json_config_validation_options_t validation_options = {
        .validate_json_syntax = AICAM_TRUE,
        .validate_data_types = AICAM_TRUE,
        .validate_value_ranges = AICAM_TRUE,
        .validate_checksum = AICAM_FALSE,
        .strict_mode = AICAM_FALSE
    };
    
    aicam_result_t result = json_config_validate(&g_test_config, &validation_options);
    if (result == AICAM_OK) {
        printf("[CONFIG] ✓ Configuration validation: PASSED\r\n");
        g_test_stats.config_tests_passed++;
    } else {
        printf("[CONFIG] ✗ Configuration validation: FAILED (error: %d)\r\n", result);
        g_test_stats.config_tests_failed++;
    }
}

/* ==================== Event Bus Test Functions ==================== */

/**
 * @brief Test event publishing
 */
static aicam_result_t test_event_publish(void)
{
    printf("[EVENT] Testing event publishing...\r\n");
    
    static uint32_t event_sequence = 0;
    event_sequence++;
    
    // Test publishing different types of events
    test_event_data_t event_data = {
        .sequence_id = event_sequence,
        .timestamp = osKernelGetTickCount()
    };
    
    // Test 1: Config changed event
    strncpy(event_data.message, "Test config change", sizeof(event_data.message) - 1);
    aicam_result_t result = event_bus_publish(TEST_EVENT_CONFIG_CHANGED, &event_data, sizeof(event_data), AICAM_PRIORITY_HIGH);
    if (result == AICAM_OK) {
        printf("[EVENT] ✓ Config changed event published\r\n");
    } else {
        printf("[EVENT] ✗ Config changed event failed: %d\r\n", result);
        g_test_stats.event_tests_failed++;
        return result;
    }
    
    osDelay(100);
    
    // Test 2: System status event (no data)
    result = event_bus_publish(TEST_EVENT_SYSTEM_STATUS, NULL, 0, AICAM_PRIORITY_HIGH);
    if (result == AICAM_OK) {
        printf("[EVENT] ✓ System status event published\r\n");
    } else {
        printf("[EVENT] ✗ System status event failed: %d\r\n", result);
        g_test_stats.event_tests_failed++;
        return result;
    }
    
    osDelay(100);
    
    // Test 3: User action event
    event_data.sequence_id = ++event_sequence;
    strncpy(event_data.message, "Test user action", sizeof(event_data.message) - 1);
    result = event_bus_publish(TEST_EVENT_USER_ACTION, &event_data, sizeof(event_data), AICAM_PRIORITY_HIGH);
    if (result == AICAM_OK) {
        printf("[EVENT] ✓ User action event published\r\n");
    } else {
        printf("[EVENT] ✗ User action event failed: %d\r\n", result);
        g_test_stats.event_tests_failed++;
        return result;
    }
    
    osDelay(100);
    
    // Test 4: Error event
    event_data.sequence_id = ++event_sequence;
    strncpy(event_data.message, "Test error event", sizeof(event_data.message) - 1);
    result = event_bus_publish(TEST_EVENT_ERROR_OCCURRED, &event_data, sizeof(event_data), AICAM_PRIORITY_HIGH);
    if (result == AICAM_OK) {
        printf("[EVENT] ✓ Error event published\r\n");
    } else {
        printf("[EVENT] ✗ Error event failed: %d\r\n", result);
        g_test_stats.event_tests_failed++;
        return result;
    }
    
    return AICAM_OK;
}

/* ==================== Test Tasks ==================== */

/**
 * @brief Configuration test task
 */
static void config_test_task(void *argument)
{
    AICAM_UNUSED(argument);
    
    printf("[TASK] Configuration test task started\r\n");
    
    // Initial configuration load
    test_config_load();
    
    while (1) {
        printf("\r\n[TASK] === Configuration Test Cycle ===\r\n");
        
        // Test sequence
        test_config_validation();
        osDelay(500);
        
        test_config_modify();
        osDelay(500);
        
        test_config_validation();
        osDelay(500);
        
        test_config_save();
        osDelay(1000);
        
        // Test loading the saved config
        test_config_load();
        
        printf("[TASK] Configuration test cycle completed\r\n");
        osDelay(CONFIG_TEST_INTERVAL);
    }
}

/**
 * @brief Event bus test task
 */
static void event_test_task(void *argument)
{
    AICAM_UNUSED(argument);
    
    printf("[TASK] Event bus test task started\r\n");
    
    while (1) {
        printf("\r\n[TASK] === Event Bus Test Cycle ===\r\n");
        
        test_event_publish();
        
        printf("[TASK] Event bus test cycle completed\r\n");
        osDelay(EVENT_TEST_INTERVAL);
    }
}

/**
 * @brief System monitor task
 */
static void monitor_task(void *argument)
{
    AICAM_UNUSED(argument);
    
    printf("[TASK] System monitor task started\r\n");
    
    while (1) {
        printf("\r\n[MONITOR] === System Statistics ===\r\n");
        printf("[MONITOR] Config Tests - Passed: %lu, Failed: %lu\r\n",
               g_test_stats.config_tests_passed, g_test_stats.config_tests_failed);
        printf("[MONITOR] Event Tests - Passed: %lu, Failed: %lu\r\n",
               g_test_stats.event_tests_passed, g_test_stats.event_tests_failed);
        printf("[MONITOR] Total Events Received: %lu\r\n",
               g_test_stats.total_events_received);
        printf("[MONITOR] Total Configs Loaded: %lu\r\n",
               g_test_stats.total_configs_loaded);
        
        // Calculate success rates
        uint32_t total_config_tests = g_test_stats.config_tests_passed + g_test_stats.config_tests_failed;
        uint32_t total_event_tests = g_test_stats.event_tests_passed + g_test_stats.event_tests_failed;
        
        if (total_config_tests > 0) {
            uint32_t config_success_rate = (g_test_stats.config_tests_passed * 100) / total_config_tests;
            printf("[MONITOR] Config Success Rate: %lu%%\r\n", config_success_rate);
        }
        
        if (total_event_tests > 0) {
            uint32_t event_success_rate = (g_test_stats.event_tests_passed * 100) / total_event_tests;
            printf("[MONITOR] Event Success Rate: %lu%%\r\n", event_success_rate);
        }
        
        printf("[MONITOR] System Uptime: %lu ms\r\n", osKernelGetTickCount());
        
        osDelay(10000); // 10 second interval
    }
}

/* ==================== Main Test Functions ==================== */

/**
 * @brief Initialize event bus subscriptions
 */
static aicam_result_t init_event_subscriptions(void)
{
    printf("[INIT] Initializing event subscriptions...\r\n");
    
    // Subscribe to test events
    event_handle_t handle;
    
    handle = event_bus_subscribe(TEST_EVENT_CONFIG_CHANGED, on_config_changed_event, NULL, NULL);
    if (handle == 0) {
        printf("[INIT] ✗ Failed to subscribe to config changed event: %d\r\n", (int)handle);
        return AICAM_ERROR;
    }
    
    handle = event_bus_subscribe(TEST_EVENT_SYSTEM_STATUS, on_system_status_event, NULL, NULL);
    if (handle == 0) {
        printf("[INIT] ✗ Failed to subscribe to system status event: %d\r\n", (int)handle);
        return AICAM_ERROR;
    }
    
    handle = event_bus_subscribe(TEST_EVENT_USER_ACTION, on_user_action_event, NULL, NULL);
    if (handle == 0) {
        printf("[INIT] ✗ Failed to subscribe to user action event: %d\r\n", (int)handle);
        return AICAM_ERROR;
    }
    
    handle = event_bus_subscribe(TEST_EVENT_ERROR_OCCURRED, on_error_event, NULL, NULL);
    if (handle == 0) {
        printf("[INIT] ✗ Failed to subscribe to error event: %d\r\n", (int)handle);
        return AICAM_ERROR;
    }
    
    printf("[INIT] ✓ All event subscriptions initialized\r\n");
    return AICAM_OK;
}

/**
 * @brief Create test tasks
 */
static aicam_result_t create_test_tasks(void)
{
    printf("[INIT] Creating test tasks...\r\n");
    
    // Create configuration test task
    osThreadAttr_t task_attr = {
        .name = "ConfigTest",
        .stack_size = TEST_TASK_STACK_SIZE,
        .priority = osPriorityNormal
    };
    
    g_config_test_task = osThreadNew(config_test_task, NULL, &task_attr);
    if (g_config_test_task == NULL) {
        printf("[INIT] ✗ Failed to create config test task\r\n");
        return AICAM_ERROR;
    }
    
    // Create event bus test task
    task_attr.name = "EventTest";
    task_attr.priority = osPriorityNormal;
    
    g_event_test_task = osThreadNew(event_test_task, NULL, &task_attr);
    if (g_event_test_task == NULL) {
        printf("[INIT] ✗ Failed to create event test task\r\n");
        return AICAM_ERROR;
    }
    
    // Create monitor task
    task_attr.name = "Monitor";
    task_attr.priority = osPriorityLow;
    task_attr.stack_size = TEST_TASK_STACK_SIZE;
    
    g_monitor_task = osThreadNew(monitor_task, NULL, &task_attr);
    if (g_monitor_task == NULL) {
        printf("[INIT] ✗ Failed to create monitor task\r\n");
        return AICAM_ERROR;
    }
    
    printf("[INIT] ✓ All test tasks created successfully\r\n");
    return AICAM_OK;
}

/**
 * @brief Run core system tests
 */
aicam_result_t run_core_system_test(void)
{
    printf("\r\n=== AICAM Core System Test ===\r\n");
    printf("[INIT] Starting configuration and event bus tests...\r\n");
    
    // Initialize event subscriptions
    aicam_result_t result = init_event_subscriptions();
    if (result != AICAM_OK) {
        printf("[INIT] ✗ Failed to initialize event subscriptions\r\n");
        return result;
    }
    
    // Create test tasks
    result = create_test_tasks();
    if (result != AICAM_OK) {
        printf("[INIT] ✗ Failed to create test tasks\r\n");
        return result;
    }
    
    printf("[INIT] ✓ Core system test initialized successfully\r\n");
    printf("[INIT] Monitor the output to see test results...\r\n");
    printf("===============================================\r\n");
    
    return AICAM_OK;
}

/**
 * @brief Get test statistics
 */
void get_test_statistics(test_statistics_t* stats)
{
    if (stats) {
        *stats = g_test_stats;
    }
} 