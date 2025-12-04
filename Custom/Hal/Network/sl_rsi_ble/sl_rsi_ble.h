#ifndef __SL_RSI_BLE_H__
#define __SL_RSI_BLE_H__

#ifdef __cplusplus
    extern "C" {
#endif

#include "cmsis_os2.h"
#include "aicam_types.h"

#define SL_BLE_SCAN_RESULT_MAX_COUNT                64

/// @brief BLE device information
typedef struct
{
    uint8_t addr_type;      ///< Address type.
    uint8_t addr[6];        ///< Address.
} sl_ble_device_t;
/// @brief BLE scan device information
typedef struct
{
    uint8_t addr_type;      ///< Address type.
    uint8_t addr[6];        ///< Address.
    int8_t rssi;            ///< Received signal strength.
    uint8_t adv_type;       ///< Advertisement type.
    uint8_t name[31];       ///< Device name.
} sl_ble_scan_info_t;
/// @brief BLE scan result
typedef struct
{
    uint8_t scan_count;                     ///< Number of available scan results
    sl_ble_scan_info_t *scan_info;          ///< Scan infos
} sl_ble_scan_result_t;
/// @brief BLE scan callback
typedef void (*sl_ble_scan_callback_t)(sl_ble_scan_info_t *scan_info);
/// @brief BLE scan configuration
typedef struct
{
    uint8_t scan_type;                      ///< Scan type (0x00 - passive, 0x01 - active)
    uint16_t scan_int;                      ///< Scan interval (0x0004 - 0xFFFF, unit: 0.625 ms)
    uint16_t scan_win;                      ///< Scan window (0x0004 - 0xFFFF, unit: 0.625 ms)
    uint32_t scan_duration;                 ///< Scan duration (unit: 1 ms), 0 means infinite
    int8_t rssi_threshold;                  ///< RSSI threshold (unit: dBm), only devices with RSSI >= threshold will be added, -127 means no filtering
    uint8_t accept_num;                     ///< Number of devices to accept
    sl_ble_device_t *accept_list;           ///< List of devices to accept
    sl_ble_scan_callback_t callback;        ///< Scan callback
} sl_ble_scan_config_t;

/// @brief BLE scan start
/// @param[in] config Scan configuration
/// @return 0 on success, error code on failure
int sl_ble_scan_start(sl_ble_scan_config_t *config);

/// @brief BLE scan stop
/// @return 0 on success, error code on failure
int sl_ble_scan_stop(void);

/// @brief Get current BLE scan state
/// @return 1 if scanning, 0 otherwise
uint8_t sl_ble_is_scanning(void);

/// @brief BLE scan get result, must be called after scanning has stopped
/// @return Scan result
sl_ble_scan_result_t *sl_ble_scan_get_result(void);

/// @brief BLE printf scan result
/// @param[in] scan_result Scan result
void sl_ble_printf_scan_result(sl_ble_scan_result_t *scan_result);

/// @brief BLE test commands register
/// @param None
void sl_ble_test_commands_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __SL_RSI_BLE_H__ */
