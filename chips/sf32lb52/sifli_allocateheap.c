/****************************************************************************
 * vendor/sifli/chip/sf32lb52/sifli_uart.c
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
#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>

#include <stdbool.h>

#include "chip.h"
#include "arm_internal.h"
#include "bf0_hal.h"

extern void BSP_PIN_Init(void);
extern void BSP_Power_Up(bool is_deep_sleep);
extern void BSP_Board_PreInit(void);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SRAM memory configuration for SF32LB52 */

#define SRAM_START  0x20000000
#define SRAM_SIZE   0x00080000    /* 512 KB */

/* The top of HPSYS RAM does not belong to us.  From
 * vendor/sifli/chips/drivers/cmsis/sf32lb52x/mem_map.h, top down:
 *
 *   0x2007FE00..0x2007FFFF  HCPU2LCPU_MB_CH1 buffer - the BT HCI H4 ring
 *   0x2007FC00..0x2007FDFF  HCPU2LCPU_MB_CH2 buffer
 *   0x2007FB00..0x2007FBFF  HCPU custom config
 *
 * i.e. HCPU_RAM_DATA ends at 0x2007FAFF.  Letting the heap run to 0x20080000
 * stays invisible until the heap actually grows that far: the LCPU then
 * writes incoming HCI bytes and its ring read pointer straight through
 * whatever malloc() handed out, and heap writes corrupt the ring in the
 * other direction.  With .bss at ~455 KB the heap is only ~32 KB, so it does
 * reach up there - that is what made getenv() take a HardFault inside nsh
 * once BNEP traffic started flowing, and why the symptom moved around
 * whenever .bss size changed.
 */

#define SRAM_RESERVED_TOP 0x2007fb00
#define SRAM_END          SRAM_RESERVED_TOP

/* PSRAM memory configuration for SF32LB52 (MPI1 SBUS) */

#define PSRAM_START 0x60000000
#define PSRAM_SIZE  0x00800000    /* 8 MB */

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_BSP_USING_PSRAM
static bool g_psram_ready;

static void sifli_psram_preinit(void)
{
  qspi_configure_t qspi_cfg =
  {
    .Instance = hwp_qspi1,
    .SpiMode  = CONFIG_BSP_QSPI1_MODE,
    .msize    = CONFIG_BSP_QSPI1_MEM_SIZE,
    .base     = QSPI1_MEM_BASE,
  };
  static FLASH_HandleTypeDef psram_handle;

  /* Enable 1.8V LDO required by PSRAM. */

  HAL_PMU_ConfigPeriLdo(PMU_PERI_LDO_1V8, true, true);

  /* Use SYSCLK for early boot safety. DLL2 path is enabled later by HAL. */

  HAL_RCC_HCPU_ClockSelect(RCC_CLK_MOD_FLASH1, RCC_CLK_FLASH_SYSCLK);

  /* Use configured PSRAM mode directly to avoid early-boot PID dependency. */

  if (qspi_cfg.SpiMode == SPI_MODE_NOR)
    {
      g_psram_ready = false;
      return;
    }

  /* Avoid early power-mode query here; HAL_Init will handle PM state later. */

  psram_handle.wakeup = 0;

  /* Keep divider aligned with existing board implementation. */

  g_psram_ready = (HAL_MPI_PSRAM_Init(&psram_handle, &qspi_cfg, 2) == HAL_OK);
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void HAL_MspInit(void)
{
  BSP_PIN_Init();
  BSP_Power_Up(true);
}

void HAL_PreInit(void)
{
  BSP_Board_PreInit();

#ifdef CONFIG_BSP_USING_PSRAM
  HAL_MspInit();
  sifli_psram_preinit();
#endif
}

/****************************************************************************
 * Name: up_allocate_heap/up_allocate_kheap
 *
 * Description:
 *   This function will be called to dynamically set aside the heap region.
 *
 *   - For the normal "flat" build, this function returns the size of the
 *     single heap.
 *   - For the protected build (CONFIG_BUILD_PROTECTED=y) with both kernel-
 *     and user-space heaps (CONFIG_MM_KERNEL_HEAP=y), this function
 *     provides the size of the unprotected, user-space heap.
 *   - For the kernel build (CONFIG_BUILD_KERNEL=y), this function provides
 *     the size of the protected, kernel-space heap.
 *
 *   If a protected kernel-space heap is provided, the kernel heap must be
 *   allocated by an analogous up_allocate_kheap(). A custom version of this
 *   file is needed if memory protection of the kernel heap is required.
 *
 *   The following memory map is assumed for the flat build:
 *
 *     .data region.  Size determined at link time.
 *     .bss  region  Size determined at link time.
 *     IDLE thread stack.  Size determined by CONFIG_IDLETHREAD_STACKSIZE.
 *     Heap.  Extends to the end of SRAM.
 *
 *   The following memory map is assumed for the kernel build:
 *
 *     Kernel .data region.  Size determined at link time.
 *     Kernel .bss  region  Size determined at link time.
 *     Kernel IDLE thread stack.  Size determined by
 *       CONFIG_IDLETHREAD_STACKSIZE.
 *     Padding for alignment
 *     User .data region.  Size determined at link time.
 *     User .bss region  Size determined at link time.
 *     Kernel heap.  Size determined by CONFIG_MM_KERNEL_HEAPSIZE.
 *     User heap.  Extends to the end of SRAM.
 *
 ****************************************************************************/

void up_allocate_heap(FAR void **heap_start, size_t *heap_size)
{
  /* The heap starts at g_idle_topstack and extends to the end of SRAM */
  
  *heap_start = (FAR void *)g_idle_topstack;
  *heap_size  = SRAM_END - g_idle_topstack;
}

/******************************************************************************
 * Name: arm_addregion
 *
 * Description:
 *   Memory may be added in non-contiguous chunks.  Additional chunks are
 *   added by calling this function.
 *
 ******************************************************************************/

#if CONFIG_MM_REGIONS > 1
void arm_addregion(void)
{
#ifdef CONFIG_BSP_USING_PSRAM
  if (g_psram_ready)
    {
      kumm_addregion((void *)PSRAM_START, PSRAM_SIZE);
    }
#endif
}
#endif
