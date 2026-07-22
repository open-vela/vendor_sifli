#include <sfconfig.h>

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <debug.h>
#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>

#include "drv_lcd.h"
#include "epd_waveinit.h"

#define DEBUG_PRINTF(...)   lcdinfo(__VA_ARGS__)
#define rt_kprintf(...)     printf(__VA_ARGS__)
#define LOG_W(...)          lcdwarn(__VA_ARGS__)
#define LOG_D(...)          lcdinfo(__VA_ARGS__)

#define EPD_RESET_PIN                  (0)   /* PA00 */
#define EPD_BUSY_PIN                   (2)   /* PA02 */

#define EPD_WIDTH 528
#define EPD_HEIGHT 792

static const unsigned char EPD_lut_full_update[] = {
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22, 0x66, 0x69,
    0x69, 0x59, 0x58, 0x99, 0x99, 0x88, 0x00, 0x00, 0x00, 0x00,
    0xF8, 0xB4, 0x13, 0x51, 0x35, 0x51, 0x51, 0x19, 0x01, 0x00};

static const unsigned char EPD_lut_partial_update[] = {
    0x10, 0x18, 0x18, 0x08, 0x18, 0x18, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x13, 0x14, 0x44, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char LUT_GC[282]={
/*Vcom*/
0x00,0x1A,0x1A,0x01,0x00,0x01,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
/*WW*/
0x60,0x1A,0x1A,0x01,0x00,0x01,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
/*BW*/
0x20,0x1A,0x1A,0x01,0x00,0x01,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
/*WB*/
0x10,0x1A,0x1A,0x01,0x00,0x01,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
/*BB*/
0x90,0x1A,0x1A,0x01,0x00,0x01,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,
};
#define EPD_FULL 0
#define EPD_PART 1

#define EPD_LCD_ID 0x09ff
#define LCD_PIXEL_WIDTH EPD_WIDTH
#define LCD_PIXEL_HEIGHT EPD_HEIGHT
#define LCD_HOR_RES_MAX_8 LCD_HOR_RES_MAX / 8

#define PICTURE_LENGTH (LCD_HOR_RES_MAX / 8 * LCD_VER_RES_MAX)
#define PIC_WHITE 255                  // All white
#define PIC_BLACK 254                  // All black
#define PIC_LEFT_BLACK_RIGHT_WHITE 253 // Black left, white right
#define PIC_UP_BLACK_DOWN_WHITE 252    // Black top, white bottom
#define PART_DISP_TIMES 10

#define REG_LUT_VCOM 0x20
#define REG_LUT_W2W 0x21
#define REG_LUT_K2W 0x22
#define REG_LUT_W2K 0x23
#define REG_LUT_K2K 0x24
#define REG_LUT_OPT 0x2A
#define REG_WRITE_NEW_DATA 0x13
#define REG_AUTO_REFRESH 0x17
#define REG_PWR_ON_MEASURE 0x05
#define REG_TEMP_CALIB 0x40
#define REG_TEMP_SEL 0x41
#define REG_TEMP_READ 0x43
#define REG_PANEL_SETTING 0x00
#define REG_POWER_SETTING 0x01
#define REG_BOOSTER_SOFTSTART 0x06
#define REG_PLL_CTRL 0x30
#define REG_VCOM_DATA_INTERV 0x50
#define REG_TCON_SETTING 0x60
#define REG_RESOLUTION 0x61
#define REG_REV 0x70
#define REG_VDCS 0x82
#define REG_WRITE_NEW_DATA 0x13

static int reflesh_times;
static uint8_t current_refresh_mode;
static unsigned char LUT_Flag = 0; // LUT selection flag
static unsigned char Var_Temp = 0; // Temperature value

static LCDC_InitTypeDef lcdc_int_cfg = {
    .lcd_itf = LCDC_INTF_SPI_DCX_1DATA,
    .freq = 5000000,
    .color_mode = LCDC_PIXEL_FORMAT_RGB332,
    .cfg =
        {
            .spi =
                {
                    .dummy_clock = 0,
                    .syn_mode = HAL_LCDC_SYNC_DISABLE,
                    .vsyn_polarity = 1,
                    .vsyn_delay_us = 0,
                    .hsyn_num = 0,
                },
        },
};

static uint32_t LCD_ReadID(LCDC_HandleTypeDef *hlcdc);
static void LCD_WriteReg(LCDC_HandleTypeDef *hlcdc, uint16_t LCD_Reg,
                         uint8_t *Parameters, uint32_t NbParameters);
static uint32_t LCD_ReadData(LCDC_HandleTypeDef *hlcdc, uint16_t RegValue,
                             uint8_t ReadSize);
static void EPD_TemperatureMeasure(LCDC_HandleTypeDef *hlcdc);
static void EPD_EnterDeepSleep(LCDC_HandleTypeDef *hlcdc);
static void EPD_DisplayImage(LCDC_HandleTypeDef *hlcdc, uint8_t img_flag);
static void EPD_EnterDeepSleep(LCDC_HandleTypeDef *hlcdc);
static void EPD_LoadLUT(LCDC_HandleTypeDef *hlcdc, uint8_t lut_mode);
static void LUTGC(LCDC_HandleTypeDef *hlcdc);
static void EPD_Refresh(LCDC_HandleTypeDef *hlcdc);
static void EPD_SendCommandDataBuf(LCDC_HandleTypeDef *hlcdc, uint8_t cmd,
                                   const uint8_t *data, uint16_t len);

static uint8_t mixed_framebuffer[EPD_WIDTH / 8 * EPD_HEIGHT]
    __attribute__((aligned(64)));

static uint8_t framebuffer_initialized = 0;

/**
 * @brief Initialize the framebuffer to white
 */
static void EPD_FrameBuffer_Init(void)
{
    memset(mixed_framebuffer, 0xFF, EPD_WIDTH / 8 * EPD_HEIGHT);
    framebuffer_initialized = 1;
}

/**
 * @brief Update a framebuffer region
 * @param data Source data in RGB332 format, one byte per pixel
 * @param x0 Starting X coordinate
 * @param y0 Starting Y coordinate
 * @param x1 Ending X coordinate
 * @param y1 Ending Y coordinate
 * @note Convert RGB332 data to 1bpp before storing it in the buffer
 */
static void EPD_FrameBuffer_UpdateRegion(const uint8_t *data, uint16_t x0, uint16_t y0,
                                          uint16_t x1, uint16_t y1)
{
    const uint16_t *src16 = (const uint16_t *)data;
    uint32_t white_count = 0;
    uint32_t black_count = 0;

    if (data == NULL || x0 > x1 || y0 > y1)
    {
        return;
    }

    // Check bounds
    if (x1 >= EPD_WIDTH) x1 = EPD_WIDTH - 1;
    if (y1 >= EPD_HEIGHT) y1 = EPD_HEIGHT - 1;

    if (!framebuffer_initialized)
    {
        EPD_FrameBuffer_Init();
    }

    uint16_t src_width = x1 - x0 + 1;

    for (uint16_t y = y0; y <= y1; y++)
    {
        for (uint16_t x = x0; x <= x1; x++)
        {
            // Calculate the source data offset
            uint32_t src_idx = (y - y0) * src_width + (x - x0);
            uint16_t pixel = src16[src_idx];

            // Convert the RGB565 (16bpp) framebuffer data to 1bpp
            uint8_t r = (pixel >> 11) & 0x1F;
            uint8_t g = (pixel >> 5) & 0x3F;
            uint8_t b = pixel & 0x1F;
            uint16_t gray = r * 3 + g * 6 + b;
            uint8_t mono = (gray > 126) ? 1 : 0;  // 1=white, 0=black

            // Calculate the 1bpp framebuffer position
            uint32_t byte_idx = y * (EPD_WIDTH / 8) + (x / 8);
            uint8_t bit_pos = 7 - (x % 8);  // MSB first

            if (mono)
            {
                mixed_framebuffer[byte_idx] |= (1 << bit_pos);   // Set white
                white_count++;
            }
            else
            {
                mixed_framebuffer[byte_idx] &= ~(1 << bit_pos);  // Set black
                black_count++;
            }
        }
    }

    rt_kprintf("EPD update: (%u,%u)-(%u,%u), white=%lu black=%lu, fb[0..3]=%02x %02x %02x %02x\n",
               x0, y0, x1, y1, (unsigned long)white_count,
               (unsigned long)black_count, mixed_framebuffer[0],
               mixed_framebuffer[1], mixed_framebuffer[2],
               mixed_framebuffer[3]);
}

/**
 * @brief Copy 1bpp data directly into a framebuffer region
 * @param data Source data in 1bpp format
 * @param x0 Starting X coordinate, aligned to a multiple of 8
 * @param y0 Starting Y coordinate
 * @param x1 Ending X coordinate
 * @param y1 Ending Y coordinate
 */
static void EPD_FrameBuffer_UpdateRegion1bpp(const uint8_t *data, uint16_t x0, uint16_t y0,
                                              uint16_t x1, uint16_t y1)
{
    if (data == NULL || x0 > x1 || y0 > y1)
    {
        return;
    }

    // Check bounds
    if (x1 >= EPD_WIDTH) x1 = EPD_WIDTH - 1;
    if (y1 >= EPD_HEIGHT) y1 = EPD_HEIGHT - 1;

    if (!framebuffer_initialized)
    {
        EPD_FrameBuffer_Init();
    }

    // Align the X coordinates to byte boundaries
    uint16_t x0_byte = x0 / 8;
    uint16_t x1_byte = x1 / 8;
    uint16_t src_bytes_per_row = x1_byte - x0_byte + 1;

    for (uint16_t y = y0; y <= y1; y++)
    {
        uint32_t dst_offset = y * (EPD_WIDTH / 8) + x0_byte;
        uint32_t src_offset = (y - y0) * src_bytes_per_row;
        memcpy(&mixed_framebuffer[dst_offset], &data[src_offset], src_bytes_per_row);
    }
}

/**
 * @brief Refresh the entire EPD from the framebuffer
 * @param hlcdc LCDC handle
 */
static void EPD_FrameBuffer_Flush(LCDC_HandleTypeDef *hlcdc)
{
    if (!framebuffer_initialized)
    {
        EPD_FrameBuffer_Init();
    }

    // Load the GC mode LUT
    LUTGC(hlcdc);

    // Send the framebuffer through an LCDC layer
    uint32_t total_size = EPD_WIDTH / 8 * EPD_HEIGHT;

    // Use RGB332 layer format for the packed 1bpp data
    HAL_LCDC_LayerSetFormat(hlcdc, HAL_LCDC_LAYER_DEFAULT, LCDC_PIXEL_FORMAT_RGB332);

    // Set the entire framebuffer as the layer source
    HAL_LCDC_LayerSetData(hlcdc, HAL_LCDC_LAYER_DEFAULT, mixed_framebuffer,
                          0, 0, EPD_WIDTH / 8 - 1, EPD_HEIGHT - 1);

    /* mixed_framebuffer is filled by CPU and read by LCDC/DMA.  Clean D-cache
     * before starting the transfer, otherwise the controller may read stale
     * all-white data and refresh without visible change.
     */
    up_clean_dcache((uintptr_t)mixed_framebuffer,
                    (uintptr_t)mixed_framebuffer + total_size);

    // The EPD must finish writing the full frame RAM before command 0x12
    // triggers a refresh. Do not use the asynchronous _IT interface here.
    if (HAL_LCDC_SendLayerData2Reg(hlcdc, REG_WRITE_NEW_DATA, 1) != HAL_OK)
    {
        rt_kprintf("EPD flush: SendLayerData failed\n");
        return;
    }

    // Refresh the display
    EPD_Refresh(hlcdc);
}

/**
 * @brief Clear the framebuffer to white
 */
static void EPD_FrameBuffer_Clear(void)
{
    memset(mixed_framebuffer, 0xFF, EPD_WIDTH / 8 * EPD_HEIGHT);
    framebuffer_initialized = 1;
}

/**
 * @brief Fill the framebuffer with black
 */
static void EPD_FrameBuffer_Fill(void)
{
    memset(mixed_framebuffer, 0x00, EPD_WIDTH / 8 * EPD_HEIGHT);
    framebuffer_initialized = 1;
}

/**
 * @brief Get the framebuffer pointer
 * @return Framebuffer pointer
 */
static uint8_t* EPD_FrameBuffer_GetPtr(void)
{
    if (!framebuffer_initialized)
    {
        EPD_FrameBuffer_Init();
    }
    return mixed_framebuffer;
}
// Send a command without data
static void EPD_SendCommand(LCDC_HandleTypeDef *hlcdc, uint8_t cmd)
{
    HAL_LCDC_WriteU8Reg(hlcdc, cmd, NULL, 0);
}

// Send a command with one data byte
static void EPD_SendCommandData(LCDC_HandleTypeDef *hlcdc, uint8_t cmd,
                                uint8_t data)
{
    HAL_LCDC_WriteU8Reg(hlcdc, cmd, &data, 1);
}

// Send a command with multiple data bytes
static void EPD_SendCommandDataBuf(LCDC_HandleTypeDef *hlcdc, uint8_t cmd,
                                   const uint8_t *data, uint16_t len)
{
    HAL_LCDC_WriteU8Reg(hlcdc, cmd, (uint8_t *)data, len);
}
static void epd_sem_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pin = EPD_BUSY_PIN;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(hwp_gpio1, &GPIO_InitStruct);
}

static void EPD_ReadBusy(void)
{
    uint32_t timeout;
    bool saw_busy = false;

    /* BUSY is idle high / busy low on the reference design.  Do not return
     * immediately when it is already high: after a refresh command the panel
     * may need several milliseconds before pulling BUSY low.  First wait a
     * short window for the busy edge, then wait until it returns high.
     */
    timeout = 200;
    while (timeout-- > 0)
    {
        if (HAL_GPIO_ReadPin(hwp_gpio1, EPD_BUSY_PIN) == GPIO_PIN_RESET)
        {
            saw_busy = true;
            break;
        }

        LCD_DRIVER_DELAY_MS(1);
    }

    if (!saw_busy)
    {
        rt_kprintf("EPD busy: no low edge, already idle high\n");
        return;
    }

    timeout = 10000;
    while (timeout-- > 0)
    {
        if (HAL_GPIO_ReadPin(hwp_gpio1, EPD_BUSY_PIN) == GPIO_PIN_SET)
        {
            rt_kprintf("EPD busy: idle high\n");
            return;
        }

        LCD_DRIVER_DELAY_MS(1);
    }

    rt_kprintf("EPD busy wait timeout! (may cause display error)\n");
}

static void EPD_Reset(LCDC_HandleTypeDef *hlcdc)
{
    BSP_LCD_Reset(1);
    LCD_DRIVER_DELAY_MS(200);
    BSP_LCD_Reset(0);
    LCD_DRIVER_DELAY_MS(2);
    BSP_LCD_Reset(1);
    LCD_DRIVER_DELAY_MS(200);

    LUT_Flag = 0;
}
static void LCD_ReadMode(LCDC_HandleTypeDef *hlcdc, bool enable)
{
    if (HAL_LCDC_IS_SPI_IF(lcdc_int_cfg.lcd_itf))
    {
        if (enable)
        {
            HAL_LCDC_SetFreq(hlcdc, 2800000);
        }
        else
        {
            HAL_LCDC_SetFreq(hlcdc, lcdc_int_cfg.freq);
        }
    }
}

static void LCD_Drv_Init(LCDC_HandleTypeDef *hlcdc, uint8_t Mode)
{
    BSP_LCD_PowerUp();
    memcpy(&hlcdc->Init, &lcdc_int_cfg, sizeof(LCDC_InitTypeDef));
    HAL_LCDC_Init(hlcdc);
    epd_sem_init();
    EPD_Reset(hlcdc);


    // Reverse scan direction 1
    uint8_t scan_data[2] = {0x3F, 0x0A};
    EPD_SendCommandDataBuf(hlcdc, 0x00, scan_data, 2);

    // Resolution
    uint8_t resolution_data[4] = {0x02, 0x10, 0x03, 0x18}; // 528 x 792
    EPD_SendCommandDataBuf(hlcdc, 0x61, resolution_data, 4);
    EPD_ReadBusy();

    // Scan starting address
    uint8_t scan_addr_data[4] = {0x00, 0x00, 0x00, 0x00};
    EPD_SendCommandDataBuf(hlcdc, 0x65, scan_addr_data, 4);

    // PFS
    EPD_SendCommandData(hlcdc, 0x03, 0x30);

    // Power Setting
    uint8_t power_data[5] = {0x07, 0x17, 0x3F, 0x3F, 0x17};
    EPD_SendCommandDataBuf(hlcdc, 0x01, power_data, 5);

    // Vcom Voltage setting
    EPD_SendCommandData(hlcdc, 0x82, 0x25);

    // Power boost setting
    uint8_t boost_data[4] = {0x25, 0x25, 0x3C, 0x37};
    EPD_SendCommandDataBuf(hlcdc, 0x06, boost_data, 4);

    // Border and color setting
    uint8_t border_data[2] = {0x29, 0x07};
    EPD_SendCommandDataBuf(hlcdc, 0x50, border_data, 2);

    // Frame Frequence
    EPD_SendCommandData(hlcdc, 0x30, 0x09);

    // Gate Scan mode
    EPD_SendCommandData(hlcdc, 0xE1, 0x02);

    // Power ON
    EPD_SendCommand(hlcdc, 0x04);
    EPD_ReadBusy();

    // Write BW RAM and fill it with white
    // Gate_Pixel and Source_Pixel depend on the actual panel dimensions.
    // Use EPD_WIDTH and EPD_HEIGHT here.
    uint16_t ram_size = (EPD_WIDTH / 8) * EPD_HEIGHT;
    uint8_t *ram_buf = kmm_malloc(ram_size);
    if (ram_buf != NULL)
    {
        memset(ram_buf, 0xFF, ram_size);
        EPD_SendCommandDataBuf(hlcdc, 0x10, ram_buf, ram_size);
        kmm_free(ram_buf);
    }
}



// Refresh the EPD
static void EPD_Refresh(LCDC_HandleTypeDef *hlcdc)
{
    uint8_t parameter = 0xA5;

    rt_kprintf("EPD refresh: 0x17 0xA5\n");
    EPD_SendCommandData(hlcdc, REG_AUTO_REFRESH, parameter);
    EPD_ReadBusy();
}

// Load the GC mode LUT
// LUT_GC must contain 282 bytes.
static void LUTGC(LCDC_HandleTypeDef *hlcdc)
{
    // LUT_GC is defined externally.
    extern const unsigned char LUT_GC[282];

    // 0x20: VCOM LUT (60 bytes)
    EPD_SendCommandDataBuf(hlcdc, 0x20, &LUT_GC[0], 60);

    // 0x21: W2W LUT (42 bytes)
    EPD_SendCommandDataBuf(hlcdc, 0x21, &LUT_GC[60], 42);

    // 0x22: K2W LUT (60 bytes)
    EPD_SendCommandDataBuf(hlcdc, 0x22, &LUT_GC[102], 60);

    // 0x23: W2K LUT (60 bytes)
    EPD_SendCommandDataBuf(hlcdc, 0x23, &LUT_GC[162], 60);

    // 0x24: K2K LUT (60 bytes)
    EPD_SendCommandDataBuf(hlcdc, 0x24, &LUT_GC[222], 60);
}

void EPD_Clear(LCDC_HandleTypeDef *hlcdc)
{
    rt_kprintf("EPD black clear test start\n");

    // Load the GC mode LUT
    LUTGC(hlcdc);

    // Fill BW RAM with black to verify that the RAM write and refresh work.
    uint32_t ram_size = (EPD_WIDTH / 8) * EPD_HEIGHT;
    uint8_t *ram_buf = kmm_malloc(ram_size);
    if (ram_buf != NULL)
    {
        memset(ram_buf, 0x00, ram_size);
        rt_kprintf("EPD black clear test: write 0x13 %lu bytes 0x00\n",
                   (unsigned long)ram_size);
        EPD_SendCommandDataBuf(hlcdc, 0x13, ram_buf, ram_size);
        kmm_free(ram_buf);
    }
    else
    {
        rt_kprintf("EPD clear ram buf malloc failed\n");
    }

    EPD_Refresh(hlcdc);
    rt_kprintf("EPD black clear test done\n");
}


void EPD_Sleep(LCDC_HandleTypeDef *hlcdc)
{
    EPD_SendCommandData(hlcdc, 0x10, 0x01); // DEEP_SLEEP_MODE
}

static void LCD_Init(LCDC_HandleTypeDef *hlcdc)
{
    LCD_Drv_Init(hlcdc, EPD_FULL);
    rt_kprintf("EPD initialized, run one EPD_Clear test\n");
    EPD_Clear(hlcdc);
    rt_kprintf("EPD init clear test finished\n");
}

static uint32_t LCD_ReadID(LCDC_HandleTypeDef *hlcdc)
{
    uint32_t epd_id = 0x09ff;
    // Read and print the panel ID
    uint8_t id_data[10] = {0};

    for (int i = 0; i < 10; i++)
    {
        id_data[i] = (uint8_t)LCD_ReadData(hlcdc, 0x2E, 1);
        rt_kprintf("EPD ID byte %d: 0x%x\n", i, id_data[i]);
    }

    // rt_kprintf("EPD ID: 0x%x (240x416 mono EPD)", epd_id);
    return epd_id;
}
static void LCD_DisplayOn(LCDC_HandleTypeDef *hlcdc)
{
}

static void LCD_DisplayOff(LCDC_HandleTypeDef *hlcdc)
{
}
static void LCD_SetRegion(LCDC_HandleTypeDef *hlcdc, uint16_t Xpos0,
                          uint16_t Ypos0, uint16_t Xpos1, uint16_t Ypos1)
{
    HAL_LCDC_SetROIArea(hlcdc, 0, 0, LCD_HOR_RES_MAX_8 - 1,
                        LCD_VER_RES_MAX - 1);
}

static void LCD_WritePixel(LCDC_HandleTypeDef *hlcdc, uint16_t Xpos,
                           uint16_t Ypos, const uint8_t *RGBCode)
{
    if (RGBCode == NULL || Xpos >= EPD_WIDTH || Ypos >= EPD_HEIGHT)
    {
        return;
    }
    rt_kprintf("EPD single pixel: (%d,%d)\n", Xpos, Ypos);
    // Update one framebuffer pixel
    EPD_FrameBuffer_UpdateRegion(RGBCode, Xpos, Ypos, Xpos, Ypos);

    // A single-pixel write does not refresh the panel automatically.
    // The caller must trigger the refresh explicitly.
    // EPD_FrameBuffer_Flush(hlcdc);
}

static void LCD_WriteMultiplePixels(LCDC_HandleTypeDef *hlcdc,
                                    const uint8_t *RGBCode, uint16_t Xpos0,
                                    uint16_t Ypos0, uint16_t Xpos1,
                                    uint16_t Ypos1)
{
    if (RGBCode == NULL || Xpos0 > Xpos1 || Ypos0 > Ypos1)
    {
        rt_kprintf("EPD multiple pixels param error\n");
        return;
    }
    rt_kprintf("EPD multiple pixels: (%d,%d)-(%d,%d)\n",
               Xpos0, Ypos0, Xpos1, Ypos1);

    // Update the corresponding framebuffer region
    EPD_FrameBuffer_UpdateRegion(RGBCode, Xpos0, Ypos0, Xpos1, Ypos1);

    // Refresh the entire EPD from the framebuffer
    EPD_FrameBuffer_Flush(hlcdc);

    reflesh_times++;
}
static void LCD_WriteReg(LCDC_HandleTypeDef *hlcdc, uint16_t LCD_Reg,
                         uint8_t *Parameters, uint32_t NbParameters)
{
    EPD_ReadBusy();
    HAL_LCDC_WriteU8Reg(hlcdc, LCD_Reg, Parameters, NbParameters);
}
static uint32_t LCD_ReadData(LCDC_HandleTypeDef *hlcdc, uint16_t RegValue,
                             uint8_t ReadSize)
{
    uint32_t rd_data = 0;
    EPD_ReadBusy();
    LCD_ReadMode(hlcdc, true);
    HAL_LCDC_ReadU8Reg(hlcdc, RegValue, (uint8_t *)&rd_data, ReadSize);
    LCD_ReadMode(hlcdc, false);

    return rd_data;
}

static uint32_t LCD_ReadPixel(LCDC_HandleTypeDef *hlcdc, uint16_t Xpos,
                              uint16_t Ypos)
{
    if (Xpos >= EPD_WIDTH || Ypos >= EPD_HEIGHT)
    {
        LOG_W("EPD read pixel out of range");
        return 0;
    }

    LCD_SetRegion(hlcdc, Xpos, Ypos, Xpos, Ypos);
    uint8_t read_data = (uint8_t)LCD_ReadData(hlcdc, 0x2E, 1);
    uint32_t color = (read_data & 0x80) ? 0xFFFFFF : 0x000000;
    LOG_D("EPD read pixel: (%d,%d), color: 0x%x", Xpos, Ypos, color);
    return color;
}

static void LCD_SetColorMode(LCDC_HandleTypeDef *hlcdc, uint16_t color_mode)
{
    if (color_mode != RTGRAPHIC_PIXEL_FORMAT_RGB332 &&
        color_mode != LCDC_PIXEL_FORMAT_RGB332)
    {
        rt_kprintf("EPD only support mono-color, ignore mode: %d\n",
                   color_mode);
        return;
    }
    lcdc_int_cfg.color_mode = LCDC_PIXEL_FORMAT_RGB332;
    HAL_LCDC_SetOutFormat(hlcdc, lcdc_int_cfg.color_mode);
    // rt_kprintf("EPD set color mode: mono-color (1bit)\n");
}

static void LCD_SetBrightness(LCDC_HandleTypeDef *hlcdc, uint8_t br)
{
    LOG_W("EPD has no brightness adjustment, ignore br: %d", br);
}

static void LCD_IdleModeOn(LCDC_HandleTypeDef *hlcdc)
{
    EPD_EnterDeepSleep(hlcdc);
    BSP_LCD_PowerDown();
    BSP_LCD_Reset(0);
}

static void LCD_IdleModeOff(LCDC_HandleTypeDef *hlcdc)
{
    BSP_LCD_PowerUp();
    BSP_LCD_Reset(1);
    HAL_Delay(1);
    LCD_Drv_Init(hlcdc, EPD_FULL);
}
static void EPD_LoadLUT(LCDC_HandleTypeDef *hlcdc, uint8_t lut_mode)
{
    uint16_t count;
    switch (lut_mode)
    {
    case 0: // 5S mode (clear screen)
        LCD_WriteReg(hlcdc, REG_LUT_VCOM, lut_R20_5S, 56);
        LCD_WriteReg(hlcdc, REG_LUT_W2W, lut_R21_5S, 42);
        LCD_WriteReg(hlcdc, REG_LUT_K2K, lut_R24_5S, 42);

        if (LUT_Flag == 0)
        {
            LCD_WriteReg(hlcdc, REG_LUT_K2W, lut_R22_5S, 56);
            LCD_WriteReg(hlcdc, REG_LUT_W2K, lut_R23_5S, 42);
            LUT_Flag = 1;
        }
        else
        {
            LCD_WriteReg(hlcdc, REG_LUT_K2W, lut_R23_5S, 56);
            LCD_WriteReg(hlcdc, REG_LUT_W2K, lut_R22_5S, 42);
            LUT_Flag = 0;
        }
        break;

    case 1: // GC mode (full refresh without ghosting)
        LCD_WriteReg(hlcdc, REG_LUT_VCOM, lut_R20_GC, 56);
        LCD_WriteReg(hlcdc, REG_LUT_W2W, lut_R21_GC, 42);
        LCD_WriteReg(hlcdc, REG_LUT_K2K, lut_R24_GC, 42);
        if (LUT_Flag == 0)
        {
            LCD_WriteReg(hlcdc, REG_LUT_K2W, lut_R22_GC, 56);
            LCD_WriteReg(hlcdc, REG_LUT_W2K, lut_R23_GC, 42);
            LUT_Flag = 1;
        }
        else
        {
            LCD_WriteReg(hlcdc, REG_LUT_K2W, lut_R23_GC, 56);
            LCD_WriteReg(hlcdc, REG_LUT_W2K, lut_R22_GC, 42);
            LUT_Flag = 0;
        }
        break;
    case 2: // DU mode (partial refresh with ghosting)
        LCD_WriteReg(hlcdc, REG_LUT_VCOM, lut_R20_DU, 56);
        LCD_WriteReg(hlcdc, REG_LUT_W2W, lut_R21_DU, 42);
        LCD_WriteReg(hlcdc, REG_LUT_K2K, lut_R24_DU, 42);
        if (LUT_Flag == 0)
        {
            LCD_WriteReg(hlcdc, REG_LUT_K2W, lut_R22_DU, 56);
            LCD_WriteReg(hlcdc, REG_LUT_W2K, lut_R23_DU, 42);
            LUT_Flag = 1;
        }
        else
        {
            LCD_WriteReg(hlcdc, REG_LUT_K2W, lut_R23_DU, 56);
            LCD_WriteReg(hlcdc, REG_LUT_W2K, lut_R22_DU, 42);
            LUT_Flag = 0;
        }
        break;
    default:
        break;
    }
}

static void EPD_DisplayImage(LCDC_HandleTypeDef *hlcdc, uint8_t img_flag)
{
    uint16_t row, col;
    uint16_t pcnt = 0;
    uint8_t *temp_buf = kmm_malloc(PICTURE_LENGTH);

    if (temp_buf == NULL)
    {
        rt_kprintf("EPD image buf malloc failed\n");
        return;
    }

    for (col = 0; col < LCD_VER_RES_MAX; col++)
    {
        for (row = 0; row < LCD_HOR_RES_MAX / 8; row++)
        {
            switch (img_flag)
            {
            case PIC_BLACK:
                temp_buf[pcnt] = 0x00;
                break;
            case PIC_WHITE:
                temp_buf[pcnt] = 0xFF;
                break;
            case PIC_LEFT_BLACK_RIGHT_WHITE:
                temp_buf[pcnt] = (col >= LCD_VER_RES_MAX / 2) ? 0xFF : 0x00;
                break;
            case PIC_UP_BLACK_DOWN_WHITE:
                temp_buf[pcnt] = (row > LCD_HOR_RES_MAX / 16)    ? 0xFF
                                 : (row == LCD_HOR_RES_MAX / 16) ? 0x0F
                                                                 : 0x00;
                break;
            default:
                temp_buf[pcnt] = 0x00;
                break;
            }
            pcnt++;
        }
    }

    LCD_WriteReg(hlcdc, REG_WRITE_NEW_DATA, temp_buf, PICTURE_LENGTH);
    kmm_free(temp_buf);

    uint8_t parameter[5];
    parameter[0] = 0xA5;
    LCD_WriteReg(hlcdc, REG_AUTO_REFRESH, parameter, 1);
    EPD_ReadBusy();
}
static void EPD_EnterDeepSleep(LCDC_HandleTypeDef *hlcdc)
{
    uint8_t parameter[5];
    parameter[0] = 0xA5;
    LCD_WriteReg(hlcdc, 0x07, parameter, 1);
}
static void EPD_TemperatureMeasure(LCDC_HandleTypeDef *hlcdc)
{
    uint8_t parameter[5];
    LCD_WriteReg(hlcdc, REG_PWR_ON_MEASURE, NULL, 0);
    parameter[0] = 0xA5;
    LCD_WriteReg(hlcdc, REG_TEMP_SEL, parameter, 1);
    LCD_WriteReg(hlcdc, REG_TEMP_CALIB, NULL, 0);
    EPD_ReadBusy();

    HAL_LCDC_ReadDatas(hlcdc, REG_TEMP_READ, 0, &Var_Temp, 1);
    // rt_kprintf("EPD internal temp: %d °C", (int8_t)Var_Temp);
}

static const LCD_DrvOpsDef epd_spi_drv = {LCD_Init,
                                          LCD_ReadID,
                                          LCD_DisplayOn,
                                          LCD_DisplayOff,
                                          LCD_SetRegion,
                                          LCD_WritePixel,
                                          LCD_WriteMultiplePixels,
                                          LCD_ReadPixel,
                                          LCD_SetColorMode,
                                          LCD_SetBrightness,
                                          LCD_IdleModeOn,
                                          LCD_IdleModeOff};

LCD_DRIVER_EXPORT(epd_spi, EPD_LCD_ID, &lcdc_int_cfg, &epd_spi_drv,
                  LCD_PIXEL_WIDTH, LCD_PIXEL_HEIGHT, 8);
