/****************************************************************************
 * vendor/sifli/boards/sf32lb52_devkit_lcd/src/sifli_ap.c
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
// specify chip arch internal header
// eg: arm_internal.h riscv_internal.h
#include <nuttx/config.h>

#include <syslog.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <debug.h>

#if defined(CONFIG_RTC) && defined(CONFIG_RTC_DRIVER)
#  include <nuttx/timers/rtc.h>
#endif

#include "arm_internal.h"
#include "sf32lb52_devkit_lcd.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "sifli_gpio.h"

#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/lcd/lcd_dev.h>
#include <nuttx/timers/pwm.h>
#include <nuttx/timers/timer.h>
#include <nuttx/video/fb.h>
#if defined(CONFIG_I2C) && defined(CONFIG_SENSORS_LSM6DSL)
#  include <nuttx/sensors/lsm6dsl.h>
#endif
#if defined(CONFIG_SPI) && defined(CONFIG_BSP_USING_SPI1)
#  include <nuttx/spi/spi.h>
#  include "sf32lb_spi.h"
#  ifdef CONFIG_SPI_DRIVER
#    include <nuttx/spi/spi_transfer.h>
#  endif
#endif

#if defined(CONFIG_SPI) && defined(CONFIG_MMCSD_SPI) && \
  defined(CONFIG_BSP_USING_SPI1)
#  include <nuttx/mmcsd.h>
#endif

#ifdef CONFIG_ADC
extern int sf32lb_adc_init(const char *devpath);
#endif

#ifdef CONFIG_CDCACM
#  include <nuttx/usb/cdcacm.h>
#endif

#ifdef CONFIG_MTD
extern int sf32lb_nor_automount(int minor, int block_offset, int block_count);
#endif

/* Follow SiFli SDK partition table (sf32lb52-lchspi-ulp):
 * FS_REGION offset=0x009A0000 size=0x00400000 on flash2.
 */
#define SF32LB52_NOR_FS_OFFSET_BLOCKS (0x009A0000 / 4096)
#define SF32LB52_NOR_FS_SIZE_BLOCKS   (0x00400000 / 4096)

#if defined(CONFIG_SPI) && defined(CONFIG_BSP_USING_SPI1)
#  define SF32LB52_SPI1_PORT           0
#endif

#if defined(CONFIG_I2C) && defined(CONFIG_SENSORS_LSM6DSL)
#  define SF32LB52_LSM6DS3_I2C_BUS     1
#  define SF32LB52_LSM6DS3_DEVPATH     "/dev/lsm6dsl0"
#  define SF32LB52_LSM6DS3_LDO_PIN     GET_PIN_2(hwp_gpio1, 30)
#  define SF32LB52_LSM6DS3_INT_PIN     GET_PIN_2(hwp_gpio1, 31)
#endif

#if defined(CONFIG_SPI) && defined(CONFIG_MMCSD_SPI) && \
  defined(CONFIG_BSP_USING_SPI1)
#  define SF32LB52_TFCARD_SPI_PORT     0
#  define SF32LB52_TFCARD_SLOT         0
#  define SF32LB52_TFCARD_MINOR        0
#  define SF32LB52_TFCARD_MOUNTPOINT   "/data/tf"
#endif

#if defined(CONFIG_RTC) && defined(CONFIG_RTC_DRIVER)
#  include "sf32lb_rtc.h"
#endif

#ifdef CONFIG_WATCHDOG
extern void sf32lb_iwdginitialize(const char *devpath);
#endif

#ifdef CONFIG_UART_BTH4
extern int sf32lb52_bt_initialize(void);
#endif

#ifdef CONFIG_TIMER
#  include "sf32lb_timer.h"
#endif

#ifdef CONFIG_PWM
#  include "sf32lb_pwm.h"
#endif

#ifdef CONFIG_INPUT_FT6146
int ft6146_touch_initialize(struct i2c_master_s *i2c, uint32_t irq_pin);
#endif

#if defined(CONFIG_SPI) && defined(CONFIG_BSP_USING_SPI1)
static struct spi_dev_s *g_sf32lb52_spi1;

static FAR struct spi_dev_s *sf32lb52_spi1_getbus(void)
{
  if (g_sf32lb52_spi1 == NULL)
    {
      g_sf32lb52_spi1 = sifli_spibus_initialize(SF32LB52_SPI1_PORT);
    }

  return g_sf32lb52_spi1;
}

#ifdef CONFIG_SPI_DRIVER
static int sf32lb52_spi1_register_char(void)
{
  FAR struct spi_dev_s *spi;
  int ret;

  spi = sf32lb52_spi1_getbus();
  if (spi == NULL)
    {
      return -ENODEV;
    }

  ret = spi_register(spi, 1);
  if (ret < 0 && ret != -EEXIST)
    {
      return ret;
    }

  return OK;
}
#endif

#if defined(CONFIG_MMCSD_SPI)
static int sf32lb52_tfcard_mount(void)
{
#ifndef CONFIG_DISABLE_MOUNTPOINT
  static const char *const devpaths[] =
  {
    "/dev/mmcsd0p0",
    "/dev/mmcsd0"
  };
  int tmpret;
  int ret = -ENODEV;
  size_t i;

  tmpret = mkdir(SF32LB52_TFCARD_MOUNTPOINT, 0755);
  if (tmpret < 0 && errno != EEXIST)
    {
      serr("WARN: mkdir %s failed: %d\n", SF32LB52_TFCARD_MOUNTPOINT, errno);
      return -errno;
    }

  for (i = 0; i < sizeof(devpaths) / sizeof(devpaths[0]); i++)
    {
      ret = nx_mount(devpaths[i], SF32LB52_TFCARD_MOUNTPOINT, "vfat", 0, NULL);
      if (ret >= 0 || ret == -EBUSY)
        {
          syslog(LOG_INFO, "INFO: TF mounted from %s to %s\n",
                 devpaths[i], SF32LB52_TFCARD_MOUNTPOINT);
          return OK;
        }
    }

  return ret;
#else
  return -ENOSYS;
#endif
}

static int sf32lb52_tfcard_initialize(void)
{
  FAR struct spi_dev_s *spi;
  int ret;

  spi = sf32lb52_spi1_getbus();
  if (spi == NULL)
    {
      serr("ERROR: sifli_spibus_initialize(%d) failed\n",
           SF32LB52_TFCARD_SPI_PORT);
      return -ENODEV;
    }

  ret = mmcsd_spislotinitialize(SF32LB52_TFCARD_MINOR,
                                SF32LB52_TFCARD_SLOT,
                                spi);
  if (ret < 0)
    {
      serr("ERROR: mmcsd_spislotinitialize failed: %d\n", ret);
      return ret;
    }

  ret = sf32lb52_tfcard_mount();
  if (ret < 0)
    {
      serr("WARN: TF mount failed: %d\n", ret);
    }

  return ret;
}
#endif
#endif

#if defined(CONFIG_I2C) && defined(CONFIG_SENSORS_LSM6DSL)
static int sf32lb52_lsm6ds3_initialize(FAR struct i2c_master_s *i2c)
{
  int ret;

  /* LSM6DS3TR-C is register-compatible enough with the in-tree LSM6DSL
   * test driver for board bringup.  The board wiring is:
   *   PA39 - I2C2 SDA
   *   PA40 - I2C2 SCL
   *   PA31 - INT
   *   PA30 - sensor LDO enable, active high
   */

  HAL_PIN_Set(PAD_PA30, GPIO_A30, PIN_NOPULL, 1);
  sifli_gpio_config(SF32LB52_LSM6DS3_LDO_PIN, GPIO_OUTPUT);
  sifli_gpio_write(SF32LB52_LSM6DS3_LDO_PIN, true);
  usleep(10000);

  HAL_PIN_Set(PAD_PA31, GPIO_A31, PIN_PULLUP, 1);
  sifli_gpio_config(SF32LB52_LSM6DS3_INT_PIN, GPIO_INPUT);

  ret = lsm6dsl_sensor_register(SF32LB52_LSM6DS3_DEVPATH,
                                i2c,
                                LSM6DSLACCEL_ADDR0);
  if (ret < 0)
    {
      syslog(LOG_WARNING,
             "WARN: LSM6DS3 not found at 0x%02x on I2C%d: %d\n",
             LSM6DSLACCEL_ADDR0, SF32LB52_LSM6DS3_I2C_BUS, ret);

      ret = lsm6dsl_sensor_register(SF32LB52_LSM6DS3_DEVPATH,
                                    i2c,
                                    LSM6DSLACCEL_ADDR1);
    }

  if (ret < 0)
    {
      syslog(LOG_ERR,
             "ERROR: LSM6DS3 register failed on I2C%d: %d\n",
             SF32LB52_LSM6DS3_I2C_BUS, ret);
      return ret;
    }

  syslog(LOG_INFO,
         "INFO: LSM6DS3 test device registered as %s, INT=%d\n",
         SF32LB52_LSM6DS3_DEVPATH, SF32LB52_LSM6DS3_INT_PIN);
  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_LCD
#define LCD_INIT_TASK_STACKSIZE 4096
#define LCD_INIT_TASK_PRIORITY (SCHED_PRIORITY_DEFAULT - 5)

static struct i2c_master_s *g_pending_touch_i2c = NULL;

static int lcd_async_init_thread(int argc, FAR char *argv[])
{
  int ret;

  ret = board_lcd_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: board_lcd_initialize failed: %d\n", ret);
      return ret;
    }

#ifdef CONFIG_INPUT_FT6146
  if (g_pending_touch_i2c != NULL)
    {
      usleep(80000);
      ret = ft6146_touch_initialize(g_pending_touch_i2c,
                                    GET_PIN_2(hwp_gpio1,
                                              CONFIG_TOUCH_IRQ_PIN));
      if (ret < 0)
        {
          syslog(LOG_ERR,
                 "ERROR: ft6146_touch_initialize failed: %d\n", ret);
        }
    }
#endif

  return OK;
}
#endif

/****************************************************************************
 * Name: sf32lb52_lchspi_ulp_bringup
 *
 * Description:
 *   Perform architecture-specific initialization
 *
 *   CONFIG_BOARD_LATE_INITIALIZE=y
 *     Called from board_late_initialize().
 *
 *   CONFIG_BOARD_LATE_INITIALIZE=n && CONFIG_BOARDCTL=y
 *     Called from the NSH library
 *
 ****************************************************************************/

int sf32lb52_lchspi_ulp_bringup(void)
{
  int ret = OK;
  int tmpret;

#ifdef CONFIG_FS_PROCFS
  tmpret = mkdir("/proc", 0755);
  if (tmpret < 0 && errno != EEXIST)
    {
      serr("WARN: mkdir /proc failed: %d\n", errno);
    }

  tmpret = nx_mount(NULL, "/proc", "procfs", 0, NULL);
  if (tmpret < 0 && tmpret != -EBUSY)
    {
      serr("WARN: mount procfs failed: %d\n", tmpret);
    }
#endif

  tmpret = mkdir("/data", 0755);
  if (tmpret < 0 && errno != EEXIST)
    {
      serr("WARN: mkdir /data failed: %d\n", errno);
    }

#ifdef CONFIG_FS_TMPFS
  /* Keep /data writable for syscall and file API test cases. */
  tmpret = nx_mount(NULL, "/data", "tmpfs", 0, NULL);
  if (tmpret < 0 && tmpret != -EBUSY)
    {
      serr("WARN: mount tmpfs on /data failed: %d\n", tmpret);
    }
#endif

#if defined(CONFIG_RTC) && defined(CONFIG_RTC_DRIVER)
  struct rtc_lowerhalf_s *rtclower = NULL;

  rtclower = sf32lb_rtc_lowerhalf();
  if (rtclower == NULL)
    {
      serr("ERROR: Failed to instantiate RTC lower-half\n");
      return -ENOMEM;
    }

  ret = rtc_initialize(0, rtclower);
  if (ret < 0)
    {
      serr("ERROR: rtc_initialize failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_ADC
  ret = sf32lb_adc_init("/dev/adc0");
  if (ret < 0)
    {
      serr("ERROR: sf32lb_adc_init failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_DEV_GPIO
  ret = sifli_gpio_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: sifli_gpio_initialize failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_TIMER
#if SF32LB_TIMER_DEFAULT_INDEX >= 0
  {
    FAR struct timer_lowerhalf_s *timer_lower;
    FAR void *timer_upper;

    timer_lower = sf32lb_timer_initialize(SF32LB_TIMER_DEFAULT_INDEX,
                                          1000000);
    if (timer_lower == NULL)
      {
        syslog(LOG_ERR, "ERROR: sf32lb_timer_initialize failed\n");
        return -ENODEV;
      }

    timer_upper = timer_register("/dev/timer0", timer_lower);
    if (timer_upper == NULL)
      {
        ret = -errno;
        syslog(LOG_ERR, "ERROR: timer_register(/dev/timer0) failed: %d\n",
               errno);
        return ret;
      }
  }
#endif
#endif

#ifdef CONFIG_PWM
#if SF32LB_PWM_DEFAULT_INDEX >= 0
  {
    FAR struct pwm_lowerhalf_s *pwm_lower;

    pwm_lower = sf32lb_pwm_initialize(SF32LB_PWM_DEFAULT_INDEX);
    if (pwm_lower == NULL)
      {
        syslog(LOG_ERR, "ERROR: sf32lb_pwm_initialize failed\n");
        return -ENODEV;
      }

    ret = pwm_register("/dev/pwm0", pwm_lower);
    if (ret < 0 && ret != -EEXIST)
      {
        syslog(LOG_ERR, "ERROR: pwm_register(/dev/pwm0) failed: %d\n", ret);
        return ret;
      }
  }
#endif
#endif

#ifdef CONFIG_INPUT_BUTTONS
  ret = sf32lb52_button_initialize("/dev/buttons");
  if (ret < 0 && ret != -EEXIST)
    {
      syslog(LOG_ERR, "ERROR: sf32lb52_button_initialize() failed: %d\n",
             ret);
      return ret;
    }
#endif

#ifdef CONFIG_I2C
  /* Initialize I2C bus 0 on the touch panel pins. */
  struct i2c_master_s *i2c0 = NULL;

  HAL_PIN_Set(PAD_PA37, I2C1_SCL, PIN_PULLUP, 1);
  HAL_PIN_Set(PAD_PA33, I2C1_SDA, PIN_PULLUP, 1);

  i2c0 = sifli_i2cbus_initialize(0);
  if (i2c0 == NULL)
    {
      syslog(LOG_ERR, "ERROR: sifli_i2cbus_initialize(0) failed\n");
      return -ENODEV;
    }

  ret = i2c_register(i2c0, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: i2c_register(/dev/i2c0) failed: %d\n", ret);
      return ret;
    }

#if defined(CONFIG_INPUT_FT6146) && defined(CONFIG_LCD)
  g_pending_touch_i2c = i2c0;
#elif defined(CONFIG_INPUT_FT6146)
  ret = ft6146_touch_initialize(i2c0, GET_PIN_2(hwp_gpio1, CONFIG_TOUCH_IRQ_PIN));
  if (ret < 0)
    {
      syslog(LOG_ERR,
             "ERROR: ft6146_touch_initialize failed: %d, continue bringup\n",
             ret);
    }
#endif

#ifdef CONFIG_BSP_USING_I2C2
  /* Initialize I2C bus 1 for charger (AW32001). */
  struct i2c_master_s *i2c1 = NULL;

#if defined(CONFIG_SENSORS_LSM6DSL)
  HAL_PIN_Set(PAD_PA40, I2C2_SCL, PIN_PULLUP, 1);
  HAL_PIN_Set(PAD_PA39, I2C2_SDA, PIN_PULLUP, 1);
#endif

  i2c1 = sifli_i2cbus_initialize(1);
  if (i2c1 == NULL)
    {
      syslog(LOG_ERR, "ERROR: sifli_i2cbus_initialize(1) failed\n");
    }
  else
    {
      ret = i2c_register(i2c1, 1);
      if (ret < 0)
        {
          syslog(LOG_ERR, "ERROR: i2c_register(/dev/i2c1) failed: %d\n", ret);
        }

#if defined(CONFIG_SENSORS_LSM6DSL)
      tmpret = sf32lb52_lsm6ds3_initialize(i2c1);
      if (tmpret < 0)
        {
          syslog(LOG_ERR,
                 "ERROR: sf32lb52_lsm6ds3_initialize failed: %d\n",
                 tmpret);
        }
#endif
    }
#endif /* CONFIG_BSP_USING_I2C2 */
#endif /* CONFIG_I2C */

#if defined(CONFIG_SPI) && defined(CONFIG_BSP_USING_SPI1) && \
    defined(CONFIG_SPI_DRIVER)
  tmpret = sf32lb52_spi1_register_char();
  if (tmpret < 0)
    {
      serr("WARN: SPI1 char device register failed: %d\n", tmpret);
    }
#endif

#if defined(CONFIG_SPI) && defined(CONFIG_MMCSD_SPI) && \
    defined(CONFIG_BSP_USING_SPI1)
  tmpret = sf32lb52_tfcard_initialize();
  if (tmpret < 0)
    {
      serr("WARN: TF card init failed: %d\n", tmpret);
    }
#endif

#ifdef CONFIG_LCD
  int pid;

  pid = task_create("lcd_async_init",
                    LCD_INIT_TASK_PRIORITY,
                    LCD_INIT_TASK_STACKSIZE,
                    lcd_async_init_thread,
                    NULL);
  if (pid < 0)
    {
      ret = -errno;
      syslog(LOG_ERR, "ERROR: lcd_async_init task_create failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_WATCHDOG
  sf32lb_iwdginitialize("/dev/watchdog0");
#endif

#ifdef CONFIG_MTD
  ret = sf32lb_nor_automount(0,
                             SF32LB52_NOR_FS_OFFSET_BLOCKS,
                             SF32LB52_NOR_FS_SIZE_BLOCKS);
  if (ret < 0)
    {
      serr("WARN: sf32lb_nor_automount failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_CDCACM
  ret = cdcacm_initialize(0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: cdcacm_initialize failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_UART_BTH4
  tmpret = sf32lb52_bt_initialize();
  if (tmpret < 0 && tmpret != -EEXIST)
    {
      serr("WARN: sf32lb52_bt_initialize failed: %d\n", tmpret);
    }
#endif

  return ret;
}

/****************************************************************************
 * Name: board_early_initialize
 *
 * Description:
 *   If CONFIG_BOARD_EARLY_INITIALIZE is selected, then an additional
 *   initialization call will be performed in the boot-up sequence to a
 *   function called board_early_initialize().  board_early_initialize()
 *   will be called immediately after up_initialize() and well before
 *   board_early_initialize() is called and the initial application is
 *   started.  The context in which board_early_initialize() executes is
 *   suitable for early initialization of most, simple device drivers and
 *   is a logical, board-specific extension of up_initialize().
 *
 *   board_early_initialize() runs on the startup, initialization thread.
 *   Some initialization operations cannot be performed on the start-up,
 *   initialization thread.  That is because the initialization thread
 *   cannot wait for event.  Waiting may be required, for example, to
 *   mount a file system or or initialize a device such as an SD card.
 *   For this reason, such driver initialize must be deferred to
 *   board_late_initialize().

 ****************************************************************************/

#ifdef CONFIG_BOARD_EARLY_INITIALIZE
void board_early_initialize(void)
{

}
#endif

/****************************************************************************
 * Name: board_late_initialize
 *
 * Description:
 *   If CONFIG_BOARD_LATE_INITIALIZE is selected, then an additional
 *   initialization call will be performed in the boot-up sequence to a
 *   function called board_late_initialize().  board_late_initialize() will
 *   be called after up_initialize() and board_early_initialize() and just
 *   before the initial application is started.  This additional
 *   initialization phase may be used, for example, to initialize board-
 *   specific device drivers for which board_early_initialize() is not
 *   suitable.
 *
 *   Waiting for events, use of I2C, SPI, etc are permissible in the context
 *   of board_late_initialize().  That is because board_late_initialize()
 *   will run on a temporary, internal kernel thread.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void)
{
  /* Perform board-specific initialization */

  sf32lb52_lchspi_ulp_bringup();
}
#endif

/****************************************************************************
 * Name: board_app_initialize
 *
 * Description:
 *   Perform application specific initialization.  This function is never
 *   called directly from application code, but only indirectly via the
 *   (non-standard) boardctl() interface using the command BOARDIOC_INIT.
 *
 * Input Parameters:
 *   arg - The boardctl() argument is passed to the board_app_initialize()
 *         implementation without modification.  The argument has no
 *         meaning to NuttX; the meaning of the argument is a contract
 *         between the board-specific initialization logic and the
 *         matching application logic.  The value could be such things as a
 *         mode enumeration value, a set of DIP switch settings, a
 *         pointer to configuration data read from a file or serial FLASH,
 *         or whatever you would like to do with it.  Every implementation
 *         should accept zero/NULL as a default configuration.
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned on
 *   any failure to indicate the nature of the failure.
 *
 ****************************************************************************/

int board_app_initialize(uintptr_t arg)
{
#ifdef CONFIG_BOARD_LATE_INITIALIZE
  /* Board initialization already performed by board_late_initialize() */

  return OK;
#else
  /* Perform board-specific initialization */

  return sf32lb52_lchspi_ulp_bringup();
#endif
}

#ifdef CONFIG_BOARDCTL_RESET
int board_reset(int status)
{
  (void)status;

  up_systemreset();
  return OK;
}
#endif

/****************************************************************************
 * Name: board_app_finalinitialize
 *
 * Description:
 *   Perform application specific initialization.  This function is never
 *   called directly from application code, but only indirectly via the
 *   (non-standard) boardctl() interface using the command
 *   BOARDIOC_FINALINIT.
 *
 * Input Parameters:
 *   arg - The argument has no meaning.
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned on
 *   any failure to indicate the nature of the failure.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARDCTL_FINALINIT
int board_app_finalinitialize(uintptr_t arg)
{
  return 0;
}
#endif
