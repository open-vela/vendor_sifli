/****************************************************************************
 * vendor/sifli/boards/sf32lb52/drivers/input/ft6146.c
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

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include <nuttx/input/touchscreen.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/wqueue.h>

#include "sifli_gpio.h"
#include "drv_io.h"

#include "ft6146.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FT6146_NAME_TOUCH     "input0"
#define FT6146_I2C_DEF_FREQ   400000
#define FT6146_DEV_ADDR        0x38

#define FT6146_REG_TD_STATUS   0x02
#define FT6146_REG_P1_XH       0x03
#define FT6146_REG_ID_H        0xa3
#define FT6146_REG_ID_L        0x9f

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct ft6146_touch_lowerhalf_s
{
  struct touch_lowerhalf_s lower;
  struct i2c_master_s *i2c;
  struct work_s work;
#if CONFIG_INPUT_FT6146_POLL_MS > 0
  struct work_s pollwork;
  bool was_down;
#endif
  mutex_t devlock;
  sem_t waitsem;
  uint32_t pin_irq;
  uint16_t last_x;
  uint16_t last_y;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static ssize_t ft6146_touch_notify(FAR void *input_lower,
                                   FAR const char *buffer,
                                   size_t buflen);
static ssize_t ft6146_touch_write(FAR struct touch_lowerhalf_s *input_lower,
                                  FAR const char *buffer,
                                  size_t buflen);
static void ft6146_irq_handler(void *arg);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int ft6146_i2c_read(struct i2c_master_s *i2c,
                           uint8_t reg, uint8_t *buf, uint8_t len)
{
  struct i2c_msg_s msgs[2];
  int ret;

  msgs[0].addr      = FT6146_DEV_ADDR;
  msgs[0].frequency = FT6146_I2C_DEF_FREQ;
  msgs[0].flags     = I2C_M_NOSTOP;
  msgs[0].buffer    = &reg;
  msgs[0].length    = 1;

  msgs[1].addr      = FT6146_DEV_ADDR;
  msgs[1].frequency = FT6146_I2C_DEF_FREQ;
  msgs[1].flags     = I2C_M_READ | I2C_M_NOSTART;
  msgs[1].buffer    = buf;
  msgs[1].length    = len;

  ret = I2C_TRANSFER(i2c, msgs, 2);
  return ret < 0 ? ret : OK;
}

#ifdef CONFIG_INPUT_FT6146_TRACE
static unsigned int g_ft6146_irqs;
#endif

static void ft6146_report(struct ft6146_touch_lowerhalf_s *ft6146)
{
  struct touch_sample_s sample;
  uint8_t buf[5] = {0};
  uint8_t touch_num;
  int ret;

  ret = ft6146_i2c_read(ft6146->i2c, FT6146_REG_TD_STATUS, buf, sizeof(buf));
  if (ret < 0)
    {
      ierr("ft6146: read touch data failed: %d\n", ret);
#ifdef CONFIG_INPUT_FT6146_TRACE
      syslog(LOG_ERR, "[ft6146] i2c read failed: %d\n", ret);
#endif
      return;
    }

  touch_num = buf[0] & 0x0f;

#if CONFIG_INPUT_FT6146_POLL_MS > 0
  /* When polling, only publish transitions: a steady stream of TOUCH_UP
   * samples would keep waking the input upper half for nothing.
   */

  if (touch_num == 0 && !ft6146->was_down)
    {
      return;
    }

  ft6146->was_down = (touch_num > 0);
#endif

  sample.npoints = 1;
  if (touch_num > 0)
    {
      ft6146->last_x = ((uint16_t)(buf[1] & 0x0f) << 8) | buf[2];
      ft6146->last_y = ((uint16_t)(buf[3] & 0x0f) << 8) | buf[4];
      sample.point[0].flags = TOUCH_DOWN;
    }
  else
    {
      sample.point[0].flags = TOUCH_UP;
    }

  sample.point[0].x = ft6146->last_x;
  sample.point[0].y = ft6146->last_y;

#ifdef CONFIG_INPUT_FT6146_TRACE
  syslog(LOG_INFO, "[ft6146] irq=%u n=%u x=%u y=%u %s\n",
         g_ft6146_irqs, touch_num, sample.point[0].x, sample.point[0].y,
         touch_num > 0 ? "DOWN" : "UP");
#endif

  touch_event(ft6146->lower.priv, &sample);
}

static void ft6146_data_worker(void *param)
{
  ft6146_report((struct ft6146_touch_lowerhalf_s *)param);
}

#if CONFIG_INPUT_FT6146_POLL_MS > 0
static void ft6146_poll_worker(void *param)
{
  struct ft6146_touch_lowerhalf_s *ft6146 =
    (struct ft6146_touch_lowerhalf_s *)param;

  ft6146_report(ft6146);
  work_queue(LPWORK, &ft6146->pollwork, ft6146_poll_worker, ft6146,
             MSEC2TICK(CONFIG_INPUT_FT6146_POLL_MS));
}
#endif

static void ft6146_irq_handler(void *arg)
{
  struct ft6146_touch_lowerhalf_s *ft6146 =
    (struct ft6146_touch_lowerhalf_s *)arg;
  int ret;

#ifdef CONFIG_INPUT_FT6146_TRACE
  g_ft6146_irqs++;
#endif

  ret = work_queue(HPWORK, &ft6146->work, ft6146_data_worker, ft6146, 0);
  DEBUGASSERT(ret == OK);
}

static int ft6146_hw_init(struct ft6146_touch_lowerhalf_s *ft6146)
{
  uint8_t id_h = 0;
  uint8_t id_l = 0;
  int ret;

  BSP_TP_PowerUp();
  BSP_TP_Reset(0);
  usleep(5000);
  BSP_TP_Reset(1);
  usleep(80000);

  ret = sifli_gpio_config(ft6146->pin_irq, GPIO_INPUT);
  if (ret < 0)
    {
      ierr("ft6146: irq pin config failed: %d\n", ret);
      return ret;
    }

  ret = ft6146_i2c_read(ft6146->i2c, FT6146_REG_ID_H, &id_h, 1);
  if (ret < 0)
    {
      ierr("ft6146: read id_h failed: %d\n", ret);
      return ret;
    }

  ret = ft6146_i2c_read(ft6146->i2c, FT6146_REG_ID_L, &id_l, 1);
  if (ret < 0)
    {
      ierr("ft6146: read id_l failed: %d\n", ret);
      return ret;
    }

  iinfo("ft6146 id_h=0x%02x id_l=0x%02x\n", id_h, id_l);
#ifdef CONFIG_INPUT_FT6146_TRACE
  syslog(LOG_INFO, "[ft6146] id_h=0x%02x id_l=0x%02x irq_pin=%lu\n",
         id_h, id_l, (unsigned long)ft6146->pin_irq);
#endif

  ret = sifli_gpio_set_event(ft6146->pin_irq, false, true,
                             ft6146_irq_handler, ft6146);
  if (ret < 0)
    {
      ierr("ft6146: irq event setup failed: %d\n", ret);
      return ret;
    }

  /* The controller holds INT asserted until the host reads the touch data
   * register.  If it is already asserted when the falling edge is armed
   * above, no further edge is ever produced and the panel stays silent for
   * the rest of the session.  Drain it once so the edge can re-arm.
   */

  ft6146_i2c_read(ft6146->i2c, FT6146_REG_TD_STATUS, &id_l, 1);

#ifdef CONFIG_INPUT_FT6146_TRACE
  syslog(LOG_INFO, "[ft6146] armed irq on pin %lu, td_status=0x%02x\n",
         (unsigned long)ft6146->pin_irq, id_l);
#endif

#if CONFIG_INPUT_FT6146_POLL_MS > 0
  /* Some boards route the CTP interrupt to a pad that is not usable as an
   * edge source; polling keeps the panel working there.  It runs alongside
   * the interrupt, and ft6146_report() collapses repeats, so enabling both
   * costs one I2C read per interval and produces no duplicate events.
   */

  work_queue(LPWORK, &ft6146->pollwork, ft6146_poll_worker, ft6146,
             MSEC2TICK(CONFIG_INPUT_FT6146_POLL_MS));
#endif

  return OK;
}

static ssize_t ft6146_touch_notify(FAR void *input_lower,
                                   FAR const char *buffer,
                                   size_t buflen)
{
  FAR struct ft6146_touch_lowerhalf_s *ft6146 =
    (FAR struct ft6146_touch_lowerhalf_s *)input_lower;
  FAR const struct touch_sample_s *sample =
    (FAR const struct touch_sample_s *)buffer;

  touch_event(ft6146->lower.priv, sample);
  return buflen;
}

static ssize_t ft6146_touch_write(FAR struct touch_lowerhalf_s *input_lower,
                                  FAR const char *buffer,
                                  size_t buflen)
{
  FAR struct ft6146_touch_lowerhalf_s *ft6146 =
    (FAR struct ft6146_touch_lowerhalf_s *)input_lower;

  return ft6146_touch_notify(ft6146, buffer, buflen);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ft6146_touch_initialize(struct i2c_master_s *i2c, uint32_t irq_pin)
{
  FAR struct ft6146_touch_lowerhalf_s *ft6146;
  int ret;

  DEBUGASSERT(i2c != NULL);

  ft6146 = kmm_zalloc(sizeof(struct ft6146_touch_lowerhalf_s));
  if (ft6146 == NULL)
    {
      return -ENOMEM;
    }

  ft6146->lower.write = ft6146_touch_write;
  ft6146->lower.maxpoint = 1;
  ft6146->i2c = i2c;
  ft6146->pin_irq = irq_pin;

  nxmutex_init(&ft6146->devlock);
  nxsem_init(&ft6146->waitsem, 0, 0);

  ret = touch_register(&ft6146->lower, "/dev/" FT6146_NAME_TOUCH, 16);
  if (ret < 0)
    {
      nxmutex_destroy(&ft6146->devlock);
      nxsem_destroy(&ft6146->waitsem);
      kmm_free(ft6146);
      return ret;
    }

  ret = ft6146_hw_init(ft6146);
  if (ret < 0)
    {
      touch_unregister(&ft6146->lower, "/dev/" FT6146_NAME_TOUCH);
      nxmutex_destroy(&ft6146->devlock);
      nxsem_destroy(&ft6146->waitsem);
      kmm_free(ft6146);
      return ret;
    }

  return OK;
}
