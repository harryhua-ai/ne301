#ifndef __THREADING_ALT_H__
#define __THREADING_ALT_H__

#ifdef __cplusplus
extern "C" {
#endif

#if defined(MBEDTLS_THREADING_C)
#if defined(MBEDTLS_THREADING_ALT)

#include "cmsis_os2.h"

typedef struct
{
    osMutexId_t mutex;      /**< @brief CMSIS-RTOS2 mutex ID. */
} mbedtls_threading_mutex_t;

void mbedtls_threading_alt_init(void);
void mbedtls_threading_alt_deinit(void);

#endif /* MBEDTLS_THREADING_ALT */
#endif /* MBEDTLS_THREADING_C */

#ifdef __cplusplus
}
#endif

#endif /* __THREADING_ALT_H__ */

