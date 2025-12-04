#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include "Log/debug.h"
#include "aicam_error.h"
#include "aicam_types.h"
#include "sl_constants.h"
#include "rsi_ble_apis.h"
#include "rsi_ble.h"
#include "rsi_bt_common.h"
#include "rsi_bt_common_apis.h"
#include "rsi_common_apis.h"
#include "sl_rsi_ble.h"
#include "cmsis_os2.h"
#include "ble_config.h"

// Internal scan state
typedef struct {
    volatile uint8_t is_scanning;
    sl_ble_scan_config_t config;  // Copy of config (accept_list not copied, only used during hardware setup)
    sl_ble_scan_info_t scan_results[SL_BLE_SCAN_RESULT_MAX_COUNT];
    uint8_t scan_count;
    osMutexId_t mutex;
    osSemaphoreId_t scan_sem;
    osThreadId_t scan_timeout_thread;
    uint32_t scan_start_time;
} sl_ble_scan_state_t;

static sl_ble_scan_state_t g_ble_scan_state = {0};

// Forward declarations
static void sl_ble_adv_report_callback(rsi_ble_event_adv_report_t *adv_report);
static void sl_ble_scan_timeout_thread(void *arg);

// Check if device already exists in scan results
static int sl_ble_find_device(uint8_t *addr, uint8_t addr_type)
{
    for (int i = 0; i < g_ble_scan_state.scan_count; i++) {
        if (g_ble_scan_state.scan_results[i].addr_type == addr_type &&
            memcmp(g_ble_scan_state.scan_results[i].addr, addr, 6) == 0) {
            return i;
        }
    }
    return -1;
}

// Add or update device in scan results
static void sl_ble_add_device(rsi_ble_event_adv_report_t *adv_report)
{
    // Check RSSI threshold if configured
    if (g_ble_scan_state.config.rssi_threshold > -127) {
        if (adv_report->rssi < g_ble_scan_state.config.rssi_threshold) {
            return; // RSSI below threshold, ignore this device
        }
    }

    if (g_ble_scan_state.scan_count >= SL_BLE_SCAN_RESULT_MAX_COUNT) {
        return; // Results buffer full
    }

    int idx = sl_ble_find_device(adv_report->dev_addr, adv_report->dev_addr_type);
    
    if (idx >= 0) {
        // Update existing device
        sl_ble_scan_info_t *info = &g_ble_scan_state.scan_results[idx];

        // Update RSSI if better
        // if (adv_report->rssi > info->rssi) {
        //     info->rssi = adv_report->rssi;
        // }
        // update rssi to the latest value
        info->rssi = adv_report->rssi;

        // Extract device name from advertisement data and update if valid & changed
        uint8_t new_name[sizeof(info->name)] = {0};
        BT_LE_ADPacketExtract(new_name, adv_report->adv_data, adv_report->adv_data_len);
        if (new_name[0] != '\0' && memcmp(new_name, info->name, sizeof(info->name)) != 0) {
            memcpy(info->name, new_name, sizeof(info->name));
        }
        return;
    }

    // Add new device
    idx = g_ble_scan_state.scan_count;
    sl_ble_scan_info_t *info = &g_ble_scan_state.scan_results[idx];
    
    info->addr_type = adv_report->dev_addr_type;
    memcpy(info->addr, adv_report->dev_addr, 6);
    info->rssi = adv_report->rssi;
    info->adv_type = adv_report->report_type;
    
    // Extract device name from advertisement data
    memset(info->name, 0, sizeof(info->name));
    BT_LE_ADPacketExtract(info->name, adv_report->adv_data, adv_report->adv_data_len);
    
    g_ble_scan_state.scan_count++;
}

// BLE advertisement report callback
static void sl_ble_adv_report_callback(rsi_ble_event_adv_report_t *adv_report)
{
    if (!g_ble_scan_state.is_scanning) {
        return;
    }

    osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
    
    // Add or update device (hardware filtering is already done)
    sl_ble_add_device(adv_report);
    
    // Call user callback if set
    if (g_ble_scan_state.config.callback) {
        int idx = sl_ble_find_device(adv_report->dev_addr, adv_report->dev_addr_type);
        if (idx >= 0) {
            g_ble_scan_state.config.callback(&g_ble_scan_state.scan_results[idx]);
        }
    }
    
    osMutexRelease(g_ble_scan_state.mutex);
}

// Scan timeout thread
static void sl_ble_scan_timeout_thread(void *arg)
{
    uint32_t duration = (uint32_t)(uintptr_t)arg;
    
    osDelay(duration);
    
    osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
    if (g_ble_scan_state.is_scanning) {
        g_ble_scan_state.is_scanning = 0;
        rsi_ble_stop_scanning();
        
        // Release semaphore to unblock waiting thread
        if (g_ble_scan_state.scan_sem) {
            osSemaphoreRelease(g_ble_scan_state.scan_sem);
        }
    }
    g_ble_scan_state.scan_timeout_thread = NULL;
    osMutexRelease(g_ble_scan_state.mutex);
    
    osThreadExit();
}

uint8_t sl_ble_is_scanning(void)
{
    // If mutex is not initialized yet, no scan is running
    if (g_ble_scan_state.mutex == NULL) {
        return 0;
    }

    uint8_t scanning;

    osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
    scanning = g_ble_scan_state.is_scanning;
    osMutexRelease(g_ble_scan_state.mutex);

    return scanning;
}

int sl_ble_scan_start(sl_ble_scan_config_t *config)
{
    if (config == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    // Initialize mutex if needed
    if (g_ble_scan_state.mutex == NULL) {
        g_ble_scan_state.mutex = osMutexNew(NULL);
        if (g_ble_scan_state.mutex == NULL) {
            return AICAM_ERROR_NO_MEMORY;
        }
    }
    
    osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
    
    // Check if already scanning
    if (g_ble_scan_state.is_scanning) {
        osMutexRelease(g_ble_scan_state.mutex);
        return AICAM_ERROR_BUSY;
    }

    // Terminate timeout thread if exists
    if (g_ble_scan_state.scan_timeout_thread != NULL) {
        osThreadTerminate(g_ble_scan_state.scan_timeout_thread);
        g_ble_scan_state.scan_timeout_thread = NULL;
    }
    
    // Clear previous results
    g_ble_scan_state.scan_count = 0;
    memset(g_ble_scan_state.scan_results, 0, sizeof(g_ble_scan_state.scan_results));
    
    // Copy config (but not accept_list, it's only used during hardware setup)
    memcpy(&g_ble_scan_state.config, config, sizeof(sl_ble_scan_config_t));
    // Clear accept_list pointer in stored config since we don't need it after hardware setup
    g_ble_scan_state.config.accept_list = NULL;
    g_ble_scan_state.config.accept_num = 0;
    
    // Configure hardware accept list if provided (use original config, not stored copy)
    if (config->accept_num > 0 && config->accept_list != NULL) {
        // Clear accept list first
        int32_t ret = rsi_ble_clear_acceptlist();
        if (ret != 0) {
            LOG_DRV_ERROR("rsi_ble_clear_acceptlist failed: %d\n", ret);
            osMutexRelease(g_ble_scan_state.mutex);
            return AICAM_ERROR_HARDWARE;
        }
        
        // Add devices to accept list
        for (int i = 0; i < config->accept_num; i++) {
            sl_ble_device_t *device = &config->accept_list[i];
            ret = rsi_ble_addto_acceptlist((const int8_t *)device->addr, device->addr_type);
            if (ret != 0) {
                LOG_DRV_ERROR("rsi_ble_addto_acceptlist failed: %d\n", ret);
                osMutexRelease(g_ble_scan_state.mutex);
                return AICAM_ERROR_HARDWARE;
            }
        }
    }
    
    // Prepare scan parameters
    rsi_ble_req_scan_t scan_params = {0};
    scan_params.status = RSI_BLE_START_SCAN;
    scan_params.scan_type = g_ble_scan_state.config.scan_type;
    scan_params.scan_int = g_ble_scan_state.config.scan_int;
    scan_params.scan_win = g_ble_scan_state.config.scan_win;
    scan_params.own_addr_type = LE_PUBLIC_ADDRESS;
    
    // Set filter type based on accept list (use original config)
    if (config->accept_num > 0 && config->accept_list != NULL) {
        scan_params.filter_type = SCAN_FILTER_TYPE_ONLY_ACCEPT_LIST;
    } else {
        scan_params.filter_type = SCAN_FILTER_TYPE_ALL;
    }
    
    // Register BLE callbacks if not already registered
    // Note: This should ideally be done once during initialization
    // For now, we register the callback each time
    rsi_ble_gap_register_callbacks(
        sl_ble_adv_report_callback,  // adv_report
        NULL,                         // conn_status
        NULL,                         // disconnect
        NULL,                         // le_ping_timeout
        NULL,                         // phy_update
        NULL,                         // data_length_update
        NULL,                         // enhance_conn_status
        NULL,                         // directed_adv_report
        NULL,                         // conn_update_complete
        NULL                          // remote_conn_params_request
    );
    
    // Start scanning
    int32_t ret = rsi_ble_start_scanning_with_values(&scan_params);
    if (ret != 0) {
        LOG_DRV_ERROR("rsi_ble_start_scanning_with_values failed: %d\n", ret);
        osMutexRelease(g_ble_scan_state.mutex);
        return AICAM_ERROR_HARDWARE;
    }
    
    g_ble_scan_state.is_scanning = 1;
    g_ble_scan_state.scan_start_time = osKernelGetTickCount();
    
    // Create semaphore for blocking mode if needed
    if (g_ble_scan_state.config.callback == NULL && g_ble_scan_state.config.scan_duration > 0) {
        if (g_ble_scan_state.scan_sem == NULL) {
            g_ble_scan_state.scan_sem = osSemaphoreNew(1, 0, NULL);
            if (g_ble_scan_state.scan_sem == NULL) {
                osMutexRelease(g_ble_scan_state.mutex);
                return AICAM_ERROR_NO_MEMORY;
            }
        } else {
            // Reset semaphore if it exists (try to acquire without blocking)
            osSemaphoreAcquire(g_ble_scan_state.scan_sem, 0);
        }
        
        // Create timeout thread
        const osThreadAttr_t thread_attr = {
            .name = "ble_scan_timeout",
            .stack_size = 1024 * 4,
            .priority = osPriorityNormal
        };
        g_ble_scan_state.scan_timeout_thread = osThreadNew(
            sl_ble_scan_timeout_thread, 
            (void *)(uintptr_t)g_ble_scan_state.config.scan_duration, 
            &thread_attr
        );
        
        if (g_ble_scan_state.scan_timeout_thread == NULL) {
            osMutexRelease(g_ble_scan_state.mutex);
            return AICAM_ERROR_NO_MEMORY;
        }
    }
    
    osMutexRelease(g_ble_scan_state.mutex);
    
    // Note: Non-blocking mode - function returns immediately
    // If blocking mode is needed, caller should wait using semaphore or callback
    
    return AICAM_OK;  // AICAM_OK is 0
}

int sl_ble_scan_stop(void)
{
    osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
    
    if (!g_ble_scan_state.is_scanning) {
        osMutexRelease(g_ble_scan_state.mutex);
        return AICAM_OK; // Already stopped, AICAM_OK is 0
    }
    
    g_ble_scan_state.is_scanning = 0;
    
    // Stop scanning
    int32_t ret = rsi_ble_stop_scanning();
    
    // Terminate timeout thread if exists
    if (g_ble_scan_state.scan_timeout_thread != NULL) {
        osThreadTerminate(g_ble_scan_state.scan_timeout_thread);
        g_ble_scan_state.scan_timeout_thread = NULL;
    }
    
    // Release semaphore if waiting
    if (g_ble_scan_state.scan_sem) {
        osSemaphoreRelease(g_ble_scan_state.scan_sem);
    }
    
    osMutexRelease(g_ble_scan_state.mutex);
    
    return (ret == 0) ? AICAM_OK : AICAM_ERROR_HARDWARE;
}

sl_ble_scan_result_t *sl_ble_scan_get_result(void)
{
    static sl_ble_scan_result_t result = {0};
    
    osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
    
    result.scan_count = g_ble_scan_state.scan_count;
    result.scan_info = g_ble_scan_state.scan_results;
    
    osMutexRelease(g_ble_scan_state.mutex);
    
    return &result;
}

void sl_ble_printf_scan_result(sl_ble_scan_result_t *scan_result)
{
    if (scan_result == NULL) {
        return;
    }

    printf("BLE Scan Results (%d devices):\n", scan_result->scan_count);
    printf("--------------------------------------------------------------------------------\n");
    printf("Idx  Address               Type    RSSI(dBm)  Adv   Name\n");
    printf("--------------------------------------------------------------------------------\n");

    for (int i = 0; i < scan_result->scan_count; i++) {
        sl_ble_scan_info_t *info = &scan_result->scan_info[i];

        // Index
        printf("%-4d", i + 1);

        // Address
        printf("%02X:%02X:%02X:%02X:%02X:%02X  ",
               info->addr[5], info->addr[4], info->addr[3],
               info->addr[2], info->addr[1], info->addr[0]);

        // Type (fixed width)
        printf("%-7s", info->addr_type == 0 ? "Public" : "Random");

        // RSSI, right-aligned in 4 chars
        printf("%7d     ", info->rssi);

        // Adv type
        printf("0x%02X  ", info->adv_type);

        // Name
        if (info->name[0] != '\0') {
            printf("%s", info->name);
        } else {
            printf("(N/A)");
        }

        printf("\n");
    }

    printf("--------------------------------------------------------------------------------\n");
}

// Test accept list management
#define BLE_TEST_ACCEPT_LIST_MAX 10
static sl_ble_device_t g_test_accept_list[BLE_TEST_ACCEPT_LIST_MAX];
static uint8_t g_test_accept_list_count = 0;

// Helper function to parse MAC address
static int parse_mac_address(const char *str, uint8_t *addr, uint8_t *addr_type)
{
    if (str == NULL || addr == NULL) {
        return -1;
    }
    
    int values[6];
    int count = sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X", 
                       &values[0], &values[1], &values[2], 
                       &values[3], &values[4], &values[5]);
    if (count != 6) {
        return -1;
    }
    for (int i = 0; i < 6; i++) {
        if (values[i] < 0 || values[i] > 255) {
            return -1;
        }
        addr[i] = (uint8_t)values[i];
    }
    // Default to public address type if not specified
    if (addr_type != NULL) {
        *addr_type = LE_PUBLIC_ADDRESS;
    }
    return 0;
}

// Unified test command handler
static int ble_test_cmd(int argc, char *argv[])
{
    uint8_t mac_addr[6] = { 0 };
    int ret = 0;

    if (argc < 2) {
        printf("Usage: ble <command> [args...]\n");
        printf("Commands:\n");
        printf("  mac - Show MAC address\n");
        printf("  scan_start [scan_type] [duration_sec] [rssi_threshold] [scan_int] [scan_win] - Start scan (non-blocking)\n");
        printf("  scan_stop - Stop scan\n");
        printf("  scan_result - Show scan results\n");
        printf("  scan_status - Show current scan status\n");
        printf("  scan_accept_add <mac> [addr_type] - Add device to accept list (mac: XX:XX:XX:XX:XX:XX)\n");
        printf("  scan_accept_del <mac> - Remove device from accept list\n");
        printf("  scan_accept_list - Show accept list\n");
        printf("  scan_accept_clear - Clear accept list\n");
        return -1;
    }
    
    const char *cmd = argv[1];
    
    // Check scan state for commands that require scan to be stopped
    if (strcmp(cmd, "scan_accept_add") == 0 || strcmp(cmd, "scan_accept_del") == 0 || 
        strcmp(cmd, "scan_accept_clear") == 0) {
        if (g_ble_scan_state.mutex != NULL) {
            osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
            if (g_ble_scan_state.is_scanning) {
                osMutexRelease(g_ble_scan_state.mutex);
                printf("Error: Cannot modify accept list while scanning. Please stop scan first.\n");
                return -1;
            }
            osMutexRelease(g_ble_scan_state.mutex);
        }
    }
    
    if (strcmp(cmd, "mac") == 0) {
        ret = rsi_bt_get_local_device_address(mac_addr);
        if (ret != RSI_SUCCESS) {
            printf("Failed to get MAC address: %d\n", ret);
        } else {
            printf("MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   mac_addr[5], mac_addr[4], mac_addr[3], mac_addr[2], mac_addr[1], mac_addr[0]);
        }
        return ret;
    } else if (strcmp(cmd, "scan_start") == 0) {
        // Check if already scanning
        if (sl_ble_is_scanning()) {
            printf("Error: Scan is already in progress. Please stop it first.\n");
            return -1;
        }
        
        sl_ble_scan_config_t config = {0};
        
        // Default scan parameters
        config.scan_type = 0x01;  // SCAN_TYPE_ACTIVE
        config.scan_int = 0x0100;  // 256 * 0.625ms = 160ms
        config.scan_win = 0x0050;  // 80 * 0.625ms = 50ms
        config.scan_duration = 0;  // 0 means infinite (non-blocking)
        config.rssi_threshold = -127;  // No RSSI filtering by default
        config.accept_num = g_test_accept_list_count;
        config.accept_list = (g_test_accept_list_count > 0) ? g_test_accept_list : NULL;
        config.callback = NULL;  // No callback
        
        // Parse arguments: scan_type [duration_sec] [rssi_threshold] [scan_int] [scan_win]
        if (argc > 2) {
            config.scan_type = (uint8_t)atoi(argv[2]);
        }
        if (argc > 3) {
            config.scan_duration = (uint32_t)atoi(argv[3]) * 1000;  // Convert seconds to milliseconds
        }
        if (argc > 4) {
            config.rssi_threshold = (int8_t)atoi(argv[4]);
        }
        if (argc > 5) {
            config.scan_int = (uint16_t)strtoul(argv[5], NULL, 0);
        }
        if (argc > 6) {
            config.scan_win = (uint16_t)strtoul(argv[6], NULL, 0);
        }
        
        printf("Starting BLE scan (non-blocking):\n");
        printf("  Type: %s\n", config.scan_type == 0x01 ? "active" : "passive");
        if (config.scan_duration > 0) {
            printf("  Duration: %lu ms\n", (unsigned long)config.scan_duration);
        } else {
            printf("  Duration: infinite\n");
        }
        printf("  Interval: 0x%04X (%.1f ms)\n", config.scan_int, config.scan_int * 0.625f);
        printf("  Window: 0x%04X (%.1f ms)\n", config.scan_win, config.scan_win * 0.625f);
        if (config.rssi_threshold > -127) {
            printf("  RSSI threshold: %d dBm\n", config.rssi_threshold);
        } else {
            printf("  RSSI threshold: disabled\n");
        }
        printf("  Accept list: %d devices\n", config.accept_num);
        
        int ret = sl_ble_scan_start(&config);
        if (ret != AICAM_OK) {
            printf("BLE scan start failed: %s (%d)\n", aicam_error_to_string((aicam_result_t)ret), ret);
            return -1;
        }
        
        printf("BLE scan started successfully.\n");
        printf("Use 'ble scan_stop' to stop scanning or 'ble scan_result' to view results.\n");
        
        return 0;
    }
    else if (strcmp(cmd, "scan_stop") == 0) {
        // Check scan state
        if (!sl_ble_is_scanning()) {
            printf("Scan is not in progress.\n");
            return 0;
        }

        int ret = sl_ble_scan_stop();
        if (ret == AICAM_OK) {
            printf("BLE scan stopped.\n");
        } else {
            printf("BLE scan stop failed: %s (%d)\n", aicam_error_to_string((aicam_result_t)ret), ret);
        }
        return (ret == AICAM_OK) ? 0 : -1;
    }
    else if (strcmp(cmd, "scan_status") == 0) {
        uint8_t scanning = sl_ble_is_scanning();
        uint32_t elapsed_ms = 0;
        uint8_t count = 0;

        if (g_ble_scan_state.mutex != NULL) {
            osMutexAcquire(g_ble_scan_state.mutex, osWaitForever);
            if (scanning) {
                elapsed_ms = osKernelGetTickCount() - g_ble_scan_state.scan_start_time;
            }
            count = g_ble_scan_state.scan_count;
            osMutexRelease(g_ble_scan_state.mutex);
        }

        printf("BLE scan status: %s\n", scanning ? "running" : "stopped");
        if (scanning) {
            printf("  Elapsed: %lu ms\n", (unsigned long)elapsed_ms);
        }
        printf("  Devices found: %d\n", count);
        return 0;
    }
    else if (strcmp(cmd, "scan_result") == 0) {
        sl_ble_scan_result_t *result = sl_ble_scan_get_result();
        if (result) {
            sl_ble_printf_scan_result(result);
        } else {
            printf("No scan results available.\n");
        }
        return 0;
    }
    else if (strcmp(cmd, "scan_accept_add") == 0) {
        if (argc < 3) {
            printf("Usage: ble scan_accept_add <mac> [addr_type]\n");
            printf("  mac: MAC address in format XX:XX:XX:XX:XX:XX\n");
            printf("  addr_type: 0=Public, 1=Random (default: 0)\n");
            return -1;
        }
        
        if (g_test_accept_list_count >= BLE_TEST_ACCEPT_LIST_MAX) {
            printf("Accept list is full (max %d devices)\n", BLE_TEST_ACCEPT_LIST_MAX);
            return -1;
        }
        
        sl_ble_device_t *device = &g_test_accept_list[g_test_accept_list_count];
        uint8_t addr_type = LE_PUBLIC_ADDRESS;
        
        if (parse_mac_address(argv[2], device->addr, &addr_type) != 0) {
            printf("Invalid MAC address format. Use XX:XX:XX:XX:XX:XX\n");
            return -1;
        }

        // Convert from human-readable big-endian to hardware order (little-endian)
        uint8_t hw_addr[6];
        for (int i = 0; i < 6; i++) {
            hw_addr[i] = device->addr[5 - i];
        }
        memcpy(device->addr, hw_addr, sizeof(hw_addr));
        
        if (argc > 3) {
            addr_type = (uint8_t)atoi(argv[3]);
        }
        device->addr_type = addr_type;
        
        g_test_accept_list_count++;
        printf("Added device to accept list: %02X:%02X:%02X:%02X:%02X:%02X (type: %d)\n",
               device->addr[5], device->addr[4], device->addr[3],
               device->addr[2], device->addr[1], device->addr[0],
               device->addr_type);
        return 0;
    }
    else if (strcmp(cmd, "scan_accept_del") == 0) {
        if (argc < 3) {
            printf("Usage: ble scan_accept_del <mac>\n");
            return -1;
        }
        
        uint8_t target_addr[6];
        if (parse_mac_address(argv[2], target_addr, NULL) != 0) {
            printf("Invalid MAC address format. Use XX:XX:XX:XX:XX:XX\n");
            return -1;
        }

        // Convert from human-readable big-endian to hardware order (little-endian)
        uint8_t hw_addr[6];
        for (int i = 0; i < 6; i++) {
            hw_addr[i] = target_addr[5 - i];
        }
        memcpy(target_addr, hw_addr, sizeof(hw_addr));
        
        int found = -1;
        for (int i = 0; i < g_test_accept_list_count; i++) {
            if (memcmp(g_test_accept_list[i].addr, target_addr, 6) == 0) {
                found = i;
                break;
            }
        }
        
        if (found < 0) {
            printf("Device not found in accept list\n");
            return -1;
        }
        
        // Remove by shifting remaining elements
        for (int i = found; i < g_test_accept_list_count - 1; i++) {
            g_test_accept_list[i] = g_test_accept_list[i + 1];
        }
        g_test_accept_list_count--;
        
        printf("Removed device from accept list: %02X:%02X:%02X:%02X:%02X:%02X\n",
               target_addr[5], target_addr[4], target_addr[3],
               target_addr[2], target_addr[1], target_addr[0]);
        return 0;
    }
    else if (strcmp(cmd, "scan_accept_list") == 0) {
        printf("Accept list (%d/%d devices):\n", g_test_accept_list_count, BLE_TEST_ACCEPT_LIST_MAX);
        if (g_test_accept_list_count == 0) {
            printf("  (empty)\n");
        } else {
            for (int i = 0; i < g_test_accept_list_count; i++) {
                sl_ble_device_t *device = &g_test_accept_list[i];
                printf("  [%d] %02X:%02X:%02X:%02X:%02X:%02X (type: %s)\n",
                       i + 1,
                       device->addr[5], device->addr[4], device->addr[3],
                       device->addr[2], device->addr[1], device->addr[0],
                       device->addr_type == LE_PUBLIC_ADDRESS ? "Public" : "Random");
            }
        }
        return 0;
    }
    else if (strcmp(cmd, "scan_accept_clear") == 0) {
        g_test_accept_list_count = 0;
        memset(g_test_accept_list, 0, sizeof(g_test_accept_list));
        printf("Accept list cleared.\n");
        return 0;
    }
    else {
        printf("Unknown command: %s\n", cmd);
        printf("Use 'ble' without arguments to see usage.\n");
        return -1;
    }
}

// Command registration table
static debug_cmd_reg_t ble_test_cmd_table[] = {
    {"ble", "BLE test commands: ble <command> [args...]", ble_test_cmd},
};

void sl_ble_test_commands_register(void)
{
    debug_cmdline_register(ble_test_cmd_table, 
                          sizeof(ble_test_cmd_table) / sizeof(ble_test_cmd_table[0]));
}
