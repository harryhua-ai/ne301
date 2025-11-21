/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usb_otg.c
  * @brief   This file provides code for the configuration
  *          of the USB_OTG instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "usb_otg.h"
#include "ux_stm32_config.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */
#ifndef ISP_MW_TUNING_TOOL_SUPPORT
PCD_HandleTypeDef hpcd_USB_OTG_HS1;
#endif
HCD_HandleTypeDef hhcd_USB_OTG_HS2;

/* USB1_OTG_HS init function */

#ifndef ISP_MW_TUNING_TOOL_SUPPORT
void MX_USB1_OTG_HS_PCD_Init(void)
{

  /* USER CODE BEGIN USB1_OTG_HS_Init 0 */

  /* USER CODE END USB1_OTG_HS_Init 0 */

  /* USER CODE BEGIN USB1_OTG_HS_Init 1 */

  /* USER CODE END USB1_OTG_HS_Init 1 */
  hpcd_USB_OTG_HS1.Instance = USB1_OTG_HS;
  hpcd_USB_OTG_HS1.Init.dev_endpoints = 9;
  hpcd_USB_OTG_HS1.Init.speed = PCD_SPEED_HIGH;
  hpcd_USB_OTG_HS1.Init.phy_itface = USB_OTG_HS_EMBEDDED_PHY;
  hpcd_USB_OTG_HS1.Init.Sof_enable = DISABLE;
  hpcd_USB_OTG_HS1.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_HS1.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_HS1.Init.use_dedicated_ep1 = DISABLE;
  hpcd_USB_OTG_HS1.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_OTG_HS1.Init.dma_enable = ENABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_HS1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB1_OTG_HS_Init 2 */

  /* USER CODE END USB1_OTG_HS_Init 2 */

}
#endif

/* USB2_OTG_HS init function */

void MX_USB2_OTG_HS_HCD_Init(void)
{

  /* USER CODE BEGIN USB2_OTG_HS_Init 0 */

  /* USER CODE END USB2_OTG_HS_Init 0 */

  /* USER CODE BEGIN USB2_OTG_HS_Init 1 */

  /* USER CODE END USB2_OTG_HS_Init 1 */
  hhcd_USB_OTG_HS2.Instance = USB2_OTG_HS;
  hhcd_USB_OTG_HS2.Init.dev_endpoints = UX_DCD_STM32_MAX_ED;
  hhcd_USB_OTG_HS2.Init.Host_channels = UX_HCD_STM32_MAX_NB_CHANNELS;
  hhcd_USB_OTG_HS2.Init.speed = HCD_SPEED_HIGH;
  hhcd_USB_OTG_HS2.Init.dma_enable = ENABLE;
  hhcd_USB_OTG_HS2.Init.phy_itface = USB_OTG_HS_EMBEDDED_PHY;
  hhcd_USB_OTG_HS2.Init.Sof_enable = DISABLE;
  hhcd_USB_OTG_HS2.Init.low_power_enable = DISABLE;
  hhcd_USB_OTG_HS2.Init.vbus_sensing_enable = DISABLE;
  hhcd_USB_OTG_HS2.Init.use_external_vbus = ENABLE;
  if (HAL_HCD_Init(&hhcd_USB_OTG_HS2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB2_OTG_HS_Init 2 */

  /* USER CODE END USB2_OTG_HS_Init 2 */

}

#ifndef ISP_MW_TUNING_TOOL_SUPPORT
void HAL_PCD_MspInit(PCD_HandleTypeDef* pcdHandle)
{

  // RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(pcdHandle->Instance==USB1_OTG_HS)
  {
  /* USER CODE BEGIN USB1_OTG_HS_MspInit 0 */

  /* USER CODE END USB1_OTG_HS_MspInit 0 */
    __HAL_RCC_PWR_CLK_ENABLE();
    /* Enable the VDD33USB independent USB 33 voltage monitor */
    HAL_PWREx_EnableVddUSBVMEN();

    /* Wait until VDD33USB is ready */
    while (__HAL_PWR_GET_FLAG(PWR_FLAG_USB33RDY) == 0U);

    /* Enable VDDUSB supply */
    HAL_PWREx_EnableVddUSB();

    /* Enable USB1 OTG clock */
    __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();

    /* Set FSEL to 24 Mhz */
    USB1_HS_PHYC->USBPHYC_CR &= ~(0x7U << 0x4U);
    USB1_HS_PHYC->USBPHYC_CR |= (0x2U << 0x4U);

    /* Enable USB1 OTG PHY clock */
    __HAL_RCC_USB1_OTG_HS_PHY_CLK_ENABLE();

    HAL_NVIC_SetPriority(USB1_OTG_HS_IRQn, 6U, 0U);

    /* Enable USB OTG interrupt */
    HAL_NVIC_EnableIRQ(USB1_OTG_HS_IRQn);
  }
}
#endif

void HAL_HCD_MspInit(HCD_HandleTypeDef* hcdHandle)
{

  // RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(hcdHandle->Instance==USB2_OTG_HS)
  {
  /* USER CODE BEGIN USB2_OTG_HS_MspInit 0 */

  /* USER CODE END USB2_OTG_HS_MspInit 0 */

    __HAL_RCC_PWR_CLK_ENABLE();
    /* Enable the VDD33USB independent USB 33 voltage monitor */
    HAL_PWREx_EnableVddUSBVMEN();

    /* Wait until VDD33USB is ready */
    while (__HAL_PWR_GET_FLAG(PWR_FLAG_USB33RDY) == 0U);

    /* Enable VDDUSB supply */
    HAL_PWREx_EnableVddUSB();

    /* Enable USB2 OTG clock */
    __HAL_RCC_USB2_OTG_HS_CLK_ENABLE();

    /* Set FSEL to 24 Mhz */
    USB2_HS_PHYC->USBPHYC_CR &= ~(0x7U << 0x4U);
    USB2_HS_PHYC->USBPHYC_CR |= (0x2U << 0x4U);

    /* Enable USB2 OTG PHY clock */
    __HAL_RCC_USB2_OTG_HS_PHY_CLK_ENABLE();

    HAL_NVIC_SetPriority(USB2_OTG_HS_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(USB2_OTG_HS_IRQn);

  /* USER CODE BEGIN USB2_OTG_HS_MspInit 1 */

  /* USER CODE END USB2_OTG_HS_MspInit 1 */
  }
}

#ifndef ISP_MW_TUNING_TOOL_SUPPORT
void HAL_PCD_MspDeInit(PCD_HandleTypeDef* pcdHandle)
{

  if(pcdHandle->Instance==USB1_OTG_HS)
  {
  /* USER CODE BEGIN USB1_OTG_HS_MspDeInit 0 */

  /* USER CODE END USB1_OTG_HS_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USB1_OTG_HS_CLK_DISABLE();
    __HAL_RCC_USB1_OTG_HS_PHY_CLK_DISABLE();

    /* Disable VDDUSB */
    HAL_PWREx_DisableVddUSB();

    /* USB1_OTG_HS interrupt Deinit */
    HAL_NVIC_DisableIRQ(USB1_OTG_HS_IRQn);
  /* USER CODE BEGIN USB1_OTG_HS_MspDeInit 1 */

  /* USER CODE END USB1_OTG_HS_MspDeInit 1 */
  }
}
#endif

void HAL_HCD_MspDeInit(HCD_HandleTypeDef* hcdHandle)
{

  if(hcdHandle->Instance==USB2_OTG_HS)
  {
  /* USER CODE BEGIN USB2_OTG_HS_MspDeInit 0 */

  /* USER CODE END USB2_OTG_HS_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USB2_OTG_HS_CLK_DISABLE();
    __HAL_RCC_USB2_OTG_HS_PHY_CLK_DISABLE();

    /* Disable VDDUSB */
    HAL_PWREx_DisableVddUSB();

    /* USB2_OTG_HS interrupt Deinit */
    HAL_NVIC_DisableIRQ(USB2_OTG_HS_IRQn);
  /* USER CODE BEGIN USB2_OTG_HS_MspDeInit 1 */

  /* USER CODE END USB2_OTG_HS_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
