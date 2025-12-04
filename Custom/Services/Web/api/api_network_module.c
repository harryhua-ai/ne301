/**
 * @file api_network_module.c
 * @brief Network API Module Implementation
 * @details Network management API implementation based on communication_service
 */

#include "api_network_module.h"
#include "web_api.h"
#include "communication_service.h"
#include "debug.h"
#include "cJSON.h"
#include "cmsis_os2.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "web_server.h"
/* ==================== Helper Functions ==================== */

/**
 * @brief Convert network interface type to string
 */
static const char* get_interface_type_string(netif_type_t type) {
    switch (type) {
        case NETIF_TYPE_WIRELESS:
            return "wireless";
        case NETIF_TYPE_LOCAL:
            return "local";
        default:
            return "unknown";
    }
}

/**
 * @brief Convert network interface state to string
 */
static const char* get_interface_state_string(netif_state_t state) {
    switch (state) {
        case NETIF_STATE_UP:
            return "up";
        case NETIF_STATE_DOWN:
            return "down";
        default:
            return "unknown";
    }
}

/**
 * @brief Convert security type to string
 */
static const char* get_security_type_string(wireless_security_t security) {
    switch (security) {
        case WIRELESS_OPEN:
            return "open";
        case WIRELESS_WEP:
            return "wep";
        case WIRELESS_WPA:
            return "wpa_psk";
        case WIRELESS_WPA2:
            return "wpa2_psk";
        case WIRELESS_WPA_WPA2_MIXED:
            return "wpa_wpa2_mixed";
        case WIRELESS_WPA3:
            return "wpa3_psk";
        default:
            return "unknown";
    }
}

/**
 * @brief Create network interface JSON object
 */
static cJSON* create_interface_json(const network_interface_status_t* interface) {
    if (!interface) return NULL;
    
    cJSON* interface_json = cJSON_CreateObject();
    if (!interface_json) return NULL;
    
    cJSON_AddStringToObject(interface_json, "name", interface->if_name);
    cJSON_AddStringToObject(interface_json, "type", get_interface_type_string(interface->type));
    cJSON_AddStringToObject(interface_json, "state", get_interface_state_string(interface->state));
    cJSON_AddBoolToObject(interface_json, "connected", interface->connected);
    cJSON_AddStringToObject(interface_json, "ip_address", interface->ip_addr);
    cJSON_AddStringToObject(interface_json, "mac_address", interface->mac_addr);
    
    if (interface->type == NETIF_TYPE_WIRELESS) {
        cJSON_AddStringToObject(interface_json, "ssid", interface->ssid);
        cJSON_AddNumberToObject(interface_json, "rssi", interface->rssi);
        cJSON_AddNumberToObject(interface_json, "channel", interface->channel);
    }
    
    return interface_json;
}

/**
 * @brief Create scan result JSON object
 */
static cJSON* create_scan_result_json(const network_scan_result_t* result) {
    if (!result) return NULL;
    
    cJSON* result_json = cJSON_CreateObject();
    if (!result_json) return NULL;
    
    cJSON_AddStringToObject(result_json, "ssid", result->ssid);
    cJSON_AddStringToObject(result_json, "bssid", result->bssid);
    cJSON_AddNumberToObject(result_json, "rssi", result->rssi);
    cJSON_AddNumberToObject(result_json, "channel", result->channel);
    cJSON_AddStringToObject(result_json, "security", get_security_type_string(result->security));
    cJSON_AddBoolToObject(result_json, "connected", result->connected);
    cJSON_AddBoolToObject(result_json, "is_known", result->is_known);
    cJSON_AddNumberToObject(result_json, "last_connected_time", result->last_connected_time);
    
    return result_json;
}

/* ==================== API Handler Functions ==================== */

/**
 * @brief GET /api/v1/system/network/status - Get network status
 */
aicam_result_t network_status_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow GET method
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    
    // Check if communication service is running
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }

    //get current network service configuration
    network_service_config_t network_service_config;
    aicam_result_t result = json_config_get_network_service_config(&network_service_config);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get network service configuration");
    }
    LOG_SVC_INFO("network_service_config.ssid: %s", network_service_config.ssid);
    LOG_SVC_INFO("network_service_config.password: %s", network_service_config.password);
    LOG_SVC_INFO("network_service_config.ap_sleep_time: %d", network_service_config.ap_sleep_time);
    cJSON* network_service_json = cJSON_CreateObject();
    if (network_service_json) {
        cJSON_AddStringToObject(network_service_json, "ssid", network_service_config.ssid);
        cJSON_AddStringToObject(network_service_json, "password", network_service_config.password);
        cJSON_AddNumberToObject(network_service_json, "ap_sleep_time", network_service_config.ap_sleep_time);  
        cJSON_AddItemToObject(response_json, "network_service", network_service_json);
    }
    // Get communication service statistics
    // communication_service_stats_t stats;
    // aicam_result_t result = communication_get_stats(&stats);
    // if (result == AICAM_OK) {
    //     cJSON* stats_json = cJSON_CreateObject();
    //     if (stats_json) {
    //         cJSON_AddNumberToObject(stats_json, "total_connections", stats.total_connections);
    //         cJSON_AddNumberToObject(stats_json, "successful_connections", stats.successful_connections);
    //         cJSON_AddNumberToObject(stats_json, "failed_connections", stats.failed_connections);
    //         cJSON_AddNumberToObject(stats_json, "disconnections", stats.disconnections);
    //         cJSON_AddNumberToObject(stats_json, "current_connections", stats.current_connections);
    //         cJSON_AddNumberToObject(stats_json, "network_scans", stats.network_scans);
    //         cJSON_AddNumberToObject(stats_json, "bytes_sent", stats.bytes_sent);
    //         cJSON_AddNumberToObject(stats_json, "bytes_received", stats.bytes_received);
    //         cJSON_AddNumberToObject(stats_json, "last_error_code", stats.last_error_code);
    //         cJSON_AddItemToObject(response_json, "statistics", stats_json);
    //     }
    // }
    
    // Get network interfaces
    network_interface_status_t interfaces[8];
    uint32_t interface_count = 0;
    result = communication_get_network_interfaces(interfaces, 8, &interface_count);
    if (result == AICAM_OK) {
        cJSON* interfaces_array = cJSON_CreateArray();
        if (interfaces_array) {
            for (uint32_t i = 0; i < interface_count; i++) {
                if(strcmp(interfaces[i].if_name, NETIF_NAME_LOCAL) == 0) continue;
                cJSON* interface_json = create_interface_json(&interfaces[i]);
                if (interface_json) {
                    cJSON_AddItemToArray(interfaces_array, interface_json);
                }
            }
            cJSON_AddItemToObject(response_json, "interfaces", interfaces_array);
        }
    }
    cJSON_AddNumberToObject(response_json, "interface_count", interface_count - 1);

    // Get classified scan results
    classified_scan_results_t scan_results;
    result = communication_get_classified_scan_results(&scan_results);
    if (result == AICAM_OK) {
        cJSON* scan_json = cJSON_CreateObject();
        if (scan_json) {
            // Known networks
            cJSON* known_array = cJSON_CreateArray();
            if (known_array) {
                for (uint32_t i = 0; i < scan_results.known_count; i++) {
                    cJSON* network_json = create_scan_result_json(&scan_results.known_networks[i]);
                    if (network_json) {
                        cJSON_AddItemToArray(known_array, network_json);
                    }
                }
                cJSON_AddItemToObject(scan_json, "known_networks", known_array);
            }
            cJSON_AddNumberToObject(scan_json, "known_count", scan_results.known_count);
            
            // Unknown networks
            cJSON* unknown_array = cJSON_CreateArray();
            if (unknown_array) {
                for (uint32_t i = 0; i < scan_results.unknown_count; i++) {
                    cJSON* network_json = create_scan_result_json(&scan_results.unknown_networks[i]);
                    if (network_json) {
                        cJSON_AddItemToArray(unknown_array, network_json);
                    }
                }
                cJSON_AddItemToObject(scan_json, "unknown_networks", unknown_array);
            }
            cJSON_AddNumberToObject(scan_json, "unknown_count", scan_results.unknown_count);
            
            cJSON_AddItemToObject(response_json, "scan_results", scan_json);
        }
    }
    
    
    // Add service status
    service_state_t service_state = communication_service_get_state();
    const char* state_str = "unknown";
    switch (service_state) {
        case SERVICE_STATE_UNINITIALIZED:
            state_str = "uninitialized";
            break;
        case SERVICE_STATE_INITIALIZING:
            state_str = "initializing";
            break;
        case SERVICE_STATE_INITIALIZED:
            state_str = "initialized";
            break;
        case SERVICE_STATE_RUNNING:
            state_str = "running";
            break;
        case SERVICE_STATE_SUSPENDED:
            state_str = "suspended";
            break;
        case SERVICE_STATE_ERROR:
            state_str = "error";
            break;
        case SERVICE_STATE_SHUTDOWN:
            state_str = "shutdown";
            break;
        default:
            state_str = "unknown";
            break;
    }
    cJSON_AddStringToObject(response_json, "service_state", state_str);
    cJSON_AddStringToObject(response_json, "service_version", communication_get_version());
    
    // Send response
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Network status retrieved successfully");
    //hal_mem_free(json_string);
    
    return api_result;
}

/**
 * @brief POST /api/v1/system/network/wifi - Configure WiFi settings
 */
aicam_result_t network_wifi_config_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // Check if communication service is running
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    // Parse JSON request body
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }

    // get network service configuration
    network_service_config_t network_service_config;
    aicam_result_t result = json_config_get_network_service_config(&network_service_config);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get network service configuration");
    }
    
    // Extract interface type (ap or sta)
    cJSON* interface_item = cJSON_GetObjectItem(request_json, "interface");
    if (!interface_item || !cJSON_IsString(interface_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'interface' field");
    }
    
    const char* interface_str = cJSON_GetStringValue(interface_item);
    const char* if_name = NULL;
    aicam_bool_t ssid_changed = AICAM_FALSE;
    aicam_bool_t password_changed = AICAM_FALSE;

    if (strcmp(interface_str, NETIF_NAME_WIFI_AP) == 0) {
        if_name = NETIF_NAME_WIFI_AP;
    } else if (strcmp(interface_str, NETIF_NAME_WIFI_STA) == 0) {
        if_name = NETIF_NAME_WIFI_STA;
    } else {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid interface type (use 'ap' or 'wl')");
    }
    
    // Extract SSID
    cJSON* ssid_item = cJSON_GetObjectItem(request_json, "ssid");
    if (!ssid_item || !cJSON_IsString(ssid_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'ssid' field");
    }
    
    const char* ssid = cJSON_GetStringValue(ssid_item);
    if (strlen(ssid) == 0 || strlen(ssid) >= 32) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "SSID must be 1-31 characters");
    }
    if (strcmp(network_service_config.ssid, ssid) != 0) {
        ssid_changed = AICAM_TRUE;
        strncpy(network_service_config.ssid, ssid, sizeof(network_service_config.ssid) - 1);
        network_service_config.ssid[sizeof(network_service_config.ssid) - 1] = '\0';
    }
    
        
    // Extract password (optional)
    const char* password = "";
    cJSON* password_item = cJSON_GetObjectItem(request_json, "password");
    if (password_item && cJSON_IsString(password_item)) {
        password = cJSON_GetStringValue(password_item);
        if (strlen(password) > 0) {
            if (strlen(password) < 8 || strlen(password) >= 64) {
                cJSON_Delete(request_json);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Password must be 8-63 characters");
            }
        }
        if (strcmp(network_service_config.password, password) != 0) {
            password_changed = AICAM_TRUE;
            strncpy(network_service_config.password, password, sizeof(network_service_config.password) - 1);
            network_service_config.password[sizeof(network_service_config.password) - 1] = '\0';
        }
    }
    
    // Extract AP sleep time (optional, only for AP mode)
    uint32_t ap_sleep_time = 0;
    if (strcmp(interface_str, "ap") == 0) {
        cJSON* sleep_time_item = cJSON_GetObjectItem(request_json, "ap_sleep_time");
        if (sleep_time_item && cJSON_IsNumber(sleep_time_item)) {
            ap_sleep_time = (uint32_t)cJSON_GetNumberValue(sleep_time_item);
            if (ap_sleep_time > 3600) { // Max 1 hour
                cJSON_Delete(request_json);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "AP sleep time must be <= 3600 seconds");
            }
        }
        network_service_config.ap_sleep_time = ap_sleep_time;
        //update to web server
        result = web_server_ap_sleep_timer_update(ap_sleep_time);
        if (result != AICAM_OK) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to set AP sleep time");
        }
    }

    // Extract bssid (optional)
    const char* bssid = "";
    cJSON* bssid_item = cJSON_GetObjectItem(request_json, "bssid");
    if (bssid_item && cJSON_IsString(bssid_item)) {
        bssid = cJSON_GetStringValue(bssid_item);
    }
    
    // Get current configuration first 
    netif_config_t config;
    result = communication_get_interface_config(if_name, &config);
    if (result != AICAM_OK) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get current interface configuration");
    }
    
    // Update wireless configuration
    strncpy(config.wireless_cfg.ssid, ssid, sizeof(config.wireless_cfg.ssid) - 1);
    config.wireless_cfg.ssid[sizeof(config.wireless_cfg.ssid) - 1] = '\0';
    
    if (strlen(password) > 0) {
        strncpy(config.wireless_cfg.pw, password, sizeof(config.wireless_cfg.pw) - 1);
        config.wireless_cfg.pw[sizeof(config.wireless_cfg.pw) - 1] = '\0';
        config.wireless_cfg.security = WIRELESS_WPA_WPA2_MIXED;
    } else {
        memset(config.wireless_cfg.pw, 0, sizeof(config.wireless_cfg.pw));
        config.wireless_cfg.security = WIRELESS_OPEN;
    }

    if (strlen(bssid) > 0 && strcmp(if_name, NETIF_NAME_WIFI_STA) == 0) {
        unsigned int bssid_bytes[6];
        sscanf(bssid, "%02X:%02X:%02X:%02X:%02X:%02X",
               &bssid_bytes[0], &bssid_bytes[1], &bssid_bytes[2],
               &bssid_bytes[3], &bssid_bytes[4], &bssid_bytes[5]);
        for (int i = 0; i < 6; i++) {
            config.wireless_cfg.bssid[i] = (uint8_t)(bssid_bytes[i] & 0xFF);
        }
    }


    
    
    // Apply configuration through communication service layer
    if (strcmp(if_name, NETIF_NAME_WIFI_AP) == 0 && !ssid_changed && !password_changed) {
        LOG_SVC_INFO("AP mode and ssid not changed, skip configuration");
    }
    else {
        result = communication_configure_interface(if_name, &config);
        if (result != AICAM_OK) {
            return api_response_error(ctx, API_BUSINESS_ERROR_NETWORK_TIMEOUT, "Failed to configure WiFi interface");
        }
    }

    // store config to json_config_mgr(only for AP mode)
    if (strcmp(interface_str, "ap") == 0) {
        result = json_config_set_network_service_config(&network_service_config);
        if (result != AICAM_OK) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to set network service configuration");
        }
    }

    
    cJSON_Delete(request_json);
    
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to configure WiFi interface");
    }
    
    // Create response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddStringToObject(response_json, "message", "WiFi configuration updated successfully");
    cJSON_AddStringToObject(response_json, "interface", interface_str);
    cJSON_AddStringToObject(response_json, "ssid", ssid);

    
    if (strcmp(interface_str, "ap") == 0 && ap_sleep_time > 0) {
        cJSON_AddNumberToObject(response_json, "ap_sleep_time", ap_sleep_time);
    }
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "WiFi configuration updated successfully");
    //hal_mem_free(json_string);
    
    return api_result;
}

/**
 * @brief POST /api/v1/system/network/scan - Refresh network scan list
 */
aicam_result_t network_scan_refresh_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // Check if communication service is running
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    // Create immediate response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    // start scan
    start_network_scan();

    cJSON_AddStringToObject(response_json, "status", "scan_started");
    cJSON_AddStringToObject(response_json, "message", "Network scan started successfully in background task");

  
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Network scan refresh request processed");
    
    return api_result;
}

/**
 * @brief POST /api/v1/system/network/disconnect - Disconnect WiFi interface
 */
aicam_result_t network_disconnect_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // Check if communication service is running
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    // Parse JSON request body
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    
    // Extract interface type (ap or sta)
    cJSON* interface_item = cJSON_GetObjectItem(request_json, "interface");
    if (!interface_item || !cJSON_IsString(interface_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'interface' field");
    }
    
    const char* interface_str = cJSON_GetStringValue(interface_item);
    const char* if_name = NULL;
    
    if (strcmp(interface_str, "ap") == 0) {
        if_name = NETIF_NAME_WIFI_AP;
    } else if (strcmp(interface_str, "sta") == 0 || strcmp(interface_str, "wl") == 0) {
        if_name = NETIF_NAME_WIFI_STA;
    } else {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid interface type (use 'ap', 'sta', or 'wl')");
    }
    
    cJSON_Delete(request_json);
    
    // Stop the specified interface
    aicam_result_t result = communication_disconnect_network(if_name);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to disconnect WiFi interface");
    }
    
    // Create response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddStringToObject(response_json, "message", "WiFi interface disconnected successfully");
    cJSON_AddStringToObject(response_json, "interface", interface_str);
    cJSON_AddStringToObject(response_json, "if_name", if_name);
    cJSON_AddStringToObject(response_json, "status", "disconnected");
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "WiFi interface disconnected successfully");
    //hal_mem_free(json_string);
    
    return api_result;
}

/**
 * @brief POST /api/v1/system/network/delete - Delete known network
 */
aicam_result_t network_delete_known_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // Check if communication service is running
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    // Parse JSON request body
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    
    // Extract SSID
    cJSON* ssid_item = cJSON_GetObjectItem(request_json, "ssid");
    if (!ssid_item || !cJSON_IsString(ssid_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'ssid' field");
    }
    
    const char* ssid = cJSON_GetStringValue(ssid_item);
    if (strlen(ssid) == 0 || strlen(ssid) >= 32) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "SSID must be 1-31 characters");
    }
    
    // Extract BSSID
    cJSON* bssid_item = cJSON_GetObjectItem(request_json, "bssid");
    if (!bssid_item || !cJSON_IsString(bssid_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'bssid' field");
    }
    
    const char* bssid = cJSON_GetStringValue(bssid_item);
    if (strlen(bssid) == 0) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "BSSID cannot be empty");
    }
    
    // Validate BSSID format (should be XX:XX:XX:XX:XX:XX)
    if (strlen(bssid) != 17) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "BSSID must be in format XX:XX:XX:XX:XX:XX");
    }
    
    // Basic BSSID format validation
    int valid_bssid = 1;
    for (int i = 0; i < 17; i++) {
        if (i % 3 == 2) {
            if (bssid[i] != ':') {
                valid_bssid = 0;
                break;
            }
        } else {
            if (!((bssid[i] >= '0' && bssid[i] <= '9') || 
                  (bssid[i] >= 'A' && bssid[i] <= 'F') || 
                  (bssid[i] >= 'a' && bssid[i] <= 'f'))) {
                valid_bssid = 0;
                break;
            }
        }
    }
    
    if (!valid_bssid) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid BSSID format");
    }
    
    cJSON_Delete(request_json);
    
    // Delete the known network
    aicam_result_t result = communication_delete_known_network(ssid, bssid);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to delete known network");
    }
    
    // Create response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddStringToObject(response_json, "status", "deleted");
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Known network deleted successfully");
    //hal_mem_free(json_string);
    
    return api_result;
}

/* ==================== Module Registration ==================== */

/**
 * @brief Network API routes
 */
static const api_route_t network_module_routes[] = {
    {
        .path = API_PATH_PREFIX"/system/network/status",
        .method = "GET",
        .handler = network_status_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/wifi",
        .method = "POST",
        .handler = network_wifi_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/scan",
        .method = "POST",
        .handler = network_scan_refresh_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/disconnect",
        .method = "POST",
        .handler = network_disconnect_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/delete",
        .method = "POST",
        .handler = network_delete_known_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

/**
 * @brief Register network API module
 */
aicam_result_t web_api_register_network_module(void) {
    LOG_SVC_INFO("Registering Network API module...");
    
    // Register each route
    for (size_t i = 0; i < sizeof(network_module_routes) / sizeof(network_module_routes[0]); i++) {
        aicam_result_t result = http_server_register_route(&network_module_routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register route %s: %d", network_module_routes[i].path, result);
            return result;
        }
    }
    
    LOG_SVC_INFO("Network API module registered successfully (%zu routes)", 
                sizeof(network_module_routes) / sizeof(network_module_routes[0]));
    
    return AICAM_OK;
}
