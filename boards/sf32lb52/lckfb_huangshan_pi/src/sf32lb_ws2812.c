/****************************************************************************
 * vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/src/sf32lb_ws2812.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * WS2812 (SK6812MINI-HS) LED driver for LCKFB Huangshan Pi.
 * PA32 drives the single RGB LED using GPIO bit-banging.
 *
 * Usage:
 *   echo -ne 'FF0000' > /dev/led0    # 红色 (HEX RRGGBB)
 *   echo -ne '00FF00' > /dev/led0    # 绿色
 *   echo -ne '0000FF' > /dev/led0    # 蓝色
 *   echo -ne '000000' > /dev/led0    # 关闭LED (黑色)
 *   cat /dev/led0                     # 读取当前颜色
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/arch.h>
#include <nuttx/fs/fs.h>

#ifdef CONFIG_WS2812

#include <bf0_hal.h>
#include "bsp_board.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WS2812_LED_PIN       32   /* PA32 */

/* WS2812 timing for 24MHz CPU clock */

#define CYCLES_PER_US    24
#define CYCLES_T0H       9
#define CYCLES_T1H       19
#define CYCLES_T0L       19
#define CYCLES_T1L       9
#define RESET_US         60

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sf32lb_ws2812_dev_s
{
  uint32_t color;  /* 当前颜色 (0xRRGGBB) */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct sf32lb_ws2812_dev_s g_ws2812_dev;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline void ws2812_delay_cycles(uint32_t n)
{
  volatile uint32_t count = n / 4;
  while (count--)
    {
      __asm__ volatile("nop");
    }
}

static inline void ws2812_gpio_high(void)
{
  HAL_GPIO_WritePin(hwp_gpio1, WS2812_LED_PIN, GPIO_PIN_SET);
}

static inline void ws2812_gpio_low(void)
{
  HAL_GPIO_WritePin(hwp_gpio1, WS2812_LED_PIN, GPIO_PIN_RESET);
}

static inline void ws2812_send_bit(uint8_t bit)
{
  if (bit)
    {
      ws2812_gpio_high();
      ws2812_delay_cycles(CYCLES_T1H);
      ws2812_gpio_low();
      ws2812_delay_cycles(CYCLES_T1L);
    }
  else
    {
      ws2812_gpio_high();
      ws2812_delay_cycles(CYCLES_T0H);
      ws2812_gpio_low();
      ws2812_delay_cycles(CYCLES_T0L);
    }
}

static inline void ws2812_send_byte(uint8_t byte)
{
  int i;
  for (i = 7; i >= 0; i--)
    {
      ws2812_send_bit((byte >> i) & 1);
    }
}

static inline void ws2812_send_reset(void)
{
  volatile uint32_t count = RESET_US * CYCLES_PER_US / 4;
  ws2812_gpio_low();
  while (count--)
    {
      __asm__ volatile("nop");
    }
}

/* 发送像素，输入是0xRRGGBB格式，转换为WS2812的GRB顺序 */

static void ws2812_send_pixel(uint32_t rrgbb)
{
  uint8_t r = (rrgbb >> 16) & 0xff;
  uint8_t g = (rrgbb >> 8)  & 0xff;
  uint8_t b = (rrgbb >> 0)  & 0xff;

  /* WS2812 数据顺序: GRB */

  ws2812_send_byte(g);
  ws2812_send_byte(r);
  ws2812_send_byte(b);
}

/* 更新LED显示 */

static void ws2812_update(void)
{
  irqstate_t flags = up_irq_save();
  ws2812_send_pixel(g_ws2812_dev.color);
  ws2812_send_reset();
  up_irq_restore(flags);
}

/* 解析HEX颜色字符串 */

static int ws2812_parse_hex(const char *hex, uint32_t *color)
{
  uint32_t c = 0;
  int i;

  for (i = 0; i < 6; i++)
    {
      char ch = hex[i];
      uint8_t val;

      if (ch >= '0' && ch <= '9')
        {
          val = ch - '0';
        }
      else if (ch >= 'A' && ch <= 'F')
        {
          val = ch - 'A' + 10;
        }
      else if (ch >= 'a' && ch <= 'f')
        {
          val = ch - 'a' + 10;
        }
      else
        {
          return -EINVAL;
        }

      c = (c << 4) | val;
    }

  *color = c;
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static int ws2812_open(FAR struct file *filep);
static int ws2812_close(FAR struct file *filep);
static ssize_t ws2812_write(FAR struct file *filep,
                            FAR const char *buffer,
                            size_t len);
static ssize_t ws2812_read(FAR struct file *filep,
                           FAR char *buffer,
                           size_t len);

static const struct file_operations g_ws2812_ops =
{
  ws2812_open,
  ws2812_close,
  ws2812_read,
  ws2812_write,
};

static int ws2812_open(FAR struct file *filep)
{
  return OK;
}

static int ws2812_close(FAR struct file *filep)
{
  return OK;
}

static ssize_t ws2812_write(FAR struct file *filep,
                            FAR const char *buffer,
                            size_t len)
{
  if (buffer == NULL || len < 6)
    {
      return -EINVAL;
    }

  uint32_t color;
  int ret = ws2812_parse_hex(buffer, &color);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ws2812: invalid hex color\n");
      return -EINVAL;
    }

  g_ws2812_dev.color = color;
  ws2812_update();

  syslog(LOG_INFO, "ws2812: color = 0x%06x\n", color);
  return len;
}

static ssize_t ws2812_read(FAR struct file *filep,
                           FAR char *buffer,
                           size_t len)
{
  if (buffer == NULL || len < 7)
    {
      return -EINVAL;
    }

  snprintf(buffer, len, "%06X", g_ws2812_dev.color);
  return 6;
}

/****************************************************************************
 * Name: sf32lb_ws2812_initialize
 *
 * Description:
 *   Initialize the WS2812 LED driver and register as /dev/led0.
 *
 ****************************************************************************/

int sf32lb_ws2812_initialize(void)
{
  GPIO_InitTypeDef gpio_init;

  /* 配置PA32为GPIO输出 */

  gpio_init.Mode = GPIO_MODE_OUTPUT;
  gpio_init.Pin = WS2812_LED_PIN;
  gpio_init.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(hwp_gpio1, &gpio_init);

  /* 初始状态: 关闭 */

  g_ws2812_dev.color = 0x000000;
  ws2812_gpio_low();
  ws2812_send_reset();

  /* 注册字符设备 */

  int ret = register_driver("/dev/led0", &g_ws2812_ops, 0666, &g_ws2812_dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ws2812: register /dev/led0 failed: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "ws2812: registered /dev/led0\n");
  syslog(LOG_INFO, "ws2812: echo -ne 'FF0000' > /dev/led0 (red)\n");
  syslog(LOG_INFO, "ws2812: echo -ne '000000' > /dev/led0 (off)\n");

  return OK;
}

#endif /* CONFIG_WS2812 */
