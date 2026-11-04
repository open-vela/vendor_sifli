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
#include <syslog.h>

#include <nuttx/clock.h>
#include <nuttx/input/buttons.h>
#include <nuttx/wdog.h>

#include <arch/board/board.h>

#include "sifli_gpio.h"

#ifdef CONFIG_INPUT_BUTTONS

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

/* Both keys read high when pressed (pads default to pull-down).
 *
 * Key1 (PA34) is also the pin the ROM UART download path samples, which is
 * why bsp_pinmux.c deliberately leaves its pad alone.  We only set the GPIO
 * direction here -- sifli_gpio_config() does not touch the pad mux or pull,
 * so the download strap keeps working.
 */

#define SF32LB52_BUTTON_KEY2_PIN      GET_PIN_2(hwp_gpio1, 11)
#define SF32LB52_BUTTON_KEY1_PIN      GET_PIN_2(hwp_gpio1, 34)
#define SF32LB52_BUTTON_POLL_MS       10

struct sf32lb52_btnmap_s
{
  uint32_t pin;
  btn_buttonset_t bit;
  bool active_low;
};

static const struct sf32lb52_btnmap_s g_btnmap[] =
{
  { SF32LB52_BUTTON_KEY2_PIN, BUTTON_KEY2_BIT, false },
  { SF32LB52_BUTTON_KEY1_PIN, BUTTON_KEY1_BIT, false },
};

#define SF32LB52_NBUTTONS (int)(sizeof(g_btnmap) / sizeof(g_btnmap[0]))
#define SF32LB52_BUTTON_MASK (BUTTON_KEY2_BIT | BUTTON_KEY1_BIT)

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
  btn_buttonset_t set = 0;
  int i;

  for (i = 0; i < SF32LB52_NBUTTONS; i++)
    {
      bool level = sifli_gpio_read(g_btnmap[i].pin);

      if (level != g_btnmap[i].active_low)
        {
          set |= g_btnmap[i].bit;
        }
    }

  return set;
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
  return SF32LB52_BUTTON_MASK;
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
  int i;

  if (either == 0 || handler == NULL)
    {
      if (priv->irq_enabled)
        {
          for (i = 0; i < SF32LB52_NBUTTONS; i++)
            {
              sifli_gpio_set_event(g_btnmap[i].pin, false, false, NULL, NULL);
            }

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
      /* Arm both keys.  A key whose edge event cannot be installed still
       * works through the 10 ms poll below, so a failure here is not fatal.
       */

      for (i = 0; i < SF32LB52_NBUTTONS; i++)
        {
          ret = sifli_gpio_set_event(g_btnmap[i].pin, true, true,
                                     sf32lb52_button_interrupt, priv);
          if (ret >= 0)
            {
              priv->irq_enabled = true;
            }
          else
            {
              syslog(LOG_WARNING,
                     "WARN: button pin %lu edge event failed: %d "
                     "(falling back to poll)\n",
                     (unsigned long)g_btnmap[i].pin, ret);
            }
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
  int i;

  for (i = 0; i < SF32LB52_NBUTTONS; i++)
    {
      sifli_gpio_config(g_btnmap[i].pin, GPIO_INPUT);
    }

  ret = btn_register(devname, &g_btnlower.lower);
  if (ret < 0)
    {
      return ret;
    }

  /* Idle levels, logged once: a key that reads pressed while nothing is
   * touched means its pad is not muxed to GPIO (or the pull is wrong).
   */

  syslog(LOG_INFO, "INFO: buttons registered at %s, idle set=0x%x\n",
         devname, (unsigned)sf32lb52_button_readset());

  return OK;
}

#endif /* CONFIG_INPUT_BUTTONS */
