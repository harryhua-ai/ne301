/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  * @brief   This file provides code for the configuration
  *          of the USART instances.
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
#include "usart.h"

/* USER CODE BEGIN 0 */
#include "n6_comm.h"
/* USER CODE END 0 */

UART_HandleTypeDef hlpuart2;
UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_lpuart2_rx;
DMA_HandleTypeDef hdma_lpuart2_tx;

/* LPUART2 init function */

void MX_LPUART2_UART_Init(void)
{

  /* USER CODE BEGIN LPUART2_Init 0 */

  /* USER CODE END LPUART2_Init 0 */

  /* USER CODE BEGIN LPUART2_Init 1 */

  /* USER CODE END LPUART2_Init 1 */
  hlpuart2.Instance = LPUART2;
  hlpuart2.Init.BaudRate = 115200;
  hlpuart2.Init.WordLength = UART_WORDLENGTH_8B;
  hlpuart2.Init.StopBits = UART_STOPBITS_1;
  hlpuart2.Init.Parity = UART_PARITY_NONE;
  hlpuart2.Init.Mode = UART_MODE_TX_RX;
  hlpuart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hlpuart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  hlpuart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  hlpuart2.FifoMode = UART_FIFOMODE_DISABLE;
  if (HAL_UART_Init(&hlpuart2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&hlpuart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&hlpuart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&hlpuart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LPUART2_Init 2 */

  /* USER CODE END LPUART2_Init 2 */

}
/* USART1 init function */

void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if(uartHandle->Instance==LPUART2)
  {
  /* USER CODE BEGIN LPUART2_MspInit 0 */

  /* USER CODE END LPUART2_MspInit 0 */

  /** Initializes the peripherals clocks
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_LPUART2;
    PeriphClkInit.Lpuart2ClockSelection = RCC_LPUART2CLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* LPUART2 clock enable */
    __HAL_RCC_LPUART2_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**LPUART2 GPIO Configuration
    PB6     ------> LPUART2_TX
    PB7     ------> LPUART2_RX
    */
    GPIO_InitStruct.Pin = LU2_TX_Pin|LU2_RX_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF10_LPUART2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    __HAL_SYSCFG_FASTMODEPLUS_ENABLE(SYSCFG_FASTMODEPLUS_PB6);

    __HAL_SYSCFG_FASTMODEPLUS_ENABLE(SYSCFG_FASTMODEPLUS_PB7);

    /* LPUART2 DMA Init */
    /* LPUART2_RX Init */
    hdma_lpuart2_rx.Instance = DMA1_Channel3;
    hdma_lpuart2_rx.Init.Request = DMA_REQUEST_LPUART2_RX;
    hdma_lpuart2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_lpuart2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_lpuart2_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_lpuart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_lpuart2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_lpuart2_rx.Init.Mode = DMA_NORMAL;
    hdma_lpuart2_rx.Init.Priority = DMA_PRIORITY_MEDIUM;
    if (HAL_DMA_Init(&hdma_lpuart2_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmarx,hdma_lpuart2_rx);

    /* LPUART2_TX Init */
    hdma_lpuart2_tx.Instance = DMA1_Channel2;
    hdma_lpuart2_tx.Init.Request = DMA_REQUEST_LPUART2_TX;
    hdma_lpuart2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_lpuart2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_lpuart2_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_lpuart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_lpuart2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_lpuart2_tx.Init.Mode = DMA_NORMAL;
    hdma_lpuart2_tx.Init.Priority = DMA_PRIORITY_MEDIUM;
    if (HAL_DMA_Init(&hdma_lpuart2_tx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmatx,hdma_lpuart2_tx);

    /* LPUART2 interrupt Init */
    HAL_NVIC_SetPriority(USART2_LPUART2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART2_LPUART2_IRQn);
  /* USER CODE BEGIN LPUART2_MspInit 1 */

  /* USER CODE END LPUART2_MspInit 1 */
  }
  else if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspInit 0 */

  /* USER CODE END USART1_MspInit 0 */

  /** Initializes the peripherals clocks
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
    PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* USART1 clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    GPIO_InitStruct.Pin = U1_TX_Pin|U1_RX_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    __HAL_SYSCFG_FASTMODEPLUS_ENABLE(SYSCFG_FASTMODEPLUS_PA9);

    __HAL_SYSCFG_FASTMODEPLUS_ENABLE(SYSCFG_FASTMODEPLUS_PA10);

    /* USART1 interrupt Init */
    HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspInit 1 */

  /* USER CODE END USART1_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==LPUART2)
  {
  /* USER CODE BEGIN LPUART2_MspDeInit 0 */

  /* USER CODE END LPUART2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_LPUART2_CLK_DISABLE();

    /**LPUART2 GPIO Configuration
    PB6     ------> LPUART2_TX
    PB7     ------> LPUART2_RX
    */
    HAL_GPIO_DeInit(GPIOB, LU2_TX_Pin|LU2_RX_Pin);

    /* LPUART2 DMA DeInit */
    HAL_DMA_DeInit(uartHandle->hdmarx);
    HAL_DMA_DeInit(uartHandle->hdmatx);

    /* LPUART2 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART2_LPUART2_IRQn);
  /* USER CODE BEGIN LPUART2_MspDeInit 1 */

  /* USER CODE END LPUART2_MspDeInit 1 */
  }
  else if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspDeInit 0 */

  /* USER CODE END USART1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART1_CLK_DISABLE();

    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    HAL_GPIO_DeInit(GPIOA, U1_TX_Pin|U1_RX_Pin);

    /* USART1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspDeInit 1 */

  /* USER CODE END USART1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
/* UART transmit complete interrupt */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == LPUART2) {
    n6_comm_set_event_isr(N6_COMM_EVENT_TX_DONE);
  }
}
/* UART idle interrupt */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
  if (huart->Instance == LPUART2) {
    n6_comm_set_event_isr(N6_COMM_EVENT_RX_DONE);
  }
}
/* Interrupt error handling function, handle overrun error here */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_ORE) != RESET) __HAL_UART_CLEAR_OREFLAG(huart);
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_FE) != RESET) __HAL_UART_CLEAR_FEFLAG(huart);
    if (huart->Instance == LPUART2) {
      n6_comm_set_event_isr(N6_COMM_EVENT_ERR);
    }
}
/* USER CODE END 1 */
