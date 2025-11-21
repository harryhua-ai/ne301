/**
 * @file aicam_error.c
 * @brief AI Camera Error Code Implementation
 * @details Error code to string conversion implementation
 */

#include "aicam_error.h"

/**
 * @brief Convert error code to string representation
 * @param error Error code to convert
 * @return const char* String representation of error code
 */
const char* aicam_error_to_string(aicam_result_t error)
{
    switch (error) {
        case AICAM_OK:                    return "Success";
        case AICAM_ERROR:                 return "Generic error";
        case AICAM_ERROR_INVALID_PARAM:   return "Invalid parameter";
        case AICAM_ERROR_NO_MEMORY:       return "Out of memory";
        case AICAM_ERROR_TIMEOUT:         return "Operation timeout";
        case AICAM_ERROR_BUSY:            return "Resource busy";
        case AICAM_ERROR_NOT_FOUND:       return "Resource not found";
        case AICAM_ERROR_NOT_SUPPORTED:   return "Operation not supported";
        case AICAM_ERROR_PERMISSION:      return "Permission denied";
        case AICAM_ERROR_IO:              return "I/O error";
        case AICAM_ERROR_NETWORK:         return "Network error";
        case AICAM_ERROR_FORMAT:          return "Format error";
        case AICAM_ERROR_CHECKSUM:        return "Checksum error";
        case AICAM_ERROR_OVERFLOW:        return "Buffer overflow";
        case AICAM_ERROR_UNDERFLOW:       return "Buffer underflow";
        case AICAM_ERROR_CORRUPTED:       return "Data corrupted";
        case AICAM_ERROR_LOCKED:          return "Resource locked";
        case AICAM_ERROR_UNAVAILABLE:     return "Service unavailable";
        case AICAM_ERROR_CANCELLED:       return "Operation cancelled";
        case AICAM_ERROR_DUPLICATE:       return "Duplicate operation";
        case AICAM_ERROR_FULL:            return "Container full";
        case AICAM_ERROR_EMPTY:           return "Container empty";
        case AICAM_ERROR_CONFIG:          return "Configuration error";
        case AICAM_ERROR_HARDWARE:        return "Hardware error";
        case AICAM_ERROR_FIRMWARE:        return "Firmware error";
        case AICAM_ERROR_PROTOCOL:        return "Protocol error";
        case AICAM_ERROR_VERSION:         return "Version incompatible";
        case AICAM_ERROR_SIGNATURE:       return "Signature verification failed";
        case AICAM_ERROR_ENCRYPTION:      return "Encryption/decryption failed";
        case AICAM_ERROR_AUTHENTICATION:  return "Authentication failed";
        case AICAM_ERROR_AUTHORIZATION:   return "Authorization failed";
        case AICAM_ERROR_QUOTA_EXCEEDED:  return "Quota exceeded";
        case AICAM_ERROR_RATE_LIMIT:      return "Rate limit exceeded";
        case AICAM_ERROR_MAINTENANCE:     return "System under maintenance";
        case AICAM_ERROR_DEPRECATED:      return "Feature deprecated";
        
        // Layer-specific errors
        case AICAM_ERROR_HAL_INIT:        return "HAL initialization failed";
        case AICAM_ERROR_HAL_CONFIG:      return "HAL configuration error";
        case AICAM_ERROR_HAL_IO:          return "HAL I/O error";
        
        case AICAM_ERROR_CORE_INIT:       return "Core initialization failed";
        case AICAM_ERROR_CORE_CONFIG:     return "Core configuration error";
        
        case AICAM_ERROR_SERVICE_INIT:    return "Service initialization failed";
        case AICAM_ERROR_SERVICE_CONFIG:  return "Service configuration error";
        
        case AICAM_ERROR_APP_INIT:        return "Application initialization failed";
        case AICAM_ERROR_APP_CONFIG:      return "Application configuration error";
        
        case AICAM_ERROR_UNKNOWN:         return "Unknown error";
        default:                          return "Undefined error";
    }
} 