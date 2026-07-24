/**
 * @file api_people_counting_module.h
 * @brief People Counting API Module Header
 * @details REST API for people-counting config, stats, reset, and backlog inspection
 */
#ifndef API_PEOPLE_COUNTING_MODULE_H
#define API_PEOPLE_COUNTING_MODULE_H

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register People Counting API module
 * @return aicam_result_t Operation result
 */
aicam_result_t web_api_register_people_counting_module(void);

#ifdef __cplusplus
}
#endif

#endif /* API_PEOPLE_COUNTING_MODULE_H */
