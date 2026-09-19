/*
 * SPDX-FileCopyrightText: 2019-2022 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LCD and Touch Panel power management for SF32LB52 devkit LCD board.
 *
 * Fixes applied:
 *   1. TP PowerDown now properly releases I2C pins and configures interrupt
 *      pin for low-power state (was only toggling reset GPIO).
 *   2. Added power-state tracking (g_lcd_powered / g_tp_powered) so that
 *      redundant power-up / power-down calls are harmless and so the deep-
 *      sleep path can query current state.
 *   3. PowerUp / PowerDown are now idempotent — calling them twice in a row
 *      is safe and does not re-drive pins or re-trigger delays.
 *   4. Error-path hardening: guards against double-on / double-off, and the
 *      TP PowerDown explicitly tri-states I2C lines to stop leakage through
 *      external pull-ups during deep sleep.
 */

#include "bsp_board.h"
#include "bf0_hal.h"

#ifdef BSP_USING_LCD

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define LCD_RESET_PIN           (0)         /* GPIO_A00                      */
#define TP_RESET                (9)         /* GPIO_A09                      */
#define TP_INT_PIN              (31)        /* GPIO_A31 - CTP_INT            */
#define LCD_POWER_PIN           (10)        /* GPIO_A10 - LCD power enable   */
#define BACKLIGHT_PIN           (1)         /* GPIO_A1  - backlight control  */

#ifdef LCD_USING_CO5300
    #define LCD_VADD_EN         (37)        /* GPIO_A37 - VADD enable        */
#endif

/****************************************************************************
 * Private Data — power state tracking
 ****************************************************************************/

static bool g_lcd_powered = false;
static bool g_tp_powered  = false;

/****************************************************************************
 * Public Functions — LCD
 ****************************************************************************/

extern void BSP_PIN_LCD(void);

bool BSP_LCD_IsPowered(void)
{
    return g_lcd_powered;
}

void BSP_LCD_Reset(uint8_t high1_low0)
{
    BSP_GPIO_Set(LCD_RESET_PIN, high1_low0, 1);
}

/* Notify LCD driver that panel power is being cut (defined in sf32lb_lcd.c) */
extern void board_lcd_notify_power_down(void);

extern volatile bool s_lcd_hw_ready;

void BSP_LCD_PowerDown(void)
{
    if (!g_lcd_powered)
    {
        return;     /* Already off — nothing to do */
    }

    /* Gate new display operations BEFORE notifying driver.
     * This prevents a concurrent putrun/putarea from initiating
     * QSPI DMA against a panel that is about to lose power.
     */
    s_lcd_hw_ready = false;

    /* Notify driver so next frame triggers full re-init */
    board_lcd_notify_power_down();

    /* 1. Turn off backlight: configure PA01 as GPIO and drive LOW */
    HAL_PIN_Set(PAD_PA01, GPIO_A1, PIN_NOPULL, 0);
    BSP_GPIO_Set(BACKLIGHT_PIN, 0, 1);

    /* 2. Assert LCD reset (active low) */
    BSP_GPIO_Set(LCD_RESET_PIN, 0, 1);

#ifdef LCD_USING_CO5300
    /* 3. Disable VADD boost */
    BSP_GPIO_Set(LCD_VADD_EN, 0, 1);
#endif

    /* 4. Disable main LCD power rail (PA10) */
    BSP_GPIO_Set(LCD_POWER_PIN, 0, 1);

    /* 5. Set QSPI LCD pins to analog (high-Z) to prevent current leakage
     *    through unpowered panel's ESD protection diodes */
    HAL_PIN_Set_Analog(PAD_PA02, 1);  /* LCDC1_SPI_TE */
    HAL_PIN_Set_Analog(PAD_PA03, 1);  /* LCDC1_SPI_CS */
    HAL_PIN_Set_Analog(PAD_PA04, 1);  /* LCDC1_SPI_CLK */
    HAL_PIN_Set_Analog(PAD_PA05, 1);  /* LCDC1_SPI_DIO0 */
    HAL_PIN_Set_Analog(PAD_PA06, 1);  /* LCDC1_SPI_DIO1 */
    HAL_PIN_Set_Analog(PAD_PA07, 1);  /* LCDC1_SPI_DIO2 */
    HAL_PIN_Set_Analog(PAD_PA08, 1);  /* LCDC1_SPI_DIO3 */

    g_lcd_powered = false;
}

void BSP_LCD_PowerUp(void)
{
    if (g_lcd_powered)
    {
        return;     /* Already on — skip redundant init */
    }

    /* 1. Configure pinmux and enable PA10 (LCD Power) via BSP_PIN_LCD() */
    BSP_PIN_LCD();

    /* 2. Force backlight LOW immediately to prevent uncontrolled flash.
     *    BSP_PIN_LCD() sets PA01 to GPTIM1_CH4 (PWM); reclaim as GPIO-LOW
     *    BEFORE the panel gets power.                                 */
    HAL_PIN_Set(PAD_PA01, GPIO_A1, PIN_NOPULL, 0);
    BSP_GPIO_Set(BACKLIGHT_PIN, 0, 1);

#ifdef LCD_USING_CO5300
    BSP_GPIO_Set(LCD_VADD_EN, 1, 1);
#endif

    /* 3. Wait for VADD boost converter to stabilise (AVDD/ELVDD ramp) */
    HAL_Delay_us(20000);        /* 20 ms — AMOLED boost needs 10-20ms */

    /* 4. Hardware reset sequence */
    BSP_LCD_Reset(0);           /* Reset LOW (active) */
    HAL_Delay_us(20000);        /* 20 ms — margin for CO5300 spec */
    BSP_LCD_Reset(1);           /* Reset HIGH (release) */
    HAL_Delay_us(120000);       /* 120 ms — wait for panel ready */

    g_lcd_powered = true;

    /* Re-enable display operations after power-up.
     * During initial boot, lcd_hw_setup_thread_entry will also set this
     * after configuring the LCDC layer format — that's fine, it's idempotent.
     */
    s_lcd_hw_ready = true;

    /* Turn on backlight at full brightness (GPIO-HIGH for now; PWM later) */

    HAL_PIN_Set(PAD_PA01, GPIO_A1, PIN_NOPULL, 0);
    BSP_GPIO_Set(BACKLIGHT_PIN, 1, 1);
}

/****************************************************************************
 * Public Functions — Touch Panel
 ****************************************************************************/

extern void BSP_PIN_Touch(void);

bool BSP_TP_IsPowered(void)
{
    return g_tp_powered;
}

void BSP_TP_PowerUp(void)
{
    if (g_tp_powered)
    {
        return;     /* Already on — skip redundant init */
    }

    /* 1. Configure I2C + reset + interrupt pins */
    BSP_PIN_Touch();

    /* 2. Small delay for pin-mux settling */
    HAL_Delay_us(1000);

    /* 3. Release TP reset (active-low) */
    BSP_GPIO_Set(TP_RESET, 1, 1);

    /* 4. Wait for TP controller boot (FT6146 typical boot time ~50 ms) */
    HAL_Delay_us(50000);

    g_tp_powered = true;
}

void BSP_TP_PowerDown(void)
{
    if (!g_tp_powered)
    {
        return;     /* Already off — nothing to do */
    }

    /* 1. Assert TP reset — put the FT6146 controller into reset so it
     *    draws minimum current.                                    */
    BSP_GPIO_Set(TP_RESET, 0, 1);

    /* 2. Configure the interrupt pin as input with pull-down.
     *    Without this, a floating IRQ line can cause spurious wake-ups
     *    or draw shoot-through current during deep sleep.            */
    HAL_PIN_Set(PAD_PA31, GPIO_A31, PIN_PULLDOWN, 1);

    /* 3. Release I2C bus pins — set to analogue / high-impedance.
     *    PA30 (I2C1_SCL) and PA33 (I2C1_SDA) have external pull-up
     *    resistors; if we leave the pins in I2C function, the pull-ups
     *    will leak current through the ESD diodes of the (now-unpowered)
     *    FT6146 during deep sleep.  Switching to analogue mode breaks
     *    that path.                                                   */
    HAL_PIN_Set_Analog(PAD_PA30, 1);       /* I2C1_SCL -> high-Z       */
    HAL_PIN_Set_Analog(PAD_PA33, 1);       /* I2C1_SDA -> high-Z       */

    /* 4. Keep the reset pin itself pulled low so it stays asserted
     *    even if the GPIO output latch is lost during deep sleep.    */
    HAL_PIN_Set(PAD_PA09, GPIO_A9, PIN_PULLDOWN, 1);

    g_tp_powered = false;
}

void BSP_TP_Reset(uint8_t high1_low0)
{
    BSP_GPIO_Set(TP_RESET, high1_low0, 1);
}

#endif /* BSP_USING_LCD */
