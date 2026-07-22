/*
 * SPDX-FileCopyrightText: 2019-2022 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __DRV_EPIC_H__
#define __DRV_EPIC_H__

#include "drv_lcd_nuttx.h"

typedef void (*drv_epic_cplt_cbk)(EPIC_HandleTypeDef *epic);

void drv_gpu_open(void);
void drv_gpu_close(void);

rt_err_t drv_epic_copy(const uint8_t *src, uint8_t *dst,
                       const EPIC_AreaTypeDef *src_area,
                       const EPIC_AreaTypeDef *dst_area,
                       const EPIC_AreaTypeDef *copy_area,
                       uint32_t src_cf, uint32_t dst_cf,
                       drv_epic_cplt_cbk cbk);

#endif /* __DRV_EPIC_H__ */
