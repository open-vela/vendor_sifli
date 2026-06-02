#include "bsp_board.h"
#include "sf32lb_flash.h"
#include "mem_map.h"

void BSP_GPIO_Set(int pin, int val, int is_porta)
{
    GPIO_TypeDef *gpio = (is_porta) ? hwp_gpio1 : hwp_gpio2;
    GPIO_InitTypeDef GPIO_InitStruct;

    // set sensor pin to output mode
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT;
    GPIO_InitStruct.Pin = pin;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(gpio, &GPIO_InitStruct);

    // set sensor pin to high == power on sensor board
    HAL_GPIO_WritePin(gpio, pin, (GPIO_PinState)val);
}

#define MPI2_POWER_PIN  (11)

__WEAK void BSP_PowerDownCustom(int coreid, bool is_deep_sleep)
{
    BSP_GPIO_Set(MPI2_POWER_PIN, 0, 1);
}

__WEAK void BSP_PowerUpCustom(bool is_deep_sleep)
{
    /* Always power on MPI2 NOR flash */
    BSP_GPIO_Set(MPI2_POWER_PIN, 1, 1);

    if (is_deep_sleep)
    {
        /* VSYS */
        BSP_GPIO_Set(38, 1, 1);

        /* VSYS to VCC_3V3 */
        BSP_GPIO_Set(26, 1, 1);

        /* GS_3V3 */
        BSP_GPIO_Set(30, 1, 1);
        HAL_Delay_us(500);

        /* Configure Charger (AW32001) via I2C2 */
        I2C_HandleTypeDef i2c_Handle = {0};
        HAL_RCC_EnableModule(RCC_MOD_I2C2);
        i2c_Handle.Instance = I2C2;
        i2c_Handle.Mode = HAL_I2C_MODE_MASTER;
        i2c_Handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
        i2c_Handle.Init.ClockSpeed = 400000;
        i2c_Handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
        i2c_Handle.Init.OwnAddress1 = 0;

        if (HAL_I2C_Init(&i2c_Handle) == HAL_OK)
        {
            __HAL_I2C_ENABLE(&i2c_Handle);
            const uint8_t charger_addr = 0x49;
            uint8_t data = 0;

            /* CEB, address 0x01, bit3 */
            if (HAL_I2C_Mem_Read(&i2c_Handle, charger_addr, 0x01,
                                 I2C_MEMADD_SIZE_8BIT, &data, 1, 1000) == HAL_OK)
            {
                data &= ~(1 << 3);
                HAL_I2C_Mem_Write(&i2c_Handle, charger_addr, 0x01,
                                  I2C_MEMADD_SIZE_8BIT, &data, 1, 1000);
            }

            /* Disable watchdog, address 0x05, bit5-6 */
            if (HAL_I2C_Mem_Read(&i2c_Handle, charger_addr, 0x05,
                                 I2C_MEMADD_SIZE_8BIT, &data, 1, 1000) == HAL_OK)
            {
                data &= ~(3 << 5);
                HAL_I2C_Mem_Write(&i2c_Handle, charger_addr, 0x05,
                                  I2C_MEMADD_SIZE_8BIT, &data, 1, 1000);
            }

            __HAL_I2C_DISABLE(&i2c_Handle);
        }
    }
}


void BSP_Power_Up(bool is_deep_sleep)
{
    BSP_PowerUpCustom(is_deep_sleep);
#ifdef SOC_BF0_HCPU
    if (!is_deep_sleep)
    {
#ifdef BSP_USING_NOR_FLASH2
        FLASH_HandleTypeDef *flash_handle = sf32lb_flash_get_handle();
        if (flash_handle)
        {
            HAL_FLASH_RELEASE_DPD(flash_handle);
            HAL_Delay_us(80);
        }
#endif /* BSP_USING_NOR_FLASH2 */
    }
#endif /* SOC_BF0_HCPU */
}



void BSP_IO_Power_Down(int coreid, bool is_deep_sleep)
{
    BSP_PowerDownCustom(coreid, is_deep_sleep);
#ifdef SOC_BF0_HCPU
    if (coreid == CORE_ID_HCPU)
    {
#ifdef BSP_USING_NOR_FLASH2
        FLASH_HandleTypeDef *flash_handle = sf32lb_flash_get_handle();
        if (flash_handle)
        {
            HAL_FLASH_DEEP_PWRDOWN(flash_handle);
            HAL_Delay_us(3);
        }
#endif /* BSP_USING_NOR_FLASH2 */
    }
#endif /* SOC_BF0_HCPU */
}

void BSP_SDIO_Power_Up(void)
{
#ifdef RT_USING_SDIO
    /* TODO: Add SDIO power up */
#endif
}

void BSP_SDIO_Power_Down(void)
{
#ifdef RT_USING_SDIO
    /* TODO: Add SDIO power down */
#endif
}


