/****************************************************************************
 * vendor/sifli/chips/sf32lb52/sf32lb_timer.c
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

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <debug.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/clock.h>
#include <nuttx/timers/timer.h>

#ifdef CONFIG_BSP_USING_ATIM1
#  define BSP_USING_ATIM1 CONFIG_BSP_USING_ATIM1
#endif
#ifdef CONFIG_BSP_USING_ATIM2
#  define BSP_USING_ATIM2 CONFIG_BSP_USING_ATIM2
#endif
#ifdef CONFIG_BSP_USING_BTIM1
#  define BSP_USING_BTIM1 CONFIG_BSP_USING_BTIM1
#endif
#ifdef CONFIG_BSP_USING_BTIM2
#  define BSP_USING_BTIM2 CONFIG_BSP_USING_BTIM2
#endif
#ifdef CONFIG_BSP_USING_BTIM3
#  define BSP_USING_BTIM3 CONFIG_BSP_USING_BTIM3
#endif
#ifdef CONFIG_BSP_USING_BTIM4
#  define BSP_USING_BTIM4 CONFIG_BSP_USING_BTIM4
#endif

#include <bf0_hal.h>
#include "tim_config.h"
#include "sf32lb_timer.h"

#if 0
#  undef tmrinfo
#  define tmrinfo _info
#endif

#if defined(CONFIG_BSP_USING_ATIM1) || defined(CONFIG_BSP_USING_ATIM2) || \
    defined(CONFIG_BSP_USING_BTIM1) || defined(CONFIG_BSP_USING_BTIM2) || \
    defined(CONFIG_BSP_USING_BTIM3) || defined(CONFIG_BSP_USING_BTIM4)

struct sf32lb_timer_lowerhalf_s
{
  /* This is the part of the lower half driver that is visible to the upper-
   * half client of the driver.
   */

  FAR const struct timer_ops_s *ops;
  GPT_HandleTypeDef tim_handle;        /* HW timer low level handle */
  IRQn_Type tim_irqn;                  /* interrupt number for timer */
  FAR char *name;                      /* HW timer device name */
  uint8_t core;                        /* Clock source from which core */

  volatile bool running;               /* True: the timer is running */
  tccb_t cbk;                          /* Call back function when timeout */
  FAR void *arg;                       /* The argument that will accompany */
  uint32_t frequency;
  uint32_t period;
  uint32_t timeout;
};

static struct sf32lb_timer_lowerhalf_s g_low_timer[] =
{
#if defined(CONFIG_BSP_USING_ATIM1)
  ATIM1_CONFIG,
#endif
#if defined(CONFIG_BSP_USING_ATIM2)
  ATIM2_CONFIG,
#endif
#if defined(CONFIG_BSP_USING_BTIM1)
  BTIM1_CONFIG,
#endif
#if defined(CONFIG_BSP_USING_BTIM2)
  BTIM2_CONFIG,
#endif
#if defined(CONFIG_BSP_USING_BTIM3)
  BTIM3_CONFIG,
#endif
#if defined(CONFIG_BSP_USING_BTIM4)
  BTIM4_CONFIG,
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int sf32lb_timer_handler(int irq, void *context, void *arg);
static int sf32lb_timer_stop(struct timer_lowerhalf_s *lower);
static int sf32lb_timer_start(struct timer_lowerhalf_s *lower);
static int sf32lb_timer_getstatus(struct timer_lowerhalf_s *lower,
                                  struct timer_status_s *status);
static int sf32lb_timer_settimeout(struct timer_lowerhalf_s *lower,
                                   uint32_t timeout);
static void sf32lb_timer_setcallback(struct timer_lowerhalf_s *lower,
                                     tccb_t callback, void *arg);
static int sf32lb_timer_maxtimeout(struct timer_lowerhalf_s *lower,
                                   uint32_t *maxtimeout);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct timer_ops_s g_timer_ops =
{
  .start       = sf32lb_timer_start,
  .stop        = sf32lb_timer_stop,
  .getstatus   = sf32lb_timer_getstatus,
  .settimeout  = sf32lb_timer_settimeout,
  .setcallback = sf32lb_timer_setcallback,
  .maxtimeout  = sf32lb_timer_maxtimeout,
  .ioctl       = NULL,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void timer_init(struct sf32lb_timer_lowerhalf_s *timer)
{
  uint32_t prescaler_value;
  FAR GPT_HandleTypeDef *tim;
  irqstate_t flags;

  DEBUGASSERT(timer != NULL);

  flags = enter_critical_section();
  tim = &timer->tim_handle;

  prescaler_value = HAL_RCC_GetPCLKFreq(timer->core, 1) / timer->frequency - 1;
  tim->Init.Period            = 10000 - 1;
  tim->Init.Prescaler         = prescaler_value;
  tim->core                   = timer->core;
  tim->Init.CounterMode       = GPT_COUNTERMODE_UP;
  tim->Init.RepetitionCounter = 0;

  if (HAL_GPT_Base_Init(tim) != HAL_OK)
    {
      tmrerr("%s init failed\n", timer->name);
      leave_critical_section(flags);
      return;
    }

  timer->ops = &g_timer_ops;
  irq_attach(timer->tim_irqn + 16, sf32lb_timer_handler, timer);

  /* Clear pending update and keep update request source to overflow event. */

  __HAL_GPT_CLEAR_FLAG(tim, GPT_FLAG_UPDATE);
  __HAL_GPT_URS_ENABLE(tim);

  tmrinfo("%s init success\n", timer->name);
  leave_critical_section(flags);
}

static int sf32lb_timer_handler(int irq, void *context, void *arg)
{
  FAR struct sf32lb_timer_lowerhalf_s *timer =
    (FAR struct sf32lb_timer_lowerhalf_s *)arg;
  uint32_t next_interval_us = 0;
  tccb_t timer_handler;

  DEBUGASSERT(timer != NULL);

  /* Acknowledge/clear interrupt flags first. */

  HAL_GPT_IRQHandler(&timer->tim_handle);

  timer_handler = timer->cbk;
  if (timer_handler != NULL && timer_handler(&next_interval_us, timer->arg))
    {
      if (next_interval_us > 0)
        {
          sf32lb_timer_settimeout((struct timer_lowerhalf_s *)timer,
                                  next_interval_us);
        }

      /* Re-arm single-shot hardware timer for periodic callback path. */

      HAL_GPT_Base_Start_IT(&timer->tim_handle);
    }
  else
    {
      sf32lb_timer_stop((struct timer_lowerhalf_s *)timer);
    }

  return OK;
}

static void sf32lb_timer_setcallback(struct timer_lowerhalf_s *lower,
                                     tccb_t callback, void *arg)
{
  FAR struct sf32lb_timer_lowerhalf_s *priv =
    (FAR struct sf32lb_timer_lowerhalf_s *)lower;
  irqstate_t flags;

  flags = enter_critical_section();
  priv->cbk = callback;
  priv->arg = arg;
  leave_critical_section(flags);
}

static int sf32lb_timer_settimeout(struct timer_lowerhalf_s *lower,
                                   uint32_t timeout)
{
  FAR struct sf32lb_timer_lowerhalf_s *timer =
    (FAR struct sf32lb_timer_lowerhalf_s *)lower;
  uint64_t period;

  period = timeout;
  period = (period * (uint64_t)timer->frequency) / USEC_PER_SEC;
  DEBUGASSERT(period > 0);
  DEBUGASSERT(period <= UINT32_MAX);

  timer->period = (uint32_t)period;
  timer->timeout = timeout;
  __HAL_GPT_SET_AUTORELOAD(&timer->tim_handle, timer->period);

  return OK;
}

static int sf32lb_timer_getstatus(struct timer_lowerhalf_s *lower,
                                  struct timer_status_s *status)
{
  FAR struct sf32lb_timer_lowerhalf_s *priv =
    (FAR struct sf32lb_timer_lowerhalf_s *)lower;

  DEBUGASSERT(priv != NULL);
  DEBUGASSERT(status != NULL);

  status->flags = 0;

  if (priv->running)
    {
      status->flags |= TCFLAGS_ACTIVE;
    }

  if (priv->cbk != NULL)
    {
      status->flags |= TCFLAGS_HANDLER;
    }

  status->timeleft = __HAL_GPT_GET_COUNTER(&priv->tim_handle) * 1000000 /
                     priv->frequency;
  status->timeout = priv->timeout;

  if (status->timeleft > status->timeout)
    {
      status->timeleft = 0;
    }
  else
    {
      status->timeleft = status->timeout - status->timeleft;
    }

  return OK;
}

static int sf32lb_timer_start(struct timer_lowerhalf_s *lower)
{
  irqstate_t flags;
  int ret;
  FAR struct sf32lb_timer_lowerhalf_s *timer =
    (FAR struct sf32lb_timer_lowerhalf_s *)lower;

  DEBUGASSERT(timer != NULL);

  flags = enter_critical_section();
  if (timer->running)
    {
      sf32lb_timer_stop(lower);
    }

  timer->tim_handle.Instance->CR1 |= GPT_OPMODE_SINGLE;

  ret = HAL_GPT_Base_Start_IT(&timer->tim_handle);
  if (ret == HAL_OK)
    {
      up_enable_irq(timer->tim_irqn + 16);
      timer->running = true;
      ret = OK;
    }
  else
    {
      ret = -EIO;
    }

  leave_critical_section(flags);
  return ret;
}

static int sf32lb_timer_stop(struct timer_lowerhalf_s *lower)
{
  FAR struct sf32lb_timer_lowerhalf_s *timer =
    (FAR struct sf32lb_timer_lowerhalf_s *)lower;
  irqstate_t flags;

  flags = enter_critical_section();
  if (!timer->running)
    {
      leave_critical_section(flags);
      return OK;
    }

  HAL_GPT_Base_Stop_IT(&timer->tim_handle);
  up_disable_irq(timer->tim_irqn + 16);
  timer->running = false;

  leave_critical_section(flags);
  return OK;
}

static int sf32lb_timer_maxtimeout(struct timer_lowerhalf_s *lower,
                                   uint32_t *maxtimeout)
{
  if (maxtimeout != NULL)
    {
      *maxtimeout = UINT32_MAX;
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

struct timer_lowerhalf_s *sf32lb_timer_initialize(int chan, uint16_t resolution)
{
  FAR struct sf32lb_timer_lowerhalf_s *timer;

  DEBUGASSERT(resolution > 0);
  DEBUGASSERT(chan >= 0 && chan < BTIM_MAX);

  timer = &g_low_timer[chan];
  timer->frequency = resolution;
  timer_init(timer);

  timer->running = false;
  timer->cbk     = NULL;
  timer->arg     = NULL;

  return (struct timer_lowerhalf_s *)timer;
}

#else

struct timer_lowerhalf_s *sf32lb_timer_initialize(int chan, uint16_t resolution)
{
  UNUSED(chan);
  UNUSED(resolution);
  return NULL;
}

#endif
