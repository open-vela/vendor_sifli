/****************************************************************************
 * vendor/sifli/chips/sf32lb52/include/sf32lb52_epic.h
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

#ifndef __VENDOR_SIFLI_CHIPS_SF32LB52_INCLUDE_SF32LB52_EPIC_H
#define __VENDOR_SIFLI_CHIPS_SF32LB52_INCLUDE_SF32LB52_EPIC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SF32LB52_EPIC_SCALE_NONE       1024u
#define SF32LB52_EPIC_MAX_INPUT_LAYERS 3u

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* The numeric values intentionally match the SF32LB52 EPIC hardware/HAL
 * encoding.  Only RGB565 through ARGB8888 are valid output formats.
 */

enum sf32lb52_epic_color_format_e
{
  SF32LB52_EPIC_COLOR_RGB565   = 0,
  SF32LB52_EPIC_COLOR_ARGB8565 = 1,
  SF32LB52_EPIC_COLOR_RGB888   = 2,
  SF32LB52_EPIC_COLOR_ARGB8888 = 3,
  SF32LB52_EPIC_COLOR_L8       = 4,
  SF32LB52_EPIC_COLOR_A8       = 5,
  SF32LB52_EPIC_COLOR_A4       = 6,
  SF32LB52_EPIC_COLOR_A2       = 7,
  SF32LB52_EPIC_COLOR_MONO     = 8
};

enum sf32lb52_epic_alpha_mode_e
{
  SF32LB52_EPIC_ALPHA_COLOR     = 0,
  SF32LB52_EPIC_ALPHA_MASK      = 1,
  SF32LB52_EPIC_ALPHA_OVERWRITE = 2
};

struct sf32lb52_epic_transform_s
{
  int16_t angle;               /* Angle in 0.1 degree units */
  uint8_t h_mirror;
  uint8_t v_mirror;
  int16_t pivot_x;
  int16_t pivot_y;
  uint32_t scale_x;            /* SF32LB52_EPIC_SCALE_NONE is 1:1 */
  uint32_t scale_y;            /* SF32LB52_EPIC_SCALE_NONE is 1:1 */
};

struct sf32lb52_epic_layer_s
{
  void *data;                  /* Pixel at (x_offset, y_offset) */
  enum sf32lb52_epic_color_format_e format;
  uint16_t width;
  uint16_t height;
  uint16_t total_width;        /* Pixels per complete buffer row */
  int16_t x_offset;
  int16_t y_offset;
  uint8_t alpha;
  uint8_t color_en;
  uint8_t color_r;
  uint8_t color_g;
  uint8_t color_b;
  enum sf32lb52_epic_alpha_mode_e alpha_mode;
  const void *lookup_table;
  uint16_t lookup_table_size;  /* Number of 32-bit palette entries */
  size_t data_size;            /* Accessible bytes starting at data */
  struct sf32lb52_epic_transform_s transform;
};

/* Four corner colors: [row][column], where [0][0] is top-left and
 * [1][1] is bottom-right.
 */

struct sf32lb52_epic_gradient_s
{
  uint8_t color_r[2][2];
  uint8_t color_g[2][2];
  uint8_t color_b[2][2];
  uint8_t color_a[2][2];
};

struct sf32lb52_epic_stats_s
{
  uint32_t submit_count;
  uint32_t complete_count;
  uint32_t normal_submit_count;
  uint32_t normal_wait_count;
  uint32_t cont_start_count;
  uint32_t cont_repeat_count;
  uint32_t cont_wait_count;
  uint32_t cont_stop_count;
  uint32_t task_count;
  uint32_t task_tail_wait_count;
  uint32_t error_count;
  uint32_t timeout_count;
  uint32_t cache_clean_count;
  uint32_t cache_invalidate_count;
  /* Time for which EPIC was executing accepted operations.  This is kept
   * separate from CPU wait time and is reported in microseconds so LVGL can
   * derive both GPU/render and GPU/frame utilization. */
  uint64_t busy_time_us;
  uint32_t busy_op_count;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int sf32lb52_epic_initialize(void);
int sf32lb52_epic_uninitialize(void);
bool sf32lb52_epic_is_initialized(void);
bool sf32lb52_epic_is_ready(void);

bool sf32lb52_epic_buffer_supported(
  const struct sf32lb52_epic_layer_s *layer, bool output);

/* A draw task owns EPIC from task_begin() through task_end().  Submission
 * functions keep at most one hardware operation in flight: a new submission
 * first completes the previous one, while task_end() completes the final one.
 * This mirrors the SiFli SDK driver's one-deep pipeline and lets the LVGL draw
 * worker prepare the next operation while EPIC is running.
 */

int sf32lb52_epic_task_begin(void);
int sf32lb52_epic_task_wait(void);
int sf32lb52_epic_task_flush(void);
int sf32lb52_epic_task_end(void);

int sf32lb52_epic_blend_submit(
  const struct sf32lb52_epic_layer_s *inputs, size_t input_count,
  const struct sf32lb52_epic_layer_s *output, bool *submitted);

int sf32lb52_epic_gradient_submit(
  const struct sf32lb52_epic_layer_s *output,
  const struct sf32lb52_epic_gradient_s *gradient, bool *submitted);

/* Continuous blend is intended for a sequence of untransformed alpha/color
 * layers targeting the same non-cacheable output buffer, notably glyphs.  It
 * avoids reinitializing EPIC and taking an IRQ/semaphore round trip for every
 * layer.  task_wait() is used before a caller reuses its source staging buffer;
 * task_flush()/task_end() stop continuous mode and wait for the last layer.
 */

int sf32lb52_epic_cont_blend_submit(
  const struct sf32lb52_epic_layer_s *inputs, size_t input_count,
  const struct sf32lb52_epic_layer_s *output, bool *submitted);

/* Blend input_count layers into output and wait for completion.  input_count
 * may be zero through SF32LB52_EPIC_MAX_INPUT_LAYERS.  With zero inputs,
 * output->color_en must be set and its color/alpha fields describe a solid
 * fill.  The function returns zero or a negative errno value.  When non-NULL,
 * submitted is cleared before validation and set only after the HAL accepts
 * the operation; callers use it to distinguish safe software fallback from a
 * possibly partially written output.
 */

int sf32lb52_epic_blend_wait(
  const struct sf32lb52_epic_layer_s *inputs, size_t input_count,
  const struct sf32lb52_epic_layer_s *output, bool *submitted);

int sf32lb52_epic_gradient_wait(
  const struct sf32lb52_epic_layer_s *output,
  const struct sf32lb52_epic_gradient_s *gradient, bool *submitted);

void sf32lb52_epic_get_stats(struct sf32lb52_epic_stats_s *stats);
void sf32lb52_epic_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* __VENDOR_SIFLI_CHIPS_SF32LB52_INCLUDE_SF32LB52_EPIC_H */
