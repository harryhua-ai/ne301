/**
 * @file communication_service.h
 * @brief Communication Service Interface Header
 * @details communication service standard interface definition, support network interface information collection and configuration management
 */

#ifndef COMMUNICATION_SERVICE_H
#define COMMUNICATION_SERVICE_H

#include "aicam_types.h"
#include "service_interfaces.h"
#include "netif_manager.h"
#include "json_config_mgr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Communication Service Configuration ==================== */

/**
 * @brief Communication service configuration structure
 */
typedef struct {
    aicam_bool_t auto_start_wifi_ap;        // Auto start WiFi AP mode
    aicam_bool_t auto_start_wifi_sta;       // Auto start WiFi STA mode
    aicam_bool_t enable_network_scan;       // Enable network scanning
    aicam_bool_t enable_auto_reconnect;     // Enable auto reconnection
    uint32_t reconnect_interval_ms;         // Reconnection interval in milliseconds
    uint32_t scan_interval_ms;              // Network scan interval in milliseconds
    uint32_t connection_timeout_ms;         // Connection timeout in milliseconds
    aicam_bool_t enable_debug;              // Enable debug logging
    aicam_bool_t enable_stats;              // Enable statistics logging
} communication_service_config_t;

/**
 * @brief Network interface status
 */
typedef struct {
    netif_state_t state;                    // Interface state
    netif_type_t type;                      // Interface type
    char if_name[16];                       // Interface name
    char ssid[32];                          // WiFi SSID (for wireless interfaces)
    char ip_addr[16];                       // IP address
    char mac_addr[18];                      // MAC address
    int32_t rssi;                           // Signal strength (for wireless)
    uint32_t channel;                       // WiFi channel (for wireless)
    aicam_bool_t connected;                 // Connection status
} network_interface_status_t;

/**
 * @brief Communication service statistics
 */
typedef struct {
    uint64_t total_connections;             // Total connection attempts
    uint64_t successful_connections;        // Successful connections
    uint64_t failed_connections;            // Failed connections
    uint64_t disconnections;                // Total disconnections
    uint64_t network_scans;                 // Total network scans performed
    uint64_t bytes_sent;                    // Total bytes sent
    uint64_t bytes_received;                // Total bytes received
    uint32_t current_connections;           // Current active connections
    uint32_t last_error_code;               // Last error code
} communication_service_stats_t;

/**
 * @brief Network scan results with known/unknown classification
 */
typedef struct {
    network_scan_result_t known_networks[16];    // Known networks
    network_scan_result_t unknown_networks[16];  // Unknown networks
    uint32_t known_count;                        // Number of known networks
    uint32_t unknown_count;                      // Number of unknown networks
} classified_scan_results_t;

/* ==================== Communication Service Interface Functions ==================== */

/**
 * @brief Start network scan
 */
void start_network_scan(void);


/**
 * @brief Initialize communication service
 * @param config Service configuration (optional)
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_service_init(void *config);

/**
 * @brief Start communication service
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_service_start(void);

/**
 * @brief Stop communication service
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_service_stop(void);

/**
 * @brief Deinitialize communication service
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_service_deinit(void);

/**
 * @brief Get communication service state
 * @return service_state_t Service state
 */
service_state_t communication_service_get_state(void);

/* ==================== Network Interface Management ==================== */

/**
 * @brief Get network interface list
 * @param interfaces Array to store interface information
 * @param max_count Maximum number of interfaces to return
 * @param actual_count Actual number of interfaces returned
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_network_interfaces(network_interface_status_t *interfaces, 
                                                   uint32_t max_count, 
                                                   uint32_t *actual_count);

/**
 * @brief Get specific network interface status
 * @param if_name Interface name
 * @param status Interface status structure
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_interface_status(const char *if_name, 
                                                 network_interface_status_t *status);

/**
 * @brief Check if network interface is connected
 * @param if_name Interface name
 * @return aicam_bool_t TRUE if connected, FALSE otherwise
 */
aicam_bool_t communication_is_interface_connected(const char *if_name);

/**
 * @brief Get network interface configuration
 * @param if_name Interface name
 * @param config Buffer to store interface configuration
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_interface_config(const char *if_name, 
                                                 netif_config_t *config);

/**
 * @brief Configure network interface
 * @param if_name Interface name
 * @param config Interface configuration
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_configure_interface(const char *if_name, 
                                                 netif_config_t *config);

/**
 * @brief Start network interface
 * @param if_name Interface name
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_start_interface(const char *if_name);

/**
 * @brief Stop network interface
 * @param if_name Interface name
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_stop_interface(const char *if_name);

/**
 * @brief Restart network interface
 * @param if_name Interface name
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_restart_interface(const char *if_name);

/**
 * @brief Disconnect network
 * @param if_name Interface name
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_disconnect_network(const char *if_name);

/* ==================== Network Scanning ==================== */

/**
 * @brief Start network scan
 * @param callback Callback function for scan results
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_start_network_scan(wireless_scan_callback_t callback);

/**
 * @brief Get last scan results
 * @param results Array to store scan results
 * @param max_count Maximum number of results to return
 * @param actual_count Actual number of results returned
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_scan_results(network_scan_result_t *results, 
                                             uint32_t max_count, 
                                             uint32_t *actual_count);

/**
 * @brief Get classified network scan results (known/unknown)
 * @param results Buffer to store classified scan results
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_classified_scan_results(classified_scan_results_t *results);

/**
 * @brief Get known networks only
 * @param results Buffer to store known network results
 * @param max_count Maximum number of results to return
 * @param actual_count Actual number of results returned
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_known_networks(network_scan_result_t *results, 
                                               uint32_t max_count, 
                                               uint32_t *actual_count);

/**
 * @brief Get unknown networks only
 * @param results Buffer to store unknown network results
 * @param max_count Maximum number of results to return
 * @param actual_count Actual number of results returned
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_unknown_networks(network_scan_result_t *results, 
                                                 uint32_t max_count, 
                                                 uint32_t *actual_count);

/**
 * @brief Delete a known network from the database
 * @param ssid Network SSID
 * @param bssid Network BSSID
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_delete_known_network(const char *ssid, const char *bssid);

/* ==================== Service Management ==================== */

/**
 * @brief Get communication service configuration
 * @param config Configuration structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_config(communication_service_config_t *config);

/**
 * @brief Set communication service configuration
 * @param config Configuration structure
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_set_config(const communication_service_config_t *config);

/**
 * @brief Get communication service statistics
 * @param stats Statistics structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_stats(communication_service_stats_t *stats);

/**
 * @brief Reset communication service statistics
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_reset_stats(void);

/**
 * @brief Check if communication service is running
 * @return aicam_bool_t TRUE if running, FALSE otherwise
 */
aicam_bool_t communication_is_running(void);

/**
 * @brief Get communication service version
 * @return const char* Version string
 */
const char* communication_get_version(void);

/**
 * @brief Register CLI commands
 */
void comm_cmd_register(void);

#ifdef __cplusplus
}
#endif

#endif /* COMMUNICATION_SERVICE_H */
