/****************************************************************************
 * vendor/sifli/chip/sf32lb52/sifli_lowput.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdbool.h>
#include "bf0_hal.h"
#include "sf32lb_serial.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void arm_lowputc(char ch)
{
    while ((hwp_usart1->ISR & UART_FLAG_TXE) == 0);
    hwp_usart1->TDR = (uint32_t)ch;
}

/****************************************************************************
 * Name: arm_earlyserialinit
 *
 * Description:
 *   Performs the low level USART initialization early in debug so that the
 *   serial console will be available during bootup.  This must be called
 *   before arm_serialinit.
 *
 ****************************************************************************/

/* UART handler declaration */
static UART_HandleTypeDef g_early_uart_handle;

void arm_earlyserialinit(void)
{
    /* 1. Configure pinmux for UART1 */
    HAL_PIN_Set(PAD_PA19, USART1_TXD, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA18, USART1_RXD, PIN_PULLUP, 1);

    /* 2. Enable UART1 clock */
    HAL_RCC_EnableModule(RCC_MOD_USART1);

    /* 3. Configure UART parameters */
    g_early_uart_handle.Instance        = hwp_usart1;
    g_early_uart_handle.Init.BaudRate   = CONFIG_UART_BAUD;
    g_early_uart_handle.Init.WordLength = UART_WORDLENGTH_8B;
    g_early_uart_handle.Init.StopBits   = UART_STOPBITS_1;
    g_early_uart_handle.Init.Parity     = UART_PARITY_NONE;
    g_early_uart_handle.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
    g_early_uart_handle.Init.Mode       = UART_MODE_TX_RX;
    g_early_uart_handle.Init.OverSampling = UART_OVERSAMPLING_16;
    
    /* 4. Initialize UART */
    if (HAL_UART_Init(&g_early_uart_handle) != HAL_OK)
    {
        /* Initialization Error */
        HAL_ASSERT(0);
    }
}
