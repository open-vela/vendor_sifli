/*
 * SPDX-FileCopyrightText: 2019-2022 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __DRV_LCD_PRIVATE_H__
#define __DRV_LCD_PRIVATE_H__

#include "drv_lcd.h"

// LCD driver status flag: asynchronous write task is running
#define LCD_DRV_AYSNC_WRITE  0x00000001
// LCD driver is opened and activated
#define LCD_DRV_ACTIVED      0x00000002

#if defined(BSP_LCDC_USING_DIRECT_EPD)
    #define MAX_LCD_DRAW_TIME  (5000)  /* Maximum drawing timeout for E-paper display, unit: ms */
#else
    #define MAX_LCD_DRAW_TIME  (500)  /* Maximum drawing timeout for normal LCD panel, unit: ms */
#endif
#define LCD_ENTER_LP_INTERVAL  (1)  /* Delay 1ms after drawing before entering low-power mode */

#define MIN_BRIGHTNESS_LEVEL 0   /* Minimum backlight brightness level */
#define MAX_BRIGHTNESS_LEVEL 100 /* Maximum backlight brightness level */

#define MAX_TIMEOUT_RETRY 3  /* Max retry count for transmission timeout, -1 means unlimited retries */

// Supplement general minimum macro if undefined
#ifndef MIN
    #define MIN(x,y) (((x)<(y))?(x):(y))
#endif

// Supplement general maximum macro if undefined
#ifndef MAX
    #define MAX(x,y) (((x)>(y))?(x):(y))
#endif

/**
 * @brief LCD drawing performance statistics structure
 */
typedef struct
{
    uint32_t start_tick;        // System tick at the start of current statistics cycle
    uint32_t draw_core_max;     // Maximum tick consumption of single drawing operation
    uint32_t draw_core_min;     // Minimum tick consumption of single drawing operation
    uint32_t draw_core_cnt;     // Total count of drawing operations
    uint32_t draw_core_err_cnt; // Total count of drawing failure occurrences
} LCD_DRVStatistTypeDef;

/**
 * @brief Core LCD low-level driver device structure, inherits standard RT-Thread device
 */
typedef struct
{
    struct lcd_dev_s parent;                // Parent class of NuttX LCD lower-half device
    LCDC_HandleTypeDef hlcdc;               // Hardware handle of LCDC peripheral
    lcd_drv_desc_t *p_drv_ops;             // Low-level screen hardware operation interface (DCX control, flush, CO5300 initialization, etc.)
    uint16_t buf_format;                   // Current framebuffer pixel format: RGB565 / RGB888
    HAL_LCDC_LayerDef select_layer;         // Selected hardware output layer of LCDC

    struct rt_semaphore  sem;               // Mutex semaphore for API calls, prevent concurrent LCD access from multiple threads
    struct rt_semaphore  draw_sem;          // Synchronization semaphore for completion of asynchronous drawing
    struct rt_semaphore  sync_msg_sem;      // Semaphore for synchronous message processing
    LCD_DrvStatusTypeDef status;            // Hardware running status flag of LCD
    uint8_t brightness;                    // Current backlight brightness value, range 0~100

    uint32_t start_tick;                   // System tick when last asynchronous pixel transmission started
    uint32_t end_tick;                     // System tick when last asynchronous pixel transmission finished
    uint8_t draw_lock;                     // Drawing mutex lock, 1 means drawing task is occupied
    uint8_t draw_error;                    // Flag for hardware transmission error during drawing

    // Bit field configuration area start
    uint32_t auto_lowpower : 1;            // 1 = automatically enter low-power mode after drawing finished
    uint32_t force_lcd_missing : 1;        // Force mark screen absent, used for debugging
    /* Drawing timeout handling policy:
        0 - Reset LCD hardware and retry operation
        1 - Trigger assertion halt for debugging
        2 - Unload LCD driver and stop further processing
    */
    uint32_t assert_timeout : 2;
    uint32_t send_time_log: 1;             // Enable log printing of pixel transmission duration
    uint32_t statistics_log: 1;            // Periodically print drawing performance statistics log
    uint32_t skip_draw_core: 1;            // Skip hardware drawing execution, only run upper-layer logic
    uint32_t reserved: 25;                // Reserved unused bits
    // Bit field configuration area end

    int8_t timeout_retry_cnt;              // Remaining available timeout retry count, -1 for unlimited retries
    uint32_t last_esd_check_tick;         // System tick of last ESD screen fault detection

    LCD_DrvRotateTypeDef rotate;           // Screen rotation configuration (0/90/180/270 degree)

    struct rt_thread task;                // Background thread for LCD message processing
    rt_mq_t  mq;                          // Message queue for receiving drawing/control commands from upper layer

    uint32_t debug_cnt1; /* Counter of asynchronous draw rectangle requests (draw_rect_async) */
    uint32_t debug_cnt2; /* Total count of LCDC transmission complete interrupt callbacks */
    uint32_t debug_cnt3; /* Completion count of asynchronous drawing business callbacks */

    LCD_DRVStatistTypeDef statistics;     // Drawing performance statistics data
} LCD_DrvTypeDef;

/**
 * @brief Enumeration of LCD message IDs, distinguish synchronous and asynchronous commands
 */
typedef enum
{
    LCD_MSG_INVALID,                     // Invalid empty message
    /* Start marker of asynchronous message segment */
    __LCD_ASYNCHRONIZED_MSG_START,

    LCD_MSG_OPEN,                        // Open LCD device
    LCD_MSG_POWER_ON,                    // Power on and initialize screen
    LCD_MSG_DRAW_RECT_ASYNC,             // Asynchronous rendering of normal pixel rectangle
    LCD_MSG_DRAW_COMP_RECT_ASYNC,        // Asynchronous rendering of compressed pixel rectangle
    LCD_MSG_SET_NEXT_TE,                 // Configure next TE synchronization signal
    LCD_MSG_GET_BRIGHTNESS_ASYNC,        // Asynchronously read backlight brightness value
    LCD_MSG_FLUSH_RECT_ASYNC,            // Asynchronously flush framebuffer data to physical screen via hardware
    __LCD_ASYNCHRONIZED_MSG_END,         // End marker of asynchronous message segment

    /* Start marker of synchronous blocking message segment */
    __LCD_SYNCHRONIZED_MSG_START,
    LCD_MSG_CONTROL,                     // Generic device control command
    LCD_MSG_CLOSE,                       // Close LCD device
    LCD_MSG_POWER_OFF,                   // Power off screen and enter sleep mode
    LCD_MSG_SET_MODE,                    // Set screen working mode
    LCD_MSG_DRAW_RECT,                   // Synchronous blocking rectangle drawing
    LCD_MSG_SET_WINDOW,                  // Set LCD display window area
    LCD_MSG_SET_PIXEL,                   // Write single pixel
    LCD_MSG_GET_PIXEL,                   // Read single pixel value
    LCD_MSG_SET_BRIGHTNESS,              // Set backlight brightness level
    LCD_MSG_CTRL_SET_LCD_PRESENT,        // Flag whether physical screen is connected
    LCD_MSG_CTRL_ASSERT_IF_DRAWTIMEOUT,  // Configure drawing timeout handling policy
    __LCD_SYNCHRONIZED_MSG_END,          // End marker of synchronous message segment
} LCD_MsgIdDef;

// Judge whether a message ID belongs to synchronous blocking message
#define IS_SYNC_MSG_ID(msg_id) (((msg_id)>__LCD_SYNCHRONIZED_MSG_START)&&((msg_id) < __LCD_SYNCHRONIZED_MSG_END))

/**
 * @brief Rectangle drawing context: pixel buffer address + target drawing region
 */
typedef struct
{
    const uint8_t *pixels;  // Address of pixel data buffer
    LCD_AreaDef area;       // Coordinate rectangle of target drawing area
} LCD_DrawCtxDef;

/**
 * @brief Parameters for single pixel write operation
 */
typedef struct
{
    const uint8_t *data; // Data address of single pixel
    int16_t x;           // X coordinate of pixel
    int16_t y;           // Y coordinate of pixel
} LCD_PixelDef;

/**
 * @brief Context for generic rt_device_control command
 */
typedef struct
{
    rt_device_t dev; // Handle of LCD device
    int cmd;         // Control command code
    void *args;      // Input argument of command
} LCD_CtrlDef;

/**
 * @brief Unified message structure for LCD message queue, parsed and processed by background thread for all drawing/control commands
 */
typedef struct
{
    LCD_DrvTypeDef *driver; // Attached LCD driver device instance
    LCD_MsgIdDef id;        // Message type ID
    uint32_t tick;          // System tick when message enqueued, used for duration statistics

    // Shared union storage, different members used for different message types
    union
    {
        uint8_t brightness;                     // Backlight brightness value
        uint8_t TE_on;                          // TE synchronization enable switch
        LCD_DrawCtxDef draw_ctx;                // Rectangle drawing context
        LCD_AreaDef    window;                  // Screen display window region
        LCD_PixelDef pixel;                     // Single pixel operation parameters
        void *compress_buf;                     // Address of compressed framebuffer
        uint8_t *p_brightness_ret;              // Pointer for storing read-back brightness value
        lcd_flush_info_t flush;                 // Hardware flush configuration of LCDC
        uint8_t idle_mode_on;                   // Low-power idle mode enable switch
        uint8_t is_lcd_present;                 // Flag indicating physical screen existence
        uint8_t assert_timeout;                 // Drawing timeout handling policy value
        LCD_CtrlDef ctrl_ctx;                   // Context for generic control command
    } content;


} LCD_MsgTypeDef;

/**
 * @brief Convert RT graphic standard pixel format to ST HAL LCDC hardware enumeration format
 * @param rt_color_format RT-Thread standard pixel format identifier
 * @return HAL_LCDC_PixelFormat Low-level hardware enumeration value
 */
HAL_LCDC_PixelFormat lcd_format_to_hal_lcd_format(uint16_t rt_color_format);

int drv_lcd_irq_initialize(void);

/**
 * @brief Execute hardware control operations on LCDC layer (window setting, rotation, compression, etc.)
 * @param p_drv_lcd Pointer of LCD driver device instance
 * @param cmd Layer control command code
 * @param args Input parameters of target command
 * @return rt_err_t Execution result status
 */
rt_err_t lcd_layer_control(LCD_DrvTypeDef *p_drv_lcd, int cmd, void *args);

#endif /* __DRV_LCD_PRIVATE_H__ */