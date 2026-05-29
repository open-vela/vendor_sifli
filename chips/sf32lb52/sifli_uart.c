/**
  ******************************************************************************
  * @file   drv_usart.c
  * @author Sifli software development team
  * @brief USART BSP driver
  * @{
  ******************************************************************************
*/
/**
 * @attention
 * Copyright (c) 2019 - 2022,  Sifli Technology
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Sifli integrated circuit
 *    in a product or a software update for such product, must reproduce the above
 *    copyright notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Sifli nor the names of its contributors may be used to endorse
 *    or promote products derived from this software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Sifli integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY SIFLI TECHNOLOGY "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL SIFLI TECHNOLOGY OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */ 
#include <nuttx/config.h>
#include "sf32lb_serial.h"
#include <nuttx/power/pm.h>
#include <nuttx/cache.h>

#ifndef CONFIG_UART_BUFSZ
#define CONFIG_UART_BUFSZ 512
#endif

int uart_isr(int irq, FAR void *context, FAR void *arg);
int uart_dma_isr(int irq, FAR void *context, FAR void *arg);

#define SIFLI_UART_DMA_CACHE_ALIGN 32

#include "dma_config.h"
#include "uart_config.h"

/** @addtogroup bsp_driver Driver IO
  * @{
  */

/** @defgroup drv_usart UART
  * @brief USART BSP driver
  * @{
  */

enum
{
#ifdef CONFIG_BSP_USING_UART1
    UART1_INDEX,
#endif
#ifdef CONFIG_BSP_USING_UART2
    UART2_INDEX,
#endif
#ifdef CONFIG_BSP_USING_UART3
    UART3_INDEX,
#endif
#ifdef CONFIG_BSP_USING_UART4
    UART4_INDEX,
#endif
#ifdef CONFIG_BSP_USING_UART5
    UART5_INDEX,
#endif
#ifdef CONFIG_BSP_USING_LPUART1
    LPUART1_INDEX,
#endif
#ifdef CONFIG_BSP_USING_UART6
    UART6_INDEX,
#endif
    UART_MAX,
};

#undef CR1
#undef CR2
#undef CR3



/* Force uart_config into .data section by adding attribute.
 * This prevents it from being placed in .bss which gets cleared after
 * arm_earlyserialinit() has already initialized the console UART.
 */
static struct sifli_uart_config uart_config[] __attribute__((section(".data"))) =
{
#ifdef CONFIG_BSP_USING_UART1
    UART1_CONFIG,
#endif
#ifdef CONFIG_BSP_USING_UART2
    UART2_CONFIG,
#endif
#ifdef CONFIG_BSP_USING_UART3
    UART3_CONFIG,
#endif
#ifdef CONFIG_BSP_USING_UART4
    UART4_CONFIG,
#endif
#ifdef CONFIG_BSP_USING_UART5
    UART5_CONFIG,
#endif
#ifdef CONFIG_BSP_USING_UART6
    UART6_CONFIG,
#endif
};

/* Force uart_obj into .data section by adding attribute.
 * This prevents it from being placed in .bss which gets cleared after
 * arm_earlyserialinit() has already initialized the console UART.
 */
struct sifli_uart uart_obj[sizeof(uart_config) / sizeof(uart_config[0])] __attribute__((section(".data"))) =
{
#ifdef CONFIG_BSP_USING_UART1
    [UART1_INDEX] = { .uart_rx_dma_flag = 0 },
#endif
#ifdef CONFIG_BSP_USING_UART2  
    [UART2_INDEX] = { .uart_rx_dma_flag = 0 },
#endif
#ifdef CONFIG_BSP_USING_UART3
    [UART3_INDEX] = { .uart_rx_dma_flag = 0 },
#endif
#ifdef CONFIG_BSP_USING_UART4
    [UART4_INDEX] = { .uart_rx_dma_flag = 0 },
#endif
#ifdef CONFIG_BSP_USING_UART5
    [UART5_INDEX] = { .uart_rx_dma_flag = 0 },
#endif
#ifdef CONFIG_BSP_USING_UART6
    [UART6_INDEX] = { .uart_rx_dma_flag = 0 },
#endif
};

#ifdef CONFIG_PM
// static void sifli_serial_setsuspend(struct uart_dev_s *dev, bool suspend);
static void sifli_serial_pm_setsuspend(bool suspend);
static void sifli_serial_pmnotify(struct pm_callback_s *cb, int domain,
                                   enum pm_state_e pmstate);
// static int  sifli_serial_pmprepare(struct pm_callback_s *cb, int domain,
//                                     enum pm_state_e pmstate);
#endif


#ifdef CONFIG_PM
static struct
{
  struct pm_callback_s pm_cb;
  bool serial_suspended;
} g_serialpm =
  {
    .pm_cb.notify  = sifli_serial_pmnotify,
    .pm_cb.prepare = NULL,
    .serial_suspended = false
  };
#endif


static int sifli_setup(uart_dev_t *serial)
{
    struct sifli_uart *uart;
    uart = (struct sifli_uart *)serial->priv;
    struct serial_configure *cfg=&uart->ser_cfg;

    uart->handle.Instance          = uart->config->Instance;
    uart->handle.Init.BaudRate     = cfg->baud_rate;
    uart->handle.Init.HwFlowCtl    = cfg->hwfc;
    uart->handle.Init.HwFlowCtl    <<= USART_CR3_RTSE_Pos;
    uart->handle.Init.Mode         = UART_MODE_TX_RX;
    uart->handle.Init.OverSampling = UART_OVERSAMPLING_16;

    if (cfg->parity && cfg->data_bits < DATA_BITS_9)
        cfg->data_bits++;                           // parity is part of data

    switch (cfg->data_bits)
    {
    case DATA_BITS_6:
        uart->handle.Init.WordLength = UART_WORDLENGTH_6B;
        break;
    case DATA_BITS_7:
        uart->handle.Init.WordLength = UART_WORDLENGTH_7B;
        break;
    case DATA_BITS_8:
        uart->handle.Init.WordLength = UART_WORDLENGTH_8B;
        break;
    case DATA_BITS_9:
        uart->handle.Init.WordLength = UART_WORDLENGTH_9B;
        break;
    default:
        uart->handle.Init.WordLength = UART_WORDLENGTH_8B;
        break;
    }
    switch (cfg->stop_bits)
    {
    case STOP_BITS_1:
        uart->handle.Init.StopBits   = UART_STOPBITS_1;
        break;
    case STOP_BITS_2:
        uart->handle.Init.StopBits   = UART_STOPBITS_2;
        break;
    case STOP_BITS_3:
        uart->handle.Init.StopBits   = UART_STOPBITS_0_5;
        break;
    case STOP_BITS_4:
        uart->handle.Init.StopBits   = UART_STOPBITS_1_5;
        break;
    default:
        uart->handle.Init.StopBits   = UART_STOPBITS_1;
        break;
    }
    switch (cfg->parity)
    {
    case PARITY_NONE:
        uart->handle.Init.Parity     = UART_PARITY_NONE;
        break;
    case PARITY_ODD:
        uart->handle.Init.Parity     = UART_PARITY_ODD;
        break;
    case PARITY_EVEN:
        uart->handle.Init.Parity     = UART_PARITY_EVEN;
        break;
    default:
        uart->handle.Init.Parity     = UART_PARITY_NONE;
        break;
    }

    if (HAL_UART_Init(&uart->handle) != HAL_OK)
    {
        return -EINVAL;
    }

    return OK;
}

/****************************************************************************
 * Name: nrf53_ioctl
 *
 * Description:
 *   All ioctl calls will be routed through this method
 *
 ****************************************************************************/

static int sifli_ioctl(struct file *filep, int cmd, unsigned long arg)
{
#ifdef CONFIG_SERIAL_TERMIOS
  struct inode         *inode  = filep->f_inode;
  struct uart_dev_s    *dev    = inode->i_private;
  struct sifli_uart    *uart = (struct sifli_uart *)dev->priv;
  struct serial_configure *config = &uart->ser_cfg;
#endif
  int                   ret    = OK;

  switch (cmd)
    {
#ifdef CONFIG_SERIAL_TERMIOS
      case TCGETS:
        {
          struct termios *termiosp = (struct termios *)arg;

          if (!termiosp)
            {
              ret = -EINVAL;
              break;
            }

          termiosp->c_cflag = ((config->parity != 0) ? PARENB : 0)
                              | ((config->parity == 1) ? PARODD : 0)
                              | ((config->stop_bits==STOP_BITS_2) ? CSTOPB : 0) |
#ifdef CONFIG_SERIAL_OFLOWCONTROL
                              ((config->hwfc&RT_SERIAL_HWFC_CTS) ? CCTS_OFLOW : 0) |
#endif
#ifdef CONFIG_SERIAL_IFLOWCONTROL
                              ((config->hwfc&RT_SERIAL_HWFC_RTS) ? CRTS_IFLOW : 0) |
#endif
                              CS8;

          cfsetispeed(termiosp, config->baud_rate);

          break;
        }

      case TCSETS:
        {
          struct termios *termiosp = (struct termios *)arg;

          if (!termiosp)
            {
              ret = -EINVAL;
              break;
            }

          /* Perform some sanity checks before accepting any changes */

          if ((termiosp->c_cflag & CSIZE) != CS8)
            {
              ret = -EINVAL;
              break;
            }

#ifndef HAVE_UART_STOPBITS
          if ((termiosp->c_cflag & CSTOPB) != 0)
            {
              ret = -EINVAL;
              break;
            }
#endif

          if (termiosp->c_cflag & PARODD)
            {
              ret = -EINVAL;
              break;
            }

          /* TODO: CCTS_OFLOW and CRTS_IFLOW */

          /* Parity */

          if (termiosp->c_cflag & PARENB)
            {
              config->parity = (termiosp->c_cflag & PARODD) ? 1 : 2;
            }
          else
            {
              config->parity = 0;
            }

#ifdef HAVE_UART_STOPBITS
          /* Stop bits */

          config->stop_bits = (termiosp->c_cflag & CSTOPB) ? STOP_BITS_2:STOP_BITS_1;
#endif

          /* Note that only cfgetispeed is used because we have knowledge
           * that only one speed is supported.
           */

          config->baud_rate = cfgetispeed(termiosp);

          /* Effect the changes */

          sifli_setup(dev);

          break;
        }
#endif

      default:
        {
          ret = -ENOTTY;
          break;
        }
    }

  return ret;
}


static int sifli_receive(struct uart_dev_s *dev, unsigned int *status)
{
    int ch;
    struct sifli_uart *uart = (struct sifli_uart *)dev->priv;

    ch = -1;
    if (__HAL_UART_GET_FLAG(&(uart->handle), UART_FLAG_RXNE) != RESET)
        ch = __HAL_UART_GETC(&uart->handle);
    if (status)
      *status = 0x00;
    return ch;
}

static void sifli_rxint(struct uart_dev_s *dev, bool enable)
{
    struct sifli_uart *uart = (struct sifli_uart *)dev->priv;

    /* In DMA RX mode, use IDLE interrupt instead of RXNE interrupt */
    if (uart->uart_rx_dma_flag)
    {
        if (enable)
        {
            __HAL_UART_ENABLE_IT(&(uart->handle), UART_IT_IDLE);
            up_enable_irq(uart->config->irq_type+16);
        }
        else
        {
            __HAL_UART_DISABLE_IT(&(uart->handle), UART_IT_IDLE);
            up_disable_irq(uart->config->irq_type+16);
        }
    }
    else
    {
        if (enable)
        {
            /* Enable UART RXNE interrupt */
            __HAL_UART_ENABLE_IT(&(uart->handle), UART_IT_RXNE);
            up_enable_irq(uart->config->irq_type+16);
        }
        else
        {
            /* Disable UART RXNE interrupt */
            __HAL_UART_DISABLE_IT(&(uart->handle), UART_IT_RXNE);
            up_disable_irq(uart->config->irq_type+16);
        }
    }
}

static int sifli_dma_receive(struct uart_dev_s *dev)
{
    struct sifli_uart *uart = (struct sifli_uart *)dev->priv;
    IRQn_Type irq;

    //_info("Start uart DMA\n");

    /* Check if DMA is properly configured */
    if (!uart->uart_rx_dma_flag)
    {
        return OK;
    }

    if (uart->config->dma_rx == NULL || uart->config->dma_rx->Instance == NULL)
    {
        return -EINVAL;
    }

    __HAL_LINKDMA(&(uart->handle), hdmarx, uart->dma_rx.handle);
    uart->handle.hdmarx->Instance = uart->config->dma_rx->Instance;
    uart->handle.hdmarx->Init.Request = uart->config->dma_rx->request;
    uart->handle.hdmarx->Init.Direction = DMA_PERIPH_TO_MEMORY;
    uart->handle.hdmarx->Init.PeriphInc = DMA_PINC_DISABLE;
    uart->handle.hdmarx->Init.MemInc = DMA_MINC_ENABLE;
    uart->handle.hdmarx->Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    uart->handle.hdmarx->Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    uart->handle.hdmarx->Init.Mode = DMA_CIRCULAR;
    uart->handle.hdmarx->Init.Priority = DMA_PRIORITY_HIGH;
    irq = uart->config->dma_rx->dma_irq;

    /* Initialize the DMA with circular mode */
    HAL_DMA_Init(uart->handle.hdmarx);

    /* Reset last_index for DMA circular buffer tracking */
    uart->dma_rx.last_index = 0;

    up_invalidate_dcache((uintptr_t)uart->serial.recv.buffer,
                         (uintptr_t)uart->serial.recv.buffer +
                         uart->serial.recv.size);

    /* Clear any pending DMA interrupts before starting */
    HAL_UART_Receive_DMA(&(uart->handle), (uint8_t*)uart->serial.recv.buffer, uart->serial.recv.size);
    /* Note: IRQ will be enabled in sifli_attach() after irq_attach() */
    __HAL_UART_ENABLE_IT(&(uart->handle), UART_IT_IDLE);    
    return 0;
}


/****************************************************************************
 * Name: nrf53_rxavailable
 *
 * Description:
 *   Return true if the receive register is not empty
 *
 ****************************************************************************/

static bool sifli_rxavailable(struct uart_dev_s *dev)
{
  struct sifli_uart *uart = (struct sifli_uart *)dev->priv;

  /* Return true if the receive buffer/fifo is not "empty." */

  return __HAL_UART_GET_FLAG(&uart->handle, UART_FLAG_RXNE)? true:false;
}


static void sifli_send(struct uart_dev_s *dev, int c)
{
    struct sifli_uart *uart = (struct sifli_uart *)dev->priv;

    HAL_UART_LOCK_DEF();

#ifdef SF32LB56X
    HAL_UART_LOCK(&uart->handle);
#endif /* SF32LB56X */

    UART_INSTANCE_CLEAR_FUNCTION(&(uart->handle), UART_FLAG_TC);
    __HAL_UART_PUTC(&uart->handle, c);
    while (__HAL_UART_GET_FLAG(&(uart->handle), UART_FLAG_TC) == RESET);

#ifdef SF32LB56X
    HAL_UART_UNLOCK(&uart->handle);
#endif /* SF32LB56X */

}


/****************************************************************************
 * Name: sifli_txint
 *
 * Description:
 *   Call to enable or disable TX interrupts
 *
 ****************************************************************************/

static void sifli_txint(struct uart_dev_s *dev, bool enable)
{
    irqstate_t flags;

    flags = enter_critical_section();
    if (enable) 
        uart_xmitchars(dev);            
    leave_critical_section(flags);

}



/****************************************************************************
 * Name: sifli_txready
 *
 * Description:
 *   Return true if the tranmsit data register is empty
 *
 ****************************************************************************/

static bool sifli_txready(struct uart_dev_s *dev)
{
    struct sifli_uart *uart = (struct sifli_uart *)dev->priv;
    
    /* Return true if the receive buffer/fifo is not "empty." */
    
    return __HAL_UART_GET_FLAG(&uart->handle, UART_FLAG_TXE)? true:false;
}

/****************************************************************************
 * Name: sifli_txempty
 *
 * Description:
 *   Return true if the transmit data register is empty
 *
 ****************************************************************************/

static bool sifli_txempty(struct uart_dev_s *dev)
{
    return sifli_txready(dev);
}



/****************************************************************************
 * Name: nrf53_attach
 *
 * Description:
 *   Configure the UART to operation in interrupt driven mode.  This method
 *   is called when the serial port is opened.  Normally, this is just after
 *   the the setup() method is called, however, the serial console may
 *   operate in a non-interrupt driven mode during the boot phase.
 *
 *   RX and TX interrupts are not enabled when by the attach method (unless
 *   the hardware supports multiple levels of interrupt enabling).
 *   The RX and TX interrupts are not enabled until the txint() and rxint()
 *   methods are called.
 *
 ****************************************************************************/

static int sifli_attach(struct uart_dev_s *dev)
{
    struct sifli_uart *uart = (struct sifli_uart *)dev->priv;
    int ret;

    /* sifli_attach called */
    
    //_info("recv %p\n", &(dev->recv));
    // Start to receive data.
    sifli_dma_receive(dev);

    /* Attach and enable the IRQ(s).  The interrupts are (probably) still
    * disabled in the C2 register.
    */
    ret = irq_attach(uart->config->irq_type+16, uart_isr, uart);
    if (ret == OK) {
        up_enable_irq(uart->config->irq_type+16);
    }

    if (uart->config->dma_rx && uart->config->dma_rx->dma_irq) {
        ret=irq_attach(uart->config->dma_rx->dma_irq+16, uart_dma_isr, uart);
        if (ret == OK) {
            up_enable_irq(uart->config->dma_rx->dma_irq+16);
        }
    }
    return ret;
}

/****************************************************************************
 * Name: nrf53_detach
 *
 * Description:
 *   Detach UART interrupts.  This method is called when the serial port is
 *   closed normally just before the shutdown method is called.
 *   The exception is the serial console which is never shutdown.
 *
 ****************************************************************************/

static void sifli_detach(struct uart_dev_s *dev)
{
    struct sifli_uart *uart = (struct sifli_uart *)dev->priv;

    /* Disable interrupts */
    up_disable_irq(uart->config->irq_type+16);

    /* Detach from the interrupt(s) */
    irq_detach(uart->config->irq_type+16);

    if (uart->config->dma_rx && uart->config->dma_rx->dma_irq) {
        /* Disable interrupts */
        up_disable_irq(uart->config->dma_rx->dma_irq+16);
        
        /* Detach from the interrupt(s) */
        irq_detach(uart->config->dma_rx->dma_irq+16);

        
    }
}


/****************************************************************************
 * Name: sifli_shutdown
 *
 * Description:
 *   Disable the UART.  This method is called when the serial
 *   port is closed
 *
 ****************************************************************************/

static void sifli_shutdown(struct uart_dev_s *dev)
{
    struct sifli_uart *uart = (struct sifli_uart *)dev->priv;

    /* Disable interrupts */
    HAL_UART_DeInit(&uart->handle);
}


static const struct uart_ops_s g_uart_ops =
{
  .setup          = sifli_setup,
  .shutdown       = sifli_shutdown,
  .attach         = sifli_attach,
  .detach         = sifli_detach,
  .ioctl          = sifli_ioctl,
  .receive        = sifli_receive,
  .rxint          = sifli_rxint,
  .rxavailable    = sifli_rxavailable,
  .send           = sifli_send,
  .txint          = sifli_txint,
  .txready        = sifli_txready,
  .txempty        = sifli_txempty,
};



#ifdef CONFIG_PM

static void sifli_serial_pm_setsuspend(bool suspend)
{
  int n;
  int obj_num = sizeof(uart_obj) / sizeof(uart_obj[0]);

  /* Already in desired state? */

  if (suspend == g_serialpm.serial_suspended)
    return;

  g_serialpm.serial_suspended = suspend;


  if (!suspend)
  {
    for (n = 0; n < obj_num; n++)    
    {
        sifli_setup(&uart_obj[n].serial);
        sifli_attach(&uart_obj[n].serial);
    }
  }
}

static void sifli_serial_pmnotify(struct pm_callback_s *cb, int domain,
                                   enum pm_state_e pmstate)
{
  switch (pmstate)
    {
      case PM_NORMAL:
        {
          sifli_serial_pm_setsuspend(false);
        }
        break;

      case PM_IDLE:
        {
          sifli_serial_pm_setsuspend(false);
        }
        break;

      case PM_STANDBY:
        {
          sifli_serial_pm_setsuspend(true);
        }
        break;

      case PM_SLEEP:
        {
          sifli_serial_pm_setsuspend(true);
        }
        break;

      default:

        /* Should not get here */

        break;
    }
}
#endif


/**
 * Uart common interrupt process. This need add to uart ISR.
 *
 * @param serial serial device
 */
int uart_isr(int irq, FAR void *context, FAR void *arg)
{
    struct sifli_uart *uart;


    uart = (struct sifli_uart *) arg;

    /* Clear error flags first to prevent data loss */
    if (__HAL_UART_GET_FLAG(&(uart->handle), UART_FLAG_ORE) != RESET)
    {
        __HAL_UART_CLEAR_OREFLAG(&uart->handle);
    }
    if (__HAL_UART_GET_FLAG(&(uart->handle), UART_FLAG_NE) != RESET)
    {
        __HAL_UART_CLEAR_NEFLAG(&uart->handle);
    }
    if (__HAL_UART_GET_FLAG(&(uart->handle), UART_FLAG_FE) != RESET)
    {
        __HAL_UART_CLEAR_FEFLAG(&uart->handle);
    }
    if (__HAL_UART_GET_FLAG(&(uart->handle), UART_FLAG_PE) != RESET)
    {
        __HAL_UART_CLEAR_PEFLAG(&uart->handle);
    }

    /* UART in mode Receiver -------------------------------------------------*/
    if (__HAL_UART_GET_FLAG(&(uart->handle), UART_FLAG_RXNE) != RESET)
    {
        uart_recvchars(&(uart->serial));
    }

    /* DMA RX IDLE interrupt */
    if ((uart->uart_rx_dma_flag) && (__HAL_UART_GET_FLAG(&(uart->handle), UART_FLAG_IDLE) != RESET))
    {
        HAL_UART_RxCpltCallback(&(uart->handle));
        __HAL_UART_CLEAR_IDLEFLAG(&uart->handle);
    }

    /* DMA TX complete interrupt */
    if ((uart->uart_tx_dma_flag) && (__HAL_UART_GET_FLAG(&(uart->handle), UART_FLAG_TC) != RESET))
    {
        __HAL_UART_CLEAR_FLAG(&uart->handle, UART_CLEAR_TCF);
        uart->handle.gState = HAL_UART_STATE_READY;
        uart_xmitchars(&uart->serial);
    }

    /* Clear remaining flags */
    if (__HAL_UART_GET_FLAG(&(uart->handle), UART_FLAG_CTS) != RESET)
    {
        UART_INSTANCE_CLEAR_FUNCTION(&(uart->handle), UART_FLAG_CTS);
    }
    if (__HAL_UART_GET_FLAG(&(uart->handle), UART_FLAG_TXE) != RESET)
    {
        UART_INSTANCE_CLEAR_FUNCTION(&(uart->handle), UART_FLAG_TXE);
    }

    return OK;
}

/**
 * Uart common interrupt process. This need add to uart ISR.
 *
 * @param serial serial device
 */
int uart_dma_isr(int irq, FAR void *context, FAR void *arg)
{
    struct sifli_uart *uart =(struct sifli_uart *) arg;

    HAL_DMA_IRQHandler(&(uart->dma_rx.handle));
    return OK;
}

/**
  * @brief  Rx Half Transfer completed callback.
  * @param huart UART handle.
  * @retval None
  */
void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
    struct sifli_uart *uart;
    int recv_total_index;

    uart = (struct sifli_uart *)huart;

    /* Get current DMA write position */
    recv_total_index = uart->serial.recv.size - __HAL_DMA_GET_COUNTER(&uart->dma_rx.handle);

    up_invalidate_dcache((uintptr_t)uart->serial.recv.buffer,
                         (uintptr_t)uart->serial.recv.buffer +
                         uart->serial.recv.size);

    /* In DMA circular mode, DMA writes directly to recv.buffer.
     * The head pointer should track DMA write position.
     * NuttX serial layer reads from tail to head.
     */
    if (recv_total_index != uart->serial.recv.head)
    {
        uart->serial.recv.head = recv_total_index;
#if defined(CONFIG_TTY_SIGINT) || defined(CONFIG_TTY_SIGTSTP) || \
    defined(CONFIG_TTY_FORCE_PANIC) || defined(CONFIG_TTY_LAUNCH)        
        uart_check_special(&(uart->serial), &(uart->serial.recv.buffer[uart->serial.recv.tail]), 1);
#endif
        uart_datareceived(&(uart->serial));
    }
}


void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    HAL_UART_RxHalfCpltCallback(huart);
}

static void sifli_uart_get_dma_config(void)
{
#ifdef CONFIG_BSP_UART1_RX_USING_DMA
    uart_obj[UART1_INDEX].uart_rx_dma_flag = 1;
    /* Force into .data section by using non-zero initialization. */
    static struct dma_config uart1_dma_rx = { .Instance = (void *)-1 };
    uart1_dma_rx = (struct dma_config)UART1_RX_DMA_CONFIG;
    uart_config[UART1_INDEX].dma_rx = &uart1_dma_rx;
#endif

#ifdef CONFIG_BSP_UART2_RX_USING_DMA
    uart_obj[UART2_INDEX].uart_rx_dma_flag = 1;
    static struct dma_config uart2_dma_rx = { .Instance = (void *)-1 };
    uart2_dma_rx = (struct dma_config)UART2_RX_DMA_CONFIG;
    uart_config[UART2_INDEX].dma_rx = &uart2_dma_rx;
#endif

#ifdef CONFIG_BSP_UART3_RX_USING_DMA
    uart_obj[UART3_INDEX].uart_rx_dma_flag = 1;
    static struct dma_config uart3_dma_rx = UART3_RX_DMA_CONFIG;
    uart_config[UART3_INDEX].dma_rx = &uart3_dma_rx;
#endif

#ifdef CONFIG_BSP_UART4_RX_USING_DMA
    uart_obj[UART4_INDEX].uart_rx_dma_flag = 1;
    static struct dma_config uart4_dma_rx = UART4_RX_DMA_CONFIG;
    uart_config[UART4_INDEX].dma_rx = &uart4_dma_rx;
#endif

#ifdef CONFIG_BSP_UART5_RX_USING_DMA
    uart_obj[UART5_INDEX].uart_rx_dma_flag = 1;
    static struct dma_config uart5_dma_rx = UART5_RX_DMA_CONFIG;
    uart_config[UART5_INDEX].dma_rx = &uart5_dma_rx;
#endif
#ifdef CONFIG_BSP_UART6_RX_USING_DMA
    uart_obj[UART6_INDEX].uart_rx_dma_flag = 1;
    static struct dma_config uart6_dma_rx = UART6_RX_DMA_CONFIG;
    uart_config[UART6_INDEX].dma_rx = &uart6_dma_rx;
#endif


}




/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef USE_SERIALDRIVER

/****************************************************************************
 * Name: arm_serialinit
 *
 * Description:
 *   Register serial console and serial ports.  This assumes
 *   that arm_earlyserialinit was called previously.
 *
 ****************************************************************************/
static char g_uart_buffer[sizeof(uart_config)*2/sizeof(uart_config[0])][CONFIG_UART_BUFSZ]
    __attribute__((aligned(SIFLI_UART_DMA_CACHE_ALIGN)));

int sifli_usart_init(void)
{
    int obj_num = sizeof(uart_obj) / sizeof(struct sifli_uart);
    if (obj_num > 0)
    {
        sifli_uart_get_dma_config();
    }
    return OK;
}


void arm_serialinit(void)
{
    unsigned minor = 0;
    unsigned i     = 0;
    char devname[16];
    int obj_num = sizeof(uart_obj) / sizeof(struct sifli_uart);
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;

#ifdef CONFIG_PM
  int ret;
#endif

#ifdef CONFIG_PM
    ret = pm_register(&g_serialpm.pm_cb);
    DEBUGASSERT(ret == OK);
    UNUSED(ret);
#endif

    sifli_usart_init();

    for (i = 0; i < obj_num; i++)
    {
        uart_obj[i].config = &(uart_config[i]);
        uart_obj[i].serial.ops = &g_uart_ops;
        uart_obj[i].serial.priv = &uart_obj[i];
        uart_obj[i].serial.xmit.size = CONFIG_UART_BUFSZ;
        uart_obj[i].serial.xmit.buffer = g_uart_buffer[2*i];
        uart_obj[i].serial.recv.size = CONFIG_UART_BUFSZ;
        uart_obj[i].serial.recv.buffer = g_uart_buffer[2*i+1];
        uart_obj[i].ser_cfg=config;
#ifdef CONSOLE_UART
        if (i == (CONSOLE_UART -1))
        {
            // uart_obj[i].serial.isconsole = true;
        }
#endif /* CONSOLE_UART */        
    }
    
#ifdef CONSOLE_UART
    /* Register the serial console */
    /* isconsole was already set in arm_earlyserialinit */
    
    uart_register("/dev/console", &(uart_obj[CONSOLE_UART-1].serial));
#endif

    /* Register all remaining UARTs */
    strcpy(devname, "/dev/ttySx");
    for (i = 1; i <= obj_num; i++)
    {
        /* Register USARTs as devices in increasing order */
#ifdef CONSOLE_UART
        if (i==CONSOLE_UART)
            continue;
#endif        
        
        devname[9] = '0' + minor++;
        uart_register(devname, &(uart_obj[i-1].serial));
    }
}

void up_putc(int ch)
{
#if CONSOLE_UART > 0
  /* Check for LF */

  if (ch == '\n')
    {
      /* Add CR */

      arm_lowputc('\r');
    }

  arm_lowputc(ch);
#endif
  return ch;
}

#endif /* USE_SERIALDRIVER */

/// @} drv_usart
/// @} bsp_driver
/// @} file


/************************ (C) COPYRIGHT Sifli Technology *******END OF FILE****/
