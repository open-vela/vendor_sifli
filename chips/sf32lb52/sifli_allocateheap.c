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

/* Reserved by the board linker script for static PSRAM framebuffers. */
extern uint8_t _epsrambss __attribute__((weak));

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SRAM memory configuration for SF32LB52 */

#define SRAM_START  HCPU_RAM_DATA_START_ADDR
#define SRAM_SIZE   HCPU_RAM_DATA_SIZE
#define SRAM_END    (SRAM_START + SRAM_SIZE)

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

  /* BSP_Board_PreInit has enabled DLL2; match the validated RT PSRAM path. */

  HAL_RCC_HCPU_ClockSelect(RCC_CLK_MOD_FLASH1, RCC_CLK_FLASH_DLL2);

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
      uintptr_t heap_start = (uintptr_t)&_epsrambss;
      uintptr_t psram_end = PSRAM_START + PSRAM_SIZE;

      if (heap_start < PSRAM_START)
        {
          heap_start = PSRAM_START;
        }
      else if (heap_start >= psram_end)
        {
          return;
        }

      heap_start = (heap_start + 63) & ~((uintptr_t)63);
      if (heap_start < psram_end)
        {
          kumm_addregion((void *)heap_start, psram_end - heap_start);
        }
    }
#endif
}
#endif
