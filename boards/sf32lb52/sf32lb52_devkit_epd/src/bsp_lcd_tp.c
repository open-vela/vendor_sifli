#include "bsp_board.h"


#ifdef BSP_USING_LCD
#define LCD_RESET_PIN           (0)         // GPIO_A00
#define LCD_POWER_EN_PIN        (10)        // GPIO_A10, EPD LDO enable, high active
#define TP_RESET                (9)         // GPIO_A09

#ifdef LCD_USING_CO5300
    #define LCD_VADD_EN             (37)
#endif


/***************************LCD ***********************************/
extern void BSP_PIN_LCD(void);
void BSP_LCD_Reset(uint8_t high1_low0)
{
    BSP_GPIO_Set(LCD_RESET_PIN, high1_low0, 1);
}

void BSP_LCD_PowerDown(void)
{
    BSP_GPIO_Set(LCD_RESET_PIN, 0, 1);
    BSP_GPIO_Set(LCD_POWER_EN_PIN, 0, 1);
#ifdef LCD_USING_CO5300
    BSP_GPIO_Set(LCD_VADD_EN, 0, 1); //POwer down VADD EN
#endif
}

void BSP_LCD_PowerUp(void)
{
    BSP_PIN_LCD();
    BSP_GPIO_Set(LCD_POWER_EN_PIN, 1, 1);
    HAL_Delay_us(500);      // EPD LDO power on finish, need 500us
#ifdef LCD_USING_CO5300
    BSP_GPIO_Set(LCD_VADD_EN, 1, 1); //POwer up VADD EN
#endif
}


/***************************Touch ***********************************/
extern void BSP_PIN_Touch(void);
void BSP_TP_PowerUp(void)
{
    // TODO: Setup TP power up pin
    BSP_PIN_Touch();
    BSP_GPIO_Set(TP_RESET,  1, 1);
}

void BSP_TP_PowerDown(void)
{
    // TODO: Setup TP power down pin
    BSP_GPIO_Set(TP_RESET,  0, 1);
}
void BSP_TP_Reset(uint8_t high1_low0)
{
    BSP_GPIO_Set(TP_RESET, high1_low0, 1);
}

#endif
