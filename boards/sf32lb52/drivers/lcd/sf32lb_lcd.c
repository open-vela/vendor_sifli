/****************************************************************************
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

#include <sfconfig.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>
#include <pthread.h>
#include <semaphore.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <nuttx/spinlock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/spi/spi.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/lcd/lcd_dev.h>
#include <nuttx/lcd/memlcd.h>
#include <nuttx/nuttx.h>
#include <nuttx/cache.h>
#include <nuttx/video/fb.h>

#include "chip.h"
#include "arm_internal.h"


#include "drv_io.h"
#include "sf32lb_lcd.h"

/* Force linker to pull in LCD driver objects from static library.
 * LCD_DRIVER_EXPORT places descriptors in the LcdDriverDescTab section,
 * but the linker will not extract unreferenced .o files from .a archives.
 * These extern references ensure the driver objects are linked in.
 */

#ifdef CONFIG_LCD_USING_CO5300
extern const lcd_drv_desc_t __lcddriver_co5300;
const void *_lcd_drv_ref_co5300
  __attribute__((used, section(".rodata"))) = &__lcddriver_co5300;
#endif

#ifdef CONFIG_LCD_USING_ILI8688E
extern const lcd_drv_desc_t __lcddriver_ili8688e;
const void *_lcd_drv_ref_ili8688e
  __attribute__((used, section(".rodata"))) = &__lcddriver_ili8688e;
#endif
/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
#define lcd_debug_print lcdinfo

struct sf32lb_lcd_dev_s
{
    struct lcd_dev_s dev;
    LCDC_HandleTypeDef hlcdc;
    lcd_drv_desc_t *p_drv_ops;
    uint16_t buf_format;
    HAL_LCDC_LayerDef select_layer;

    pthread_mutex_t init_lock;
    pthread_cond_t  init_cond;
    sem_t draw_sem;
    volatile bool   init_done;

    int power;
    uint8_t bpp;

    pthread_mutex_t conv_lock;  /* Protects s_conv_buf for 8-bit conversion */
};

/* Configuration ************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/
static struct sf32lb_lcd_dev_s s_drv_lcd;
/* Shared with the board power sequencer. */
volatile bool s_lcd_hw_ready;
static volatile bool s_lcd_power_cycled;  /* Set by BSP_LCD_PowerDown to trigger full re-init */

/* Static conversion buffer for 8-bit -> 16-bit pixel format conversion.
 * Sized for maximum supported display width; avoids per-call heap allocation.
 * LCD_CONV_CHUNK_ROWS controls how many rows are batched per DMA transfer
 * in the 8-bit putarea path to reduce DMA transaction overhead.
 */
#define LCD_CONV_MAX_WIDTH   480
#define LCD_CONV_CHUNK_ROWS  8
static uint16_t s_conv_buf[LCD_CONV_MAX_WIDTH * LCD_CONV_CHUNK_ROWS]
    __attribute__((aligned(sizeof(uintptr_t))));

static void sf32lb_lcd_ensure_display_on(FAR struct sf32lb_lcd_dev_s *dev)
{
  if (dev == NULL || dev->p_drv_ops == NULL || dev->p_drv_ops->p_ops == NULL)
    {
      return;
    }

  pthread_mutex_lock(&dev->init_lock);

  /* After a power cycle (deep sleep resume), the CO5300 loses all register
   * state.  We must re-run the full Init sequence, not just DisplayOn.
   */
  if (s_lcd_power_cycled)
    {
      if (dev->p_drv_ops->p_ops->Init != NULL)
        {
          dev->p_drv_ops->p_ops->Init(&dev->hlcdc);
        }
      s_lcd_power_cycled = false;
      dev->power = CONFIG_LCD_MAXPOWER;
      goto out;
    }

  if (dev->power > 0)
    {
      goto out;
    }

  if (dev->p_drv_ops->p_ops->DisplayOn != NULL)
    {
      dev->p_drv_ops->p_ops->DisplayOn(&dev->hlcdc);
      dev->power = CONFIG_LCD_MAXPOWER;
    }

out:
  pthread_mutex_unlock(&dev->init_lock);
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/
static lcd_drv_desc_t *find_right_driver(void)
{

#ifdef CONFIG_LCD_USING_CO5300
  lcdinfo("Use configured lcd driver: co5300");
  return (lcd_drv_desc_t *)&__lcddriver_co5300;
#endif

#ifdef CONFIG_LCD_USING_ILI8688E
  lcdinfo("Use configured lcd driver: ili8688e");
  return (lcd_drv_desc_t *)&__lcddriver_ili8688e;
#endif

    lcd_drv_desc_t *table_begin = NULL;
    lcd_drv_desc_t *table_end = NULL;
    lcd_drv_desc_t *p_drv_desc = NULL;

#if defined(__CC_ARM) || (defined (__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050))                                 /* ARM C Compiler */
    extern const int LcdDriverDescTab$$Base;
    extern const int LcdDriverDescTab$$Limit;
    table_begin = (lcd_drv_desc_t *) &LcdDriverDescTab$$Base;
    table_end = (lcd_drv_desc_t *)   &LcdDriverDescTab$$Limit;
#elif defined (__ICCARM__) || defined(__ICCRX__)      /* for IAR Compiler */
#error "tobe contribute"
#elif defined (__GNUC__)                              /* for GCC Compiler */
    extern const int LcdDriverDescTab_start;
    extern const int LcdDriverDescTab_end;
    table_begin = (lcd_drv_desc_t *)&LcdDriverDescTab_start;
    table_end = (lcd_drv_desc_t *) &LcdDriverDescTab_end;
#endif /* defined(__CC_ARM) */

    if ((NULL == table_begin) || (NULL == table_end) || (table_begin == table_end))
    {
        lcdwarn("No LCD driver registered!");
        return NULL;

    }

#ifndef LCD_MISSING
    for (p_drv_desc = table_begin; p_drv_desc < table_end; p_drv_desc++)
    {
        if ((p_drv_desc->p_ops != NULL) && (p_drv_desc->p_init_cfg != NULL))
        {
            if (p_drv_desc->p_ops->ReadID != NULL)
            {
                uint32_t id;

                if (p_drv_desc->p_ops->Init != NULL)
                    p_drv_desc->p_ops->Init(&s_drv_lcd.hlcdc);

                id = p_drv_desc->p_ops->ReadID(&s_drv_lcd.hlcdc);

                if (p_drv_desc->id == id)
                {
                    lcdinfo("Found lcd %s id:%lxh", p_drv_desc->name, id);
                    return p_drv_desc;
                }
                else
                {
                    lcdinfo("Try lcd %s, read id:%lxh, expect:%lxh", p_drv_desc->name, id, p_drv_desc->id);
                }
            }
        }
    }
#endif
    lcdwarn("unknow lcd!");
    return NULL;
}

static void sf32lb_lcd_setarea(FAR struct sf32lb_lcd_dev_s *dev,
                           uint16_t x0, uint16_t y0,
                           uint16_t x1, uint16_t y1)
{
	if (dev && dev->p_drv_ops && dev->p_drv_ops->p_ops 
		   && dev->p_drv_ops->p_ops->SetRegion)
    {
        int new_x0, new_x1, new_y0, new_y1;


        //disable_low_power(&drv_lcd);

        lcd_debug_print("set_window [%d,%d,%d,%d]", x0, y0, x1, y1);
        new_x0 = x0;
        new_x1 = x1;
        new_y0 = y0;
        new_y1 = y1;


        DEBUGASSERT((new_x0 <= new_x1) && (new_y0 <= new_y1));
        DEBUGASSERT((new_x1 - new_x0 + 1) <= dev->p_drv_ops->lcd_horizonal_res);
        DEBUGASSERT((new_y1 - new_y0 + 1) <= dev->p_drv_ops->lcd_vertical_res);

        dev->p_drv_ops->p_ops->SetRegion(&dev->hlcdc, new_x0, new_y0, new_x1, new_y1);
        //enable_low_power(&drv_lcd);

    }
}
static void SendLayerDataCpltCbk(LCDC_HandleTypeDef *lcdc)
{

   //lcdinfo("SendLayerDataCpltCbk \r\n");

   if (lcdc->XferCpltCallback != NULL)
   {
       lcdc->XferCpltCallback = NULL;


       struct sf32lb_lcd_dev_s *p_drvlcd = container_of(lcdc, struct sf32lb_lcd_dev_s, hlcdc);
       sem_post(&p_drvlcd->draw_sem);
   }

}

static void SendLayerDataErrCbk(LCDC_HandleTypeDef *lcdc)
{
    lcdinfo("SendLayerDataErrCbk \r\n");
    /* Clear both callbacks to prevent double semaphore post.
     * The HAL overflow handler (ICB_OF) clears g_LCDC_CpltCallback;
     * we mirror that pattern here for XferCpltCallback.
     */
    lcdc->XferCpltCallback = NULL;
    if (lcdc->XferErrorCallback != NULL)
    {
        lcdc->XferErrorCallback = NULL;
        struct sf32lb_lcd_dev_s *p_drvlcd =
            container_of(lcdc, struct sf32lb_lcd_dev_s, hlcdc);
        sem_post(&p_drvlcd->draw_sem);
    }
}


static void sf32lb_lcd_wrram(FAR struct sf32lb_lcd_dev_s *dev, FAR const uint8_t *buffer,
                          uint16_t x0, uint16_t y0,
                          uint16_t x1, uint16_t y1)
{
   if (dev && dev->p_drv_ops && dev->p_drv_ops->p_ops 
   		  && dev->p_drv_ops->p_ops->WriteMultiplePixels)
   {
       uint16_t new_x0, new_x1, new_y0, new_y1;
  size_t pixels;
  size_t xfer_bytes;

       DEBUGASSERT((x0 <= x1) && (y0 <= y1));

       //disable_low_power(&drv_lcd);

       lcd_debug_print("sf32lb_lcd_wrram [%d,%d,%d,%d]", x0, y0, x1, y1);
       new_x0 = x0;
       new_x1 = x1;
       new_y0 = y0;
       new_y1 = y1;


       DEBUGASSERT((new_x0 <= new_x1) && (new_y0 <= new_y1));
       DEBUGASSERT((new_x1 - new_x0 + 1) <= dev->p_drv_ops->lcd_horizonal_res);
       DEBUGASSERT((new_y1 - new_y0 + 1) <= dev->p_drv_ops->lcd_vertical_res);

        /* Ensure DMA reads the latest pixel data from memory.
         * The DMA engine always transfers in the LCD panel's native pixel
         * format (RGB565 = 2 bytes/pixel).  When the source is 8-bit the
         * caller converts to 16-bit before calling wrram, so the minimum
         * clean size is pixels * sizeof(uint16_t) regardless of dev->bpp.
         */

        pixels = (size_t)(new_x1 - new_x0 + 1) * (size_t)(new_y1 - new_y0 + 1);
        xfer_bytes = pixels * ((size_t)dev->bpp >> 3);
        if (xfer_bytes < pixels * sizeof(uint16_t))
          {
            xfer_bytes = pixels * sizeof(uint16_t);
          }

        if (buffer != NULL && xfer_bytes > 0)
        {
          up_clean_dcache((uintptr_t)buffer, (uintptr_t)buffer + xfer_bytes);
        }
       
        /* The caller holds conv_lock until DMA completion below. */

        /* Protect callback publication, but keep interrupts enabled during
         * HAL register transfers: those can wait for LCDC/TE and need timer
         * interrupts to enforce their deadlines. conv_lock serializes writers.
         */
        {
          irqstate_t flags = enter_critical_section();
          while (sem_trywait(&(dev->draw_sem)) == 0) {}
          dev->hlcdc.XferCpltCallback = SendLayerDataCpltCbk;
          dev->hlcdc.XferErrorCallback = SendLayerDataErrCbk;
          __sync_fetch_and_add(&dev->hlcdc.debug_cnt0, 1);
          leave_critical_section(flags);
        }
        dev->p_drv_ops->p_ops->WriteMultiplePixels(&dev->hlcdc, buffer,
          new_x0, new_y0, new_x1, new_y1);
        //enable_low_power(&drv_lcd);
        /* --------- Wait send complete (bounded wait) -----------------*/
        {
          struct timespec ts;
          clock_gettime(CLOCK_REALTIME, &ts);
          ts.tv_nsec += 200 * 1000 * 1000; /* 200ms */
          if (ts.tv_nsec >= 1000000000L)
          {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
          }

          if (sem_timedwait(&(dev->draw_sem), &ts) < 0)
          {
            lcdwarn("lcd xfer wait timeout: %d\n", errno);
            /* Force LCDC out of BUSY state so next transfer can proceed */
            dev->hlcdc.State = HAL_LCDC_STATE_READY;
            dev->hlcdc.Lock  = HAL_UNLOCKED;
            /* Clear callbacks to suppress stale ISR posts */
            dev->hlcdc.XferCpltCallback = NULL;
            dev->hlcdc.XferErrorCallback = NULL;
            /* Drain stale tokens */
            while (sem_trywait(&(dev->draw_sem)) == 0) {}
          }
        }

   }
}


                           

/****************************************************************************
 * Name:  sf32lb_lcd_putrun
 *
 * Description:
 *   This method can be used to write a partial raster line to the LCD:
 *
 *   dev     - The lcd device
 *   row     - Starting row to write to (range: 0 <= row < yres)
 *   col     - Starting column to write to (range: 0 <= col <= xres-npixels)
 *   buffer  - The buffer containing the run to be written to the LCD
 *   npixels - The number of pixels to write to the LCD
 *             (range: 0 < npixels <= xres-col)
 *
 ****************************************************************************/

static int sf32lb_lcd_putrun(FAR struct lcd_dev_s *dev,
                         fb_coord_t row, fb_coord_t col,
                         FAR const uint8_t *buffer, size_t npixels)
{
  FAR struct sf32lb_lcd_dev_s *priv = (FAR struct sf32lb_lcd_dev_s *)dev;

  if (!s_lcd_hw_ready)
    {
      return OK;
    }

  lcd_debug_print("row: %d col: %d npixels: %d\n", row, col, npixels);
  if (buffer == NULL)
    {
      return -EINVAL;
    }

  if (priv->bpp == 8)
    {
      size_t i;

      if (npixels > LCD_CONV_MAX_WIDTH)
        {
          lcderr("putrun: npixels %zu exceeds LCD_CONV_MAX_WIDTH %d\n",
                 npixels, LCD_CONV_MAX_WIDTH);
          return -EINVAL;
        }

      pthread_mutex_lock(&priv->conv_lock);
      for (i = 0; i < npixels; i++)
        {
          uint8_t v = buffer[i];
          uint8_t r = (v >> 5) & 0x07;
          uint8_t g = (v >> 2) & 0x07;
          uint8_t b = v & 0x03;

          s_conv_buf[i] = (uint16_t)((((uint16_t)r * 31 / 7) << 11) |
                                     (((uint16_t)g * 63 / 7) << 5) |
                                     (((uint16_t)b * 31 / 3) << 0));
        }

      sf32lb_lcd_ensure_display_on(priv);
      sf32lb_lcd_setarea(priv, col, row, col + npixels - 1, row);
      sf32lb_lcd_wrram(priv, (FAR const uint8_t *)s_conv_buf,
                       col, row, col + npixels - 1, row);
      pthread_mutex_unlock(&priv->conv_lock);
      return OK;
    }

  sf32lb_lcd_ensure_display_on(priv);
  pthread_mutex_lock(&priv->conv_lock);
  sf32lb_lcd_setarea(priv, col, row, col + npixels - 1, row);
  sf32lb_lcd_wrram(priv, buffer, col, row, col + npixels - 1, row);
  pthread_mutex_unlock(&priv->conv_lock);

  return OK;
}

/****************************************************************************
 * Name:  sf32lb_lcd_putarea
 *
 * Description:
 *   This method can be used to write a partial area to the LCD:
 *
 *   dev       - The lcd device
 *   row_start - Starting row to write to (range: 0 <= row < yres)
 *   row_end   - Ending row to write to (range: row_start <= row < yres)
 *   col_start - Starting column to write to (range: 0 <= col <= xres)
 *   col_end   - Ending column to write to
 *               (range: col_start <= col_end < xres)
 *   buffer    - The buffer containing the area to be written to the LCD
 *   stride    - Length of a line in bytes. This parameter may be necessary
 *               to allow the LCD driver to calculate the offset for partial
 *               writes when the buffer needs to be splited for row-by-row
 *               writing.
 *
 ****************************************************************************/

static int sf32lb_lcd_putarea(FAR struct lcd_dev_s *dev,
                          fb_coord_t row_start, fb_coord_t row_end,
                          fb_coord_t col_start, fb_coord_t col_end,
                          FAR const uint8_t *buffer, fb_coord_t stride)
{
  FAR struct sf32lb_lcd_dev_s *priv = (FAR struct sf32lb_lcd_dev_s *)dev;
  size_t bytes_per_pixel;
  size_t row_bytes;

  if (!s_lcd_hw_ready)
    {
      return OK;
    }

  /* Validate coordinates to prevent unsigned underflow / buffer overrun */
  if (row_start > row_end || col_start > col_end)
    {
      return -EINVAL;
    }

  bytes_per_pixel = priv->bpp >> 3;
  row_bytes = (size_t)(col_end - col_start + 1) * bytes_per_pixel;

  lcd_debug_print("row_start: %d row_end: %d col_start: %d col_end: %d\n",
         row_start, row_end, col_start, col_end);

  if (buffer == NULL)
    {
      return -EINVAL;
    }

  if (priv->bpp == 8)
    {
      fb_coord_t width = col_end - col_start + 1;

      if (width > LCD_CONV_MAX_WIDTH)
        {
          lcderr("putarea: width %d exceeds LCD_CONV_MAX_WIDTH %d\n",
                 width, LCD_CONV_MAX_WIDTH);
          return -EINVAL;
        }

      /* Batch LCD_CONV_CHUNK_ROWS rows per DMA to reduce transfer overhead.
       * Source rows are converted into s_conv_buf with tight packing so the
       * DMA engine sees a contiguous block.
       */

      sf32lb_lcd_ensure_display_on(priv);
      pthread_mutex_lock(&priv->conv_lock);
      {
        fb_coord_t y = row_start;

        while (y <= row_end)
          {
            fb_coord_t chunk_end = y + LCD_CONV_CHUNK_ROWS - 1;
            fb_coord_t row;
            size_t offset = 0;

            if (chunk_end > row_end)
              {
                chunk_end = row_end;
              }

            for (row = y; row <= chunk_end; row++)
              {
                FAR const uint8_t *src = buffer + (row - row_start) * stride;
                fb_coord_t x;

                for (x = 0; x < width; x++)
                  {
                    uint8_t v = src[x];
                    uint8_t r = (v >> 5) & 0x07;
                    uint8_t g = (v >> 2) & 0x07;
                    uint8_t b = v & 0x03;

                    s_conv_buf[offset + x] = (uint16_t)(
                      (((uint16_t)r * 31 / 7) << 11) |
                      (((uint16_t)g * 63 / 7) << 5) |
                      (((uint16_t)b * 31 / 3) << 0));
                  }

                offset += width;
              }

            sf32lb_lcd_setarea(priv, col_start, y, col_end, chunk_end);
            sf32lb_lcd_wrram(priv, (FAR const uint8_t *)s_conv_buf,
                             col_start, y, col_end, chunk_end);
            y = chunk_end + 1;
          }
      }
      pthread_mutex_unlock(&priv->conv_lock);
      return OK;
    }

  sf32lb_lcd_ensure_display_on(priv);

  if ((size_t)stride == row_bytes)
    {
      fb_coord_t y = row_start;
      const fb_coord_t chunk_rows = 24;

      pthread_mutex_lock(&priv->conv_lock);
      while (y <= row_end)
        {
          fb_coord_t y1 = y + chunk_rows - 1;
          FAR const uint8_t *src;

          if (y1 > row_end)
            {
              y1 = row_end;
            }

          src = buffer + (y - row_start) * stride;
          sf32lb_lcd_setarea(priv, col_start, y, col_end, y1);
          sf32lb_lcd_wrram(priv, src, col_start, y, col_end, y1);
          y = y1 + 1;
        }
      pthread_mutex_unlock(&priv->conv_lock);
    }
  else
    {
      fb_coord_t y;

      /* The source rows are not tightly packed for this area, so send
       * one row at a time using stride to step through the source buffer.
       */

      pthread_mutex_lock(&priv->conv_lock);
      for (y = row_start; y <= row_end; y++)
        {
          FAR const uint8_t *src = buffer + (y - row_start) * stride;

          sf32lb_lcd_setarea(priv, col_start, y, col_end, y);
          sf32lb_lcd_wrram(priv, src, col_start, y, col_end, y);
        }
      pthread_mutex_unlock(&priv->conv_lock);
    }

  return OK;
}

/****************************************************************************
 * Name:  sf32lb_lcd_getrun
 *
 * Description:
 *   This method can be used to read a partial raster line from the LCD:
 *
 *  dev     - The lcd device
 *  row     - Starting row to read from (range: 0 <= row < yres)
 *  col     - Starting column to read read (range: 0 <= col <= xres-npixels)
 *  buffer  - The buffer in which to return the run read from the LCD
 *  npixels - The number of pixels to read from the LCD
 *            (range: 0 < npixels <= xres-col)
 *
 ****************************************************************************/

#ifndef CONFIG_LCD_NOGETRUN
static int sf32lb_lcd_getrun(FAR struct lcd_dev_s *dev,
                         fb_coord_t row, fb_coord_t col,
                         FAR uint8_t *buffer, size_t npixels)
{
  FAR struct sf32lb_lcd_dev_s *priv = (FAR struct sf32lb_lcd_dev_s *)dev;
  //FAR uint16_t *dest = (FAR uint16_t *)buffer;

  lcdinfo("row: %d col: %d npixels: %d\n", row, col, npixels);
  UNUSED(buffer);
  UNUSED(npixels);

  /* Write-only display — no readback possible */
  /* CO5300 is write-only SPI LCD, readback not supported */
  lcdinfo("getrun not supported on write-only LCD\n");
  return -ENOSYS;
}
#endif

/****************************************************************************
 * Name:  sf32lb_lcd_getvideoinfo
 *
 * Description:
 *   Get information about the LCD video controller configuration.
 *
 ****************************************************************************/

static int sf32lb_lcd_getvideoinfo(FAR struct lcd_dev_s *dev,
                               FAR struct fb_videoinfo_s *vinfo)
{
  DEBUGASSERT(dev && vinfo);

  /* Wait for async lcd_init task to finish before accessing driver state */

  pthread_mutex_lock(&s_drv_lcd.init_lock);
  while (!s_drv_lcd.init_done)
    {
      pthread_cond_wait(&s_drv_lcd.init_cond, &s_drv_lcd.init_lock);
    }
  /* Read p_drv_ops while still holding the lock to avoid race with uninitialize */
  FAR lcd_drv_desc_t *ops = s_drv_lcd.p_drv_ops;
  pthread_mutex_unlock(&s_drv_lcd.init_lock);

  if (ops == NULL)
    {
      return -ENODEV;
    }

 switch(s_drv_lcd.bpp)
 {
    case 8:
      vinfo->fmt     = FB_FMT_RGB8_332;
      break;

    case 16:
      vinfo->fmt     = FB_FMT_RGB16_565;    /* Color format: RGB16-565: RRRR RGGG GGGB BBBB */
      break;

    case 24:
      vinfo->fmt     = FB_FMT_RGB24;    /* Color format: RGB24 */
      break;

    default:
        DEBUGASSERT(0);
        break;
  }

  vinfo->xres    = ops->lcd_horizonal_res;        /* Horizontal resolution in pixel columns */
  vinfo->yres    = ops->lcd_vertical_res;        /* Vertical resolution in pixel rows */
  vinfo->nplanes = 1;                  /* Number of color planes supported */

  
  lcdinfo("fmt: %d xres: %d yres: %d nplanes: 1\n",
          vinfo->fmt, vinfo->xres, vinfo->yres);
  return OK;
}

/****************************************************************************
 * Name:  sf32lb_lcd_getplaneinfo
 *
 * Description:
 *   Get information about the configuration of each LCD color plane.
 *
 ****************************************************************************/

static int sf32lb_lcd_getplaneinfo(FAR struct lcd_dev_s *dev,
                               unsigned int planeno,
                               FAR struct lcd_planeinfo_s *pinfo)
{
  FAR struct sf32lb_lcd_dev_s *priv = (FAR struct sf32lb_lcd_dev_s *)dev;

  pthread_mutex_lock(&s_drv_lcd.init_lock);
  while (!s_drv_lcd.init_done)
    {
      pthread_cond_wait(&s_drv_lcd.init_cond, &s_drv_lcd.init_lock);
    }
  pthread_mutex_unlock(&s_drv_lcd.init_lock);

  DEBUGASSERT(dev && pinfo && planeno == 0);
  lcdinfo("planeno: %d bpp: %d\n", planeno, priv->bpp);

  pinfo->putrun = sf32lb_lcd_putrun;                  /* Put a run into LCD memory */
  pinfo->putarea = sf32lb_lcd_putarea;                /* Put an area into LCD */
#ifndef CONFIG_LCD_NOGETRUN
  pinfo->getrun = sf32lb_lcd_getrun;                  /* Get a run from LCD memory */
#endif
  pinfo->buffer = NULL; //(FAR uint8_t *)priv->runbuffer; /* Run scratch buffer */
  pinfo->bpp    = priv->bpp;                      /* Bits-per-pixel */
  pinfo->dev    = dev;                            /* The lcd device */
  return OK;
}

/****************************************************************************
 * Name:  sf32lb_lcd_getpower
 ****************************************************************************/

static int sf32lb_lcd_getpower(FAR struct lcd_dev_s *dev)
{
  FAR struct sf32lb_lcd_dev_s *priv = (FAR struct sf32lb_lcd_dev_s *)dev;

  lcdinfo("power: %d\n", priv->power);
  return priv->power;
}

/****************************************************************************
 * Name:  sf32lb_lcd_setpower
 ****************************************************************************/

static int sf32lb_lcd_setpower(FAR struct lcd_dev_s *dev, int power)
{
  FAR struct sf32lb_lcd_dev_s *priv = (FAR struct sf32lb_lcd_dev_s *)dev;

  lcdinfo("power: %d\n", power);
  DEBUGASSERT((unsigned)power <= CONFIG_LCD_MAXPOWER);

  pthread_mutex_lock(&priv->init_lock);

  /* Set new power level */

  if (power > 0)
    {
      /* After a power cycle the panel registers are wiped —
       * run the full init sequence instead of just DisplayOn.
       */

      if (s_lcd_power_cycled)
        {
          if (priv->p_drv_ops && priv->p_drv_ops->p_ops &&
              priv->p_drv_ops->p_ops->Init)
            {
              priv->p_drv_ops->p_ops->Init(&priv->hlcdc);
            }

          s_lcd_power_cycled = false;
        }
      else if (priv->p_drv_ops && priv->p_drv_ops->p_ops &&
               priv->p_drv_ops->p_ops->DisplayOn)
        {
          priv->p_drv_ops->p_ops->DisplayOn(&priv->hlcdc);
        }

      /* Save the power */

      priv->power = power;
    }
  else
    {
      /* Turn off the display */

      if (priv->p_drv_ops && priv->p_drv_ops->p_ops &&
          priv->p_drv_ops->p_ops->DisplayOff)
        {
          priv->p_drv_ops->p_ops->DisplayOff(&priv->hlcdc);
        }

      /* Save the power */

      priv->power = 0;
    }

  pthread_mutex_unlock(&priv->init_lock);
  return OK;
}

/****************************************************************************
 * Name:  sf32lb_lcd_getcontrast
 *
 * Description:
 *   Get the current contrast setting (0-CONFIG_LCD_MAXCONTRAST).
 *
 ****************************************************************************/

static int sf32lb_lcd_getcontrast(FAR struct lcd_dev_s *dev)
{
  lcdinfo("Not implemented\n");
  return -ENOSYS;
}

/****************************************************************************
 * Name:  sf32lb_lcd_setcontrast
 *
 * Description:
 *   Set LCD panel contrast (0-CONFIG_LCD_MAXCONTRAST).
 *
 ****************************************************************************/

static int sf32lb_lcd_setcontrast(FAR struct lcd_dev_s *dev,
                              unsigned int contrast)
{
  lcdinfo("contrast: %d\n", contrast);
  return -ENOSYS;
}

/****************************************************************************
 * Name:  sf32lb_lcd_setframerate
 *
 * Description:
 *   Set LCD panel frame rate.  Not supported on this panel.
 *
 ****************************************************************************/

static int sf32lb_lcd_setframerate(FAR struct lcd_dev_s *dev, int rate)
{
  lcdinfo("Not implemented\n");
  return -ENOSYS;
}

/****************************************************************************
 * Name:  sf32lb_lcd_getframerate
 *
 * Description:
 *   Get LCD panel frame rate.  Not supported on this panel.
 *
 ****************************************************************************/

static int sf32lb_lcd_getframerate(FAR struct lcd_dev_s *dev)
{
  lcdinfo("Not implemented\n");
  return -ENOSYS;
}

                              
static int sf32lb_lcd_getalignment(FAR struct lcd_dev_s *dev,
                    FAR struct lcddev_area_align_s *align)
{
    if(align)
    {
        align->row_start_align = 2;
        align->height_align    = 2;
        align->col_start_align = 2;
        align->width_align     = 2;
        align->buf_align       = sizeof(uintptr_t);
    }

    return OK;
}

static int lcdc1_isr(int irq, void *context, void *arg)
{
    HAL_LCDC_IRQHandler((LCDC_HandleTypeDef *)arg);

    
    return OK;
}

static int lcd_hw_setup_thread_entry(int argc, FAR char *argv[])
{
    lcd_drv_desc_t *p_drv_ops = s_drv_lcd.p_drv_ops;
  int ret;
  int retry;

    /* Configure LCDC layer format BEFORE fb_register so that
     * the initial framebuffer content (zeroed by kmm_zalloc) can
     * be written to the panel.  Without this, the display shows
     * garbage until the first application write.
     */

#ifdef SOC_BF0_HCPU     /* gpio1 only work on hcpu */
    irq_attach(NX_IRQ(LCDC1_IRQn), lcdc1_isr, (void *)&s_drv_lcd.hlcdc);
    up_enable_irq(NX_IRQ(LCDC1_IRQn));
#endif /* SOC_BF0_HCPU */

    HAL_LCDC_SetBgColor(&s_drv_lcd.hlcdc, 0, 0, 0);
    HAL_LCDC_LayerReset(&s_drv_lcd.hlcdc, HAL_LCDC_LAYER_DEFAULT);

    /* Use panel's color mode instead of hardcoded RGB565 */
    if (p_drv_ops && p_drv_ops->p_init_cfg)
    {
        HAL_LCDC_LayerSetFormat(&s_drv_lcd.hlcdc, HAL_LCDC_LAYER_DEFAULT,
                                p_drv_ops->p_init_cfg->color_mode);
    }
    else
    {
        /* Fallback to RGB565 if config not available */
        HAL_LCDC_LayerSetFormat(&s_drv_lcd.hlcdc, HAL_LCDC_LAYER_DEFAULT,
                                LCDC_PIXEL_FORMAT_RGB565);
    }

    s_lcd_hw_ready = true;

#if defined(CONFIG_VIDEO_FB) && defined(CONFIG_LCD_FRAMEBUFFER)
    /* Now register /dev/fb0 — the initial zero-fill write will go through
     * because s_lcd_hw_ready is already true.
     */
    for (retry = 0; retry < 30; retry++)
      {
        ret = fb_register(0, 0);

        if (ret == OK || ret == -EEXIST)
          {
            lcdinfo("fb_register done.\n");
            break;
          }

        if (ret != -ENOENT && ret != -ENODEV && ret != -EBUSY)
          {
            syslog(LOG_ERR, "ERROR: fb_register() failed: %d\n", ret);
            break;
          }

        usleep(100 * 1000);
      }

    if (retry >= 30)
      {
        syslog(LOG_ERR, "ERROR: fb_register() exhausted %d retries, last error: %d\n",
               retry, ret);
      }
#endif

    return OK;
}

static int lcd_init_thread_entry(int argc, FAR char *argv[])
{
  lcd_drv_desc_t *p_drv_ops;
  int ret;
  int hw_pid;
  bool lcd_registered = false;
  int retry;

  BSP_LCD_PowerUp();

  p_drv_ops = find_right_driver();

#ifdef CONFIG_LCD_USING_CO5300
  if (!p_drv_ops)
  {
    p_drv_ops = (lcd_drv_desc_t *)&__lcddriver_co5300;
  }
#endif

#ifdef CONFIG_LCD_USING_ILI8688E
  if (!p_drv_ops)
  {
    p_drv_ops = (lcd_drv_desc_t *)&__lcddriver_ili8688e;
  }
#endif

  if (p_drv_ops)
  {
    lcdinfo("Init LCD %s", p_drv_ops->name);

    /* CRITICAL: When CONFIG_LCD_USING_CO5300 (or similar) is defined,
     * find_right_driver() returns the driver directly WITHOUT calling Init().
     * We MUST call Init() here to program the panel registers.
     * Without this, the LCD panel is never configured and won't display anything.
     */

    if (p_drv_ops->p_ops && p_drv_ops->p_ops->Init)
    {
      lcdinfo("Calling Init() for %s\n", p_drv_ops->name);
      p_drv_ops->p_ops->Init(&s_drv_lcd.hlcdc);
    }
  }

  /* Publish p_drv_ops and signal all waiters via condvar.
   * bpp must be set under init_lock so that getvideoinfo() sees a
   * consistent value when it wakes from the condvar.
   */

  pthread_mutex_lock(&s_drv_lcd.init_lock);
  if (p_drv_ops && p_drv_ops->p_init_cfg)
    {
      switch (p_drv_ops->p_init_cfg->color_mode)
        {
          case LCDC_PIXEL_FORMAT_RGB565:
            s_drv_lcd.bpp = 16;
            break;
          case LCDC_PIXEL_FORMAT_RGB888:
            s_drv_lcd.bpp = 24;
            break;
          case LCDC_PIXEL_FORMAT_RGB332:
            s_drv_lcd.bpp = 8;
            break;
          default:
            s_drv_lcd.bpp = 16;
            break;
        }
    }

  s_drv_lcd.p_drv_ops = p_drv_ops;
  s_drv_lcd.init_done = true;
  pthread_cond_broadcast(&s_drv_lcd.init_cond);
  pthread_mutex_unlock(&s_drv_lcd.init_lock);

  if (!p_drv_ops)
  {
    syslog(LOG_ERR, "ERROR: No LCD driver found, skip device register\n");
    return -ENODEV;
  }

#ifdef CONFIG_LCD_DEV
  lcd_registered = false;
#endif

  /* Retry registration to tolerate early-boot timing races. */

  for (retry = 0; retry < 30; retry++)
  {
#ifdef CONFIG_LCD_DEV
    if (!lcd_registered)
    {
      ret = lcddev_register(0);
      if (ret == OK || ret == -EEXIST)
      {
        lcd_registered = true;
        lcdinfo("lcddev_register done.");
      }
      else if (ret != -ENOENT && ret != -ENODEV)
      {
        syslog(LOG_ERR, "ERROR: lcddev_register() failed: %d\n", ret);
        lcd_registered = true; /* stop retrying on hard errors */
      }
    }
#endif

#ifdef CONFIG_LCD_DEV
    if (lcd_registered)
    {
      break;
    }
#endif

    usleep(100 * 1000);
  }

  hw_pid = task_create("lcd_hw",
                       SCHED_PRIORITY_DEFAULT,
                       8192,
                       lcd_hw_setup_thread_entry,
                       NULL);

  if (hw_pid < 0)
  {
    syslog(LOG_ERR, "ERROR: lcd_hw task_create failed: %d\n", errno);

    /* Mark HW ready anyway so putrun/putarea do not hang forever.
     * Display writes will go through the LCDC layer path but the
     * IRQ and background colour will not be configured.
     */

    s_lcd_hw_ready = true;
  }

  return OK;
}
/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name:  board_lcd_initialize
 *
 * Description:
 *   Initialize the LCD video hardware.  The initial state of the LCD is
 *   fully initialized, display memory cleared, and the LCD ready to use, but
 *   with the power setting at 0 (full off).
 *
 ****************************************************************************/

int board_lcd_initialize(void)
{
    static bool initialized = false;
  int pid;

    if (initialized)
      return OK;
    initialized = true;

    lcdinfo("board_lcd_initialize\n");
    syslog(LOG_INFO, "LCD: board_lcd_initialize called\n");
    
    memset(&s_drv_lcd, 0, sizeof(s_drv_lcd));
    s_lcd_power_cycled = false;

    s_drv_lcd.hlcdc.Instance = LCDC1;

    s_drv_lcd.select_layer = HAL_LCDC_LAYER_DEFAULT;
    s_lcd_hw_ready = false;

    pthread_mutex_init(&s_drv_lcd.init_lock, NULL);
    pthread_cond_init(&s_drv_lcd.init_cond, NULL);
    sem_init(&(s_drv_lcd.draw_sem), 0, 0);
    pthread_mutex_init(&s_drv_lcd.conv_lock, NULL);

    /* Keep bringup non-blocking; init/register devices in a worker task. */

    pid = task_create("lcd_init",
                      SCHED_PRIORITY_DEFAULT,
                      4096,
                      lcd_init_thread_entry,
                      NULL);

    if (pid < 0)
      {
        lcdwarn("lcd_init task_create failed: %d", errno);
        return -errno;
      }

    return OK;
}

/****************************************************************************
 * Name:  board_lcd_getdev
 *
 * Description:
 *   Return a a reference to the LCD object for the specified LCD.  This
 *   allows support for multiple LCD devices.
 *
 ****************************************************************************/

struct lcd_dev_s *board_lcd_getdev(int devno)
{
    lcdinfo("board_lcd_getdev\n");

    
    struct lcd_dev_s *g_lcd = NULL;
    g_lcd = &s_drv_lcd.dev;

    g_lcd->getvideoinfo = sf32lb_lcd_getvideoinfo;
    g_lcd->getplaneinfo = sf32lb_lcd_getplaneinfo;
    g_lcd->getpower     = sf32lb_lcd_getpower;
    g_lcd->setpower     = sf32lb_lcd_setpower;
    g_lcd->getcontrast  = sf32lb_lcd_getcontrast;
    g_lcd->setcontrast  = sf32lb_lcd_setcontrast;
    g_lcd->setframerate = sf32lb_lcd_setframerate;
    g_lcd->getframerate = sf32lb_lcd_getframerate;
    g_lcd->getareaalign = sf32lb_lcd_getalignment;
  #if 0
  g_lcd = st7789_lcdinitialize(g_spidev);
  if (!g_lcd)
    {
      lcderr("ERROR: Failed to bind SPI port %d to LCD %d\n", LCD_SPI_PORTNO,
             devno);
    }
  else
    {
      lcdinfo("SPI port %d bound to LCD %d\n", LCD_SPI_PORTNO, devno);
      return g_lcd;
    }
  #endif /* 0 */

  return g_lcd;
}

/****************************************************************************
 * Name:  board_lcd_uninitialize
 *
 * Description:
 *   Uninitialize the LCD support
 *
 ****************************************************************************/

void board_lcd_uninitialize(void)
{
    lcdinfo("board_lcd_uninitialize\n");

    /* Gate new callers first */
    s_lcd_hw_ready = false;

    /* Unblock any thread waiting in wrram (sem_timedwait 200ms) */
    sem_post(&s_drv_lcd.draw_sem);
    usleep(300 * 1000);

    /* Drain any remaining tokens */
    while (sem_trywait(&s_drv_lcd.draw_sem) == 0) {}

#ifdef SOC_BF0_HCPU
    up_disable_irq(NX_IRQ(LCDC1_IRQn));
    irq_detach(NX_IRQ(LCDC1_IRQn));
#endif

    pthread_mutex_lock(&s_drv_lcd.init_lock);
    s_drv_lcd.p_drv_ops = NULL;
    /* Broadcast to unblock any thread waiting in getvideoinfo/getplaneinfo */
    s_drv_lcd.init_done = true;
    pthread_cond_broadcast(&s_drv_lcd.init_cond);
    pthread_mutex_unlock(&s_drv_lcd.init_lock);
    usleep(10 * 1000);  /* Let unblocked threads exit */

    BSP_LCD_PowerDown();

    sem_destroy(&s_drv_lcd.draw_sem);
    pthread_cond_destroy(&s_drv_lcd.init_cond);
    pthread_mutex_destroy(&s_drv_lcd.init_lock);
    pthread_mutex_destroy(&s_drv_lcd.conv_lock);
}

/****************************************************************************
 * Name: board_lcd_notify_power_down
 *
 * Description:
 *   Called by BSP_LCD_PowerDown() to notify the LCD driver that the panel
 *   power has been cut.  The next frame write will trigger a full panel
 *   re-initialization via ensure_display_on().
 *
 ****************************************************************************/

void board_lcd_notify_power_down(void)
{
    pthread_mutex_lock(&s_drv_lcd.init_lock);
    s_lcd_power_cycled = true;
    /* Reset power so ensure_display_on knows to re-init */
    s_drv_lcd.power = 0;
    pthread_mutex_unlock(&s_drv_lcd.init_lock);
}

