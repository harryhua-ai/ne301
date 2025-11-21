/**
 * @file core_example.h
 * @brief L2 Core System Usage Example Header File
 */

#ifndef CORE_EXAMPLE_H
#define CORE_EXAMPLE_H

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run complete L2 core system example
 * @details This function demonstrates how to initialize and use various modules of the L2 core system
 * @return aicam_result_t Operation result
 */
aicam_result_t run_core_system_example(void);

/**
 * @brief Register system error handler
 */
void example_register_error_handler(void);

#ifdef __cplusplus
}
#endif

#endif // CORE_EXAMPLE_H 