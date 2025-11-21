/* Includes ------------------------------------------------------------------*/
#include "os04c10_reg.h"

/** @addtogroup BSP
  * @{
  */

/** @addtogroup Components
  * @{
  */

/** @addtogroup OS04C10
  * @brief     This file provides a set of functions needed to drive the
  *            OS04C10 Camera sensor.
  * @{
  */

/**
  * @brief  Read OS04C10 component registers
  * @param  ctx component context
  * @param  reg Register to read from
  * @param  pdata Pointer to data buffer
  * @param  length Number of data to read
  * @retval Component status
  */
int32_t os04c10_read_reg(os04c10_ctx_t *ctx, uint16_t reg, uint8_t *pdata, uint16_t length)
{
  return ctx->ReadReg(ctx->handle, reg, pdata, length);
}

/**
  * @brief  Write OS04C10 component registers
  * @param  ctx component context
  * @param  reg Register to write to
  * @param  pdata Pointer to data buffer
  * @param  length Number of data to write
  * @retval Component status
  */
int32_t os04c10_write_reg(os04c10_ctx_t *ctx, uint16_t reg, uint8_t *data, uint16_t length)
{
  return ctx->WriteReg(ctx->handle, reg, data, length);
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
