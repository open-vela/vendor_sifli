/****************************************************************************
 * vendor/sifli/chips/sf32lb52/sf32lb52_epic.c
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

#include <nuttx/config.h>

#include <errno.h>
#include <limits.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>

#include "bf0_hal.h"
#include "irq.h"
#include "sf32lb52_epic.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The SDK driver waits for EPIC completion without a 500 ms cap.  A legal
 * 505x505 transform sourced from PSRAM exceeds 500 ms on this board, so retain
 * bounded recovery while allowing the hardware's documented maximum job.
 */

#define SF32LB52_EPIC_TIMEOUT_MS          2000
#define SF32LB52_EPIC_COORD_MAX           505
#define SF32LB52_EPIC_TRANSFORM_COORD_MAX 505
#define SF32LB52_EPIC_MAX_ROW_BYTES       8191
#define SF32LB52_EPIC_MAX_LUT_ENTRIES     256
#define SF32LB52_EPIC_IRQ                 (EPIC_IRQn + NVIC_IRQ_FIRST)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sf32lb52_epic_context_s
{
  EPIC_HandleTypeDef handle;
#ifdef HAL_EZIP_MODULE_ENABLED
  EZIP_HandleTypeDef ezip;
#endif
  mutex_t lock;
  sem_t done;
  bool initialized;
  bool irq_attached;
  bool task_active;
  int task_error;
  bool pending;
  struct sf32lb52_epic_layer_s pending_output;
  bool normal_busy;
  uint64_t normal_start_us;
  bool cont_active;
  bool cont_pending;
  bool cont_busy;
  uint64_t cont_start_us;
  enum sf32lb52_epic_color_format_e cont_input_format;
  enum sf32lb52_epic_color_format_e cont_output_format;
  uint16_t cont_output_total_width;
  bool cont_has_mask;
  enum sf32lb52_epic_color_format_e cont_mask_format;
  struct sf32lb52_epic_layer_s cont_output;
  struct sf32lb52_epic_stats_s stats;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static mutex_t g_epic_lifecycle_lock = NXMUTEX_INITIALIZER;

static struct sf32lb52_epic_context_s g_epic =
{
  .lock = NXMUTEX_INITIALIZER,
  .done = SEM_INITIALIZER(0),
};

static uint64_t sf32lb52_epic_time_us(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
    {
      return 0;
    }

  return (uint64_t)ts.tv_sec * 1000000ull +
         (uint64_t)ts.tv_nsec / 1000ull;
}

static void sf32lb52_epic_record_busy_locked(bool *active,
                                              uint64_t *start_us)
{
  uint64_t now_us;

  if (active == NULL || start_us == NULL || !*active)
    {
      return;
    }

  now_us = sf32lb52_epic_time_us();
  if (now_us >= *start_us)
    {
      g_epic.stats.busy_time_us += now_us - *start_us;
    }
  g_epic.stats.busy_op_count++;
  *active = false;
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t sf32lb52_epic_bpp(
  enum sf32lb52_epic_color_format_e format)
{
  switch (format)
    {
      case SF32LB52_EPIC_COLOR_RGB565:
        return 16;

      case SF32LB52_EPIC_COLOR_ARGB8565:
      case SF32LB52_EPIC_COLOR_RGB888:
        return 24;

      case SF32LB52_EPIC_COLOR_ARGB8888:
        return 32;

      case SF32LB52_EPIC_COLOR_L8:
      case SF32LB52_EPIC_COLOR_A8:
      case SF32LB52_EPIC_COLOR_MONO:
        return 8;

      case SF32LB52_EPIC_COLOR_A4:
        return 4;

      case SF32LB52_EPIC_COLOR_A2:
        return 2;

      default:
        return 0;
    }
}

static bool sf32lb52_epic_is_psram(uintptr_t address)
{
  /* Use the same board memory-map definition as the MPU and allocator. */

#if defined(PSRAM_BASE) && defined(PSRAM_SIZE) && PSRAM_SIZE > 0
  return address >= (uintptr_t)PSRAM_BASE &&
         address - (uintptr_t)PSRAM_BASE < (uintptr_t)PSRAM_SIZE;
#else
  /* The NuttX board allocator exposes an 8 MiB PSRAM region even though this
   * SDK-derived mem_map configuration leaves PSRAM_SIZE at zero.
   */

  return address >= 0x60000000u && address < 0x60800000u;
#endif
}

static bool sf32lb52_epic_is_ax_format(
  enum sf32lb52_epic_color_format_e format)
{
  return format == SF32LB52_EPIC_COLOR_A8 ||
         format == SF32LB52_EPIC_COLOR_A4 ||
         format == SF32LB52_EPIC_COLOR_A2 ||
         format == SF32LB52_EPIC_COLOR_MONO;
}

static bool sf32lb52_epic_is_mask(
  const struct sf32lb52_epic_layer_s *layer)
{
  return sf32lb52_epic_is_ax_format(layer->format) &&
         layer->alpha_mode != SF32LB52_EPIC_ALPHA_COLOR;
}

static uint32_t sf32lb52_epic_scale(uint32_t scale)
{
  /* Treat zero-initialized public structures as an identity transform. */

  return scale == 0 ? SF32LB52_EPIC_SCALE_NONE : scale;
}

static bool sf32lb52_epic_is_transform(
  const struct sf32lb52_epic_layer_s *layer)
{
  const struct sf32lb52_epic_transform_s *transform = &layer->transform;

  return transform->angle != 0 || transform->h_mirror != 0 ||
         transform->v_mirror != 0 ||
         sf32lb52_epic_scale(transform->scale_x) !=
           SF32LB52_EPIC_SCALE_NONE ||
         sf32lb52_epic_scale(transform->scale_y) !=
           SF32LB52_EPIC_SCALE_NONE;
}

static bool sf32lb52_epic_required_size(
  const struct sf32lb52_epic_layer_s *layer, size_t *required)
{
  uint32_t bpp = sf32lb52_epic_bpp(layer->format);
  size_t row_bytes;
  size_t last_row_bytes;

  if (bpp == 0 || layer->width == 0 || layer->height == 0 ||
      layer->total_width < layer->width)
    {
      return false;
    }

  row_bytes = ((size_t)layer->total_width * bpp + 7) / 8;
  last_row_bytes = ((size_t)layer->width * bpp + 7) / 8;

  if (row_bytes > SF32LB52_EPIC_MAX_ROW_BYTES ||
      (size_t)(layer->height - 1) >
        (SIZE_MAX - last_row_bytes) / row_bytes)
    {
      return false;
    }

  *required = (size_t)(layer->height - 1) * row_bytes + last_row_bytes;
  return true;
}

static bool sf32lb52_epic_layer_supported(
  const struct sf32lb52_epic_layer_s *layer, bool output)
{
  const struct sf32lb52_epic_transform_s *transform;
  uintptr_t start;
  size_t required;
  int32_t x1;
  int32_t y1;

  if (layer == NULL || layer->data == NULL ||
      !sf32lb52_epic_required_size(layer, &required) ||
      layer->data_size < required ||
      layer->x_offset == INT16_MIN || layer->y_offset == INT16_MIN)
    {
      return false;
    }

  start = (uintptr_t)layer->data;
  if (required > UINTPTR_MAX - start)
    {
      return false;
    }

  x1 = (int32_t)layer->x_offset + layer->width - 1;
  y1 = (int32_t)layer->y_offset + layer->height - 1;
  if (x1 > INT16_MAX || y1 > INT16_MAX ||
      layer->width > SF32LB52_EPIC_COORD_MAX ||
      layer->height > SF32LB52_EPIC_COORD_MAX)
    {
      return false;
    }

  if (output)
    {
      return layer->format >= SF32LB52_EPIC_COLOR_RGB565 &&
             layer->format <= SF32LB52_EPIC_COLOR_ARGB8888;
    }

  if (layer->format < SF32LB52_EPIC_COLOR_RGB565 ||
      layer->format > SF32LB52_EPIC_COLOR_MONO ||
      layer->alpha_mode < SF32LB52_EPIC_ALPHA_COLOR ||
      layer->alpha_mode > SF32LB52_EPIC_ALPHA_OVERWRITE)
    {
      return false;
    }

  /* A2/A4/A8/MONO need a fixed RGB color when used as a color/alpha
   * source.  A real MASK/OVERWRITE layer consumes only alpha and does not.
   */

  if (sf32lb52_epic_is_ax_format(layer->format) &&
      !sf32lb52_epic_is_mask(layer) && !layer->color_en)
    {
      return false;
    }

  /* The SF32LB52 mask formatter accepts A8, A4, and MONO only.  A2 is a
   * valid colorized input layer, but selecting it as MASK/OVERWRITE reaches
   * a HAL assertion in EPIC_GetMaskLayerColorFormat().
   */

  if (layer->format == SF32LB52_EPIC_COLOR_A2 &&
      sf32lb52_epic_is_mask(layer))
    {
      return false;
    }

  if (!sf32lb52_epic_is_ax_format(layer->format) &&
      layer->alpha_mode != SF32LB52_EPIC_ALPHA_COLOR)
    {
      return false;
    }

  if (layer->format == SF32LB52_EPIC_COLOR_L8 &&
      (layer->lookup_table == NULL || layer->lookup_table_size == 0 ||
       layer->lookup_table_size > SF32LB52_EPIC_MAX_LUT_ENTRIES))
    {
      return false;
    }

  transform = &layer->transform;
  if (transform->h_mirror > 1 || transform->v_mirror > 1 ||
      sf32lb52_epic_scale(transform->scale_x) > 1048575 ||
      sf32lb52_epic_scale(transform->scale_y) > 1048575)
    {
      return false;
    }

  /* SF32LB52 cannot combine mirror with rotation/scaling. */

  if ((transform->h_mirror || transform->v_mirror) &&
      (transform->angle != 0 ||
       sf32lb52_epic_scale(transform->scale_x) !=
         SF32LB52_EPIC_SCALE_NONE ||
       sf32lb52_epic_scale(transform->scale_y) !=
         SF32LB52_EPIC_SCALE_NONE))
    {
      return false;
    }

  return true;
}

static bool sf32lb52_epic_blend_supported(
  const struct sf32lb52_epic_layer_s *inputs, size_t input_count,
  const struct sf32lb52_epic_layer_s *output)
{
  int32_t min_x;
  int32_t min_y;
  int32_t max_x;
  int32_t max_y;
  size_t normal_count = 0;
  size_t mask_count = 0;
  size_t transform_count = 0;
  size_t l8_count = 0;
  bool has_transform = false;
  size_t i;

  if (input_count > SF32LB52_EPIC_MAX_INPUT_LAYERS ||
      (input_count != 0 && inputs == NULL) ||
      !sf32lb52_epic_layer_supported(output, true) ||
      (input_count == 0 && !output->color_en))
    {
      return false;
    }

  min_x = output->x_offset;
  min_y = output->y_offset;
  max_x = (int32_t)output->x_offset + output->width - 1;
  max_y = (int32_t)output->y_offset + output->height - 1;

  for (i = 0; i < input_count; i++)
    {
      bool output_alias;
      bool mask;
      bool transform;
      int32_t x1;
      int32_t y1;

      /* The usual background layer is an exact view of the already validated
       * output buffer.  Rechecking its row span and address range for every
       * glyph/image/fill adds no safety; still require plain input semantics
       * before taking this fast validation path.
       */

      output_alias = inputs[i].data == output->data &&
        inputs[i].format == output->format &&
        inputs[i].width == output->width &&
        inputs[i].height == output->height &&
        inputs[i].total_width == output->total_width &&
        inputs[i].x_offset == output->x_offset &&
        inputs[i].y_offset == output->y_offset &&
        inputs[i].data_size >= output->data_size &&
        inputs[i].alpha_mode == SF32LB52_EPIC_ALPHA_COLOR &&
        !sf32lb52_epic_is_transform(&inputs[i]);

      if (!output_alias &&
          !sf32lb52_epic_layer_supported(&inputs[i], false))
        {
          return false;
        }

      mask = sf32lb52_epic_is_mask(&inputs[i]);
      transform = sf32lb52_epic_is_transform(&inputs[i]);
      if (inputs[i].format == SF32LB52_EPIC_COLOR_L8)
        {
          l8_count++;
        }
      if (mask)
        {
          mask_count++;
          if (i == 0 || transform)
            {
              return false;
            }
        }
      else
        {
          normal_count++;
        }

      if (transform)
        {
          transform_count++;
          has_transform = true;
          if (inputs[i].width > SF32LB52_EPIC_TRANSFORM_COORD_MAX ||
              inputs[i].height > SF32LB52_EPIC_TRANSFORM_COORD_MAX)
            {
              return false;
            }
        }

      x1 = (int32_t)inputs[i].x_offset + inputs[i].width - 1;
      y1 = (int32_t)inputs[i].y_offset + inputs[i].height - 1;
      if (inputs[i].x_offset < min_x)
        {
          min_x = inputs[i].x_offset;
        }

      if (inputs[i].y_offset < min_y)
        {
          min_y = inputs[i].y_offset;
        }

      if (x1 > max_x)
        {
          max_x = x1;
        }

      if (y1 > max_y)
        {
          max_y = y1;
        }
    }

  /* SF32LB52 exposes one lookup-table block.  A second L8 layer would make
   * EPIC_Allocate_L8Table() return an invalid table id and assert in HAL.
   */

  if (normal_count > 2 || mask_count > 1 || transform_count > 1 ||
      l8_count > 1)
    {
      return false;
    }

  if (max_x - min_x + 1 >
        (has_transform ? SF32LB52_EPIC_TRANSFORM_COORD_MAX :
                         SF32LB52_EPIC_COORD_MAX) ||
      max_y - min_y + 1 >
        (has_transform ? SF32LB52_EPIC_TRANSFORM_COORD_MAX :
                         SF32LB52_EPIC_COORD_MAX))
    {
      return false;
    }

  return true;
}

static void sf32lb52_epic_map_layer(
  EPIC_LayerConfigTypeDef *dest,
  const struct sf32lb52_epic_layer_s *source)
{
  HAL_EPIC_LayerConfigInit(dest);
  dest->data = (uint8_t *)source->data;
  dest->color_mode = (uint32_t)source->format;
  dest->width = source->width;
  dest->height = source->height;
  dest->total_width = source->total_width;
  dest->x_offset = source->x_offset;
  dest->y_offset = source->y_offset;
  dest->alpha = source->alpha;
  dest->color_en = source->color_en != 0;
  dest->color_r = source->color_r;
  dest->color_g = source->color_g;
  dest->color_b = source->color_b;
  dest->ax_mode = (Alpha_BlendTypeDef)source->alpha_mode;
  dest->lookup_table = (uint8_t *)source->lookup_table;
  dest->lookup_table_size = source->lookup_table_size;
  dest->data_size = source->data_size > UINT32_MAX ?
                    UINT32_MAX : (uint32_t)source->data_size;
  dest->transform_cfg.angle = source->transform.angle;
  dest->transform_cfg.h_mirror = source->transform.h_mirror;
  dest->transform_cfg.v_mirror = source->transform.v_mirror;
  dest->transform_cfg.pivot_x = source->transform.pivot_x;
  dest->transform_cfg.pivot_y = source->transform.pivot_y;
  dest->transform_cfg.scale_x =
    sf32lb52_epic_scale(source->transform.scale_x);
  dest->transform_cfg.scale_y =
    sf32lb52_epic_scale(source->transform.scale_y);
}

static void sf32lb52_epic_cache_clean_layer(
  const struct sf32lb52_epic_layer_s *layer)
{
  uint32_t bpp;
  size_t row_bytes;
  size_t active_bytes;
  size_t required;
  uintptr_t start;
  uint16_t row;

  start = (uintptr_t)layer->data;

  /* Keep the CPU-to-EPIC ownership boundary explicit for every layer.  The
   * SDK normally needs this only for its cacheable PSRAM window, but this
   * NuttX image can place LVGL heap objects in a write-back SRAM alias while
   * the EPIC still observes the physical RAM view.  Cleaning non-cacheable
   * SRAM is harmless; skipping it can make a just-decoded glyph appear as a
   * previous glyph. */

  if (sf32lb52_epic_required_size(layer, &required))
    {
      bpp = sf32lb52_epic_bpp(layer->format);
      row_bytes = ((size_t)layer->total_width * bpp + 7) / 8;
      active_bytes = ((size_t)layer->width * bpp + 7) / 8;

      /* A clipped rectangle usually has a large destination stride.  EPIC
       * does not read the gap between visible rows, so maintain just the
       * active bytes of each row instead of walking the entire span.
       */

      if (layer->height > 1 && active_bytes < row_bytes)
        {
          for (row = 0; row < layer->height; row++)
            {
              uintptr_t row_start = start + (size_t)row * row_bytes;
              up_clean_dcache(row_start, row_start + active_bytes);
            }
        }
      else
        {
          up_clean_dcache(start, start + required);
        }

      g_epic.stats.cache_clean_count++;
    }

  /* Palette ownership is independent of the pixel buffer's memory region. */
  if (layer->format == SF32LB52_EPIC_COLOR_L8 &&
      layer->lookup_table != NULL && layer->lookup_table_size != 0)
    {
      start = (uintptr_t)layer->lookup_table;
      if (sf32lb52_epic_is_psram(start))
        {
          up_clean_dcache(start,
                          start + (size_t)layer->lookup_table_size *
                                  sizeof(uint32_t));
          g_epic.stats.cache_clean_count++;
        }
    }
}

static void sf32lb52_epic_cache_invalidate_layer(
  const struct sf32lb52_epic_layer_s *layer)
{
  uint32_t bpp;
  size_t row_bytes;
  size_t active_bytes;
  size_t required;
  uintptr_t start;
  uint16_t row;

  start = (uintptr_t)layer->data;
  if (!sf32lb52_epic_required_size(layer, &required))
    {
      return;
    }

  bpp = sf32lb52_epic_bpp(layer->format);
  row_bytes = ((size_t)layer->total_width * bpp + 7) / 8;
  active_bytes = ((size_t)layer->width * bpp + 7) / 8;

  if (layer->height > 1 && active_bytes < row_bytes)
    {
      for (row = 0; row < layer->height; row++)
        {
          uintptr_t row_start = start + (size_t)row * row_bytes;
          up_invalidate_dcache(row_start, row_start + active_bytes);
        }
    }
  else
    {
      up_invalidate_dcache(start, start + required);
    }

  g_epic.stats.cache_invalidate_count++;
}

static bool sf32lb52_epic_same_buffer_region(
  const struct sf32lb52_epic_layer_s *first,
  const struct sf32lb52_epic_layer_s *second)
{
  return first->data == second->data && first->format == second->format &&
         first->width == second->width && first->height == second->height &&
         first->total_width == second->total_width;
}

static void sf32lb52_epic_complete(EPIC_HandleTypeDef *handle)
{
  (void)handle;
  nxsem_post(&g_epic.done);
}

static int sf32lb52_epic_interrupt(int irq, void *context, void *arg)
{
  (void)irq;
  (void)context;
  (void)arg;

  if (g_epic.initialized)
    {
      HAL_EPIC_IRQHandler(&g_epic.handle);
    }

  return 0;
}

static int sf32lb52_epic_hal_errno(HAL_StatusTypeDef status)
{
  if (status == HAL_BUSY)
    {
      return -EBUSY;
    }

  if (status == HAL_TIMEOUT)
    {
      return -ETIMEDOUT;
    }

  return -EIO;
}

static bool sf32lb52_epic_task_owned_locked(void)
{
  return g_epic.task_active && nxmutex_is_hold(&g_epic.lock);
}

static void sf32lb52_epic_record_task_error_locked(int ret)
{
  if (ret < 0 && g_epic.task_active && g_epic.task_error == 0)
    {
      g_epic.task_error = ret;
    }
}

static HAL_StatusTypeDef sf32lb52_epic_hal_initialize_locked(void)
{
  HAL_StatusTypeDef status;

  memset(&g_epic.handle, 0, sizeof(g_epic.handle));
  g_epic.handle.Instance = hwp_epic;

#ifdef HAL_EZIP_MODULE_ENABLED
  /* The HCPU EPIC HAL unconditionally dereferences handle.hezip during
   * HAL_EPIC_Init(), even when the submitted layers are not compressed.
   * Keep a real EZIP handle for the complete EPIC lifetime, matching the
   * SiFli SDK driver setup.
   */

  memset(&g_epic.ezip, 0, sizeof(g_epic.ezip));
  g_epic.ezip.Instance = EZIP;
  g_epic.ezip.flash_handle_query_cb = NULL;
  status = HAL_EZIP_Init(&g_epic.ezip);
  if (status != HAL_OK)
    {
      return status;
    }

  g_epic.handle.hezip = &g_epic.ezip;
#endif

  status = HAL_EPIC_Init(&g_epic.handle);
  if (status != HAL_OK)
    {
#ifdef HAL_EZIP_MODULE_ENABLED
      HAL_EZIP_DeInit(&g_epic.ezip);
      HAL_RCC_DisableModule(RCC_MOD_EZIP);
#endif
    }
  return status;
}

static int sf32lb52_epic_recover_locked(void)
{
  HAL_StatusTypeDef status;

  g_epic.pending = false;
  g_epic.normal_busy = false;
  g_epic.cont_active = false;
  g_epic.cont_pending = false;
  g_epic.cont_busy = false;
  up_disable_irq(SF32LB52_EPIC_IRQ);
  HAL_NVIC_ClearPendingIRQ(EPIC_IRQn);
  HAL_RCC_ResetModule(RCC_MOD_EPIC);
#ifdef HAL_EZIP_MODULE_ENABLED
  HAL_NVIC_ClearPendingIRQ(EZIP_IRQn);
  HAL_RCC_ResetModule(RCC_MOD_EZIP);
#endif

  status = sf32lb52_epic_hal_initialize_locked();
  nxsem_reset(&g_epic.done, 0);
  HAL_NVIC_ClearPendingIRQ(EPIC_IRQn);

  if (status == HAL_OK)
    {
      up_enable_irq(SF32LB52_EPIC_IRQ);
      return 0;
    }

  if (g_epic.irq_attached)
    {
      irq_detach(SF32LB52_EPIC_IRQ);
      g_epic.irq_attached = false;
    }

  HAL_RCC_ResetModule(RCC_MOD_EPIC);
  HAL_RCC_DisableModule(RCC_MOD_EPIC);
#ifdef HAL_EZIP_MODULE_ENABLED
  HAL_EZIP_DeInit(&g_epic.ezip);
  HAL_RCC_ResetModule(RCC_MOD_EZIP);
  HAL_RCC_DisableModule(RCC_MOD_EZIP);
  memset(&g_epic.ezip, 0, sizeof(g_epic.ezip));
#endif
  memset(&g_epic.handle, 0, sizeof(g_epic.handle));
  g_epic.initialized = false;
  return sf32lb52_epic_hal_errno(status);
}

static int sf32lb52_epic_wait_locked(
  const struct sf32lb52_epic_layer_s *output)
{
  int ret;

  ret = nxsem_tickwait_uninterruptible(
          &g_epic.done, MSEC2TICK(SF32LB52_EPIC_TIMEOUT_MS));
  if (ret < 0)
    {
      if (ret == -ETIMEDOUT)
        {
          g_epic.stats.timeout_count++;
        }
      else
        {
          g_epic.stats.error_count++;
        }

      sf32lb52_epic_record_busy_locked(&g_epic.normal_busy,
                                       &g_epic.normal_start_us);
      sf32lb52_epic_recover_locked();
      sf32lb52_epic_cache_invalidate_layer(output);
      return ret;
    }

  /* EOF is raised after the peripheral has issued its writes.  Order the
   * following CPU reads after those bus transactions and discard any stale
   * cache lines for either SRAM or PSRAM-backed layers.
   */

  __DSB();
  /* The completion semaphore is signalled at EOF.  Capture the end point
   * before destination cache invalidation so the metric represents EPIC
   * execution/IRQ hand-off rather than CPU cache maintenance. */
  sf32lb52_epic_record_busy_locked(&g_epic.normal_busy,
                                   &g_epic.normal_start_us);
  sf32lb52_epic_cache_invalidate_layer(output);
  __DSB();
  if (g_epic.handle.ErrorCode != 0)
    {
      g_epic.stats.error_count++;
      return -EIO;
    }

  g_epic.stats.complete_count++;
  return 0;
}

static int sf32lb52_epic_pending_wait_locked(void)
{
  int ret;

  if (!g_epic.pending)
    {
      return 0;
    }

  g_epic.stats.normal_wait_count++;
  ret = sf32lb52_epic_wait_locked(&g_epic.pending_output);
  g_epic.pending = false;
  sf32lb52_epic_record_task_error_locked(ret);
  return ret;
}

static bool sf32lb52_epic_cont_supported(
  const struct sf32lb52_epic_layer_s *inputs, size_t input_count,
  const struct sf32lb52_epic_layer_s *output)
{
  if ((input_count != 2 && input_count != 3) || inputs == NULL ||
      !sf32lb52_epic_blend_supported(inputs, input_count, output) ||
      !sf32lb52_epic_same_buffer_region(&inputs[0], output) ||
      inputs[0].x_offset != output->x_offset ||
      inputs[0].y_offset != output->y_offset ||
      inputs[0].alpha != 255 || inputs[0].color_en != 0 ||
      sf32lb52_epic_is_transform(&inputs[0]) ||
      sf32lb52_epic_is_psram((uintptr_t)output->data) ||
      sf32lb52_epic_is_transform(&inputs[1]) ||
      sf32lb52_epic_is_mask(&inputs[1]) ||
      inputs[1].format == SF32LB52_EPIC_COLOR_L8 ||
      (!sf32lb52_epic_is_ax_format(inputs[1].format) &&
       inputs[1].color_en != 0))
    {
      return false;
    }

  return input_count != 3 ||
         (sf32lb52_epic_is_mask(&inputs[2]) &&
          !sf32lb52_epic_is_transform(&inputs[2]));
}

static int sf32lb52_epic_cont_wait_locked(void)
{
  clock_t start;
  clock_t timeout;
  uint32_t spin_count = 0;

  if (!g_epic.cont_active || !g_epic.cont_pending)
    {
      return 0;
    }

  g_epic.stats.cont_wait_count++;
  if (g_epic.handle.Instance->STATUS != 0)
    {
      /* Avoid the relatively expensive system-clock read in the usual case:
       * source preparation has already outlasted the tiny glyph operation.
       * Only arm the bounded timeout when hardware is actually still busy.
       */

      start = clock_systime_ticks();
      timeout = MSEC2TICK(SF32LB52_EPIC_TIMEOUT_MS);
      do
        {
          /* The SDK deliberately polls between continuous glyphs so it can
           * reprogram EPIC without an IRQ/semaphore round trip.  Keep that
           * hand-off tight, sampling the clock only every 256 polls.
           */

          if ((++spin_count & 0xffu) == 0 &&
              (clock_t)(clock_systime_ticks() - start) >= timeout)
            {
              g_epic.stats.timeout_count++;
              sf32lb52_epic_record_busy_locked(&g_epic.cont_busy,
                                               &g_epic.cont_start_us);
              sf32lb52_epic_record_task_error_locked(-ETIMEDOUT);
              sf32lb52_epic_recover_locked();
              return -ETIMEDOUT;
            }
        }
      while (g_epic.handle.Instance->STATUS != 0);
    }

  g_epic.cont_pending = false;
  sf32lb52_epic_record_busy_locked(&g_epic.cont_busy,
                                   &g_epic.cont_start_us);

  if (g_epic.handle.ErrorCode != 0)
    {
      g_epic.stats.error_count++;
      sf32lb52_epic_record_task_error_locked(-EIO);
      sf32lb52_epic_recover_locked();
      return -EIO;
    }

  g_epic.stats.complete_count++;
  return 0;
}

static int sf32lb52_epic_cont_stop_locked(void)
{
  HAL_StatusTypeDef status;
  int ret;

  if (!g_epic.cont_active)
    {
      return 0;
    }

  ret = sf32lb52_epic_cont_wait_locked();
  if (ret < 0)
    {
      return ret;
    }

  status = HAL_EPIC_ContBlendStop(&g_epic.handle);
  g_epic.cont_active = false;
  g_epic.cont_pending = false;
  if (status != HAL_OK)
    {
      g_epic.stats.error_count++;
      sf32lb52_epic_record_task_error_locked(
        sf32lb52_epic_hal_errno(status));
      sf32lb52_epic_recover_locked();
      return sf32lb52_epic_hal_errno(status);
    }

  /* Repeat only needs the hardware-idle hand-off.  Publish the completed
   * continuous session to CPU/SW consumers once, at the task synchronization
   * boundary, matching the SDK's ContBlendStop lifecycle.  Continuous output
   * is currently restricted to non-PSRAM, but retain the invalidate here so
   * the ownership rule stays correct if that capability is widened later.
   */

  __DSB();
  sf32lb52_epic_cache_invalidate_layer(&g_epic.cont_output);
  __DSB();

  g_epic.stats.cont_stop_count++;
  return 0;
}

static int sf32lb52_epic_task_flush_locked(void)
{
  int ret;

  ret = sf32lb52_epic_cont_stop_locked();
  if (ret < 0)
    {
      return ret;
    }

  ret = sf32lb52_epic_pending_wait_locked();
  return ret < 0 ? ret : g_epic.task_error;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int sf32lb52_epic_initialize(void)
{
  HAL_StatusTypeDef status;
  int ret;

  ret = nxmutex_lock(&g_epic_lifecycle_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&g_epic.lock);
  if (ret < 0)
    {
      nxmutex_unlock(&g_epic_lifecycle_lock);
      return ret;
    }

  if (g_epic.initialized)
    {
      ret = 0;
      goto out;
    }

  g_epic.task_active = false;
  g_epic.task_error = 0;
  g_epic.pending = false;
  g_epic.cont_active = false;
  g_epic.cont_pending = false;
  nxsem_reset(&g_epic.done, 0);

  status = sf32lb52_epic_hal_initialize_locked();
  if (status != HAL_OK)
    {
      ret = sf32lb52_epic_hal_errno(status);
      goto out;
    }

  ret = irq_attach(SF32LB52_EPIC_IRQ, sf32lb52_epic_interrupt, NULL);
  if (ret < 0)
    {
      HAL_RCC_ResetModule(RCC_MOD_EPIC);
      HAL_RCC_DisableModule(RCC_MOD_EPIC);
#ifdef HAL_EZIP_MODULE_ENABLED
      HAL_EZIP_DeInit(&g_epic.ezip);
      HAL_RCC_ResetModule(RCC_MOD_EZIP);
      HAL_RCC_DisableModule(RCC_MOD_EZIP);
#endif
      goto out;
    }

  g_epic.irq_attached = true;
  HAL_NVIC_ClearPendingIRQ(EPIC_IRQn);
  g_epic.initialized = true;
  up_enable_irq(SF32LB52_EPIC_IRQ);
  ret = 0;

out:
  nxmutex_unlock(&g_epic.lock);
  nxmutex_unlock(&g_epic_lifecycle_lock);
  return ret;
}

int sf32lb52_epic_uninitialize(void)
{
  int ret;

  ret = nxmutex_lock(&g_epic_lifecycle_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&g_epic.lock);
  if (ret < 0)
    {
      nxmutex_unlock(&g_epic_lifecycle_lock);
      return ret;
    }

  if (g_epic.initialized || g_epic.irq_attached)
    {
      up_disable_irq(SF32LB52_EPIC_IRQ);
      g_epic.initialized = false;
      if (g_epic.irq_attached)
        {
          irq_detach(SF32LB52_EPIC_IRQ);
          g_epic.irq_attached = false;
        }

      HAL_NVIC_ClearPendingIRQ(EPIC_IRQn);
      HAL_RCC_ResetModule(RCC_MOD_EPIC);
      HAL_RCC_DisableModule(RCC_MOD_EPIC);
#ifdef HAL_EZIP_MODULE_ENABLED
      HAL_EZIP_DeInit(&g_epic.ezip);
      HAL_NVIC_ClearPendingIRQ(EZIP_IRQn);
      HAL_RCC_ResetModule(RCC_MOD_EZIP);
      HAL_RCC_DisableModule(RCC_MOD_EZIP);
      memset(&g_epic.ezip, 0, sizeof(g_epic.ezip));
#endif
      memset(&g_epic.handle, 0, sizeof(g_epic.handle));
      nxsem_reset(&g_epic.done, 0);
      g_epic.task_active = false;
      g_epic.task_error = 0;
      g_epic.pending = false;
      g_epic.cont_active = false;
      g_epic.cont_pending = false;
    }

  nxmutex_unlock(&g_epic.lock);
  nxmutex_unlock(&g_epic_lifecycle_lock);
  return 0;
}

bool sf32lb52_epic_is_ready(void)
{
  /* READY means that a caller can acquire a new task immediately.  Do not use
   * the hardware STATUS register here: SF32LB52 can retain non-zero
   * bookkeeping bits after EOF even though the HAL accepts another operation.
   * Capability evaluators must use is_initialized(), because an asynchronous
   * task being in flight does not make later LVGL tasks unsupported.
   */

  return g_epic.initialized &&
         !g_epic.task_active && !g_epic.pending && !g_epic.cont_active &&
         g_epic.handle.State == HAL_EPIC_STATE_READY;
}

bool sf32lb52_epic_is_initialized(void)
{
  return g_epic.initialized;
}

bool sf32lb52_epic_buffer_supported(
  const struct sf32lb52_epic_layer_s *layer, bool output)
{
  return sf32lb52_epic_layer_supported(layer, output);
}

static int sf32lb52_epic_blend_submit_locked(
  const struct sf32lb52_epic_layer_s *inputs, size_t input_count,
  const struct sf32lb52_epic_layer_s *output, bool *submitted)
{
  EPIC_LayerConfigTypeDef hal_inputs[SF32LB52_EPIC_MAX_INPUT_LAYERS];
  EPIC_LayerConfigTypeDef hal_output;
  EPIC_FillingCfgTypeDef fill;
  HAL_StatusTypeDef status;
  size_t i;
  int ret;

  if (submitted != NULL)
    {
      *submitted = false;
    }

  ret = sf32lb52_epic_cont_stop_locked();
  if (ret < 0)
    {
      return ret;
    }

  ret = sf32lb52_epic_pending_wait_locked();
  if (ret < 0)
    {
      return ret;
    }

  if (!g_epic.initialized)
    {
      return -ENODEV;
    }

  if (!sf32lb52_epic_blend_supported(inputs, input_count, output))
    {
      return -EINVAL;
    }

  nxsem_reset(&g_epic.done, 0);
  for (i = 0; i < input_count; i++)
    {
      /* The usual background input aliases output exactly.  Cleaning it a
       * second time adds no coherency and used to double the dominant cache
       * cost of every alpha/image operation.
       */

      if (!sf32lb52_epic_same_buffer_region(&inputs[i], output))
        {
          sf32lb52_epic_cache_clean_layer(&inputs[i]);
        }
    }

  /* Always clean the old destination.  Alpha blend reads it, and cleaning
   * also preserves dirty bytes sharing the first/last cache line before the
   * post-operation invalidate.
   */

  sf32lb52_epic_cache_clean_layer(output);

  /* Drain buffered CPU writes before EPIC starts reading source layers or the
   * in-place destination background.  The maintenance is deliberately
   * address-agnostic because the active memory attributes can differ between
   * the SDK MPU setup and a NuttX bootloader/heap configuration.
   */

  __DSB();
  g_epic.handle.ErrorCode = 0;
  g_epic.handle.XferCpltCallback = sf32lb52_epic_complete;
  if (input_count == 0)
    {
      HAL_EPIC_FillDataInit(&fill);
      fill.start = (uint8_t *)output->data;
      fill.color_mode = (uint32_t)output->format;
      fill.width = output->width;
      fill.height = output->height;
      fill.total_width = output->total_width;
      fill.color_r = output->color_r;
      fill.color_g = output->color_g;
      fill.color_b = output->color_b;
      fill.alpha = output->alpha;
      status = HAL_EPIC_FillStart_IT(&g_epic.handle, &fill);
    }
  else
    {
      for (i = 0; i < input_count; i++)
        {
          sf32lb52_epic_map_layer(&hal_inputs[i], &inputs[i]);
        }

      sf32lb52_epic_map_layer(&hal_output, output);
      status = HAL_EPIC_BlendStartEx_IT(&g_epic.handle, hal_inputs,
                                        (uint8_t)input_count, &hal_output);
    }

  if (status != HAL_OK)
    {
      g_epic.handle.XferCpltCallback = NULL;
      g_epic.handle.IntXferCpltCallback = NULL;
      g_epic.stats.error_count++;
      return sf32lb52_epic_hal_errno(status);
    }

  g_epic.normal_start_us = sf32lb52_epic_time_us();
  g_epic.normal_busy = true;
  g_epic.stats.submit_count++;
  g_epic.stats.normal_submit_count++;
  if (submitted != NULL)
    {
      *submitted = true;
    }

  g_epic.pending_output = *output;
  g_epic.pending = true;
  return 0;
}

static int sf32lb52_epic_gradient_submit_locked(
  const struct sf32lb52_epic_layer_s *output,
  const struct sf32lb52_epic_gradient_s *gradient, bool *submitted)
{
  EPIC_GradCfgTypeDef hal_gradient;
  HAL_StatusTypeDef status;
  unsigned int row;
  unsigned int column;
  int ret;

  if (submitted != NULL)
    {
      *submitted = false;
    }

  ret = sf32lb52_epic_cont_stop_locked();
  if (ret < 0)
    {
      return ret;
    }

  ret = sf32lb52_epic_pending_wait_locked();
  if (ret < 0)
    {
      return ret;
    }

  if (!g_epic.initialized)
    {
      return -ENODEV;
    }

  if (gradient == NULL ||
      !sf32lb52_epic_layer_supported(output, true))
    {
      return -EINVAL;
    }

  HAL_EPIC_FillGradDataInit(&hal_gradient);
  hal_gradient.start = output->data;
  hal_gradient.color_mode = (uint32_t)output->format;
  hal_gradient.width = output->width;
  hal_gradient.height = output->height;
  hal_gradient.total_width = output->total_width;

  for (row = 0; row < 2; row++)
    {
      for (column = 0; column < 2; column++)
        {
          hal_gradient.color[row][column].ch.color_r =
            gradient->color_r[row][column];
          hal_gradient.color[row][column].ch.color_g =
            gradient->color_g[row][column];
          hal_gradient.color[row][column].ch.color_b =
            gradient->color_b[row][column];
          hal_gradient.color[row][column].ch.alpha =
            gradient->color_a[row][column];
        }
    }

  nxsem_reset(&g_epic.done, 0);

  /* Gradient alpha makes the engine read the old destination before writing
   * it, so preserve dirty cache lines exactly as in the blend path.
   */

  sf32lb52_epic_cache_clean_layer(output);
  __DSB();
  g_epic.handle.ErrorCode = 0;
  g_epic.handle.XferCpltCallback = sf32lb52_epic_complete;
  status = HAL_EPIC_FillGrad_IT(&g_epic.handle, &hal_gradient);
  if (status != HAL_OK)
    {
      g_epic.handle.XferCpltCallback = NULL;
      g_epic.handle.IntXferCpltCallback = NULL;
      g_epic.stats.error_count++;
      return sf32lb52_epic_hal_errno(status);
    }

  g_epic.stats.submit_count++;
  g_epic.stats.normal_submit_count++;
  if (submitted != NULL)
    {
      *submitted = true;
    }

  g_epic.pending_output = *output;
  g_epic.pending = true;
  g_epic.normal_start_us = sf32lb52_epic_time_us();
  g_epic.normal_busy = true;
  return 0;
}

int sf32lb52_epic_task_begin(void)
{
  int ret;

  if (nxmutex_is_hold(&g_epic.lock))
    {
      return -EDEADLK;
    }

  ret = nxmutex_lock(&g_epic.lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!g_epic.initialized)
    {
      nxmutex_unlock(&g_epic.lock);
      return -ENODEV;
    }

  if (g_epic.task_active || g_epic.pending || g_epic.cont_active ||
      g_epic.handle.State != HAL_EPIC_STATE_READY)
    {
      nxmutex_unlock(&g_epic.lock);
      return -EBUSY;
    }

  g_epic.task_active = true;
  g_epic.task_error = 0;
  g_epic.stats.task_count++;
  return 0;
}

int sf32lb52_epic_task_wait(void)
{
  int ret;

  if (!sf32lb52_epic_task_owned_locked())
    {
      return -EPERM;
    }

  if (g_epic.cont_active)
    {
      ret = sf32lb52_epic_cont_wait_locked();
      if (ret >= 0)
        {
          /* task_wait() is an explicit visibility boundary even though it
           * deliberately keeps the continuous session open for a Repeat.
           */

          __DSB();
          sf32lb52_epic_cache_invalidate_layer(&g_epic.cont_output);
          __DSB();
        }
    }
  else
    {
      ret = sf32lb52_epic_pending_wait_locked();
    }

  return ret < 0 ? ret : g_epic.task_error;
}

int sf32lb52_epic_task_flush(void)
{
  if (!sf32lb52_epic_task_owned_locked())
    {
      return -EPERM;
    }

  return sf32lb52_epic_task_flush_locked();
}

int sf32lb52_epic_task_end(void)
{
  int ret;

  if (!sf32lb52_epic_task_owned_locked())
    {
      return -EPERM;
    }

  if (g_epic.pending || g_epic.cont_pending)
    {
      g_epic.stats.task_tail_wait_count++;
    }

  ret = sf32lb52_epic_task_flush_locked();
  if (ret >= 0)
    {
      ret = g_epic.task_error;
    }

  g_epic.task_error = 0;
  g_epic.task_active = false;
  nxmutex_unlock(&g_epic.lock);
  return ret;
}

int sf32lb52_epic_blend_submit(
  const struct sf32lb52_epic_layer_s *inputs, size_t input_count,
  const struct sf32lb52_epic_layer_s *output, bool *submitted)
{
  if (!sf32lb52_epic_task_owned_locked())
    {
      if (submitted != NULL)
        {
          *submitted = false;
        }

      return -EPERM;
    }

  if (g_epic.task_error < 0)
    {
      if (submitted != NULL)
        {
          *submitted = false;
        }

      return g_epic.task_error;
    }

  return sf32lb52_epic_blend_submit_locked(inputs, input_count, output,
                                           submitted);
}

int sf32lb52_epic_gradient_submit(
  const struct sf32lb52_epic_layer_s *output,
  const struct sf32lb52_epic_gradient_s *gradient, bool *submitted)
{
  if (!sf32lb52_epic_task_owned_locked())
    {
      if (submitted != NULL)
        {
          *submitted = false;
        }

      return -EPERM;
    }

  if (g_epic.task_error < 0)
    {
      if (submitted != NULL)
        {
          *submitted = false;
        }

      return g_epic.task_error;
    }

  return sf32lb52_epic_gradient_submit_locked(output, gradient, submitted);
}

int sf32lb52_epic_cont_blend_submit(
  const struct sf32lb52_epic_layer_s *inputs, size_t input_count,
  const struct sf32lb52_epic_layer_s *output, bool *submitted)
{
  EPIC_LayerConfigTypeDef hal_input;
  EPIC_LayerConfigTypeDef hal_mask;
  EPIC_LayerConfigTypeDef hal_output;
  EPIC_LayerConfigTypeDef *mask = NULL;
  HAL_StatusTypeDef status;
  int ret;

  if (submitted != NULL)
    {
      *submitted = false;
    }

  if (!sf32lb52_epic_task_owned_locked())
    {
      return -EPERM;
    }

  if (g_epic.task_error < 0)
    {
      return g_epic.task_error;
    }

  ret = sf32lb52_epic_pending_wait_locked();
  if (ret < 0)
    {
      return ret;
    }

  if (!sf32lb52_epic_cont_supported(inputs, input_count, output))
    {
      return -ENOTSUP;
    }

  if (g_epic.cont_active &&
      (inputs[1].format != g_epic.cont_input_format ||
       output->format != g_epic.cont_output_format ||
       output->total_width != g_epic.cont_output_total_width ||
       (input_count == 3) != g_epic.cont_has_mask ||
       (input_count == 3 &&
        inputs[2].format != g_epic.cont_mask_format)))
    {
      ret = sf32lb52_epic_cont_stop_locked();
      if (ret < 0)
        {
          return ret;
        }
    }

  /* Prepare and clean the next source while the previous continuous
   * operation is still running.  With distinct caller-owned staging (as used
   * by the LVGL label unit), this preserves the SDK's CPU/EPIC overlap.  The
   * bounded wait remains immediately before Repeat reprograms the engine.
   */

  sf32lb52_epic_cache_clean_layer(&inputs[1]);
  if (input_count == 3)
    {
      sf32lb52_epic_cache_clean_layer(&inputs[2]);
    }

  sf32lb52_epic_map_layer(&hal_input, &inputs[1]);
  if (input_count == 3)
    {
      sf32lb52_epic_map_layer(&hal_mask, &inputs[2]);
      mask = &hal_mask;
    }

  sf32lb52_epic_map_layer(&hal_output, output);

  if (!g_epic.cont_active)
    {
      sf32lb52_epic_cache_clean_layer(output);
    }

  ret = sf32lb52_epic_cont_wait_locked();
  if (ret < 0)
    {
      return ret;
    }

  __DSB();
  g_epic.handle.ErrorCode = 0;

  if (!g_epic.cont_active)
    {
      g_epic.handle.XferCpltCallback = NULL;
      g_epic.handle.IntXferCpltCallback = NULL;
      status = HAL_EPIC_ContBlendStart(&g_epic.handle, &hal_input, mask,
                                       &hal_output);
      if (status == HAL_OK)
        {
          g_epic.cont_active = true;
          g_epic.cont_input_format = inputs[1].format;
          g_epic.cont_output_format = output->format;
          g_epic.cont_output_total_width = output->total_width;
          g_epic.cont_has_mask = input_count == 3;
          if (g_epic.cont_has_mask)
            {
              g_epic.cont_mask_format = inputs[2].format;
            }

          g_epic.stats.cont_start_count++;
        }
    }
  else
    {
      status = HAL_EPIC_ContBlendRepeat(&g_epic.handle, &hal_input, mask,
                                        &hal_output);
      if (status == HAL_OK)
        {
          g_epic.stats.cont_repeat_count++;
        }
    }

  if (status != HAL_OK)
    {
      g_epic.stats.error_count++;
      sf32lb52_epic_recover_locked();
      return sf32lb52_epic_hal_errno(status);
    }

  g_epic.stats.submit_count++;
  g_epic.cont_output = *output;
  g_epic.cont_pending = true;
  g_epic.cont_start_us = sf32lb52_epic_time_us();
  g_epic.cont_busy = true;
  if (submitted != NULL)
    {
      *submitted = true;
    }

  return 0;
}

int sf32lb52_epic_blend_wait(
  const struct sf32lb52_epic_layer_s *inputs, size_t input_count,
  const struct sf32lb52_epic_layer_s *output, bool *submitted)
{
  int end_ret;
  int ret;

  if (submitted != NULL)
    {
      *submitted = false;
    }

  ret = sf32lb52_epic_task_begin();
  if (ret < 0)
    {
      return ret;
    }

  ret = sf32lb52_epic_blend_submit_locked(inputs, input_count, output,
                                           submitted);
  end_ret = sf32lb52_epic_task_end();
  return ret < 0 ? ret : end_ret;
}

int sf32lb52_epic_gradient_wait(
  const struct sf32lb52_epic_layer_s *output,
  const struct sf32lb52_epic_gradient_s *gradient, bool *submitted)
{
  int end_ret;
  int ret;

  if (submitted != NULL)
    {
      *submitted = false;
    }

  ret = sf32lb52_epic_task_begin();
  if (ret < 0)
    {
      return ret;
    }

  ret = sf32lb52_epic_gradient_submit_locked(output, gradient, submitted);
  end_ret = sf32lb52_epic_task_end();
  return ret < 0 ? ret : end_ret;
}

void sf32lb52_epic_get_stats(struct sf32lb52_epic_stats_s *stats)
{
  irqstate_t flags;

  if (stats == NULL)
    {
      return;
    }

  flags = enter_critical_section();
  *stats = g_epic.stats;
  leave_critical_section(flags);
}

void sf32lb52_epic_reset_stats(void)
{
  irqstate_t flags = enter_critical_section();

  memset(&g_epic.stats, 0, sizeof(g_epic.stats));
  leave_critical_section(flags);
}
