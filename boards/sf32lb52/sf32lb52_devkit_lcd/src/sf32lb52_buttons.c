/****************************************************************************
 * vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/src/sf32lb52_buttons.c
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
#include <stdint.h>
#include <errno.h>

#include <nuttx/clock.h>
#include <nuttx/input/buttons.h>
#include <nuttx/wdog.h>

#include "sifli_gpio.h"

#ifdef CONFIG_INPUT_BUTTONS

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define SF32LB52_BUTTON_KEY2_PIN      GET_PIN_2(hwp_gpio1, 11)
#define SF32LB52_BUTTON_KEY2_ACTIVE_LOW 0
#define SF32LB52_BUTTON_POLL_MS       10

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sf32lb52_btnlower_s
{
  struct btn_lowerhalf_s lower;
  btn_handler_t handler;
  FAR void *arg;
  bool irq_enabled;
  bool poll_enabled;
  btn_buttonset_t last_sample;
  struct wdog_s poll_wdog;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static btn_buttonset_t sf32lb52_btn_supported(
  FAR const struct btn_lowerhalf_s *lower);
static btn_buttonset_t sf32lb52_btn_buttons(
  FAR const struct btn_lowerhalf_s *lower);
static void sf32lb52_btn_enable(FAR const struct btn_lowerhalf_s *lower,
                                btn_buttonset_t press,
                                btn_buttonset_t release,
                                btn_handler_t handler,
                                FAR void *arg);
static void sf32lb52_button_poll(wdparm_t arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct sf32lb52_btnlower_s g_btnlower =
{
  {
    sf32lb52_btn_supported, /* bl_supported */
    sf32lb52_btn_buttons,   /* bl_buttons */
    sf32lb52_btn_enable,    /* bl_enable */
    NULL                    /* bl_write */
  },
  NULL,
  NULL,
  false,
  false,
  0,
  {0}
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static btn_buttonset_t sf32lb52_button_readset(void)
{
  bool level = sifli_gpio_read(SF32LB52_BUTTON_KEY2_PIN);

#if SF32LB52_BUTTON_KEY2_ACTIVE_LOW
  return level ? 0 : 1;
#else
  return level ? 1 : 0;
#endif
}

static void sf32lb52_button_interrupt(void *arg)
{
  FAR struct sf32lb52_btnlower_s *priv =
    (FAR struct sf32lb52_btnlower_s *)arg;
  btn_buttonset_t sample;

  if (priv == NULL)
    {
      return;
    }

  sample = sf32lb52_button_readset();
  if (sample == priv->last_sample)
    {
      return;
    }

  priv->last_sample = sample;

  if (priv->handler != NULL)
    {
      priv->handler(&priv->lower, priv->arg);
    }
}

static void sf32lb52_button_poll(wdparm_t arg)
{
  FAR struct sf32lb52_btnlower_s *priv =
    (FAR struct sf32lb52_btnlower_s *)arg;
  btn_buttonset_t sample;

  if (priv == NULL)
    {
      return;
    }

  if (priv->poll_enabled)
    {
      sample = sf32lb52_button_readset();
      if (sample != priv->last_sample)
        {
          priv->last_sample = sample;
          if (priv->handler != NULL)
            {
              priv->handler(&priv->lower, priv->arg);
            }
        }

      wd_start(&priv->poll_wdog, MSEC2TICK(SF32LB52_BUTTON_POLL_MS),
               sf32lb52_button_poll, (wdparm_t)priv);
    }
}

static btn_buttonset_t sf32lb52_btn_supported(
  FAR const struct btn_lowerhalf_s *lower)
{
  return 1;
}

static btn_buttonset_t sf32lb52_btn_buttons(
  FAR const struct btn_lowerhalf_s *lower)
{
  return sf32lb52_button_readset();
}

static void sf32lb52_btn_enable(FAR const struct btn_lowerhalf_s *lower,
                                btn_buttonset_t press,
                                btn_buttonset_t release,
                                btn_handler_t handler,
                                FAR void *arg)
{
  FAR struct sf32lb52_btnlower_s *priv =
    (FAR struct sf32lb52_btnlower_s *)lower;
  btn_buttonset_t either = press | release;
  int ret;

  if (either == 0 || handler == NULL)
    {
      if (priv->irq_enabled)
        {
          sifli_gpio_set_event(SF32LB52_BUTTON_KEY2_PIN, false, false,
                               NULL, NULL);
          priv->irq_enabled = false;
        }

      priv->poll_enabled = false;
      wd_cancel(&priv->poll_wdog);

      priv->handler = NULL;
      priv->arg     = NULL;
      return;
    }

  priv->handler = handler;
  priv->arg     = arg;
  priv->last_sample = sf32lb52_button_readset();

  if (!priv->irq_enabled)
    {
      ret = sifli_gpio_set_event(SF32LB52_BUTTON_KEY2_PIN, true, true,
                                 sf32lb52_button_interrupt, priv);
      if (ret >= 0)
        {
          priv->irq_enabled = true;
        }
      else
        {
          priv->handler = NULL;
          priv->arg     = NULL;
        }
    }

  if (!priv->poll_enabled)
    {
      priv->poll_enabled = true;
      wd_start(&priv->poll_wdog, MSEC2TICK(SF32LB52_BUTTON_POLL_MS),
               sf32lb52_button_poll, (wdparm_t)priv);
    }
}

int sf32lb52_button_initialize(FAR const char *devname)
{
  int ret;

  sifli_gpio_config(SF32LB52_BUTTON_KEY2_PIN, GPIO_INPUT);

  ret = btn_register(devname, &g_btnlower.lower);
  if (ret < 0)
    {
      return ret;
    }

  return OK;
}

#endif /* CONFIG_INPUT_BUTTONS */
