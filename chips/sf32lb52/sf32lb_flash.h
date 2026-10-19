/*
 * SPDX-FileCopyrightText: 2019-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __VENDOR_SIFLI_SF32LB52_SF32LB_FLASH_H
#define __VENDOR_SIFLI_SF32LB52_SF32LB_FLASH_H

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int sf32lb_flash_lock(void);
int sf32lb_flash_unlock(void);
int sf32lb_nor_automount(int minor, int block_offset, int block_count);

#endif /* __VENDOR_SIFLI_SF32LB52_SF32LB_FLASH_H */
