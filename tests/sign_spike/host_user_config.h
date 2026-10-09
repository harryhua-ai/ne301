/*
 * host_user_config.h — host-side mbedTLS configuration for the Issue #32
 * offline signing feasibility spike.
 *
 * Strategy: include the firmware's REAL mbedTLS configuration
 * (Custom/Common/Lib/MbedTLS/configs/config-ccm-psk-tls1_2.h, which is what
 * Appli/Makefile:648 selects via -DMBEDTLS_CONFIG_FILE and which itself
 * tail-includes Custom/Common/Lib/mmx108/morselib/include/mm_mbedtls_config.h)
 * unmodified, then adapt ONLY the hardware/platform hooks that cannot exist on
 * a macOS host. Every #undef below is a documented deviation; the resulting
 * module set (which crypto modules are compiled in) is IDENTICAL to the
 * firmware-resolved configuration — only "which implementation backs them"
 * differs (software from the same sources under library/ instead of the
 * STM32N6 PKA/CRYP/HASH ALT shims in Custom/Common/Lib/MbedTLS/port/).
 */
#ifndef SIGN_SPIKE_HOST_USER_CONFIG_H
#define SIGN_SPIKE_HOST_USER_CONFIG_H

/* The firmware configuration, unmodified (including its "mem.h" include and
 * its mmx108 morselib tail include). mem.h is resolved by -Ishim first. */
#include "config-ccm-psk-tls1_2.h"

/* ---------------------------------------------------------------------------
 * Deviation 1: STM32N6 hardware accelerator ALT hooks (config lines 37-52,
 * 112-116, 139-146). There is no PKA/CRYP/HASH peripheral on the host, and
 * the ALT implementations live in Custom/Common/Lib/MbedTLS/port/ (files
 * ending _alt.c) which are firmware-only translation units. Undefining the
 * ALT hooks makes mbedTLS compile the generic software implementations from
 * the SAME library/ sources
 * sources (e.g. library/ecdsa.c:483 mbedtls_ecdsa_verify is compiled only
 * when MBEDTLS_ECDSA_VERIFY_ALT is NOT defined).
 * ------------------------------------------------------------------------- */
#undef MBEDTLS_AES_ALT
#undef MBEDTLS_HAL_AES_ALT
#undef MBEDTLS_SHA1_ALT
#undef MBEDTLS_HAL_SHA1_ALT
#undef MBEDTLS_SHA256_ALT
#undef MBEDTLS_HAL_SHA256_ALT
#undef MBEDTLS_RSA_ALT
#undef MBEDTLS_HAL_RSA_ALT
#undef MBEDTLS_GCM_ALT
#undef MBEDTLS_HAL_GCM_ALT
#undef MBEDTLS_ECP_INTERNAL_ALT
#undef MBEDTLS_ECP_NORMALIZE_JAC_ALT
#undef MBEDTLS_ECP_DOUBLE_JAC_ALT
#undef MBEDTLS_ECP_ADD_MIXED_ALT
#undef MBEDTLS_ECDH_GEN_PUBLIC_ALT
#undef MBEDTLS_ECDH_COMPUTE_SHARED_ALT
#undef MBEDTLS_ECDSA_SIGN_ALT
#undef MBEDTLS_ECDSA_VERIFY_ALT

/* ---------------------------------------------------------------------------
 * Deviation 2: firmware memory allocator (config lines 128-130 map mbedTLS
 * calloc/free onto hal_mem_calloc_large / hal_mem_free). Undefining the two
 * macro hooks falls back to mbedTLS's default libc-backed std allocators.
 * ------------------------------------------------------------------------- */
#undef MBEDTLS_PLATFORM_CALLOC_MACRO
#undef MBEDTLS_PLATFORM_FREE_MACRO

/* ---------------------------------------------------------------------------
 * Deviation 3: threading (config lines 124-125 use MBEDTLS_THREADING_ALT with
 * an RTOS-backed port/threading_alt.c). The spike is single-threaded.
 * ------------------------------------------------------------------------- */
#undef MBEDTLS_THREADING_C
#undef MBEDTLS_THREADING_ALT

#endif /* SIGN_SPIKE_HOST_USER_CONFIG_H */
