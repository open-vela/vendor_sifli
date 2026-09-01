/*
 * SPDX-FileCopyrightText: 2019-2022 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __DRV_LCD_FB_H__
#define __DRV_LCD_FB_H__

#include "drv_lcd.h"

/**
 * @brief Framebuffer base descriptor structure, stores memory address, resolution, pixel format and compression info of a framebuffer
 */
typedef struct
{
    LCD_AreaDef area;     /* Full canvas rectangle of framebuffer, origin at top-left of screen */
    uint8_t    *p_data;   /* Start address of framebuffer pixel memory */
    uint16_t   format;    /* Pixel format: RTGRAPHIC_PIXEL_FORMAT_RGB565 / RGB888 */
    uint8_t    cmpr_rate; /* Pixel compression ratio, 0 = no compression */
    uint32_t   line_bytes;/* Total byte count per pixel line (including alignment padding) */
} lcd_fb_desc_t;

/**
 * @brief Callback function type triggered when framebuffer write operation completes
 * @param fb_desc Pointer to descriptor of the finished framebuffer
 */
typedef void (*write_fb_cbk)(lcd_fb_desc_t *fb_desc);

/**
 * @brief Initialize framebuffer driver, open LCD device, create sync event and configure line interrupt
 * @param lcd_dev_name Name string of target LCD device
 * @return uint32_t RT_EOK on success; RT_EEMPTY if target device not found
 */
uint32_t drv_lcd_fb_init(const char *lcd_dev_name);

/**
 * @brief Deinitialize framebuffer driver, wait for all pending tasks, destroy sync events and close LCD device
 * @return uint32_t RT_EOK
 */
uint32_t drv_lcd_fb_deinit(void);

/**
 * @brief Bind or switch framebuffer, manage allocation and switching logic of double framebuffer fb0/fb1
 * @param fb_desc Parameter structure of framebuffer to attach
 * @return uint32_t RT_EOK
 */
uint32_t drv_lcd_fb_set(lcd_fb_desc_t *fb_desc);

/**
 * @brief Non-blocking query driver busy status (ongoing memory copy / screen refresh task)
 * @return uint32_t 0 = idle; 1 = busy
 */
uint32_t drv_lcd_fb_is_busy(void);

/**
 * @brief Block and wait for completion of current framebuffer memory transfer (DMA / EPIC / AES)
 * @param wait_ms Maximum timeout value in milliseconds
 * @return rt_err_t RT_EOK on normal completion; -RT_ETIMEOUT if timeout occurs
 */
rt_err_t drv_lcd_fb_wait_write_done(int32_t wait_ms);

/**
 * @brief Core API: Asynchronously copy source pixel data to framebuffer, optionally trigger hardware flush after copy completes
 * @param write_area Target rectangle region inside framebuffer to write
 * @param src_area Valid full region of source pixel buffer
 * @param src Start address of source pixel data buffer
 * @param cb Callback function invoked after write finishes
 * @param send 1 = auto trigger fb_flush_start screen refresh after memory copy; 0 = only write memory without screen output
 * @return rt_err_t RT_EOK if asynchronous copy task started successfully
 */
rt_err_t drv_lcd_fb_write_send(LCD_AreaDef *write_area, LCD_AreaDef *src_area, const uint8_t *src, write_fb_cbk cb, uint8_t send);

/**
 * @brief Get tear-free safe writable rectangle, internal logic waits for screen scanline automatically
 * @param write_area Input expected write region, output cropped safe writable region
 * @param wait_ms Maximum waiting timeout in milliseconds
 * @return rt_err_t Waiting operation result
 */
rt_err_t drv_lcd_fb_get_write_area(LCD_AreaDef *write_area, int32_t wait_ms);

/**
 * @brief Manually trigger screen refresh, output merged fb_clip region to physical LCD panel
 * @param cb Callback function invoked after screen flush completes
 * @return rt_err_t RT_EOK if refresh task started successfully
 */
rt_err_t drv_lcd_fb_send(write_fb_cbk cb);

#endif