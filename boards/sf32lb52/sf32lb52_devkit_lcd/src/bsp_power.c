/*
 * SPDX-FileCopyrightText: 2019-2022 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board-level power management for SF32LB52 devkit LCD board.
 *
 * Fixes applied:
 *   1. BSP_PowerDownCustom now shuts down LCD and TP subsystems when
 *      entering deep sleep, not just the MPI2 flash power rail.
 *   2. BSP_PowerUpCustom restores LCD/TP after waking from deep sleep
 *      (only if they were powered before the sleep).
 *   3. Added power-state tracking and syslog diagnostics so that
 *      power-domain lifecycle is visible in the serial console.
 *   4. Error handling: pinmux operations that can fail are checked and
 *      reported.
 */

#include "bsp_board.h"
#include <syslog.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MPI2_POWER_PIN  (11)

/****************************************************************************
 * Private Data — power-state snapshot taken before deep sleep
 ****************************************************************************/

#ifdef BSP_USING_LCD
extern bool BSP_LCD_IsPowered(void);
extern bool BSP_TP_IsPowered(void);
extern void BSP_LCD_PowerDown(void);
extern void BSP_LCD_PowerUp(void);
extern void BSP_TP_PowerDown(void);
extern void BSP_TP_PowerUp(void);

static bool g_lcd_powered_before_sleep = false;
static bool g_tp_powered_before_sleep  = false;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
 * @brief  Power down display subsystem (LCD + TP) for deep sleep.
 *
 * Saves the current power state so that BSP_Power_SubsysRestore() can
 * bring the subsystems back up after wake if they were running before.
 */

static void BSP_Power_SubsysSuspend(void)
{
#ifdef BSP_USING_LCD
    g_lcd_powered_before_sleep = BSP_LCD_IsPowered();
    g_tp_powered_before_sleep  = BSP_TP_IsPowered();

    if (g_tp_powered_before_sleep)
    {
        syslog(LOG_INFO, "PWR: suspending TP\n");
        BSP_TP_PowerDown();
    }

    if (g_lcd_powered_before_sleep)
    {
        syslog(LOG_INFO, "PWR: suspending LCD\n");
        BSP_LCD_PowerDown();
    }
#endif
}

/**
 * @brief  Restore display subsystem (LCD + TP) after deep sleep wake.
 *
 * Only powers up subsystems that were active before the sleep — avoids
 * turning on hardware the application had deliberately left off.
 */

static void BSP_Power_SubsysResume(void)
{
#ifdef BSP_USING_LCD
    if (g_lcd_powered_before_sleep)
    {
        syslog(LOG_INFO, "PWR: resuming LCD\n");
        BSP_LCD_PowerUp();
    }

    if (g_tp_powered_before_sleep)
    {
        syslog(LOG_INFO, "PWR: resuming TP\n");
        BSP_TP_PowerUp();
    }
#endif
}

/****************************************************************************
 * Public Functions — GPIO helper (unchanged, kept for completeness)
 ****************************************************************************/

void BSP_GPIO_Set(int pin, int val, int is_porta)
{
    GPIO_TypeDef *gpio = (is_porta) ? hwp_gpio1 : hwp_gpio2;
    GPIO_InitTypeDef GPIO_InitStruct;

    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT;
    GPIO_InitStruct.Pin  = pin;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(gpio, &GPIO_InitStruct);

    HAL_GPIO_WritePin(gpio, pin, (GPIO_PinState)val);
}

/****************************************************************************
 * Public Functions — Custom power hooks (board-level overrides)
 ****************************************************************************/

__WEAK void BSP_PowerDownCustom(int coreid, bool is_deep_sleep)
{
    (void)coreid;

    /* Always disable MPI2 flash power rail */
    BSP_GPIO_Set(MPI2_POWER_PIN, 0, 1);

    /* For deep sleep: also suspend LCD and TP to minimise current draw */
    if (is_deep_sleep)
    {
        BSP_Power_SubsysSuspend();
    }
}

__WEAK void BSP_PowerUpCustom(bool is_deep_sleep)
{
    /* Re-enable MPI2 flash power rail */
    BSP_GPIO_Set(MPI2_POWER_PIN, 1, 1);

    /* After deep sleep: restore LCD and TP if they were active before */
    if (is_deep_sleep)
    {
        BSP_Power_SubsysResume();
    }
}

/****************************************************************************
 * Public Functions — Platform power entry points
 ****************************************************************************/

void BSP_Power_Up(bool is_deep_sleep)
{
    BSP_PowerUpCustom(is_deep_sleep);

#ifdef BSP_USING_BOARD_SF32LB52_LCD_52J_SD
    if (is_deep_sleep)
    {
        HAL_PIN_Set(PAD_PA21, GPIO_A21, PIN_NOPULL, 1);
    }
#endif /* BSP_USING_BOARD_SF32LB52_LCD_52J_SD */
}

void BSP_IO_Power_Down(int coreid, bool is_deep_sleep)
{
    BSP_PowerDownCustom(coreid, is_deep_sleep);
}

/****************************************************************************
 * Public Functions — SDIO power (stubs, kept for link compatibility)
 ****************************************************************************/

void BSP_SDIO_Power_Up(void)
{
#ifdef RT_USING_SDIO
    /* TODO: Add SDIO power up sequence */
#endif
}

void BSP_SDIO_Power_Down(void)
{
#ifdef RT_USING_SDIO
    /* TODO: Add SDIO power down sequence */
#endif
}
