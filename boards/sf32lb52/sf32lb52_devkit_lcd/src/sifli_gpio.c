/****************************************************************************
 * vendor/sifli/boards/sf32lb56x-hdk/src/sf32lb_gpio.c
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
#include <assert.h>
#include <debug.h>

#include <nuttx/clock.h>
#include <nuttx/wdog.h>
#include <nuttx/ioexpander/gpio.h>

#include <arch/board/board.h>

#include "sifli_gpio.h"
#include "sf32lb52_devkit_lcd.h"



#if defined(CONFIG_DEV_GPIO) && !defined(CONFIG_GPIO_LOWER_HALF)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sifli_gpio_dev_s
{
  struct gpio_dev_s gpio;
  uint8_t id;
};

struct sifli_gpint_dev_s
{
  struct sifli_gpio_dev_s sifli_gpio;
  pin_interrupt_t callback;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int gpin_read(struct gpio_dev_s *dev, bool *value);
static int gpin_write(struct gpio_dev_s *dev, bool value);
static int gpin_setpintype(struct gpio_dev_s *dev, enum gpio_pintype_e pintype);
static int gpout_read(struct gpio_dev_s *dev, bool *value);
static int gpout_write(struct gpio_dev_s *dev, bool value);
static int gpout_setpintype(struct gpio_dev_s *dev, enum gpio_pintype_e pintype);
static int gpint_read(struct gpio_dev_s *dev, bool *value);
static int gpint_write(struct gpio_dev_s *dev, bool value);
static int gpint_attach(struct gpio_dev_s *dev,
                        pin_interrupt_t callback);
static int gpint_enable(struct gpio_dev_s *dev, bool enable);
static int gpint_setpintype(struct gpio_dev_s *dev, enum gpio_pintype_e pintype);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct gpio_operations_s gpin_ops =
{
  .go_read   = gpin_read,
  .go_write  = gpin_write,
  .go_attach = NULL,
  .go_enable = NULL,
  .go_setpintype = gpin_setpintype,  
};

static const struct gpio_operations_s gpout_ops =
{
  .go_read   = gpout_read,
  .go_write  = gpout_write,
  .go_attach = NULL,
  .go_enable = NULL,
  .go_setpintype = gpout_setpintype,  
};

static const struct gpio_operations_s gpint_ops =
{
  .go_read   = gpint_read,
  .go_write  = gpint_write,
  .go_attach = gpint_attach,
  .go_enable = gpint_enable,
  .go_setpintype = gpint_setpintype,
};

#if BOARD_NGPIOIN > 0
/* This array maps the GPIO pins used as INPUT */

static const uint32_t g_gpioinputs[BOARD_NGPIOIN] =
{
  GPIO_IN1,
};

static struct sifli_gpio_dev_s g_gpin[BOARD_NGPIOIN];
#endif

#if BOARD_NGPIOOUT
/* This array maps the GPIO pins used as OUTPUT */

static const uint32_t g_gpiooutputs[BOARD_NGPIOOUT] =
{
  GPIO_OUT1,
};

static struct sifli_gpio_dev_s g_gpout[BOARD_NGPIOOUT];
#endif

#if BOARD_NGPIOINT > 0
/* This array maps the GPIO pins used as INTERRUPT INPUTS */

static const uint32_t g_gpiointinputs[BOARD_NGPIOINT] =
{
  GPIO_INT1,
};

static struct sifli_gpint_dev_s g_gpint[BOARD_NGPIOINT];
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

//static int sifli_gpio_interrupt(int irq, void *context, void *arg)
static void sifli_gpio_interrupt(void *arg)
{
  struct sifli_gpint_dev_s *gpint_dev =
                        (struct sifli_gpint_dev_s *)arg;

  DEBUGASSERT(gpint_dev != NULL && gpint_dev->callback != NULL);
  gpioinfo("Interrupt! callback=%p\n", gpint_dev->callback);

  gpint_dev->callback(&gpint_dev->sifli_gpio.gpio,
                       gpint_dev->sifli_gpio.id);
  return;
}

static int gpin_read(struct gpio_dev_s *dev, bool *value)
{
  struct sifli_gpio_dev_s *gpio_dev =
                        (struct sifli_gpio_dev_s *)dev;

  DEBUGASSERT(gpio_dev != NULL && value != NULL);
  DEBUGASSERT(gpio_dev->id < BOARD_NGPIOIN);
  gpioinfo("Reading...\n");

  *value = sifli_gpio_read(g_gpioinputs[gpio_dev->id]);
  return OK;
}

static int gpin_write(struct gpio_dev_s *dev, bool value)
{
  struct sifli_gpio_dev_s *gpio_dev =
                             (struct sifli_gpio_dev_s *)dev;

  DEBUGASSERT(gpio_dev != NULL);
  DEBUGASSERT(gpio_dev->id < BOARD_NGPIOIN);
  gpioinfo("Writing %d\n", (int)value);

  sifli_gpio_write(g_gpioinputs[gpio_dev->id], value);
  return OK;
}

static int gpin_setpintype(struct gpio_dev_s *dev, enum gpio_pintype_e pintype)
{
  struct sifli_gpio_dev_s *gpio_dev =
                              (struct sifli_gpio_dev_s *)dev;

  DEBUGASSERT(gpio_dev != NULL);
  DEBUGASSERT(gpio_dev->id < BOARD_NGPIOIN);

  dev->gp_pintype = pintype;

  if ((pintype == GPIO_OUTPUT_PIN) || (pintype == GPIO_OUTPUT_PIN_OPENDRAIN))
  {
    sifli_gpio_config(g_gpioinputs[gpio_dev->id], GPIO_OUTPUT);
  }
  else
  {
    sifli_gpio_config(g_gpioinputs[gpio_dev->id], GPIO_INPUT);
  }
 
  return OK;
}


static int gpout_read(struct gpio_dev_s *dev, bool *value)
{
  struct sifli_gpio_dev_s *gpio_dev =
                        (struct sifli_gpio_dev_s *)dev;

  DEBUGASSERT(gpio_dev != NULL && value != NULL);
  DEBUGASSERT(gpio_dev->id < BOARD_NGPIOOUT);
  gpioinfo("Reading...\n");

  *value = sifli_gpio_read(g_gpiooutputs[gpio_dev->id]);
  return OK;
}

static int gpout_write(struct gpio_dev_s *dev, bool value)
{
  struct sifli_gpio_dev_s *gpio_dev =
                             (struct sifli_gpio_dev_s *)dev;

  DEBUGASSERT(gpio_dev != NULL);
  DEBUGASSERT(gpio_dev->id < BOARD_NGPIOOUT);
  gpioinfo("Writing %d\n", (int)value);

  sifli_gpio_write(g_gpiooutputs[gpio_dev->id], value);
  return OK;
}

static int gpout_setpintype(struct gpio_dev_s *dev, enum gpio_pintype_e pintype)
{
  struct sifli_gpio_dev_s *gpio_dev =
                              (struct sifli_gpio_dev_s *)dev;

  DEBUGASSERT(gpio_dev != NULL);
  DEBUGASSERT(gpio_dev->id < BOARD_NGPIOOUT);

  dev->gp_pintype = pintype;

  if ((pintype == GPIO_OUTPUT_PIN) || (pintype == GPIO_OUTPUT_PIN_OPENDRAIN))
  {
    sifli_gpio_config(g_gpiooutputs[gpio_dev->id], GPIO_OUTPUT);
  }
  else
  {
    sifli_gpio_config(g_gpiooutputs[gpio_dev->id], GPIO_INPUT);
  }
 
  return OK;
}

static int gpint_read(struct gpio_dev_s *dev, bool *value)
{
  struct sifli_gpint_dev_s *gpint_dev =
                              (struct sifli_gpint_dev_s *)dev;

  DEBUGASSERT(gpint_dev != NULL && value != NULL);
  DEBUGASSERT(gpint_dev->sifli_gpio.id < BOARD_NGPIOINT);
  gpioinfo("Reading int pin...\n");

  *value = sifli_gpio_read(g_gpiointinputs[gpint_dev->sifli_gpio.id]);
  return OK;
}

static int gpint_write(struct gpio_dev_s *dev, bool value)
{
  struct sifli_gpint_dev_s *gpint_dev =
                             (struct sifli_gpint_dev_s *)dev;

  DEBUGASSERT(gpint_dev != NULL);
  DEBUGASSERT(gpint_dev->sifli_gpio.id < BOARD_NGPIOINT);
  gpioinfo("Writing %d\n", (int)value);

  sifli_gpio_write(g_gpiointinputs[gpint_dev->sifli_gpio.id], value);
  return OK;
}


static int gpint_attach(struct gpio_dev_s *dev,
                        pin_interrupt_t callback)
{
  struct sifli_gpint_dev_s *gpint_dev =
                             (struct sifli_gpint_dev_s *)dev;

  gpioinfo("Attaching the callback\n");

  /* Make sure the interrupt is disabled */

  sifli_gpio_set_event(g_gpiointinputs[gpint_dev->sifli_gpio.id], false,
                     false, NULL, NULL);

  gpioinfo("Attach %p\n", callback);
  gpint_dev->callback = callback;
  return OK;
}

static int gpint_enable(struct gpio_dev_s *dev, bool enable)
{
  struct sifli_gpint_dev_s *gpint_dev =
                              (struct sifli_gpint_dev_s *)dev;

  if (enable)
    {
      if (gpint_dev->callback != NULL)
        {
          gpioinfo("Enabling the interrupt\n");

          /* Configure the interrupt for rising edge */

          sifli_gpio_set_event(g_gpiointinputs[gpint_dev->sifli_gpio.id],
                             true, true, sifli_gpio_interrupt,
                             &g_gpint[gpint_dev->sifli_gpio.id]);
        }
    }
  else
    {
      gpioinfo("Disable the interrupt\n");
      sifli_gpio_set_event(g_gpiointinputs[gpint_dev->sifli_gpio.id],
                         false, false, NULL, NULL);
    }

  return OK;
}

static int gpint_setpintype(struct gpio_dev_s *dev, enum gpio_pintype_e pintype)
{
  struct sifli_gpint_dev_s *gpint_dev =
                              (struct sifli_gpint_dev_s *)dev;

  DEBUGASSERT(gpint_dev != NULL);
  DEBUGASSERT(gpint_dev->sifli_gpio.id < BOARD_NGPIOINT);

  dev->gp_pintype = pintype;

  if ((pintype == GPIO_OUTPUT_PIN) || (pintype == GPIO_OUTPUT_PIN_OPENDRAIN))
  {
    sifli_gpio_config(g_gpiointinputs[gpint_dev->sifli_gpio.id], GPIO_OUTPUT);
  }
  else
  {
    sifli_gpio_config(g_gpiointinputs[gpint_dev->sifli_gpio.id], GPIO_INPUT);
  }
 
  return OK;
}



/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: stm32_gpio_initialize
 *
 * Description:
 *   Initialize GPIO drivers for use with /apps/examples/gpio
 *
 ****************************************************************************/

int sifli_gpio_initialize(void)
{
  int i;
  int pincount = 0;
#if BOARD_NGPIOIN > 0
  for (i = 0; i < BOARD_NGPIOIN; i++)
    {
      /* Setup and register the GPIO pin */

      g_gpin[i].gpio.gp_pintype = GPIO_INPUT_PIN;
      g_gpin[i].gpio.gp_ops     = &gpin_ops;
      g_gpin[i].id              = i;
      gpio_pin_register(&g_gpin[i].gpio, pincount);

      /* Configure the pin that will be used as input */

      sifli_gpio_config(g_gpioinputs[i], GPIO_INPUT);

      pincount++;
    }
#endif

#if BOARD_NGPIOOUT > 0
  for (i = 0; i < BOARD_NGPIOOUT; i++)
    {
      /* Setup and register the GPIO pin */

      g_gpout[i].gpio.gp_pintype = GPIO_OUTPUT_PIN;
      g_gpout[i].gpio.gp_ops     = &gpout_ops;
      g_gpout[i].id              = i;
      gpio_pin_register(&g_gpout[i].gpio, pincount);

      /* Configure the pin that will be used as output */

      sifli_gpio_config(g_gpiooutputs[i], GPIO_OUTPUT);
      sifli_gpio_write(g_gpiooutputs[i], 0);

      pincount++;
    }
#endif

#if BOARD_NGPIOINT > 0
  for (i = 0; i < BOARD_NGPIOINT; i++)
    {
      /* Setup and register the GPIO pin */

      g_gpint[i].sifli_gpio.gpio.gp_pintype = GPIO_INTERRUPT_BOTH_PIN;
      g_gpint[i].sifli_gpio.gpio.gp_ops     = &gpint_ops;
      g_gpint[i].sifli_gpio.id              = i;
      gpio_pin_register(&g_gpint[i].sifli_gpio.gpio, pincount);

      /* Configure the pin that will be used as interrupt input */

      sifli_gpio_config(g_gpiointinputs[i], GPIO_INPUT);

      pincount++;
    }
#endif

  return 0;
}
#endif /* CONFIG_DEV_GPIO && !CONFIG_GPIO_LOWER_HALF */
