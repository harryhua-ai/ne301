/**
 * @file aicam_error.h
 * @brief AI Camera Error Code Definitions
 * @details Error code definitions and utilities for the AI Camera system
 */

#ifndef AICAM_ERROR_H
#define AICAM_ERROR_H

#include "aicam_types.h"

/* 
 * Note: All error codes are now defined in aicam_types.h as part of aicam_result_t enum
 * This file only contains utility macros and functions
 */

/* Error code checking macros */
#define AICAM_IS_OK(result)        ((result) == AICAM_OK)
#define AICAM_IS_ERROR(result)     ((result) != AICAM_OK)

/* Error code to string conversion function */
const char* aicam_error_to_string(aicam_result_t error);

#endif /* AICAM_ERROR_H */ 