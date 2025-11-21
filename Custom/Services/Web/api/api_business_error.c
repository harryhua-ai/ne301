/**
 * @file api_business_error.c
 * @brief Business Error Code Implementation
 * @details Implementation of business error code number and string conversion
 */

#include "api_business_error.h"
#include <string.h>

/* ==================== Error Code String Mapping ==================== */

typedef struct {
    int32_t code;
    const char* str;
} error_code_map_t;

static const error_code_map_t g_error_code_map[] = {
    { API_BUSINESS_ERROR_NONE, "NONE" },
    { API_BUSINESS_ERROR_INVALID_PASSWORD, "INVALID_PASSWORD" },
    { API_BUSINESS_ERROR_INVALID_CREDENTIALS, "INVALID_CREDENTIALS" },
    { API_BUSINESS_ERROR_UNAUTHORIZED, "UNAUTHORIZED" },
    { API_BUSINESS_ERROR_SESSION_EXPIRED, "SESSION_EXPIRED" },
    { API_BUSINESS_ERROR_TOKEN_INVALID, "TOKEN_INVALID" },
    { API_BUSINESS_ERROR_PERMISSION_DENIED, "PERMISSION_DENIED" },
    { API_BUSINESS_ERROR_INVALID_PARAM, "INVALID_PARAM" },
    { API_BUSINESS_ERROR_MISSING_PARAM, "MISSING_PARAM" },
    { API_BUSINESS_ERROR_PARAM_OUT_OF_RANGE, "PARAM_OUT_OF_RANGE" },
    { API_BUSINESS_ERROR_INVALID_FORMAT, "INVALID_FORMAT" },
    { API_BUSINESS_ERROR_DEVICE_OFFLINE, "DEVICE_OFFLINE" },
    { API_BUSINESS_ERROR_DEVICE_BUSY, "DEVICE_BUSY" },
    { API_BUSINESS_ERROR_CAMERA_ERROR, "CAMERA_ERROR" },
    { API_BUSINESS_ERROR_HARDWARE_ERROR, "HARDWARE_ERROR" },
    { API_BUSINESS_ERROR_NETWORK_ERROR, "NETWORK_ERROR" },
    { API_BUSINESS_ERROR_NETWORK_TIMEOUT, "NETWORK_TIMEOUT" },
    { API_BUSINESS_ERROR_MQTT_NOT_CONNECTED, "MQTT_NOT_CONNECTED" },
    { API_BUSINESS_ERROR_WIFI_NOT_CONNECTED, "WIFI_NOT_CONNECTED" },
    { API_BUSINESS_ERROR_STORAGE_FULL, "STORAGE_FULL" },
    { API_BUSINESS_ERROR_FILE_NOT_FOUND, "FILE_NOT_FOUND" },
    { API_BUSINESS_ERROR_INSUFFICIENT_SPACE, "INSUFFICIENT_SPACE" },
    { API_BUSINESS_ERROR_MODEL_NOT_LOADED, "MODEL_NOT_LOADED" },
    { API_BUSINESS_ERROR_MODEL_INVALID, "MODEL_INVALID" },
    { API_BUSINESS_ERROR_INFERENCE_TIMEOUT, "INFERENCE_TIMEOUT" },
    { API_BUSINESS_ERROR_CONFIG_INVALID, "CONFIG_INVALID" },
    { API_BUSINESS_ERROR_CONFIG_NOT_FOUND, "CONFIG_NOT_FOUND" },
    { API_BUSINESS_ERROR_CONFIG_UPDATE_FAILED, "CONFIG_UPDATE_FAILED" },
    { API_BUSINESS_ERROR_OPERATION_TIMEOUT, "OPERATION_TIMEOUT" },
    { API_BUSINESS_ERROR_OPERATION_FAILED, "OPERATION_FAILED" },
    { API_BUSINESS_ERROR_OPERATION_IN_PROGRESS, "OPERATION_IN_PROGRESS" },
    { API_BUSINESS_ERROR_FIRMWARE_INVALID, "FIRMWARE_INVALID" },
    { API_BUSINESS_ERROR_OTA_IN_PROGRESS, "OTA_IN_PROGRESS" },
    { API_BUSINESS_ERROR_OTA_FAILED, "OTA_FAILED" },
    { API_BUSINESS_ERROR_RESOURCE_NOT_FOUND, "RESOURCE_NOT_FOUND" },
    { API_BUSINESS_ERROR_RESOURCE_BUSY, "RESOURCE_BUSY" },
    { API_BUSINESS_ERROR_UNKNOWN, "UNKNOWN" }
};

#define ERROR_CODE_MAP_SIZE (sizeof(g_error_code_map) / sizeof(g_error_code_map[0]))

/* ==================== Public API Implementation ==================== */

const char* api_business_error_code_to_string(int32_t error_code)
{
    for (size_t i = 0; i < ERROR_CODE_MAP_SIZE; i++) {
        if (g_error_code_map[i].code == error_code) {
            return g_error_code_map[i].str;
        }
    }
    return "UNKNOWN";
}

int32_t api_business_error_string_to_code(const char* error_str)
{
    if (!error_str) {
        return -1;
    }
    
    for (size_t i = 0; i < ERROR_CODE_MAP_SIZE; i++) {
        if (strcmp(g_error_code_map[i].str, error_str) == 0) {
            return g_error_code_map[i].code;
        }
    }
    return -1;
}

