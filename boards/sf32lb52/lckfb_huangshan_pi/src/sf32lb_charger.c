/****************************************************************************
 * vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/src/sf32lb_charger.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SY6103 电池充电器驱动 for LCKFB Huangshan Pi.
 * 使用 I2C2 (PA10/PA11) 通信。
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/power/battery_charger.h>
#include <nuttx/power/battery_ioctl.h>

#ifdef CONFIG_BATTERY_CHARGER

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SY6103_I2C_ADDR      0x49

/* 寄存器地址 */

#define SY6103_REG00         0x00  /* 系统控制0 */
#define SY6103_REG01         0x01  /* 充电控制0 (CEB, ISYS) */
#define SY6103_REG02         0x02  /* 充电控制1 (ITERM) */
#define SY6103_REG03         0x03  /* 充电控制2 (ICHRG) */
#define SY6103_REG04         0x04  /* 充电控制3 (VCHRG) */
#define SY6103_REG05         0x05  /* 安全/控制 (WDT, IRQ) */
#define SY6103_REG06         0x06  /* 升压控制 */
#define SY6103_REG07         0x07  /* 状态0 (充电状态) */
#define SY6103_REG08         0x08  /* 状态1 (故障) */
#define SY6103_REG09         0x09  /* 状态2 (电池电压ADC) */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sy6103_dev_s
{
  struct battery_charger_dev_s dev;
  FAR struct i2c_master_s *i2c;
  struct i2c_config_s config;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct sy6103_dev_s g_sy6103;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int sy6103_read_reg(FAR struct sy6103_dev_s *priv,
                           uint8_t reg, uint8_t *val)
{
  return i2c_writeread(priv->i2c, &priv->config, &reg, 1, val, 1);
}

static int sy6103_write_reg(FAR struct sy6103_dev_s *priv,
                            uint8_t reg, uint8_t val)
{
  uint8_t buf[2] = {reg, val};
  return i2c_write(priv->i2c, &priv->config, buf, 2);
}

static int sy6103_state(FAR struct battery_charger_dev_s *dev,
                        FAR int *status)
{
  FAR struct sy6103_dev_s *priv = (FAR struct sy6103_dev_s *)dev;
  uint8_t val;
  int ret = sy6103_read_reg(priv, SY6103_REG07, &val);
  if (ret < 0)
    {
      *status = BATTERY_UNKNOWN;
      return ret;
    }

  /* STATUS0 bits[5:4]: 00=空闲, 01=预充, 10=恒流, 11=恒压/完成 */

  switch ((val >> 4) & 0x03)
    {
      case 0x00:
        *status = BATTERY_IDLE;
        break;
      case 0x01:
      case 0x02:
        *status = BATTERY_CHARGING;
        break;
      case 0x03:
        *status = BATTERY_FULL;
        break;
      default:
        *status = BATTERY_UNKNOWN;
        break;
    }

  return OK;
}

static int sy6103_health(FAR struct battery_charger_dev_s *dev,
                         FAR int *health)
{
  FAR struct sy6103_dev_s *priv = (FAR struct sy6103_dev_s *)dev;
  uint8_t val;
  int ret = sy6103_read_reg(priv, SY6103_REG08, &val);
  if (ret < 0)
    {
      *health = BATTERY_HEALTH_UNKNOWN;
      return ret;
    }

  if (val & (1 << 3))  /* OT bit */
    {
      *health = BATTERY_HEALTH_OVERHEAT;
    }
  else if (val & (1 << 2))  /* VBUS fault */
    {
      *health = BATTERY_HEALTH_OVERVOLTAGE;
    }
  else
    {
      *health = BATTERY_HEALTH_GOOD;
    }

  return OK;
}

static int sy6103_online(FAR struct battery_charger_dev_s *dev,
                         FAR bool *status)
{
  FAR struct sy6103_dev_s *priv = (FAR struct sy6103_dev_s *)dev;
  uint8_t val;
  int ret = sy6103_read_reg(priv, SY6103_REG01, &val);
  if (ret < 0)
    {
      *status = false;
      return ret;
    }

  /* CEB (bit3) = 0 表示充电使能 */

  *status = !(val & (1 << 3));
  return OK;
}

static int sy6103_voltage(FAR struct battery_charger_dev_s *dev, int value)
{
  UNUSED(dev);
  UNUSED(value);
  return OK;
}

static int sy6103_current(FAR struct battery_charger_dev_s *dev, int value)
{
  UNUSED(dev);
  UNUSED(value);
  return OK;
}

static int sy6103_input_current(FAR struct battery_charger_dev_s *dev,
                                int value)
{
  UNUSED(dev);
  UNUSED(value);
  return OK;
}

static int sy6103_operate(FAR struct battery_charger_dev_s *dev,
                          uintptr_t param)
{
  UNUSED(dev);
  UNUSED(param);
  return -ENOTTY;
}

static int sy6103_get_voltage(FAR struct battery_charger_dev_s *dev,
                              FAR int *value)
{
  FAR struct sy6103_dev_s *priv = (FAR struct sy6103_dev_s *)dev;
  uint8_t val;
  int ret = sy6103_read_reg(priv, SY6103_REG09, &val);
  if (ret < 0)
    {
      return ret;
    }

  /* STATUS2: 电池电压ADC，粗略转换 */

  *value = (int)(val * 4);
  return OK;
}

static const struct battery_charger_operations_s g_sy6103ops =
{
  .state         = sy6103_state,
  .health        = sy6103_health,
  .online        = sy6103_online,
  .voltage       = sy6103_voltage,
  .current       = sy6103_current,
  .input_current = sy6103_input_current,
  .operate       = sy6103_operate,
  .get_voltage   = sy6103_get_voltage,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sf32lb_charger_initialize
 *
 * Description:
 *   初始化 SY6103 充电器驱动并注册为 /dev/charger0
 *
 ****************************************************************************/

int sf32lb_charger_initialize(FAR struct i2c_master_s *i2c)
{
  uint8_t val;
  int ret;

  if (i2c == NULL)
    {
      return -EINVAL;
    }

  memset(&g_sy6103, 0, sizeof(g_sy6103));

  g_sy6103.dev.ops = &g_sy6103ops;
  g_sy6103.i2c = i2c;

  g_sy6103.config.address = SY6103_I2C_ADDR;
  g_sy6103.config.frequency = 400000;
  g_sy6103.config.addrlen = 7;

  nxmutex_init(&g_sy6103.dev.batlock);

  /* 读取状态寄存器验证芯片存在 */

  ret = sy6103_read_reg(&g_sy6103, SY6103_REG07, &val);
  if (ret < 0)
    {
      syslog(LOG_ERR, "charger: SY6103 not found at 0x%02x\n",
             SY6103_I2C_ADDR);
      nxmutex_destroy(&g_sy6103.dev.batlock);
      return -ENODEV;
    }

  /* 禁用看门狗 */

  sy6103_write_reg(&g_sy6103, SY6103_REG05, 0x00);

  /* 使能充电: 清除 CEB (REG01 bit3) */

  sy6103_read_reg(&g_sy6103, SY6103_REG01, &val);
  val &= ~(1 << 3);
  sy6103_write_reg(&g_sy6103, SY6103_REG01, val);

  syslog(LOG_INFO, "charger: SY6103 detected, STATUS0=0x%02x\n", val);

  /* 注册充电器设备 */

  ret = battery_charger_register("/dev/charger0", &g_sy6103.dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "charger: register failed: %d\n", ret);
      nxmutex_destroy(&g_sy6103.dev.batlock);
      return ret;
    }

  syslog(LOG_INFO, "charger: registered /dev/charger0\n");
  return OK;
}

#endif /* CONFIG_BATTERY_CHARGER */
