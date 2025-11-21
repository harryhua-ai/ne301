/**
  ******************************************************************************
  * @file    gcm_alt.c
  * @author  GPM Application Team
  * @brief   Implementation of mbedtls_alt GCM module
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  * Copyright The Mbed TLS Contributors
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/**
  * NIST SP800-38D compliant GCM implementation
  *
  * This file implements GCM based on STM32 AES hardware crypto accelerator.
  */

/* Includes ------------------------------------------------------------------*/
#include "common.h"

#if defined(MBEDTLS_GCM_C)
#if defined(MBEDTLS_GCM_ALT)
#include <string.h>
#include "mbedtls/gcm.h"
#include "mbedtls/threading.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/error.h"

#if defined(MBEDTLS_HAL_GCM_ALT)

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
#define ST_GCM_TIMEOUT    0xFFU  /* 255 ms timeout for the crypto processor */
#define IV_LENGTH         12U    /* implementations restrict support to 96 bits */
#if defined(HW_CRYPTO_DPA_GCM) || defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
#define SEC_SUCCESS_CONSTANT  0x3AU   /* Secure Success value */
#endif /* HW_CRYPTO_DPA_GCM || HW_CRYPTO_DPA_CTR_FOR_GCM */

/* Private macro -------------------------------------------------------------*/
/*
 * 32-bit integer manipulation macros (big endian)
 */
#ifndef GET_UINT32_BE
#define GET_UINT32_BE(n,b,i)                            \
  do {                                                  \
    (n) = ( (uint32_t) (b)[(i)    ] << 24 )             \
          | ( (uint32_t) (b)[(i) + 1] << 16 )           \
          | ( (uint32_t) (b)[(i) + 2] <<  8 )           \
          | ( (uint32_t) (b)[(i) + 3]       );          \
  } while( 0 )
#endif /* !GET_UINT32_BE */

#ifndef PUT_UINT32_BE
#define PUT_UINT32_BE(n,b,i)                            \
  do {                                                  \
    (b)[(i)    ] = (unsigned char) ( (n) >> 24 );       \
    (b)[(i) + 1] = (unsigned char) ( (n) >> 16 );       \
    (b)[(i) + 2] = (unsigned char) ( (n) >>  8 );       \
    (b)[(i) + 3] = (unsigned char) ( (n)       );       \
  } while( 0 )
#endif /* !PUT_UINT32_BE */

/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

#if 0
#define GCM_DEBUG_PRINTF(ret) do { \
  printf("++++++++++++++++[GCM: %d] ret = %d\r\n", __LINE__, ret); \
} while (0)

#define GCM_DEBUG_FLAG(flag) do { \
  printf("++++++++++++++++[GCM: %d]%p %s\r\n", __LINE__, ctx, flag); \
} while (0)

#define GCM_DEBUG_HEX(tag, data, len) do { \
  printf("++++++++++++++++[GCM: %d]%p %s(%d) = ", __LINE__, ctx, tag, len); \
  printf("0x%08X%08X%08X%08X\r\n", data[0], data[1], data[2], data[3]); \
} while (0)
#else
#define GCM_DEBUG_PRINTF(ret) 
#define GCM_DEBUG_FLAG(flag)
#define GCM_DEBUG_HEX(tag, data, len)
#endif 

#if defined(MBEDTLS_THREADING_C)
static mbedtls_threading_mutex_t gcm_mutex;
static uint32_t gcm_context_count = 0;
#endif /* MBEDTLS_THREADING_C */

/*
 * Initialize a context
 */
void mbedtls_gcm_init(mbedtls_gcm_context *ctx)
{
#if defined(MBEDTLS_THREADING_C)
  __disable_irq();
  /* mutex cannot be initialized twice */
  if (gcm_context_count == 0) mbedtls_mutex_init(&gcm_mutex);
  gcm_context_count++;
  __enable_irq();
#endif /* MBEDTLS_THREADING_C */

  mbedtls_platform_zeroize(ctx, sizeof(mbedtls_gcm_context));
#if defined(HW_CRYPTO_DPA_GCM) || defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
  /* Enable SAES clock */
  __HAL_RCC_SAES_CLK_ENABLE();
#else
  /* Enable AES clock */
  __HAL_RCC_CRYP_CLK_ENABLE();
#endif /* HW_CRYPTO_DPA_GCM || HW_CRYPTO_DPA_CTR_FOR_GCM */
}

#if defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
/*
 * Precompute small multiples of H, that is set
 *      HH[i] || HL[i] = H times i,
 * where i is seen as a field element as in [MGV], ie high-order bits
 * correspond to low powers of P. The result is stored in the same way, that
 * is the high-order bit of HH corresponds to P^0 and the low-order bit of HL
 * corresponds to P^127.
 */
static int gcm_gen_table(mbedtls_gcm_context *ctx)
{
  int ret = 0;
  int i = 0;
  int j = 0;
  uint64_t hi = 0;
  uint64_t lo = 0;
  uint64_t vl = 0;
  uint64_t vh = 0;
  unsigned char h[16] = {0};

  memset(h, 0, 16);

#if defined(MBEDTLS_THREADING_C)
  if ((ret = mbedtls_mutex_lock(&gcm_mutex)) != 0)
  {
    return (ret);
  }
#endif /* MBEDTLS_THREADING_C */

  /* compute Hash_subkey = E(0) */
  if (HAL_CRYP_Encrypt(&ctx->hcryp_gcm,
                       (uint32_t *)h,
                       16,
                       (uint32_t *)h,
                       ST_GCM_TIMEOUT) != HAL_OK)
  {
    GCM_DEBUG_PRINTF(MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED);
    ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
    goto exit;
  }

  /* pack h as two 64-bits ints, big-endian */
  GET_UINT32_BE(hi, h,  0);
  GET_UINT32_BE(lo, h,  4);
  vh = (uint64_t) hi << 32 | lo;

  GET_UINT32_BE(hi, h,  8);
  GET_UINT32_BE(lo, h,  12);
  vl = (uint64_t) hi << 32 | lo;

  /* 8 = 1000 corresponds to 1 in GF(2^128) */
  ctx->HL[8] = vl;
  ctx->HH[8] = vh;

  /* 0 corresponds to 0 in GF(2^128) */
  ctx->HH[0] = 0;
  ctx->HL[0] = 0;

  for (i = 4; i > 0; i >>= 1)
  {
    uint32_t T = (vl & 1) * 0xe1000000U;
    vl  = (vh << 63) | (vl >> 1);
    vh  = (vh >> 1) ^ ((uint64_t) T << 32);

    ctx->HL[i] = vl;
    ctx->HH[i] = vh;
  }

  for (i = 2; i <= 8; i *= 2)
  {
    uint64_t *HiL = ctx->HL + i, *HiH = ctx->HH + i;
    vh = *HiH;
    vl = *HiL;
    for (j = 1; j < i; j++)
    {
      HiH[j] = vh ^ ctx->HH[j];
      HiL[j] = vl ^ ctx->HL[j];
    }
  }

exit :
  /* Free context access */
#if defined(MBEDTLS_THREADING_C)
  mbedtls_mutex_unlock(&gcm_mutex);
#endif /* MBEDTLS_THREADING_C */

  return (ret);
}
#endif /* HW_CRYPTO_DPA_CTR_FOR_GCM */

int mbedtls_gcm_setkey(mbedtls_gcm_context *ctx,
                       mbedtls_cipher_id_t cipher,
                       const unsigned char *key,
                       unsigned int keybits)
{
  int ret = 0;
  unsigned int i = 0;

  GCM_DEBUG_FLAG("mbedtls_gcm_setkey start");
  switch (keybits)
  {
#if defined(HW_CRYPTO_DPA_GCM) || defined (HW_CRYPTO_DPA_CTR_FOR_GCM)
    case 0:
      /* implicit request for using HUK */
      break;
#endif /* HW_CRYPTO_DPA_GCM || HW_CRYPTO_DPA_CTR_FOR_GCM */
    case 128:
      ctx->hcryp_gcm.Init.KeySize = CRYP_KEYSIZE_128B;
      break;

    case 192:
      ctx->hcryp_gcm.Init.KeySize = CRYP_KEYSIZE_192B;
      break;

    case 256:
      ctx->hcryp_gcm.Init.KeySize = CRYP_KEYSIZE_256B;
      break;

    default :
      ret = MBEDTLS_ERR_GCM_BAD_INPUT;
      goto exit;
  }

  /* Format and fill AES key  */
  for (i = 0; i < (keybits / 32); i++)
  {
    GET_UINT32_BE(ctx->gcm_key[i], key, 4 * i);
  }

#if defined(MBEDTLS_THREADING_C)
  if ((ret = mbedtls_mutex_lock(&gcm_mutex)) != 0)
  {
    return (ret);
  }
#endif /* MBEDTLS_THREADING_C */

  ctx->hcryp_gcm.Init.pKey = ctx->gcm_key;
  ctx->hcryp_gcm.Init.DataWidthUnit = CRYP_DATAWIDTHUNIT_BYTE;
  ctx->hcryp_gcm.Init.DataType = CRYP_BYTE_SWAP;
  ctx->hcryp_gcm.Init.KeyMode = CRYP_KEYMODE_NORMAL;
#if defined(HW_CRYPTO_DPA_GCM) || defined (HW_CRYPTO_DPA_CTR_FOR_GCM)
  ctx->hcryp_gcm.Instance = SAES;
#if defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
  ctx->hcryp_gcm.Init.Algorithm  = CRYP_AES_ECB;
#else
  ctx->hcryp_gcm.Init.Algorithm  = CRYP_AES_GCM;
#endif /* HW_CRYPTO_DPA_CTR_FOR_GCM */
  if (keybits == 0)
  {
    ctx->hcryp_gcm.Init.KeyProtection = CRYP_KEYSEL_HW;
  }
  else
  {
    ctx->hcryp_gcm.Init.KeyProtection = CRYP_KEYSEL_NORMAL;
  }

  /* Deinitializes the CRYP peripheral */
  if (HAL_CRYP_DeInit(&ctx->hcryp_gcm) != HAL_OK)
  {
    GCM_DEBUG_PRINTF(MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED);
    ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
    goto exit;
  }

  /* Enable SAES clock */
  __HAL_RCC_SAES_CLK_ENABLE();
#else
  ctx->hcryp_gcm.Instance = CRYP;
  ctx->hcryp_gcm.Init.Algorithm  = CRYP_AES_GCM;

  /* Deinitializes the CRYP peripheral */
  if (HAL_CRYP_DeInit(&ctx->hcryp_gcm) != HAL_OK)
  {
    GCM_DEBUG_PRINTF(MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED);
    ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
    goto exit;
  }

  /* Enable AES clock */
  __HAL_RCC_CRYP_CLK_ENABLE();
#endif /* HW_CRYPTO_DPA_GCM || HW_CRYPTO_DPA_CTR_FOR_GCM */

  if (HAL_CRYP_Init(&ctx->hcryp_gcm) != HAL_OK)
  {
    GCM_DEBUG_PRINTF(MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED);
    ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
    goto exit;
  }

#if defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
  if ((ret = gcm_gen_table(ctx)) != 0)
  {
    goto exit;
  }
#endif /* HW_CRYPTO_DPA_CTR_FOR_GCM */

  GCM_DEBUG_HEX("Init key", ctx->hcryp_gcm.Init.pKey, (ctx->hcryp_gcm.Init.KeySize == CRYP_KEYSIZE_128B) ? 16 : (ctx->hcryp_gcm.Init.KeySize == CRYP_KEYSIZE_192B) ? 24 : 32);
  /* allow multi-instance of CRYP use: save context for CRYP HW module CR */
#if USE_HAL_CRYP_SUSPEND_RESUME == 1U
  int hal_status = HAL_CRYP_Suspend(&ctx->hcryp_gcm);
  if (hal_status != HAL_OK) GCM_DEBUG_PRINTF(hal_status);
#endif
// #if defined(HW_CRYPTO_DPA_GCM) || defined (HW_CRYPTO_DPA_CTR_FOR_GCM)
//   ctx->ctx_save_cr = ((SAES_TypeDef *)(ctx->hcryp_gcm.Instance))->CR;
// #else
//   ctx->ctx_save_cr = ((CRYP_TypeDef *)(ctx->hcryp_gcm.Instance))->CR;
// #endif

exit :
  /* Free context access */
#if defined(MBEDTLS_THREADING_C)
  mbedtls_mutex_unlock(&gcm_mutex);
#endif /* MBEDTLS_THREADING_C */
  GCM_DEBUG_FLAG("mbedtls_gcm_setkey exit");
  return ret;
}

#if defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
/*
 * Shoup's method for multiplication use this table with
 *      last4[x] = x times P^128
 * where x and last4[x] are seen as elements of GF(2^128) as in [MGV]
 */
static const uint64_t last4[16] =
{
  0x0000, 0x1c20, 0x3840, 0x2460,
  0x7080, 0x6ca0, 0x48c0, 0x54e0,
  0xe100, 0xfd20, 0xd940, 0xc560,
  0x9180, 0x8da0, 0xa9c0, 0xb5e0
};

/*
 * Sets output to x times H using the precomputed tables.
 * x and output are seen as elements of GF(2^128) as in [MGV].
 */
static void gcm_mult(mbedtls_gcm_context *ctx, const unsigned char x[16],
                     unsigned char output[16])
{
  int i = 0;
  unsigned char lo = 0;
  unsigned char hi = 0;
  unsigned char rem = 0;
  uint64_t zh = 0;
  uint64_t zl = 0;

  lo = x[15] & 0xf;

  zh = ctx->HH[lo];
  zl = ctx->HL[lo];

  for (i = 15; i >= 0; i--)
  {
    lo = x[i] & 0xf;
    hi = (x[i] >> 4) & 0xf;

    if (i != 15)
    {
      rem = (unsigned char) zl & 0xf;
      zl = (zh << 60) | (zl >> 4);
      zh = (zh >> 4);
      zh ^= (uint64_t) last4[rem] << 48;
      zh ^= ctx->HH[lo];
      zl ^= ctx->HL[lo];

    }

    rem = (unsigned char) zl & 0xf;
    zl = (zh << 60) | (zl >> 4);
    zh = (zh >> 4);
    zh ^= (uint64_t) last4[rem] << 48;
    zh ^= ctx->HH[hi];
    zl ^= ctx->HL[hi];
  }

  PUT_UINT32_BE(zh >> 32, output, 0);
  PUT_UINT32_BE(zh, output, 4);
  PUT_UINT32_BE(zl >> 32, output, 8);
  PUT_UINT32_BE(zl, output, 12);
}
#endif /* HW_CRYPTO_DPA_CTR_FOR_GCM */

int mbedtls_gcm_starts(mbedtls_gcm_context *ctx,
                       int mode,
                       const unsigned char *iv, size_t iv_len)
{
#if !defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
  size_t i = 0;
  uint32_t *iv_p = (uint32_t *)(ctx->y);
#else
  int ret = 0;
#endif /* HW_CRYPTO_DPA_CTR_FOR_GCM */

  /* IV is limited to 2^64 bits, so 2^61 bytes */
  /* IV is not allowed to be zero length */
  if (iv_len == 0 || ((uint64_t) iv_len) >> 61 != 0)
  {
    return MBEDTLS_ERR_GCM_BAD_INPUT;
  }

  memset(ctx->y, 0x00, sizeof(ctx->y));
  memset(ctx->buf, 0x00, sizeof(ctx->buf));

  /* HW implementation restrict support to the length of 96 bits */
  if (IV_LENGTH != iv_len)
  {
    return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;
  }
  GCM_DEBUG_HEX("Starts key", ctx->hcryp_gcm.Init.pKey, (ctx->hcryp_gcm.Init.KeySize == CRYP_KEYSIZE_128B) ? 16 : (ctx->hcryp_gcm.Init.KeySize == CRYP_KEYSIZE_192B) ? 24 : 32);

  ctx->mode = mode;
  ctx->len = 0;

#if defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
#if defined(MBEDTLS_THREADING_C)
  if ((ret = mbedtls_mutex_lock(&gcm_mutex)) != 0)
  {
    return (ret);
  }
#endif /* MBEDTLS_THREADING_C */

  /* allow multi-context of CRYP use: restore context */
#if USE_HAL_CRYP_SUSPEND_RESUME == 1U
  int hal_status2 = HAL_CRYP_Resume(&ctx->hcryp_gcm);
  if (hal_status2 != HAL_OK) GCM_DEBUG_PRINTF(hal_status2);
#endif
// #if defined(HW_CRYPTO_DPA_GCM) || defined (HW_CRYPTO_DPA_CTR_FOR_GCM)
//   ((SAES_TypeDef *)(ctx->hcryp_gcm.Instance))->CR = ctx->ctx_save_cr;
// #else
//   ((CRYP_TypeDef *)(ctx->hcryp_gcm.Instance))->CR = ctx->ctx_save_cr;
// #endif

  // if (HAL_CRYP_Init(&ctx->hcryp_gcm) != HAL_OK)
  // {
  //   return MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
  // }
  ctx->add_len = 0;

  /* generate pre-counter block (Y = IV || 0exp(31) || 1) */
  memcpy(ctx->y, iv, iv_len);
  ctx->y[15] = 1;

  /* compute Single Block = E(Y)*/
  /* keep it in internal context for final computation (Authentication Tag) */
  if (HAL_CRYP_Encrypt(&ctx->hcryp_gcm,
                       (uint32_t *)ctx->y,
                       16,
                       (uint32_t *)ctx->base_ectr,
                       ST_GCM_TIMEOUT) != HAL_OK)
  {
    GCM_DEBUG_PRINTF(MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED);
    ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
    goto exit;
  }

  /* allow multi-context of CRYP : save context */
#if USE_HAL_CRYP_SUSPEND_RESUME == 1U
  int hal_status = HAL_CRYP_Suspend(&ctx->hcryp_gcm);
  if (hal_status != HAL_OK) GCM_DEBUG_PRINTF(hal_status);
#endif
// #if defined(HW_CRYPTO_DPA_GCM) || defined (HW_CRYPTO_DPA_CTR_FOR_GCM)
//   ctx->ctx_save_cr = ((SAES_TypeDef *)(ctx->hcryp_gcm.Instance))->CR;
// #else
//   ctx->ctx_save_cr = ((CRYP_TypeDef *)(ctx->hcryp_gcm.Instance))->CR;
// #endif
exit :
  /* Free context access */
  #if defined(MBEDTLS_THREADING_C)
    mbedtls_mutex_unlock(&gcm_mutex);
  #endif /* MBEDTLS_THREADING_C */

  return (ret);
#else
  /* Set IV with invert endianness */
  for (i = 0; i < iv_len / 4U; i++)
  {
    GET_UINT32_BE(iv_p[i], iv, 4 * i);
  }

  /* counter value must be set to 2 when processing the first block of payload */
  iv_p[3] = 0x00000002;

  ctx->hcryp_gcm.Init.pInitVect = (uint32_t *)ctx->y;

  /* Do not Allow IV reconfiguration at every gcm update */
  ctx->hcryp_gcm.Init.KeyIVConfigSkip = CRYP_KEYIVCONFIG_ONCE;

#endif /* HW_CRYPTO_DPA_CTR_FOR_GCM */

  return 0;
}

/**
  * mbedtls_gcm_context::buf contains the partial state of the computation of
  * the authentication tag.
  * mbedtls_gcm_context::add_len and mbedtls_gcm_context::len indicate
  * different stages of the computation:
  *     * len == 0 && add_len == 0:      initial state
  *     * len == 0 && add_len % 16 != 0: the first `add_len % 16` bytes have
  *                                      a partial block of AD that has been
  *                                      xored in but not yet multiplied in.
  *     * len == 0 && add_len % 16 == 0: the authentication tag is correct if
  *                                      the data ends now.
  *     * len % 16 != 0:                 the first `len % 16` bytes have
  *                                      a partial block of ciphertext that has
  *                                      been xored in but not yet multiplied in.
  *     * len > 0 && len % 16 == 0:      the authentication tag is correct if
  *                                      the data ends now.
  */
int mbedtls_gcm_update_ad(mbedtls_gcm_context *ctx,
                          const unsigned char *add, size_t add_len)
{
#if defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
  const unsigned char *p;
  size_t use_len;
  size_t i = 0;
#endif /* HW_CRYPTO_DPA_CTR_FOR_GCM */

  /* additional authentication data is limited to 2^64 bits, so 2^61 bytes */
  if ((uint64_t) add_len >> 61 != 0)
  {
    return MBEDTLS_ERR_GCM_BAD_INPUT;
  }

  /* allow multi-context of CRYP use: restore context */
// #if USE_HAL_CRYP_SUSPEND_RESUME == 1U
  // int hal_status2 = HAL_CRYP_Resume(&ctx->hcryp_gcm);
  // if (hal_status2 != HAL_OK) GCM_DEBUG_PRINTF(hal_status2);
// #endif
// #if defined(HW_CRYPTO_DPA_GCM) || defined (HW_CRYPTO_DPA_CTR_FOR_GCM)
//   ((SAES_TypeDef *)(ctx->hcryp_gcm.Instance))->CR = ctx->ctx_save_cr;
// #else
//   ((CRYP_TypeDef *)(ctx->hcryp_gcm.Instance))->CR = ctx->ctx_save_cr;
// #endif

#if defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
  ctx->add_len = add_len;
  p = add;
  while (add_len > 0)
  {
    use_len = (add_len < 16) ? add_len : 16;

    for (i = 0; i < use_len; i++)
    {
      ctx->buf[i] ^= p[i];
    }

    gcm_mult(ctx, ctx->buf, ctx->buf);

    add_len -= use_len;
    p += use_len;
  }
#else

  if (add_len != 0)
  {
    ctx->hcryp_gcm.Init.Header = (uint32_t *)add;
    /* header buffer in byte length */
    ctx->hcryp_gcm.Init.HeaderSize = (uint32_t)add_len;
  }
  else
  {
    ctx->hcryp_gcm.Init.Header = NULL;
    ctx->hcryp_gcm.Init.HeaderSize = 0;
  }

  /* Additional Authentication Data in bytes unit */
  ctx->hcryp_gcm.Init.HeaderWidthUnit = CRYP_HEADERWIDTHUNIT_BYTE;

  /* reconfigure the CRYP */
  // if (HAL_CRYP_SetConfig(&ctx->hcryp_gcm, &ctx->hcryp_gcm.Init) != HAL_OK)
  // {
  //   return MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
  // }
#endif /* HW_CRYPTO_DPA_CTR_FOR_GCM */

  /* allow multi-context of CRYP : save context */
// #if USE_HAL_CRYP_SUSPEND_RESUME == 1U
//   int hal_status = HAL_CRYP_Suspend(&ctx->hcryp_gcm);
//  if (hal_status != HAL_OK) GCM_DEBUG_PRINTF(hal_status);
// #endif
// #if defined(HW_CRYPTO_DPA_GCM) || defined (HW_CRYPTO_DPA_CTR_FOR_GCM)
//   ctx->ctx_save_cr = ((SAES_TypeDef *)(ctx->hcryp_gcm.Instance))->CR;
// #else
//   ctx->ctx_save_cr = ((CRYP_TypeDef *)(ctx->hcryp_gcm.Instance))->CR;
// #endif

  return 0;
}

int mbedtls_gcm_update(mbedtls_gcm_context *ctx,
                       const unsigned char *input, size_t input_length,
                       unsigned char *output, size_t output_size,
                       size_t *output_length)
{
  int ret = 0;
#if defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
  unsigned char ectr[16] = {0};
  size_t i = 0;
  const unsigned char *p;
  unsigned char *out_p = output;
  size_t use_len = 0;
#else
  uint16_t wordnb = 0;          /* number of four data words */
  uint16_t wordlen = 0;         /* length (in bytes) of four data words */
  uint16_t in_datalen = 0;  /* length (in bytes) of processed data within input buffer */
  __ALIGN_BEGIN unsigned char work_buf[16] __ALIGN_END;
  uint16_t work_buf_len = 0;
#endif /* HW_CRYPTO_DPA_CTR_FOR_GCM */
  size_t tmp_lenght = 0;

  tmp_lenght = ctx->len;

  // printf("in(%p): %lu, out(%p): %lu, clen %lu\r\n", input, input_length, output, output_size, ctx->len);
  if ((output > input) && ((size_t)(output - input) < input_length))
  {
    return MBEDTLS_ERR_GCM_BAD_INPUT;
  }

  /* Total length is restricted to 2^39 - 256 bits, ie 2^36 - 2^5 bytes
   * Also check for possible overflow */
  if (((ctx->len + input_length) < ctx->len) ||
      (((uint64_t) ctx->len + input_length) > 0xFFFFFFFE0ull))
  {
    return MBEDTLS_ERR_GCM_BAD_INPUT;
  }

#if defined(MBEDTLS_THREADING_C)
  if ((ret = mbedtls_mutex_lock(&gcm_mutex)) != 0)
  {
    return (ret);
  }
#endif /* MBEDTLS_THREADING_C */

  /* allow multi-context of CRYP use: restore context */
#if USE_HAL_CRYP_SUSPEND_RESUME == 1U
  int hal_status2 = HAL_CRYP_Resume(&ctx->hcryp_gcm);
  if (hal_status2 != HAL_OK) GCM_DEBUG_PRINTF(hal_status2);
#endif
  GCM_DEBUG_HEX("Update key", ctx->hcryp_gcm.Init.pKey, (ctx->hcryp_gcm.Init.KeySize == CRYP_KEYSIZE_128B) ? 16 : (ctx->hcryp_gcm.Init.KeySize == CRYP_KEYSIZE_192B) ? 24 : 32);
// #if defined(HW_CRYPTO_DPA_GCM) || defined (HW_CRYPTO_DPA_CTR_FOR_GCM)
//   ((SAES_TypeDef *)(ctx->hcryp_gcm.Instance))->CR = ctx->ctx_save_cr;
// #else
//   ((CRYP_TypeDef *)(ctx->hcryp_gcm.Instance))->CR = ctx->ctx_save_cr;
// #endif

#if defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
  p = input;
  ctx->len += input_length;
  while (input_length > 0)
  {
    use_len = (input_length < 16) ? input_length : 16;

    /* increment counter */
    for (i = 16; i > 12; i--)
    {
      ++ctx->y[i - 1];
      if (ctx->y[i - 1] != 0)
      {
        break;
      }
    }

    if (HAL_CRYP_Encrypt(&ctx->hcryp_gcm,
                         (uint32_t *)ctx->y,
                         16,
                         (uint32_t *)ectr,
                         ST_GCM_TIMEOUT) != HAL_OK)
    {
      GCM_DEBUG_PRINTF(MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED);
      ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
      goto exit;
    }

    for (i = 0; i < use_len; i++)
    {
      if (ctx->mode == MBEDTLS_GCM_DECRYPT)
      {
        ctx->buf[i] ^= p[i];
      }
      out_p[i] = ectr[i] ^ p[i];
      if (ctx->mode == MBEDTLS_GCM_ENCRYPT)
      {
        ctx->buf[i] ^= out_p[i];
      }
    }

    gcm_mult(ctx, ctx->buf, ctx->buf);

    input_length -= use_len;
    p += use_len;
    out_p += use_len;
  }
#else
  /* Calculate number of four data words */
  wordnb = input_length / 16U;

  /* if available, process them */
  if (wordnb)
  {
    /* Convert in bytes */
    wordlen = wordnb * 16U;

    if (ctx->mode == MBEDTLS_GCM_DECRYPT)
    {
      if (HAL_CRYP_Decrypt(&ctx->hcryp_gcm,
                           (uint32_t *)input,
                           wordlen,
                           (uint32_t *)output,
                           ST_GCM_TIMEOUT) != HAL_OK)
      {
        GCM_DEBUG_PRINTF(MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED);
        ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
        goto exit;
      }
    }
    else
    {
      if (HAL_CRYP_Encrypt(&ctx->hcryp_gcm,
                           (uint32_t *)input,
                           wordlen,
                           (uint32_t *)output,
                           ST_GCM_TIMEOUT) != HAL_OK)
      {
        GCM_DEBUG_PRINTF(MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED);
        ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
        goto exit;
      }
    }

    /* update total length */
    ctx->len += wordlen;

    in_datalen += wordlen;

    if (in_datalen < input_length)
    {
      /* Process them into a last four data word */
      goto last_data_word;
    }
    else
    {
      goto exit;
    }
  }

last_data_word:
  /* Calculate remaining bytes */
  /* Can have a null length when payload is omitted (GMAC) */
  work_buf_len = (uint16_t)((input_length - in_datalen) % 16U);

  if (work_buf_len) {
    memset(work_buf, 0, sizeof(work_buf));
    memcpy(work_buf, input + in_datalen, work_buf_len);

    if (ctx->mode == MBEDTLS_GCM_DECRYPT)
    {
      if (HAL_CRYP_Decrypt(&ctx->hcryp_gcm,
                          (uint32_t *)work_buf,
                          work_buf_len,
                          (uint32_t *)(output + in_datalen),
                          ST_GCM_TIMEOUT) != HAL_OK)
      {
        GCM_DEBUG_PRINTF(MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED);
        ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
        goto exit;
      }
    }
    else
    {
      if (HAL_CRYP_Encrypt(&ctx->hcryp_gcm,
                          (uint32_t *)work_buf,
                          work_buf_len,
                          (uint32_t *)(output + in_datalen),
                          ST_GCM_TIMEOUT) != HAL_OK)
      {
        GCM_DEBUG_PRINTF(MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED);
        ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
        goto exit;
      }
    }
  }

  /* update total length */
  ctx->len += work_buf_len;

#endif /* HW_CRYPTO_DPA_CTR_FOR_GCM */

  *output_length = ctx->len - tmp_lenght;

  /* allow multi-context of CRYP : save context */
#if USE_HAL_CRYP_SUSPEND_RESUME == 1U
  int hal_status = HAL_CRYP_Suspend(&ctx->hcryp_gcm);
  if (hal_status != HAL_OK) GCM_DEBUG_PRINTF(hal_status);
#endif
// #if defined(HW_CRYPTO_DPA_GCM) || defined (HW_CRYPTO_DPA_CTR_FOR_GCM)
//   ctx->ctx_save_cr = ((SAES_TypeDef *)(ctx->hcryp_gcm.Instance))->CR;
// #else
//   ctx->ctx_save_cr = ((CRYP_TypeDef *)(ctx->hcryp_gcm.Instance))->CR;
// #endif
exit:
  /* Free context access */
#if defined(MBEDTLS_THREADING_C)
  mbedtls_mutex_unlock(&gcm_mutex);
#endif /* MBEDTLS_THREADING_C */

  return ret;
}

int mbedtls_gcm_finish(mbedtls_gcm_context *ctx,
                       unsigned char *output, size_t output_size,
                       size_t *output_length,
                       unsigned char *tag, size_t tag_len)
{
  int ret = 0;
#if defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
  unsigned char work_buf[16] = {0};
  size_t i = 0;
  uint64_t orig_len = 0;
  uint64_t orig_add_len = 0;
#else
  __ALIGN_BEGIN uint8_t mac[16]      __ALIGN_END; /* temporary mac */
#endif /* HW_CRYPTO_DPA_CTR_FOR_GCM */

  if (tag_len > 16 || tag_len < 4)
  {
    return MBEDTLS_ERR_GCM_BAD_INPUT;
  }

#if defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
  orig_len = ctx->len * 8;
  orig_add_len = ctx->add_len * 8;

  memcpy(tag, ctx->base_ectr, tag_len);

  if (orig_len || orig_add_len)
  {
    memset(work_buf, 0x00, 16);

    PUT_UINT32_BE((orig_add_len >> 32), work_buf, 0);
    PUT_UINT32_BE((orig_add_len), work_buf, 4);
    PUT_UINT32_BE((orig_len     >> 32), work_buf, 8);
    PUT_UINT32_BE((orig_len), work_buf, 12);

    for (i = 0; i < 16; i++)
    {
      ctx->buf[i] ^= work_buf[i];
    }

    gcm_mult(ctx, ctx->buf, ctx->buf);

    for (i = 0; i < tag_len; i++)
    {
      tag[i] ^= ctx->buf[i];
    }
  }
#else
  /* implementation restrict support to a 16 bytes tag buffer */
  if (tag_len != 16)
  {
    return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;
  }

#if defined(MBEDTLS_THREADING_C)
  if ((ret = mbedtls_mutex_lock(&gcm_mutex)) != 0)
  {
    return (ret);
  }
#endif /* MBEDTLS_THREADING_C */

  /* allow multi-context of CRYP use: restore context */
#if USE_HAL_CRYP_SUSPEND_RESUME == 1U
  int hal_status2 = HAL_CRYP_Resume(&ctx->hcryp_gcm);
  if (hal_status2 != HAL_OK) GCM_DEBUG_PRINTF(hal_status2);
#endif
  GCM_DEBUG_HEX("Finish key", ctx->hcryp_gcm.Init.pKey, (ctx->hcryp_gcm.Init.KeySize == CRYP_KEYSIZE_128B) ? 16 : (ctx->hcryp_gcm.Init.KeySize == CRYP_KEYSIZE_192B) ? 24 : 32);
// #if defined(HW_CRYPTO_DPA_GCM) || defined (HW_CRYPTO_DPA_CTR_FOR_GCM)
//   ((SAES_TypeDef *)(ctx->hcryp_gcm.Instance))->CR = ctx->ctx_save_cr;
// #else
//   ((CRYP_TypeDef *)(ctx->hcryp_gcm.Instance))->CR = ctx->ctx_save_cr;
// #endif

  /* Tag has a variable length */
  memset(mac, 0, sizeof(mac));

  /* Generate the authentication TAG */
  if (HAL_CRYPEx_AESGCM_GenerateAuthTAG(&ctx->hcryp_gcm,
                                        (uint32_t *)mac,
                                        ST_GCM_TIMEOUT) != HAL_OK)
  {
    GCM_DEBUG_PRINTF(MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED);
    ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
    goto exit;
  }

  memcpy(tag, mac, tag_len);

  /* allow multi-context of CRYP : save context */
#if USE_HAL_CRYP_SUSPEND_RESUME == 1U
  int hal_status = HAL_CRYP_Suspend(&ctx->hcryp_gcm);
  if (hal_status != HAL_OK) GCM_DEBUG_PRINTF(hal_status);
#endif
// #if defined(HW_CRYPTO_DPA_GCM) || defined (HW_CRYPTO_DPA_CTR_FOR_GCM)
//   ctx->ctx_save_cr = ((SAES_TypeDef *)(ctx->hcryp_gcm.Instance))->CR;
// #else
//   ctx->ctx_save_cr = ((CRYP_TypeDef *)(ctx->hcryp_gcm.Instance))->CR;
// #endif

exit :
  /* Free context access */
#if defined(MBEDTLS_THREADING_C)
  mbedtls_mutex_unlock(&gcm_mutex);
#endif /* MBEDTLS_THREADING_C */

#endif /* HW_CRYPTO_DPA_CTR_FOR_GCM */

  // mbedtls_platform_zeroize(ctx->gcm_key, sizeof(ctx->gcm_key));

  return ret;
}

int mbedtls_gcm_crypt_and_tag(mbedtls_gcm_context *ctx,
                              int mode,
                              size_t length,
                              const unsigned char *iv,
                              size_t iv_len,
                              const unsigned char *add,
                              size_t add_len,
                              const unsigned char *input,
                              unsigned char *output,
                              size_t tag_len,
                              unsigned char *tag)
{
  int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
  size_t olen = 0;

  if ((ret = mbedtls_gcm_starts(ctx, mode, iv, iv_len)) != 0)
  {
    return ret;
  }

  if ((ret = mbedtls_gcm_update_ad(ctx, add, add_len)) != 0)
  {
    return ret;
  }

  if ((ret = mbedtls_gcm_update(ctx, input, length,
                                output, length, &olen)) != 0)
  {
    return ret;
  }

  if ((ret = mbedtls_gcm_finish(ctx, NULL, 0, &olen, tag, tag_len)) != 0)
  {
    return ret;
  }

  return 0;
}

int mbedtls_gcm_auth_decrypt(mbedtls_gcm_context *ctx,
                             size_t length,
                             const unsigned char *iv,
                             size_t iv_len,
                             const unsigned char *add,
                             size_t add_len,
                             const unsigned char *tag,
                             size_t tag_len,
                             const unsigned char *input,
                             unsigned char *output)
{
  int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
  unsigned char check_tag[16] = {0};
  size_t i = 0;
  unsigned char diff = 0;
#if defined(HW_CRYPTO_DPA_GCM) || defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
  size_t j = 0;
  volatile unsigned char inv_diff = 0xFF;
#endif /* HW_CRYPTO_DPA_GCM  || HW_CRYPTO_DPA_CTR_FOR_GCM */

  ret = mbedtls_gcm_crypt_and_tag(ctx, MBEDTLS_GCM_DECRYPT, length,
                                  iv, iv_len, add, add_len,
                                  input, output, tag_len, check_tag);
  if (ret != 0)
  {
    return ret;
  }
  
#if defined(HW_CRYPTO_DPA_GCM) || defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
  j = tag_len;

  /* Check tag in "constant-time" */
  for (i = 0; i < tag_len; i++)
  {
    diff |= tag[i] ^ SEC_SUCCESS_CONSTANT ^ check_tag[i];
    inv_diff &= tag[i] ^ SEC_SUCCESS_CONSTANT ^ check_tag[i];
    j--;
  }

  /* Check loop is executed ("not skipped" due to attacks) */
  if ((i != tag_len) || (j != 0) || (0 == tag_len))
  {
    mbedtls_platform_zeroize(output, length);
    return MBEDTLS_ERR_GCM_AUTH_FAILED;
  }

  if ((diff != SEC_SUCCESS_CONSTANT) || (inv_diff != SEC_SUCCESS_CONSTANT))
  {
    mbedtls_platform_zeroize(output, length);
    return MBEDTLS_ERR_GCM_AUTH_FAILED;
  }

  return ((diff ^ inv_diff) + j);
#else
  /* Check tag in "constant-time" */
  for (diff = 0, i = 0; i < tag_len; i++)
  {
    diff |= tag[i] ^ check_tag[i];
  }

  if (diff != 0)
  {
    mbedtls_platform_zeroize(output, length);
    return MBEDTLS_ERR_GCM_AUTH_FAILED;
  }

  return 0;
#endif /* HW_CRYPTO_DPA_GCM || HW_CRYPTO_DPA_CTR_FOR_GCM */
}

void mbedtls_gcm_free(mbedtls_gcm_context *ctx)
{
  if (ctx == NULL)
  {
    return;
  }

#if defined(MBEDTLS_THREADING_C)
  __disable_irq();
  if (gcm_context_count > 0) {
    gcm_context_count--;
    if (gcm_context_count == 0) {
      mbedtls_mutex_free(&gcm_mutex);
    #if defined(HW_CRYPTO_DPA_GCM) || defined(HW_CRYPTO_DPA_CTR_FOR_GCM)
      if (ctx->hcryp_gcm.Instance == SAES)
    #else
      if (ctx->hcryp_gcm.Instance == CRYP)
    #endif
        HAL_CRYP_DeInit(&ctx->hcryp_gcm);
    }
  }
  __enable_irq();
#endif /* MBEDTLS_THREADING_C */
  
  mbedtls_platform_zeroize(ctx, sizeof(mbedtls_gcm_context));
}

#endif /* MBEDTLS_HAL_GCM_ALT */

#endif /* MBEDTLS_GCM_ALT */
#endif /* MBEDTLS_GCM_C */
