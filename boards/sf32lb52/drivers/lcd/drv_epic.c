/*
 * SPDX-FileCopyrightText: 2019-2022 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "drv_epic.h"
#include "lvgl/src/draw/sifli/epic/lv_sifli_epic_cfg.h"

typedef struct
{
    EPIC_BlendingDataType src_layer;
    EPIC_BlendingDataType dst_layer;
    drv_epic_cplt_cbk cbk;
    uint8_t *dst_cache_start;
    uint32_t dst_cache_size;
} drv_epic_copy_ctx_t;

static drv_epic_copy_ctx_t g_copy;

static uint32_t drv_epic_layer_size(const EPIC_BlendingDataType *layer)
{
    uint32_t depth = HAL_EPIC_GetColorDepth(layer->color_mode);

    return (depth * layer->total_width * layer->height + 7u) >> 3;
}

static uint8_t *drv_epic_map_ptr(const uint8_t *data, uint32_t depth,
                                 uint32_t stride_pixels, uint32_t x,
                                 uint32_t y)
{
    uint32_t bit_offset = (y * stride_pixels + x) * depth;

    RT_ASSERT((bit_offset & 7u) == 0u);
    return (uint8_t *)data + (bit_offset >> 3);
}

static void drv_epic_copy_done(EPIC_HandleTypeDef *epic)
{
    drv_epic_cplt_cbk cbk = g_copy.cbk;

    lv_epic_invalidate_cache_range(g_copy.dst_cache_start,
                                   g_copy.dst_cache_size);
    g_copy.cbk = NULL;
    g_copy.dst_cache_start = NULL;
    g_copy.dst_cache_size = 0;

    if (cbk != NULL)
    {
        cbk(epic);
    }
}

void drv_gpu_open(void)
{
    if (!lv_epic_is_initialized())
    {
        lv_epic_init();
    }
}

void drv_gpu_close(void)
{
    if (lv_epic_is_initialized())
    {
        (void)lv_epic_wait();
    }
}

rt_err_t drv_epic_copy(const uint8_t *src, uint8_t *dst,
                       const EPIC_AreaTypeDef *src_area,
                       const EPIC_AreaTypeDef *dst_area,
                       const EPIC_AreaTypeDef *copy_area,
                       uint32_t src_cf, uint32_t dst_cf,
                       drv_epic_cplt_cbk cbk)
{
    uint32_t src_width;
    uint32_t dst_width;
    uint32_t copy_width;
    uint32_t copy_height;
    uint32_t dst_depth;
    uint32_t dst_x;
    uint32_t dst_y;
    HAL_StatusTypeDef status;

    RT_ASSERT(src != NULL && dst != NULL);
    RT_ASSERT(src_area != NULL && dst_area != NULL && copy_area != NULL);
    RT_ASSERT(HAL_EPIC_AreaIsIn(copy_area, src_area));
    RT_ASSERT(HAL_EPIC_AreaIsIn(copy_area, dst_area));

    if (!lv_epic_is_initialized())
    {
        return -RT_ERROR;
    }

    src_width = HAL_EPIC_AreaWidth(src_area);
    dst_width = HAL_EPIC_AreaWidth(dst_area);
    copy_width = HAL_EPIC_AreaWidth(copy_area);
    copy_height = HAL_EPIC_AreaHeight(copy_area);
    dst_x = copy_area->x0 - dst_area->x0;
    dst_y = copy_area->y0 - dst_area->y0;

    HAL_EPIC_BlendDataInit(&g_copy.src_layer);
    g_copy.src_layer.color_mode = src_cf;
    g_copy.src_layer.total_width = src_width;
    g_copy.src_layer.data = (uint8_t *)src;
    g_copy.src_layer.width = src_width;
    g_copy.src_layer.height = HAL_EPIC_AreaHeight(src_area);
    g_copy.src_layer.x_offset = src_area->x0;
    g_copy.src_layer.y_offset = src_area->y0;
    g_copy.src_layer.data_size = drv_epic_layer_size(&g_copy.src_layer);

    HAL_EPIC_BlendDataInit(&g_copy.dst_layer);
    g_copy.dst_layer.color_mode = dst_cf;
    g_copy.dst_layer.total_width = dst_width;
    g_copy.dst_layer.width = copy_width;
    g_copy.dst_layer.height = copy_height;
    g_copy.dst_layer.x_offset = copy_area->x0;
    g_copy.dst_layer.y_offset = copy_area->y0;
    dst_depth = HAL_EPIC_GetColorDepth(dst_cf);
    g_copy.dst_layer.data = drv_epic_map_ptr(dst, dst_depth, dst_width,
                                             dst_x, dst_y);
    g_copy.dst_layer.data_size = drv_epic_layer_size(&g_copy.dst_layer);

    g_copy.dst_cache_start = g_copy.dst_layer.data;
    g_copy.dst_cache_size = (((copy_height - 1u) * dst_width + copy_width) *
                             dst_depth + 7u) >> 3;
    g_copy.cbk = cbk;
    lv_epic_flush_cache_range(src, g_copy.src_layer.data_size);
    lv_epic_flush_cache_range(g_copy.dst_cache_start, g_copy.dst_cache_size);
    lv_epic_invalidate_cache_range(g_copy.dst_cache_start,
                                   g_copy.dst_cache_size);

    status = lv_epic_copy_async(&g_copy.src_layer, &g_copy.dst_layer,
                                drv_epic_copy_done);
    if (status != HAL_OK)
    {
        g_copy.cbk = NULL;
        g_copy.dst_cache_start = NULL;
        g_copy.dst_cache_size = 0;
        return -RT_ERROR;
    }

    return RT_EOK;
}
