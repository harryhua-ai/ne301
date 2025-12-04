/**
 * @file communication_service.c
 * @brief Communication Service Implementation
 * @details communication service standard interface implementation, focus on network interface information collection and configuration management
 */

#include "communication_service.h"
#include "aicam_types.h"
#include "debug.h"
#include "system_service.h"
#include "netif_manager.h"
#include "netif_init_manager.h"
#include "sl_net_netif.h"
#include "buffer_mgr.h"
#include <string.h>
#include <stdio.h>
#include "cmsis_os2.h"
#include "common_utils.h"
#include "service_init.h"
#include "mqtt_service.h"
#include "device_service.h"
#include "u0_module.h"



/* ==================== Communication Service Context ==================== */

#define COMMUNICATION_SERVICE_VERSION "1.0.0"
#define MAX_NETWORK_INTERFACES 8
#define MAX_KNOWN_NETWORKS 16
#define MAX_SCAN_RESULTS 32
uint8_t background_scan_task_stack[1024 * 4] ALIGN_32 IN_PSRAM;

typedef struct {
    aicam_bool_t initialized;
    aicam_bool_t running;
    service_state_t state;
    communication_service_config_t config;
    communication_service_stats_t stats;
    osThreadId_t scan_task_id;
    osSemaphoreId_t scan_semaphore_id;
    
    // Network interface management
    network_interface_status_t interfaces[MAX_NETWORK_INTERFACES];
    uint32_t interface_count;
    
    // Network scanning
    network_scan_result_t scan_results[MAX_SCAN_RESULTS];
    uint32_t scan_result_count;
    aicam_bool_t scan_in_progress;
    
    // Known networks database
    network_scan_result_t known_networks[MAX_KNOWN_NETWORKS];
    uint32_t known_network_count;
    
    // Auto-reconnection
    aicam_bool_t auto_reconnect_enabled;
    uint32_t reconnect_timer;
    
    // Network manager registration
    aicam_bool_t netif_manager_registered;
} communication_service_context_t;

static communication_service_context_t g_communication_service = {0};

/* ==================== Default Configuration ==================== */

static const communication_service_config_t default_config = {
    .auto_start_wifi_ap = AICAM_TRUE,
    .auto_start_wifi_sta = AICAM_TRUE,
    .enable_network_scan = AICAM_TRUE,
    .enable_auto_reconnect = AICAM_TRUE,
    .reconnect_interval_ms = 5000,
    .scan_interval_ms = 30000,
    .connection_timeout_ms = 10000,
    .enable_debug = AICAM_FALSE,
    .enable_stats = AICAM_TRUE
};

static aicam_bool_t is_connected(const char *ssid, const char *bssid);
static void add_known_network(const network_scan_result_t *network);

/* ==================== Network Interface Ready Callbacks ==================== */

static void on_wifi_ap_ready(const char *if_name, aicam_result_t result);
static void on_wifi_sta_ready(const char *if_name, aicam_result_t result);

/* ==================== Known Networks Persistence ==================== */

/**
 * @brief Save known networks to NVS
 */
static aicam_result_t save_known_networks_to_nvs(void)
{
    network_service_config_t *network_config = (network_service_config_t*)buffer_calloc(1, sizeof(network_service_config_t));
    if (!network_config) {
        LOG_SVC_ERROR("Failed to allocate memory for network config");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Get current network service config
    aicam_result_t result = json_config_get_network_service_config(network_config);
    if (result != AICAM_OK) {
        LOG_SVC_WARN("Failed to get network service config: %d", result);
        buffer_free(network_config);
        return result;
    }
    
    // Copy known networks from communication service to network config
    network_config->known_network_count = g_communication_service.known_network_count;
    for (uint32_t i = 0; i < g_communication_service.known_network_count && i < MAX_KNOWN_NETWORKS; i++) {
        memcpy(&network_config->known_networks[i], 
               &g_communication_service.known_networks[i], 
               sizeof(network_scan_result_t));
    }
    
    // Save to NVS
    result = json_config_set_network_service_config(network_config);
    
    buffer_free(network_config);
    
    if (result == AICAM_OK) {
        LOG_SVC_DEBUG("Known networks saved to NVS successfully");
    } else {
        LOG_SVC_ERROR("Failed to save known networks to NVS: %d", result);
    }
    
    return result;
}

/**
 * @brief Load known networks from NVS
 */
static aicam_result_t load_known_networks_from_nvs(void)
{
    network_service_config_t *network_config = (network_service_config_t*)buffer_calloc(1, sizeof(network_service_config_t));
    if (!network_config) {
        LOG_SVC_ERROR("Failed to allocate memory for network config");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Get network service config from NVS
    aicam_result_t result = json_config_get_network_service_config(network_config);
    if (result != AICAM_OK) {
        LOG_SVC_WARN("Failed to get network service config from NVS: %d", result);
        buffer_free(network_config);
        return result;
    }
    
    // Copy known networks to communication service
    g_communication_service.known_network_count = network_config->known_network_count;
    if (g_communication_service.known_network_count > MAX_KNOWN_NETWORKS) {
        g_communication_service.known_network_count = MAX_KNOWN_NETWORKS;
    }
    
    for (uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        memcpy(&g_communication_service.known_networks[i], 
               &network_config->known_networks[i], 
               sizeof(network_scan_result_t));
        LOG_SVC_DEBUG("Loaded known network: %s (%s)", 
                     g_communication_service.known_networks[i].ssid,
                     g_communication_service.known_networks[i].bssid);
    }
    
    buffer_free(network_config);
    
    LOG_SVC_INFO("Loaded %u known networks from NVS", g_communication_service.known_network_count);
    
    return AICAM_OK;
}

/**
 * @brief Try to connect to known networks (optimized for low power mode fast startup)
 * @note In low power mode, prioritizes last connected network for fastest connection
 */
static aicam_result_t try_connect_known_networks(void)
{
    if (g_communication_service.known_network_count == 0) {
        LOG_SVC_INFO("No known networks to connect");
        return AICAM_ERROR_NOT_FOUND;
    }
    
    // Check if woken by RTC (timing or alarm) - only enable fast connection for RTC wakeup
    // This indicates low power mode with scheduled RTC wakeup, where fast connection is critical
    uint32_t wakeup_flag = u0_module_get_wakeup_flag_ex();
    aicam_bool_t is_rtc_wakeup = (wakeup_flag & (PWR_WAKEUP_FLAG_RTC_TIMING | 
                                                  PWR_WAKEUP_FLAG_RTC_ALARM_A | 
                                                  PWR_WAKEUP_FLAG_RTC_ALARM_B)) != 0;
    
    LOG_SVC_INFO("Trying to connect to known networks (RTC wakeup: %s)...", 
                 is_rtc_wakeup ? "YES" : "NO");
    
    // Create a sorted index array
    uint32_t sorted_indices[MAX_KNOWN_NETWORKS];
    for (uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        sorted_indices[i] = i;
    }
    
    // Sort by priority: in low power mode, prioritize last_connected_time; otherwise use hybrid strategy
    for (uint32_t i = 0; i < g_communication_service.known_network_count - 1; i++) {
        for (uint32_t j = 0; j < g_communication_service.known_network_count - i - 1; j++) {
            network_scan_result_t *net1 = &g_communication_service.known_networks[sorted_indices[j]];
            network_scan_result_t *net2 = &g_communication_service.known_networks[sorted_indices[j + 1]];
            
            aicam_bool_t should_swap = AICAM_FALSE;
            
            if (is_rtc_wakeup) {
                // RTC wakeup (low power mode): prioritize last connected network (most recent first)
                // If last_connected_time is same, prefer higher RSSI
                if (net1->last_connected_time < net2->last_connected_time) {
                    should_swap = AICAM_TRUE;
                } else if (net1->last_connected_time == net2->last_connected_time && 
                          net1->rssi < net2->rssi) {
                    should_swap = AICAM_TRUE;
                }
            } else {
                // Normal mode: hybrid strategy - prioritize recent connections with good RSSI
                // Score = (last_connected_time > 0 ? 1000 : 0) + RSSI
                int32_t score1 = (net1->last_connected_time > 0 ? 1000 : 0) + net1->rssi;
                int32_t score2 = (net2->last_connected_time > 0 ? 1000 : 0) + net2->rssi;
                if (score1 < score2) {
                    should_swap = AICAM_TRUE;
                }
            }
            
            if (should_swap) {
                uint32_t temp = sorted_indices[j];
                sorted_indices[j] = sorted_indices[j + 1];
                sorted_indices[j + 1] = temp;
            }
        }
    }

    // Update scan results from known networks
    for(uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        network_scan_result_t *known = &g_communication_service.known_networks[i];
        for(uint32_t j = 0; j < g_communication_service.scan_result_count; j++) {
            if (strcmp(g_communication_service.scan_results[j].ssid, known->ssid) == 0) {
                g_communication_service.scan_results[j].is_known = AICAM_TRUE;
            }
        }
    }
    
    // In RTC wakeup mode, try last connected network first without waiting for scan
    if (is_rtc_wakeup && g_communication_service.known_network_count > 0) {
        uint32_t last_connected_idx = sorted_indices[0];
        network_scan_result_t *last_connected = &g_communication_service.known_networks[last_connected_idx];
        
        if (last_connected->last_connected_time > 0) {
            LOG_SVC_INFO("RTC wakeup: trying last connected network first: %s (%s)", 
                        last_connected->ssid, last_connected->bssid);
            
            // Try direct connection without scan verification in RTC wakeup mode
            unsigned int bssid_bytes[6];
            if (sscanf(last_connected->bssid, "%02X:%02X:%02X:%02X:%02X:%02X",
                       &bssid_bytes[0], &bssid_bytes[1], &bssid_bytes[2],
                       &bssid_bytes[3], &bssid_bytes[4], &bssid_bytes[5]) == 6) {
                
                // Configure STA interface
                netif_config_t sta_config = {0};
                nm_get_netif_cfg(NETIF_NAME_WIFI_STA, &sta_config);
                
                strncpy(sta_config.wireless_cfg.ssid, last_connected->ssid, sizeof(sta_config.wireless_cfg.ssid) - 1);
                strncpy(sta_config.wireless_cfg.pw, last_connected->password, sizeof(sta_config.wireless_cfg.pw) - 1);
                for (int i = 0; i < 6; i++) {
                    sta_config.wireless_cfg.bssid[i] = (uint8_t)(bssid_bytes[i] & 0xFF);
                }
                sta_config.wireless_cfg.channel = last_connected->channel;
                sta_config.wireless_cfg.security = last_connected->security;
                sta_config.ip_mode = NETIF_IP_MODE_DHCP;
                
                aicam_result_t result = communication_configure_interface(NETIF_NAME_WIFI_STA, &sta_config);
                
                if (result == AICAM_OK) {
                    // In RTC wakeup mode, use shorter timeout for connection check
                    uint32_t timeout_ms = 3000; // 3 seconds for fast connection
                    uint32_t start_time = rtc_get_uptime_ms();
                    
                    while ((rtc_get_uptime_ms() - start_time) < timeout_ms) {
                        if (communication_is_interface_connected(NETIF_NAME_WIFI_STA)) {
                            LOG_SVC_INFO("RTC wakeup: successfully connected to last network: %s (%s)", 
                                        last_connected->ssid, last_connected->bssid);
                            return AICAM_OK;
                        }
                        osDelay(100);
                    }
                    
                    LOG_SVC_WARN("RTC wakeup: connection timeout for last network: %s (%s)", 
                                last_connected->ssid, last_connected->bssid);
                    communication_stop_interface(NETIF_NAME_WIFI_STA);
                }
            }
        }
    }
    
    // Try to connect to each known network in order (fallback or normal mode)
    for (uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        uint32_t idx = sorted_indices[i];
        aicam_bool_t found = AICAM_FALSE;
        network_scan_result_t *known = &g_communication_service.known_networks[idx];
        
        LOG_SVC_INFO("Trying to connect to: %s (%s), RSSI: %d dBm, Last connected: %u", 
                    known->ssid, known->bssid, known->rssi, known->last_connected_time);
        
        // In RTC wakeup mode, skip scan verification for faster connection
        if (is_rtc_wakeup) {
            found = AICAM_TRUE; // Use cached network info directly
        } else {
            // Find SSID in scan results (full speed mode)
            for(uint32_t j = 0; j < g_communication_service.scan_result_count; j++) {
                LOG_SVC_DEBUG("Scan result: %s (%s)", g_communication_service.scan_results[j].ssid, g_communication_service.scan_results[j].bssid);
                if (strcmp(g_communication_service.scan_results[j].ssid, known->ssid) == 0) {
                    memcpy(known->bssid, g_communication_service.scan_results[j].bssid, sizeof(known->bssid));
                    found = AICAM_TRUE;
                    break;
                }
            }
        }
        
        if(!found && !is_rtc_wakeup) {
            LOG_SVC_INFO("Network not found in scan results: %s (%s)", known->ssid, known->bssid);
            continue;
        }

        unsigned int bssid_bytes[6];
        if (sscanf(known->bssid, "%02X:%02X:%02X:%02X:%02X:%02X",
                   &bssid_bytes[0], &bssid_bytes[1], &bssid_bytes[2],
                   &bssid_bytes[3], &bssid_bytes[4], &bssid_bytes[5]) != 6) {
            LOG_SVC_ERROR("Invalid BSSID: %s", known->bssid);
            continue;
        }

        // Configure STA interface
        netif_config_t sta_config = {0};
        nm_get_netif_cfg(NETIF_NAME_WIFI_STA, &sta_config);
        
        strncpy(sta_config.wireless_cfg.ssid, known->ssid, sizeof(sta_config.wireless_cfg.ssid) - 1);
        strncpy(sta_config.wireless_cfg.pw, known->password, sizeof(sta_config.wireless_cfg.pw) - 1);
        for (int i = 0; i < 6; i++) {
            sta_config.wireless_cfg.bssid[i] = (uint8_t)(bssid_bytes[i] & 0xFF);
        }
        sta_config.wireless_cfg.channel = known->channel;
        sta_config.wireless_cfg.security = known->security;
        sta_config.ip_mode = NETIF_IP_MODE_DHCP;
        
        aicam_result_t result = communication_configure_interface(NETIF_NAME_WIFI_STA, &sta_config);
        
        if (result == AICAM_OK) {
            uint64_t timeout_ms = 3000;
            uint64_t start_time = rtc_get_uptime_ms();
            
            while ((rtc_get_uptime_ms() - start_time) < timeout_ms) {
                if (communication_is_interface_connected(NETIF_NAME_WIFI_STA)) {
                    LOG_SVC_INFO("Successfully connected to: %s (%s)", known->ssid, known->bssid);
                    return AICAM_OK;
                }
                osDelay(100);
            }
            
            LOG_SVC_WARN("Connection timeout for: %s (%s)", known->ssid, known->bssid);
            communication_stop_interface(NETIF_NAME_WIFI_STA);
        } else {
            LOG_SVC_ERROR("Failed to configure interface for: %s (%s), error: %d", 
                         known->ssid, known->bssid, result);
        }
    }
    
    LOG_SVC_INFO("Failed to connect to any known network");
    return AICAM_ERROR_NOT_FOUND;
}

static void background_scan_task(void *argument)
{
    for(;;) {
        osSemaphoreAcquire(g_communication_service.scan_semaphore_id, osWaitForever);
        aicam_result_t result = communication_start_network_scan(NULL);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to start network scan: %d", result);
        }
        osDelay(1000);
    }
    osThreadExit();
}

/**
 * @brief Start network scan
 */
void start_network_scan(void)
{
    osSemaphoreRelease(g_communication_service.scan_semaphore_id);
}

/* ==================== Helper Functions ==================== */

/**
 * @brief Convert netif_info_t to network_interface_status_t
 */
static void convert_netif_info_to_status(const netif_info_t *netif_info, 
                                       network_interface_status_t *status)
{
    if (!netif_info || !status) return;
    
    memset(status, 0, sizeof(network_interface_status_t));
    
    status->state = netif_info->state;
    status->type = netif_info->type;
    
    if (netif_info->if_name) {
        strncpy(status->if_name, netif_info->if_name, sizeof(status->if_name) - 1);
    }
    
    if (netif_info->type == NETIF_TYPE_WIRELESS) {
        strncpy(status->ssid, netif_info->wireless_cfg.ssid, sizeof(status->ssid) - 1);
        status->rssi = netif_info->rssi;
        status->channel = netif_info->wireless_cfg.channel;
    }
    
    // Convert IP address
    snprintf(status->ip_addr, sizeof(status->ip_addr), "%d.%d.%d.%d",
             netif_info->ip_addr[0], netif_info->ip_addr[1], 
             netif_info->ip_addr[2], netif_info->ip_addr[3]);
    
    // Convert MAC address
    snprintf(status->mac_addr, sizeof(status->mac_addr), "%02X:%02X:%02X:%02X:%02X:%02X",
             netif_info->if_mac[0], netif_info->if_mac[1], netif_info->if_mac[2],
             netif_info->if_mac[3], netif_info->if_mac[4], netif_info->if_mac[5]);
    
    status->connected = (netif_info->state == NETIF_STATE_UP);
}

/**
 * @brief Update network interface list
 */
static aicam_result_t update_interface_list(void)
{
    netif_info_t *netif_list = NULL;
    int netif_count = 0;
    aicam_result_t result = AICAM_OK;
    
    // Get network interface list from netif_manager
    netif_count = nm_get_netif_list(&netif_list);
    if (netif_count < 0) {
        LOG_SVC_ERROR("Failed to get network interface list");
        return AICAM_ERROR;
    }
    
    // Clear existing interface list
    g_communication_service.interface_count = 0;
    
    // Convert and store interface information
    for (int i = 0; i < netif_count && i < MAX_NETWORK_INTERFACES; i++) {
        convert_netif_info_to_status(&netif_list[i], 
                                   &g_communication_service.interfaces[i]);
        g_communication_service.interface_count++;
    }
    
    // Free the netif_list
    nm_free_netif_list(netif_list);
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Updated interface list: %d interfaces", g_communication_service.interface_count);
    }
    
    return result;
}

/**
 * @brief Check if a network is known (previously connected)
 */
static int is_known_network(const char *ssid, const char *bssid)
{
    if (!ssid || !bssid) return -1;

    //LOG_SVC_DEBUG("Checking if known network: %s (%s)", ssid, bssid);
    
    for (uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        network_scan_result_t *known = &g_communication_service.known_networks[i];
        LOG_SVC_DEBUG("Known network: %s (%s)", known->ssid, known->bssid);
        if (strcmp(known->ssid, ssid) == 0 && strcmp(known->bssid, bssid) == 0) {
            LOG_SVC_DEBUG("Known network found: %s (%s)", ssid, bssid);
            //g_communication_service.known_networks[i].connected = AICAM_TRUE;
            return i;
        }
    }
    return -1;
}

/**
 * @brief Add connected network to known networks database
 */
static void add_known_network(const network_scan_result_t *network)
{
    if (!network || g_communication_service.known_network_count >= MAX_KNOWN_NETWORKS) return;
    
    // Check if already exists
    int known_index = is_known_network(network->ssid, network->bssid);
    if (known_index >= 0) {
        g_communication_service.known_networks[known_index].connected = network->connected;
        g_communication_service.known_networks[known_index].rssi = network->rssi;
        g_communication_service.known_networks[known_index].channel = network->channel;
        g_communication_service.known_networks[known_index].security = network->security;
        strncpy(g_communication_service.known_networks[known_index].password, network->password, sizeof(g_communication_service.known_networks[known_index].password) - 1);
    }
    else {
        LOG_SVC_DEBUG("Add known network: %s (%s)", network->ssid, network->bssid);
        if (g_communication_service.known_network_count  == MAX_KNOWN_NETWORKS) {
            for(uint32_t i = 0; i < MAX_KNOWN_NETWORKS - 1; i++) {
                memcpy(&g_communication_service.known_networks[i], &g_communication_service.known_networks[i + 1], sizeof(network_scan_result_t));
            }
            memcpy(&g_communication_service.known_networks[MAX_KNOWN_NETWORKS - 1], network, sizeof(network_scan_result_t));
            g_communication_service.known_network_count--;
        }

        // Add to known networks, deep copy
        network_scan_result_t *known = &g_communication_service.known_networks[g_communication_service.known_network_count];
        strncpy(known->ssid, network->ssid, sizeof(known->ssid) - 1);
        strncpy(known->bssid, network->bssid, sizeof(known->bssid) - 1);
        known->rssi = network->rssi;
        known->channel = network->channel;
        known->security = (wireless_security_t)network->security;
        known->connected = network->connected;
        known->is_known = AICAM_TRUE;
        known->last_connected_time = rtc_get_timeStamp();
        strncpy(known->password, network->password, sizeof(known->password) - 1);
        LOG_SVC_DEBUG("Add known network: %s (%s), password: %s", network->ssid, network->bssid, known->password);
        g_communication_service.known_network_count++;
    }    
    

    //update scan result
    for(uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        if (strcmp(g_communication_service.scan_results[i].ssid, network->ssid) == 0 && strcmp(g_communication_service.scan_results[i].bssid, network->bssid) == 0) {
            g_communication_service.scan_results[i].is_known = AICAM_TRUE;
            g_communication_service.scan_results[i].connected = AICAM_TRUE;
            g_communication_service.scan_results[i].security = network->security;
            g_communication_service.scan_results[i].channel = network->channel;
            g_communication_service.scan_results[i].rssi = network->rssi;
            if(strlen(network->password) > 0) {
                strncpy(g_communication_service.scan_results[i].password, network->password, sizeof(g_communication_service.scan_results[i].password) - 1);
            }
            else {
                strncpy(g_communication_service.scan_results[i].password, "", sizeof(g_communication_service.scan_results[i].password) - 1);
            }
            g_communication_service.scan_results[i].last_connected_time = rtc_get_timeStamp();
            break;
        }
        else {
            if(g_communication_service.scan_results[i].connected == AICAM_TRUE) {
                //set connected to false
                g_communication_service.scan_results[i].connected = AICAM_FALSE;
            }
        }
    }
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Added known network: %s (%s)", network->ssid, network->bssid);
    }
    
    // Save to NVS
    save_known_networks_to_nvs();
}

/**
 * @brief Delete network from known networks database
 */
static void delete_known_network(const char *ssid, const char *bssid)
{
    if (!ssid || !bssid) return;
    
    LOG_SVC_INFO("Deleting known network: %s (%s)", ssid, bssid);

    // Check if currently connected to this network
    aicam_bool_t was_connected = is_connected(ssid, bssid);
    
    // Disconnect from network if currently connected
    if (was_connected) {
        LOG_SVC_INFO("Disconnecting from network: %s (%s)", ssid, bssid);
        aicam_result_t disconnect_result = communication_stop_interface(NETIF_NAME_WIFI_STA);
        if (disconnect_result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to disconnect from network: %d", disconnect_result);
        } else {
            LOG_SVC_INFO("Successfully disconnected from network: %s (%s)", ssid, bssid);
        }
    }

    // Update scan results to mark as unknown
    for(uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        if (strcmp(g_communication_service.scan_results[i].ssid, ssid) == 0 && 
            strcmp(g_communication_service.scan_results[i].bssid, bssid) == 0) {
            g_communication_service.scan_results[i].is_known = AICAM_FALSE;
            g_communication_service.scan_results[i].connected = AICAM_FALSE;
            g_communication_service.scan_results[i].last_connected_time = 0;
            LOG_SVC_DEBUG("Updated scan result for network: %s (%s)", ssid, bssid);
            break;
        }
    }

    // Remove from known networks array
    int found_index = -1;
    for (uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        if (strcmp(g_communication_service.known_networks[i].ssid, ssid) == 0 && 
            strcmp(g_communication_service.known_networks[i].bssid, bssid) == 0) {
            found_index = i;
            break;
        }
    }
    
    if (found_index >= 0) {
        // Shift remaining networks to fill the gap
        for (uint32_t i = found_index; i < g_communication_service.known_network_count - 1; i++) {
            memcpy(&g_communication_service.known_networks[i], 
                   &g_communication_service.known_networks[i + 1], 
                   sizeof(network_scan_result_t));
        }
        
        // Clear the last entry and decrement count
        memset(&g_communication_service.known_networks[g_communication_service.known_network_count - 1], 
               0, sizeof(network_scan_result_t));
        g_communication_service.known_network_count--;
        
        LOG_SVC_INFO("Removed network from known networks list: %s (%s)", ssid, bssid);
    } else {
        LOG_SVC_WARN("Network not found in known networks list: %s (%s)", ssid, bssid);
    }

    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Deleted known network: %s (%s), was_connected: %s, known_count: %u", 
                     ssid, bssid, was_connected ? "true" : "false", 
                     g_communication_service.known_network_count);
    }
    
    // Save to NVS
    save_known_networks_to_nvs();
}

/**
 * @brief Check if a network is connected
 */
static aicam_bool_t is_connected(const char *ssid, const char *bssid)
{
    //get netif state
    netif_state_t state = nm_get_netif_state(NETIF_NAME_WIFI_STA);
    if (state == NETIF_STATE_DOWN) {
        return AICAM_FALSE;
    }

    //get netif info
    netif_info_t netif_info;
    nm_get_netif_info(NETIF_NAME_WIFI_STA, &netif_info);
    char bssid_str[18];
    snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             netif_info.wireless_cfg.bssid[0], netif_info.wireless_cfg.bssid[1],
             netif_info.wireless_cfg.bssid[2], netif_info.wireless_cfg.bssid[3],
             netif_info.wireless_cfg.bssid[4], netif_info.wireless_cfg.bssid[5]);
    LOG_SVC_DEBUG("Netif info: %s (%s)", netif_info.wireless_cfg.ssid, bssid_str);
    LOG_SVC_DEBUG("Checking if connected: %s (%s)", ssid, bssid);
    if (strcmp(netif_info.wireless_cfg.ssid, ssid) == 0 && strcmp(bssid_str, bssid) == 0) {
        return AICAM_TRUE;
    }
    return AICAM_FALSE;
}


/**
 * @brief Network scan callback
 */
static void network_scan_callback(int result, wireless_scan_result_t *scan_result)
{
    if (result != 0 || !scan_result) {
        LOG_SVC_ERROR("Network scan failed: %d", result);
        g_communication_service.scan_in_progress = AICAM_FALSE;
        return;
    }
    
    // Store scan results with known/unknown classification
    g_communication_service.scan_result_count = 0;
    for (int i = 0; i < scan_result->scan_count && i < MAX_SCAN_RESULTS; i++) {
        network_scan_result_t *result = &g_communication_service.scan_results[i];
        
        strncpy(result->ssid, scan_result->scan_info[i].ssid, sizeof(result->ssid) - 1);
        snprintf(result->bssid, sizeof(result->bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                 scan_result->scan_info[i].bssid[0], scan_result->scan_info[i].bssid[1],
                 scan_result->scan_info[i].bssid[2], scan_result->scan_info[i].bssid[3],
                 scan_result->scan_info[i].bssid[4], scan_result->scan_info[i].bssid[5]);
        
        result->rssi = scan_result->scan_info[i].rssi;
        result->channel = scan_result->scan_info[i].channel;
        result->security = (wireless_security_t)scan_result->scan_info[i].security;
        result->connected = is_connected(result->ssid, result->bssid);
        
        // Classify as known or unknown network
        result->is_known = is_known_network(result->ssid, result->bssid) == -1 ? AICAM_FALSE : AICAM_TRUE;
        result->last_connected_time = 0; // Will be updated when connected
        
        g_communication_service.scan_result_count++;
    }
    
    // Update known networks RSSI from scan results
    for (uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        network_scan_result_t *scan = &g_communication_service.scan_results[i];
        for (uint32_t j = 0; j < g_communication_service.known_network_count; j++) {
            network_scan_result_t *known = &g_communication_service.known_networks[j];
            if (strcmp(scan->ssid, known->ssid) == 0 && strcmp(scan->bssid, known->bssid) == 0) {
                // Update RSSI and channel info
                known->rssi = scan->rssi;
                known->channel = scan->channel;
                if (g_communication_service.config.enable_debug) {
                    LOG_SVC_DEBUG("Updated known network RSSI: %s (%s) -> %d dBm", 
                                 known->ssid, known->bssid, known->rssi);
                }
                break;
            }
        }
    }
    
    g_communication_service.scan_in_progress = AICAM_FALSE;
    g_communication_service.stats.network_scans++;
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Network scan completed: %d networks found", g_communication_service.scan_result_count);
    }
}



/* ==================== Communication Service Implementation ==================== */

aicam_result_t communication_service_init(void *config)
{
    if (g_communication_service.initialized) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_SVC_INFO("Initializing Communication Service...");
    
    // Initialize context
    memset(&g_communication_service, 0, sizeof(communication_service_context_t));
    
    // Set default configuration
    g_communication_service.config = default_config;
    
    // Apply custom configuration if provided
    if (config) {
        communication_service_config_t *custom_config = (communication_service_config_t *)config;
        g_communication_service.config = *custom_config;
    }
    
    // Register network interface manager (framework only, fast < 2s)
    netif_manager_register();
    g_communication_service.netif_manager_registered = AICAM_TRUE;
    
    // Initialize network interface initialization manager
    aicam_result_t result = netif_init_manager_framework_init();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to initialize netif init manager: %d", result);
        return result;
    }

    // Get wakeup flag directly from U0 module (doesn't require system_service to be initialized)
    uint32_t wakeup_flag = 0;
    int ret = u0_module_get_wakeup_flag(&wakeup_flag);
    if (ret != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get wakeup flag: %d", ret);
        return ret;
    }
    
    // Check if woken by RTC (timing or alarm) - this indicates low power mode with RTC wakeup
    aicam_bool_t is_rtc_wakeup = (wakeup_flag & (PWR_WAKEUP_FLAG_RTC_TIMING | 
                                                  PWR_WAKEUP_FLAG_RTC_ALARM_A | 
                                                  PWR_WAKEUP_FLAG_RTC_ALARM_B)) != 0;
    
    // Check if woken by button
    aicam_bool_t is_button_wakeup = (wakeup_flag & PWR_WAKEUP_FLAG_CONFIG_KEY) != 0;
    
    // If woken by RTC (low power mode RTC wakeup) and not by button, disable AP for faster startup
    if (is_rtc_wakeup && !is_button_wakeup) {
        g_communication_service.config.auto_start_wifi_ap = AICAM_FALSE;
        LOG_SVC_INFO("RTC wakeup detected, disabling AP for faster startup");
    }
    
    // Register WiFi AP initialization configuration
    netif_init_config_t ap_config = {
        .if_name = NETIF_NAME_WIFI_AP,
        .state = NETIF_INIT_STATE_IDLE,
        .priority = NETIF_INIT_PRIORITY_HIGH,      // High priority
        .auto_up = AICAM_TRUE,                     // Auto bring up after init
        .async = AICAM_TRUE,                       // Asynchronous initialization
        .callback = on_wifi_ap_ready
    };
    result = netif_init_manager_register(&ap_config);
    if (result != AICAM_OK) {
        LOG_SVC_WARN("Failed to register WiFi AP init config: %d", result);
    }
    
    // Register WiFi STA initialization configuration
    netif_init_config_t sta_config = {
        .if_name = NETIF_NAME_WIFI_STA,
        .state = NETIF_INIT_STATE_IDLE,
        .priority = NETIF_INIT_PRIORITY_NORMAL,    // Normal priority
        .auto_up = AICAM_FALSE,                    // Manual bring up (after connect)
        .async = AICAM_TRUE,                       // Asynchronous initialization
        .callback = on_wifi_sta_ready
    };
    result = netif_init_manager_register(&sta_config);
    if (result != AICAM_OK) {
        LOG_SVC_WARN("Failed to register WiFi STA init config: %d", result);
    }
    
    // Initialize statistics
    memset(&g_communication_service.stats, 0, sizeof(communication_service_stats_t));
    
    g_communication_service.initialized = AICAM_TRUE;
    g_communication_service.state = SERVICE_STATE_INITIALIZED;

    LOG_SVC_INFO("Communication Service initialized");
    
    return AICAM_OK;
}

aicam_result_t communication_service_start(void)
{
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_communication_service.running) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_SVC_INFO("Starting Communication Service...");
    
    // Update interface list
    // update_interface_list();

    // Load known networks from NVS
    aicam_result_t load_result = load_known_networks_from_nvs();
    if (load_result == AICAM_OK) {
        LOG_SVC_INFO("Loaded known networks from NVS successfully");
    } else {
        LOG_SVC_WARN("Failed to load known networks from NVS: %d", load_result);
    }
    
    g_communication_service.running = AICAM_TRUE;
    g_communication_service.state = SERVICE_STATE_RUNNING;


    //start background scan task
    const osThreadAttr_t scan_task_attr = {
        .name = "BackgroundScanTask",
        .stack_size = sizeof(background_scan_task_stack),
        .stack_mem = background_scan_task_stack,
        .priority = osPriorityBelowNormal,
    };
    g_communication_service.scan_semaphore_id = osSemaphoreNew(1, 0, NULL);
    g_communication_service.scan_task_id = osThreadNew(background_scan_task, NULL, &scan_task_attr);
    
    g_communication_service.running = AICAM_TRUE;
    g_communication_service.state = SERVICE_STATE_RUNNING;
    
    // Start asynchronous network interface initialization (non-blocking)
    if (g_communication_service.config.auto_start_wifi_ap) {
        LOG_SVC_INFO("Starting async WiFi AP initialization...");
        aicam_result_t ap_result = netif_init_manager_init_async(NETIF_NAME_WIFI_AP);
        if (ap_result != AICAM_OK) {
            LOG_SVC_WARN("Failed to start WiFi AP initialization: %d", ap_result);
        }
    }
    
    if (g_communication_service.config.auto_start_wifi_sta) {
        LOG_SVC_INFO("Starting async WiFi STA initialization...");
        aicam_result_t sta_result = netif_init_manager_init_async(NETIF_NAME_WIFI_STA);
        if (sta_result != AICAM_OK) {
            LOG_SVC_WARN("Failed to start WiFi STA initialization: %d", sta_result);
        }
        // Note: try_connect_known_networks() will be called in on_wifi_sta_ready() callback
    }
    
    LOG_SVC_INFO("Communication Service started (network interfaces initializing in background)");
    
    return AICAM_OK;
}

aicam_result_t communication_service_stop(void)
{
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_communication_service.running) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    LOG_SVC_INFO("Stopping Communication Service...");
    
    // Stop all network interfaces
    for (uint32_t i = 0; i < g_communication_service.interface_count; i++) {
        if (g_communication_service.interfaces[i].connected) {
            communication_stop_interface(g_communication_service.interfaces[i].if_name);
        }
    }
    
    g_communication_service.running = AICAM_FALSE;
    g_communication_service.state = SERVICE_STATE_INITIALIZED;
    
    LOG_SVC_INFO("Communication Service stopped successfully");
    
    return AICAM_OK;
}

aicam_result_t communication_service_deinit(void)
{
    if (!g_communication_service.initialized) {
        return AICAM_OK;
    }
    
    // Stop if running
    if (g_communication_service.running) {
        communication_service_stop();
    }
    
    LOG_SVC_INFO("Deinitializing Communication Service...");
    
    // Unregister network interface manager
    if (g_communication_service.netif_manager_registered) {
        netif_manager_unregister();
        g_communication_service.netif_manager_registered = AICAM_FALSE;
    }
    
    // Reset context
    memset(&g_communication_service, 0, sizeof(communication_service_context_t));
    
    LOG_SVC_INFO("Communication Service deinitialized successfully");
    
    return AICAM_OK;
}

service_state_t communication_service_get_state(void)
{
    return g_communication_service.state;
}

/* ==================== Network Interface Management ==================== */

aicam_result_t communication_get_network_interfaces(network_interface_status_t *interfaces, 
                                                   uint32_t max_count, 
                                                   uint32_t *actual_count)
{
    if (!interfaces || !actual_count) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Update interface list
    update_interface_list();
    
    uint32_t copy_count = (g_communication_service.interface_count < max_count) ? 
                         g_communication_service.interface_count : max_count;
    
    memcpy(interfaces, g_communication_service.interfaces, 
           copy_count * sizeof(network_interface_status_t));
    
    *actual_count = copy_count;
    
    return AICAM_OK;
}

aicam_result_t communication_get_interface_status(const char *if_name, 
                                                 network_interface_status_t *status)
{
    if (!if_name || !status) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    netif_info_t netif_info;
    aicam_result_t result = nm_get_netif_info(if_name, &netif_info);
    if (result != AICAM_OK) {
        return result;
    }
    
    convert_netif_info_to_status(&netif_info, status);
    
    return AICAM_OK;
}

aicam_bool_t communication_is_interface_connected(const char *if_name)
{
    if (!if_name) {
        return AICAM_FALSE;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_FALSE;
    }

    // get interface status
    network_interface_status_t status;
    aicam_result_t result = communication_get_interface_status(if_name, &status);
    if (result != AICAM_OK) {
        return AICAM_FALSE;
    }

    return status.connected;
}

    
aicam_result_t communication_get_interface_config(const char *if_name, 
                                                 netif_config_t *config)
{
    if (!if_name || !config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    // Get interface configuration using netif_manager
    aicam_result_t result = nm_get_netif_cfg(if_name, config);
    
    if (result != AICAM_OK) {
        g_communication_service.stats.last_error_code = result;
        LOG_SVC_ERROR("Failed to get interface %s configuration: %d", if_name, result);
    } else if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Interface %s configuration retrieved successfully", if_name);
    }
    
    return result;
}

aicam_result_t communication_configure_interface(const char *if_name, 
                                                 netif_config_t *config)
{
    if (!if_name || !config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    // Use netif_manager standard configuration flow
    // known network password should be filled
    char bssid[18] = {0};
    snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
             config->wireless_cfg.bssid[0], config->wireless_cfg.bssid[1],
             config->wireless_cfg.bssid[2], config->wireless_cfg.bssid[3],
             config->wireless_cfg.bssid[4], config->wireless_cfg.bssid[5]);
    int known_index = is_known_network(config->wireless_cfg.ssid, bssid);
    if(known_index >= 0) {
        strncpy(config->wireless_cfg.pw, g_communication_service.known_networks[known_index].password, sizeof(config->wireless_cfg.pw) - 1);
        config->wireless_cfg.security = g_communication_service.known_networks[known_index].security;
    }
    aicam_result_t result = nm_set_netif_cfg(if_name, config);

    // get interface status
    network_interface_status_t status;
    result = communication_get_interface_status(if_name, &status);
    if (result != AICAM_OK) {
        return result;
    }

    // if interface is not connected, start it
    if (status.connected == AICAM_FALSE) {
        result = communication_start_interface(if_name);
        if (result != AICAM_OK) {
            return result;
        }
    }
    
    // Update statistics
    if (result == AICAM_OK) {
        g_communication_service.stats.total_connections++;
        if (g_communication_service.config.enable_debug) {
            LOG_SVC_DEBUG("Interface %s configured successfully", if_name);
        }
    } else {
        g_communication_service.stats.failed_connections++;
        g_communication_service.stats.last_error_code = result;
        LOG_SVC_ERROR("Failed to configure interface %s: %d", if_name, result);
    }

    // if sta is connected, update known networks
    if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0) {
        //get netif info
        netif_info_t netif_info;
        nm_get_netif_info(if_name, &netif_info);

        //generate scan result
        network_scan_result_t scan_result;
        strncpy(scan_result.ssid, netif_info.wireless_cfg.ssid, sizeof(scan_result.ssid) - 1);
        snprintf(scan_result.bssid, sizeof(scan_result.bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                 netif_info.wireless_cfg.bssid[0], netif_info.wireless_cfg.bssid[1],
                 netif_info.wireless_cfg.bssid[2], netif_info.wireless_cfg.bssid[3],
                 netif_info.wireless_cfg.bssid[4], netif_info.wireless_cfg.bssid[5]);
        scan_result.rssi = netif_info.rssi;
        scan_result.channel = netif_info.wireless_cfg.channel;
        scan_result.security = netif_info.wireless_cfg.security;
        scan_result.connected = AICAM_TRUE;
        scan_result.is_known = AICAM_TRUE;
        scan_result.last_connected_time = rtc_get_timeStamp();   
        if(strlen(netif_info.wireless_cfg.pw) > 0) {
            strncpy(scan_result.password, netif_info.wireless_cfg.pw, sizeof(scan_result.password) - 1);
            LOG_SVC_DEBUG("Interface %s connected, add known network: %s, password: %s", if_name, scan_result.ssid, scan_result.password);
        }
        else {
            strncpy(scan_result.password, "", sizeof(scan_result.password) - 1);
        }

        LOG_SVC_DEBUG("Interface %s connected, add known network: %s", if_name, scan_result.ssid);
        add_known_network(&scan_result);

        aicam_result_t sta_ready_result = service_set_sta_ready(AICAM_TRUE);
        if (sta_ready_result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to set STA ready flag: %d", sta_ready_result);
        }
    }
    
    return result;
}

aicam_result_t communication_start_interface(const char *if_name)
{
    if (!if_name) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // start interface
    aicam_result_t result = nm_ctrl_netif_up(if_name);
    if (result != AICAM_OK) {
        return result;
    }
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Interface %s start requested", if_name);
    }
    
    return AICAM_OK;
}

aicam_result_t communication_stop_interface(const char *if_name)
{
    if (!if_name) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // stop interface
    aicam_result_t result = nm_ctrl_netif_down(if_name);
    if (result != AICAM_OK) {
        return result;
    }
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Interface %s stop requested", if_name);
    }
    
    return AICAM_OK;
}

aicam_result_t communication_restart_interface(const char *if_name)
{
    if (!if_name) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Communication service only collects information, does not control interface restart
    LOG_SVC_INFO("Interface %s restart requested (information only)", if_name);
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Interface %s restart request logged", if_name);
    }
    
    return AICAM_OK;
}

aicam_result_t communication_disconnect_network(const char *if_name)
{
    if (!if_name) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    // disconnect network
    aicam_result_t result = communication_stop_interface(if_name);
    if (result != AICAM_OK) {
        return result;
    }

    // get ssid and bssid
    netif_config_t config;
    result = communication_get_interface_config(if_name, &config);
    if (result != AICAM_OK) {
        return result;
    }
    const char* ssid = config.wireless_cfg.ssid;
    char bssid[18];
    snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
             config.wireless_cfg.bssid[0], config.wireless_cfg.bssid[1],
             config.wireless_cfg.bssid[2], config.wireless_cfg.bssid[3],
             config.wireless_cfg.bssid[4], config.wireless_cfg.bssid[5]);

    LOG_SVC_INFO("Disconnecting network: %s (%s)", ssid, bssid);


    // update scan results and known networks
    for(uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        if(strcmp(g_communication_service.scan_results[i].ssid, ssid) == 0 &&
           strcmp(g_communication_service.scan_results[i].bssid, bssid) == 0) {
            g_communication_service.scan_results[i].connected = AICAM_FALSE;
        }
    }

    for(uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        if(strcmp(g_communication_service.known_networks[i].ssid, ssid) == 0 &&
           strcmp(g_communication_service.known_networks[i].bssid, bssid) == 0) {
            g_communication_service.known_networks[i].connected = AICAM_FALSE;
        }
    }

    return AICAM_OK;
}


/* ==================== Network Scanning ==================== */

aicam_result_t communication_start_network_scan(wireless_scan_callback_t callback)
{
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_communication_service.scan_in_progress) {
        return AICAM_ERROR_BUSY;
    }
    
    g_communication_service.scan_in_progress = AICAM_TRUE;

    aicam_result_t result = nm_wireless_update_scan_result(3000);
    if (result != AICAM_OK) {
        g_communication_service.scan_in_progress = AICAM_FALSE;
        g_communication_service.stats.last_error_code = result;
        return result;
    }

    network_scan_callback(0, nm_wireless_get_scan_result());
    
    // aicam_result_t result = nm_wireless_start_scan(callback ? callback : network_scan_callback);
    // if (result != AICAM_OK) {
    //     g_communication_service.scan_in_progress = AICAM_FALSE;
    //     g_communication_service.stats.last_error_code = result;
    //     return result;
    // }
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Network scan started");
    }
    
    return AICAM_OK;
}

aicam_result_t communication_get_scan_results(network_scan_result_t *results, 
                                             uint32_t max_count, 
                                             uint32_t *actual_count)
{
    if (!results || !actual_count) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    uint32_t copy_count = (g_communication_service.scan_result_count < max_count) ? 
                         g_communication_service.scan_result_count : max_count;
    
    memcpy(results, g_communication_service.scan_results, 
           copy_count * sizeof(network_scan_result_t));
    
    *actual_count = copy_count;
    
    return AICAM_OK;
}

aicam_result_t communication_get_classified_scan_results(classified_scan_results_t *results)
{
    if (!results) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Clear results
    memset(results, 0, sizeof(classified_scan_results_t));
    
    // Classify scan results
    for (uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        network_scan_result_t *scan_result = &g_communication_service.scan_results[i];
        
        if (scan_result->is_known) {
            // Add to known networks
            if (results->known_count < MAX_KNOWN_NETWORKS) {
                results->known_networks[results->known_count] = *scan_result;
                results->known_count++;
            }
        } else {
            // Add to unknown networks
            if (results->unknown_count < MAX_KNOWN_NETWORKS) {
                results->unknown_networks[results->unknown_count] = *scan_result;
                results->unknown_count++;
            }
        }
    }

    //updtae known networks
    for(uint32_t i = 0; i < results->known_count; i++) {
        results->known_networks[i].connected = is_connected(results->known_networks[i].ssid, results->known_networks[i].bssid);
    }
    
    return AICAM_OK;
}

aicam_result_t communication_get_known_networks(network_scan_result_t *results, 
                                               uint32_t max_count, 
                                               uint32_t *actual_count)
{
    if (!results || !actual_count) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    uint32_t count = 0;
    
    // Get known networks from scan results
    for (uint32_t i = 0; i < g_communication_service.scan_result_count && count < max_count; i++) {
        network_scan_result_t *scan_result = &g_communication_service.scan_results[i];
        
        if (scan_result->is_known) {
            results[count] = *scan_result;
            count++;
        }
    }
    
    *actual_count = count;
    
    return AICAM_OK;
}

aicam_result_t communication_get_unknown_networks(network_scan_result_t *results, 
                                                 uint32_t max_count, 
                                                 uint32_t *actual_count)
{
    if (!results || !actual_count) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    uint32_t count = 0;
    
    // Get unknown networks from scan results
    for (uint32_t i = 0; i < g_communication_service.scan_result_count && count < max_count; i++) {
        network_scan_result_t *scan_result = &g_communication_service.scan_results[i];
        
        if (!scan_result->is_known) {
            results[count] = *scan_result;
            count++;
        }
    }
    
    *actual_count = count;
    
    return AICAM_OK;
}

/* ==================== Service Management ==================== */

aicam_result_t communication_get_config(communication_service_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    *config = g_communication_service.config;
    
    return AICAM_OK;
}

aicam_result_t communication_set_config(const communication_service_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    g_communication_service.config = *config;
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Communication service configuration updated");
    }
    
    return AICAM_OK;
}

aicam_result_t communication_get_stats(communication_service_stats_t *stats)
{
    if (!stats) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    *stats = g_communication_service.stats;
    
    return AICAM_OK;
}

aicam_result_t communication_reset_stats(void)
{
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    memset(&g_communication_service.stats, 0, sizeof(communication_service_stats_t));
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Communication service statistics reset");
    }
    
    return AICAM_OK;
}

aicam_bool_t communication_is_running(void)
{
    return g_communication_service.running;
}

const char* communication_get_version(void)
{
    return COMMUNICATION_SERVICE_VERSION;
}

/* ==================== CLI Commands ==================== */

/**
 * @brief CLI command: comm status
 */
static int comm_status_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    printf("\r\n================== COMMUNICATION SERVICE STATUS ==================\r\n");
    printf("Service State: %s\r\n", 
           (g_communication_service.state == SERVICE_STATE_RUNNING) ? "RUNNING" :
           (g_communication_service.state == SERVICE_STATE_INITIALIZED) ? "INITIALIZED" : "UNINITIALIZED");
    printf("Version: %s\r\n", COMMUNICATION_SERVICE_VERSION);
    printf("Auto-start WiFi AP: %s\r\n", g_communication_service.config.auto_start_wifi_ap ? "YES" : "NO");
    printf("Auto-start WiFi STA: %s\r\n", g_communication_service.config.auto_start_wifi_sta ? "YES" : "NO");
    printf("Network Scan Enabled: %s\r\n", g_communication_service.config.enable_network_scan ? "YES" : "NO");
    printf("Auto-reconnect Enabled: %s\r\n", g_communication_service.config.enable_auto_reconnect ? "YES" : "NO");
    
    printf("\r\nNetwork Interfaces (%lu):\r\n", g_communication_service.interface_count);
    for (uint32_t i = 0; i < g_communication_service.interface_count; i++) {
        network_interface_status_t *iface = &g_communication_service.interfaces[i];
        printf("  %s: %s %s %s\r\n", 
               iface->if_name,
               iface->connected ? "UP" : "DOWN",
               iface->ip_addr,
               iface->type == NETIF_TYPE_WIRELESS ? iface->ssid : "");
    }
    
    printf("\r\nStatistics:\r\n");
    printf("  Total Connections: %lu\r\n", (unsigned long)g_communication_service.stats.total_connections);
    printf("  Successful Connections: %lu\r\n", (unsigned long)g_communication_service.stats.successful_connections);
    printf("  Failed Connections: %lu\r\n", (unsigned long)g_communication_service.stats.failed_connections);
    printf("  Current Connections: %lu\r\n", (unsigned long)g_communication_service.stats.current_connections);
    printf("  Network Scans: %lu\r\n", (unsigned long)g_communication_service.stats.network_scans);
    printf("  Last Error: 0x%08lX\r\n", (unsigned long)g_communication_service.stats.last_error_code);
    printf("===============================================================\r\n\r\n");


    printf("\r\n================== NETWORK SCAN RESULTS ==================\r\n");
    printf("Found %lu networks:\r\n", g_communication_service.scan_result_count);
    for (uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        network_scan_result_t *result = &g_communication_service.scan_results[i];
        printf("  %s (%s) - %d dBm, Channel %d, Security: %d\r\n",
               result->ssid, result->bssid, (int)result->rssi, (int)result->channel, (int)result->security);
    }
    printf("=======================================================\r\n\r\n");


    printf("\r\n================== KNOWN NETWORKS ==================\r\n");
    for (uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        network_scan_result_t *result = &g_communication_service.known_networks[i];
        printf("  %s (%s) - %d dBm, Channel %d, Security: %d\r\n",
               result->ssid, result->bssid, (int)result->rssi, (int)result->channel, (int)result->security);
    }
    printf("=======================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: comm interfaces
 */
static int comm_interfaces_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    // Update interface list
    update_interface_list();
    
    printf("\r\n================== NETWORK INTERFACES ==================\r\n");
    for (uint32_t i = 0; i < g_communication_service.interface_count; i++) {
        network_interface_status_t *iface = &g_communication_service.interfaces[i];
        printf("Interface: %s\r\n", iface->if_name);
        printf("  State: %s\r\n", iface->connected ? "UP" : "DOWN");
        printf("  Type: %s\r\n", 
               (iface->type == NETIF_TYPE_WIRELESS) ? "WIRELESS" :
               (iface->type == NETIF_TYPE_LOCAL) ? "LOCAL" : "UNKNOWN");
        printf("  IP Address: %s\r\n", iface->ip_addr);
        printf("  MAC Address: %s\r\n", iface->mac_addr);
        
        if (iface->type == NETIF_TYPE_WIRELESS) {
            printf("  SSID: %s\r\n", iface->ssid);
            printf("  RSSI: %lu dBm\r\n", iface->rssi);
            printf("  Channel: %lu\r\n", iface->channel);
        }
        printf("\r\n");
    }
    printf("=======================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: comm scan
 */
static int comm_scan_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    if (g_communication_service.scan_in_progress) {
        printf("Network scan already in progress\r\n");
        return -1;
    }
    
    printf("Starting network scan...\r\n");
    
    aicam_result_t result = communication_start_network_scan(NULL);
    if (result != AICAM_OK) {
        printf("Failed to start network scan: %d\r\n", result);
        return -1;
    }
    
    // Wait for scan to complete (simplified)
    int timeout = 100; // 10 seconds
    while (g_communication_service.scan_in_progress && timeout > 0) {
        osDelay(100);
        timeout--;
    }
    
    if (g_communication_service.scan_in_progress) {
        printf("Network scan timeout\r\n");
        return -1;
    }
    
    printf("\r\n================== NETWORK SCAN RESULTS ==================\r\n");
    printf("Found %lu networks:\r\n", g_communication_service.scan_result_count);
    
    // Show known networks first
    printf("\r\n--- KNOWN NETWORKS ---\r\n");
    for (uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        network_scan_result_t *result = &g_communication_service.scan_results[i];
        if (result->is_known) {
            printf("  [KNOWN] %s (%s) - %lu dBm, Channel %lu, Security: %d\r\n",
                   result->ssid, result->bssid, result->rssi, result->channel, (int)result->security);
        }
    }
    
    // Show unknown networks
    printf("\r\n--- UNKNOWN NETWORKS ---\r\n");
    for (uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        network_scan_result_t *result = &g_communication_service.scan_results[i];
        if (!result->is_known) {
            printf("  [NEW] %s (%s) - %lu dBm, Channel %lu, Security: %d\r\n",
                   result->ssid, result->bssid, result->rssi, result->channel, (int)result->security);
        }
    }
    printf("=======================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: comm known
 */
static int comm_known_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    network_scan_result_t known_networks[MAX_KNOWN_NETWORKS];
    uint32_t count = 0;
    
    aicam_result_t result = communication_get_known_networks(known_networks, MAX_KNOWN_NETWORKS, &count);
    if (result != AICAM_OK) {
        printf("Failed to get known networks: %d\r\n", result);
        return -1;
    }
    
    printf("\r\n================== KNOWN NETWORKS ==================\r\n");
    printf("Found %lu known networks:\r\n", count);
    
    for (uint32_t i = 0; i < count; i++) {
        network_scan_result_t *network = &known_networks[i];
        printf("  %s (%s) - %lu dBm, Channel %lu, Security: %d\r\n",
               network->ssid, network->bssid, network->rssi, network->channel, (int)network->security);
    }
    printf("=======================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: comm unknown
 */
static int comm_unknown_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    network_scan_result_t unknown_networks[MAX_KNOWN_NETWORKS];
    uint32_t count = 0;
    
    aicam_result_t result = communication_get_unknown_networks(unknown_networks, MAX_KNOWN_NETWORKS, &count);
    if (result != AICAM_OK) {
        printf("Failed to get unknown networks: %d\r\n", result);
        return -1;
    }
    
    printf("\r\n================== UNKNOWN NETWORKS ==================\r\n");
    printf("Found %lu unknown networks:\r\n", count);
    
    for (uint32_t i = 0; i < count; i++) {
        network_scan_result_t *network = &unknown_networks[i];
        printf("  %s (%s) - %lu dBm, Channel %lu, Security: %d\r\n",
               network->ssid, network->bssid, network->rssi, network->channel, (int)network->security);
    }
    printf("=======================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: comm classified
 */
static int comm_classified_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    classified_scan_results_t results;
    
    aicam_result_t result = communication_get_classified_scan_results(&results);
    if (result != AICAM_OK) {
        printf("Failed to get classified scan results: %d\r\n", result);
        return -1;
    }
    
    printf("\r\n================== CLASSIFIED NETWORK SCAN ==================\r\n");
    printf("Total networks: %d\r\n", (int)results.known_count + (int)results.unknown_count);
    printf("Known networks: %d\r\n", (int)results.known_count);
    printf("Unknown networks: %d\r\n", (int)results.unknown_count);
    
    printf("\r\n--- KNOWN NETWORKS ---\r\n");
    for (uint32_t i = 0; i < results.known_count; i++) {
        network_scan_result_t *network = &results.known_networks[i];
        printf("  %s (%s) - %lu dBm, Channel %lu, Security: %d\r\n",
               network->ssid, network->bssid, network->rssi, network->channel, (int)network->security);
    }
    
    printf("\r\n--- UNKNOWN NETWORKS ---\r\n");
    for (uint32_t i = 0; i < results.unknown_count; i++) {
        network_scan_result_t *network = &results.unknown_networks[i];
        printf("  %s (%s) - %lu dBm, Channel %lu, Security: %d\r\n",
               network->ssid, network->bssid, network->rssi, network->channel, (int)network->security);
    }
    printf("=============================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: comm start (information only)
 */
static int comm_start_cmd(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: comm start <interface>\r\n");
        printf("  Interfaces: ap, sta, lo\r\n");
        printf("  Note: This command only logs the request, does not actually start the interface\r\n");
        return -1;
    }
    
    const char* if_name = NULL;
    if (strcmp(argv[1], "ap") == 0) {
        if_name = NETIF_NAME_WIFI_AP;
    } else if (strcmp(argv[1], "sta") == 0) {
        if_name = NETIF_NAME_WIFI_STA;
    } else if (strcmp(argv[1], "lo") == 0) {
        if_name = NETIF_NAME_LOCAL;
    } else {
        printf("Invalid interface: %s\r\n", argv[1]);
        return -1;
    }
    
    printf("Interface %s start request logged (information only)\r\n", if_name);
    
    aicam_result_t result = communication_start_interface(if_name);
    if (result != AICAM_OK) {
        printf("Failed to log start request for interface %s: %d\r\n", if_name, result);
        return -1;
    }
    
    printf("Interface %s start request logged successfully\r\n", if_name);
    return 0;
}

/**
 * @brief CLI command: comm stop (information only)
 */
static int comm_stop_cmd(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: comm stop <interface>\r\n");
        printf("  Interfaces: ap, sta, lo\r\n");
        printf("  Note: This command only logs the request, does not actually stop the interface\r\n");
        return -1;
    }
    
    const char* if_name = NULL;
    if (strcmp(argv[1], "ap") == 0) {
        if_name = NETIF_NAME_WIFI_AP;
    } else if (strcmp(argv[1], "sta") == 0) {
        if_name = NETIF_NAME_WIFI_STA;
    } else if (strcmp(argv[1], "lo") == 0) {
        if_name = NETIF_NAME_LOCAL;
    } else {
        printf("Invalid interface: %s\r\n", argv[1]);
        return -1;
    }
    
    printf("Interface %s stop request logged (information only)\r\n", if_name);
    
    aicam_result_t result = communication_stop_interface(if_name);
    if (result != AICAM_OK) {
        printf("Failed to log stop request for interface %s: %d\r\n", if_name, result);
        return -1;
    }
    
    printf("Interface %s stop request logged successfully\r\n", if_name);
    return 0;
}

/**
 * @brief CLI command: comm restart (information only)
 */
static int comm_restart_cmd(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: comm restart <interface>\r\n");
        printf("  Interfaces: ap, sta, lo\r\n");
        printf("  Note: This command only logs the request, does not actually restart the interface\r\n");
        return -1;
    }
    
    const char* if_name = NULL;
    if (strcmp(argv[1], "ap") == 0) {
        if_name = NETIF_NAME_WIFI_AP;
    } else if (strcmp(argv[1], "sta") == 0) {
        if_name = NETIF_NAME_WIFI_STA;
    } else if (strcmp(argv[1], "lo") == 0) {
        if_name = NETIF_NAME_LOCAL;
    } else {
        printf("Invalid interface: %s\r\n", argv[1]);
        return -1;
    }
    
    printf("Interface %s restart request logged (information only)\r\n", if_name);
    
    aicam_result_t result = communication_restart_interface(if_name);
    if (result != AICAM_OK) {
        printf("Failed to log restart request for interface %s: %d\r\n", if_name, result);
        return -1;
    }
    
    printf("Interface %s restart request logged successfully\r\n", if_name);
    return 0;
}

/**
 * @brief CLI command: comm config
 */
static int comm_config_cmd(int argc, char* argv[])
{
    if (argc < 3) {
        printf("Usage: comm config <interface> <ssid> [password]\r\n");
        printf("  Example: comm config sta MyWiFi mypassword\r\n");
        return -1;
    }
    
    const char* if_name = NULL;
    if (strcmp(argv[2], "ap") == 0) {
        if_name = NETIF_NAME_WIFI_AP;
    } else if (strcmp(argv[2], "sta") == 0) {
        if_name = NETIF_NAME_WIFI_STA;
    } else {
        printf("Invalid interface: %s (use 'ap' or 'sta')\r\n", argv[2]);
        return -1;
    }
    
    netif_config_t config = {0};
    strncpy(config.wireless_cfg.ssid, argv[3], sizeof(config.wireless_cfg.ssid) - 1);
    
    if (argc > 3) {
        strncpy(config.wireless_cfg.pw, argv[4], sizeof(config.wireless_cfg.pw) - 1);
        config.wireless_cfg.security = WIRELESS_WPA_WPA2_MIXED;
    } else {
        config.wireless_cfg.security = WIRELESS_OPEN;
    }
    
    if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0) {
        config.ip_mode = NETIF_IP_MODE_DHCP;
    } else {
        config.ip_mode = NETIF_IP_MODE_DHCPS;
    }
    
    printf("Configuring interface %s with SSID '%s'...\r\n", if_name, argv[3]);
    
    aicam_result_t result = communication_configure_interface(if_name, &config);
    if (result != AICAM_OK) {
        printf("Failed to configure interface %s: %d\r\n", if_name, result);
        return -1;
    }
    
    printf("Interface %s configured successfully\r\n", if_name);
    return 0;
}

/**
 * @brief CLI command: comm stats
 */
static int comm_stats_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    printf("\r\n================== COMMUNICATION STATISTICS ==================\r\n");
    printf("Total Connections: %llu\r\n", (unsigned long long)g_communication_service.stats.total_connections);
    printf("Successful Connections: %llu\r\n", (unsigned long long)g_communication_service.stats.successful_connections);
    printf("Failed Connections: %llu\r\n", (unsigned long long)g_communication_service.stats.failed_connections);
    printf("Disconnections: %llu\r\n", (unsigned long long)g_communication_service.stats.disconnections);
    printf("Current Connections: %lu\r\n", (unsigned long)g_communication_service.stats.current_connections);
    printf("Network Scans: %llu\r\n", (unsigned long long)g_communication_service.stats.network_scans);
    printf("Bytes Sent: %llu\r\n", (unsigned long long)g_communication_service.stats.bytes_sent);
    printf("Bytes Received: %llu\r\n", (unsigned long long)g_communication_service.stats.bytes_received);
    printf("Last Error Code: 0x%08X\r\n", (unsigned int)g_communication_service.stats.last_error_code);
    printf("=============================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: comm reset
 */
static int comm_reset_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    printf("Resetting communication service statistics...\r\n");
    
    aicam_result_t result = communication_reset_stats();
    if (result != AICAM_OK) {
        printf("Failed to reset statistics: %d\r\n", result);
        return -1;
    }
    
    printf("Statistics reset successfully\r\n");
    return 0;
}

/**
 * @brief CLI command: comm delete
 */
static int comm_delete_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    if (argc < 4) {
        printf("Usage: comm delete <ssid> <bssid>\r\n");
        printf("  ssid  - Network SSID (e.g., \"MyWiFi\")\r\n");
        printf("  bssid - Network BSSID (e.g., \"AA:BB:CC:DD:EE:FF\")\r\n");
        printf("Example: comm delete \"MyWiFi\" \"AA:BB:CC:DD:EE:FF\"\r\n");
        return -1;
    }
    
    const char* ssid = argv[2];
    const char* bssid = argv[3];
    
    printf("Deleting known network: %s (%s)\r\n", ssid, bssid);
    
    aicam_result_t result = communication_delete_known_network(ssid, bssid);
    if (result != AICAM_OK) {
        printf("Failed to delete known network: %d\r\n", result);
        return -1;
    }
    
    printf("Successfully deleted known network: %s (%s)\r\n", ssid, bssid);
    return 0;
}

/**
 * @brief Main CLI command handler
 */
static int comm_cmd(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: comm <command> [args]\r\n");
        printf("Commands:\r\n");
        printf("  status     - Show service status\r\n");
        printf("  interfaces - List network interfaces\r\n");
        printf("  scan       - Scan for networks\r\n");
        printf("  known      - Show known networks only\r\n");
        printf("  unknown    - Show unknown networks only\r\n");
        printf("  classified - Show classified scan results\r\n");
        printf("  delete     - Delete known network (ssid bssid)\r\n");
        printf("  start      - Log interface start request (information only)\r\n");
        printf("  stop       - Log interface stop request (information only)\r\n");
        printf("  restart    - Log interface restart request (information only)\r\n");
        printf("  config     - Configure interface (ap/sta ssid [password])\r\n");
        printf("  stats      - Show statistics\r\n");
        printf("  reset      - Reset statistics\r\n");
        printf("\r\nNote: This service only collects communication information and provides\r\n");
        printf("      configuration management. It does not control actual interface operations.\r\n");
        return -1;
    }
    
    if (strcmp(argv[1], "status") == 0) {
        return comm_status_cmd(argc, argv);
    } else if (strcmp(argv[1], "interfaces") == 0) {
        return comm_interfaces_cmd(argc, argv);
    } else if (strcmp(argv[1], "scan") == 0) {
        return comm_scan_cmd(argc, argv);
    } else if (strcmp(argv[1], "known") == 0) {
        return comm_known_cmd(argc, argv);
    } else if (strcmp(argv[1], "unknown") == 0) {
        return comm_unknown_cmd(argc, argv);
    } else if (strcmp(argv[1], "classified") == 0) {
        return comm_classified_cmd(argc, argv);
    } else if (strcmp(argv[1], "delete") == 0) {
        return comm_delete_cmd(argc, argv);
    } else if (strcmp(argv[1], "start") == 0) {
        return comm_start_cmd(argc, argv);
    } else if (strcmp(argv[1], "stop") == 0) {
        return comm_stop_cmd(argc, argv);
    } else if (strcmp(argv[1], "restart") == 0) {
        return comm_restart_cmd(argc, argv);
    } else if (strcmp(argv[1], "config") == 0) {
        return comm_config_cmd(argc, argv);
    } else if (strcmp(argv[1], "stats") == 0) {
        return comm_stats_cmd(argc, argv);
    } else if (strcmp(argv[1], "reset") == 0) {
        return comm_reset_cmd(argc, argv);
    } else {
        printf("Unknown command: %s\r\n", argv[1]);
        return -1;
    }
}

/* ==================== Known Network Management ==================== */

aicam_result_t communication_delete_known_network(const char *ssid, const char *bssid)
{
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!ssid || !bssid) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (strlen(ssid) == 0 || strlen(bssid) == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    LOG_SVC_INFO("Public API: Deleting known network: %s (%s)", ssid, bssid);
    
    // Call internal delete function
    delete_known_network(ssid, bssid);
    
    LOG_SVC_INFO("Public API: Successfully deleted known network: %s (%s)", ssid, bssid);
    
    return AICAM_OK;
}

/* ==================== Network Interface Ready Callbacks Implementation ==================== */

/**
 * @brief WiFi AP ready callback
 */
static void on_wifi_ap_ready(const char *if_name, aicam_result_t result)
{
    if (result == AICAM_OK) {
        LOG_SVC_INFO("WiFi AP initialized and ready (broadcasting)");
        
        // Update statistics
        g_communication_service.stats.successful_connections++;

        // Update device MAC address
        device_service_update_device_mac_address();

        // Update communication type
        device_service_update_communication_type();

        // Update MQTT client ID and topic
        mqtt_service_update_client_id_and_topic();

        // Keep LED on
        device_service_led_on();
     
        //Configure AP interface using configuration from json_config_mgr
        network_service_config_t* network_config = (network_service_config_t*)buffer_calloc(1, sizeof(network_service_config_t));
        if (!network_config) {
            LOG_SVC_ERROR("Failed to allocate memory for network config");
            return;
        }
        netif_config_t ap_config = {0};
        aicam_result_t config_result = json_config_get_network_service_config(network_config);
        if (config_result == AICAM_OK) {
            LOG_SVC_INFO("Configuring AP with SSID: %s", network_config->ssid);
            // Configure AP interface
            if (strcmp(network_config->ssid, "AICAM-AP") == 0 || strlen(network_config->ssid) == 0) {
                LOG_SVC_INFO("Default AP SSID, skip configuration");
                communication_get_interface_config(NETIF_NAME_WIFI_AP, &ap_config);
                strncpy(network_config->ssid, ap_config.wireless_cfg.ssid, sizeof(network_config->ssid) - 1);
                strncpy(network_config->password, ap_config.wireless_cfg.pw, sizeof(network_config->password) - 1);
                aicam_result_t result = json_config_set_network_service_config(network_config);
                LOG_SVC_INFO("ssid: %s, password: %s", network_config->ssid, network_config->password);
                if (result != AICAM_OK) {
                    LOG_SVC_WARN("Failed to set network service configuration: %d", result);
                } else {
                    LOG_SVC_INFO("Network service configuration set successfully");
                }
            } else {
                netif_config_t ap_config = {0};
                nm_get_netif_cfg(NETIF_NAME_WIFI_AP, &ap_config);
                strncpy(ap_config.wireless_cfg.ssid, network_config->ssid, sizeof(ap_config.wireless_cfg.ssid) - 1);
                strncpy(ap_config.wireless_cfg.pw, network_config->password, sizeof(ap_config.wireless_cfg.pw) - 1);
                ap_config.wireless_cfg.security = (strlen(network_config->password) > 0) ? WIRELESS_WPA_WPA2_MIXED : WIRELESS_OPEN;
                
                aicam_result_t result = communication_configure_interface(NETIF_NAME_WIFI_AP, &ap_config);
                if (result != AICAM_OK) {
                    LOG_SVC_WARN("Failed to configure WiFi AP: %d", result);
                } else {
                    LOG_SVC_INFO("WiFi AP configured successfully with SSID: %s", network_config->ssid);
                }
            }
        } else {
            LOG_SVC_WARN("Failed to get network service configuration: %d", config_result);
        }

        buffer_free(network_config);

        // Notify other services that AP is ready
        // Web service can now be accessed

        
        uint32_t init_time = netif_init_manager_get_init_time(if_name);
        LOG_SVC_INFO("WiFi AP initialization completed in %u ms", init_time);
        service_set_ap_ready(AICAM_TRUE);
    } else {
        LOG_SVC_ERROR("WiFi AP initialization failed: %d", result);
        
        // Update statistics
        g_communication_service.stats.failed_connections++;
        g_communication_service.stats.last_error_code = result;
    }
}

/**
 * @brief WiFi STA ready callback (optimized for low power mode fast startup)
 */
static void on_wifi_sta_ready(const char *if_name, aicam_result_t result)
{
    if (result == AICAM_OK) {
        LOG_SVC_INFO("WiFi STA initialized and ready");

        // Check if woken by RTC (timing or alarm) - only enable fast connection for RTC wakeup
        uint32_t wakeup_flag = u0_module_get_wakeup_flag_ex();
        aicam_bool_t is_rtc_wakeup = (wakeup_flag & (PWR_WAKEUP_FLAG_RTC_TIMING | 
                                                     PWR_WAKEUP_FLAG_RTC_ALARM_A | 
                                                     PWR_WAKEUP_FLAG_RTC_ALARM_B)) != 0;

        // In RTC wakeup mode, skip scan result loading to save time
        // We'll use cached known network info directly
        if (!is_rtc_wakeup) {
            //get scan results from storage (full speed mode only)
            aicam_result_t scan_result_result = communication_start_network_scan(NULL);
            if (scan_result_result != AICAM_OK) {
                LOG_SVC_ERROR("Failed to update network scan result: %d", scan_result_result);
            }
            wireless_scan_result_t *scan_result = nm_wireless_get_scan_result();
            if(scan_result) {
                g_communication_service.scan_result_count = scan_result->scan_count;
                for(uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
                    strncpy(g_communication_service.scan_results[i].ssid, scan_result->scan_info[i].ssid, sizeof(g_communication_service.scan_results[i].ssid) - 1);
                    snprintf(g_communication_service.scan_results[i].bssid, sizeof(g_communication_service.scan_results[i].bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                            scan_result->scan_info[i].bssid[0], scan_result->scan_info[i].bssid[1],
                            scan_result->scan_info[i].bssid[2], scan_result->scan_info[i].bssid[3],
                            scan_result->scan_info[i].bssid[4], scan_result->scan_info[i].bssid[5]);
                    g_communication_service.scan_results[i].rssi = scan_result->scan_info[i].rssi;
                    g_communication_service.scan_results[i].channel = scan_result->scan_info[i].channel;
                    g_communication_service.scan_results[i].security = (wireless_security_t)scan_result->scan_info[i].security;
                    g_communication_service.scan_results[i].connected = AICAM_FALSE;
                    g_communication_service.scan_results[i].is_known = AICAM_FALSE;
                    g_communication_service.scan_results[i].last_connected_time = 0;
                }
            }
        } else {
            // RTC wakeup mode: clear scan results to use cached known networks
            g_communication_service.scan_result_count = 0;
        }
        
        // Try to connect to known networks if enabled
        if (g_communication_service.config.auto_start_wifi_sta) {
            LOG_SVC_INFO("Attempting to connect to known networks...");
            
            // In RTC wakeup mode, reduce delay for faster connection
            // Normal mode keeps original delay for stability
            uint32_t ready_delay = is_rtc_wakeup ? 100 : 500;
            osDelay(ready_delay);
            
            uint64_t start_time = rtc_get_uptime_ms();
            try_connect_known_networks();
            uint64_t end_time = rtc_get_uptime_ms();
            uint64_t duration = end_time - start_time;
            LOG_SVC_INFO("Known networks connection time: %lu ms (RTC wakeup: %s)", 
                        (unsigned long)duration, is_rtc_wakeup ? "YES" : "NO");
        }

        uint32_t init_time = netif_init_manager_get_init_time(if_name);
        LOG_SVC_INFO("WiFi STA initialization completed in %u ms", init_time);

    } else {
        LOG_SVC_ERROR("WiFi STA initialization failed: %d", result);
        
        // Update statistics
        g_communication_service.stats.failed_connections++;
        g_communication_service.stats.last_error_code = result;
    }
}

/* ==================== CLI Command Registration ==================== */

debug_cmd_reg_t comm_cmd_table[] = {
    {"comm", "Communication service management.", comm_cmd},
};

void comm_cmd_register(void)
{
    debug_cmdline_register(comm_cmd_table, sizeof(comm_cmd_table) / sizeof(comm_cmd_table[0]));
}

