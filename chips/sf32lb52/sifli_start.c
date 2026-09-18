/****************************************************************************
 * vendor/sifli/chip/sf32lb52/sifli_start.c
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

#include <stdarg.h>
#include <stdio.h>

#include <nuttx/arch.h>
#include <nuttx/init.h>
#include <nuttx/cache.h>

#include <arch/board/board.h>

#include "arm_internal.h"
#include "system_bf0_ap.h"
#include "bf0_hal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* .data is positioned first in the primary RAM followed immediately by .bss.
 * The IDLE thread stack lies just after .bss and has size give by
 * CONFIG_IDLETHREAD_STACKSIZE;  The heap then begins just after the IDLE.
 * ARM EABI requires 64 bit stack alignment.
 */

#define HEAP_BASE      ((uintptr_t)_ebss + CONFIG_IDLETHREAD_STACKSIZE)

#define SIFLI_LPSYS_RAM_BASE  0x203fc000
#define SIFLI_LPSYS_RAM_LIMIT 0x204fffff
#define SIFLI_MPU_ATTR_RAM_IDX 0
#define SIFLI_MPU_ATTR_RAM \
  ARM_MPU_ATTR(ARM_MPU_ATTR_NON_CACHEABLE, \
               ARM_MPU_ATTR_NON_CACHEABLE)

extern uint32_t _siramfunc;
extern uint32_t _sramfunc;
extern uint32_t _eramfunc;

void arm_lowputs(const char *str)
{
  while (*str)
    {
      arm_lowputc(*str++);
    }
}

/* Early printf implementation */
int arm_lowprintf(const char *fmt, ...)
{
  va_list ap;
  char buf[256];
  int ret;

  /* Format the message */
  va_start(ap, fmt);
  ret = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  /* Output the formatted string */
  arm_lowputs(buf);

  return ret;
}

/* Early/critical-context logger used by chip debug override. */
int sifli_arch_syslog(int priority, const char *fmt, ...)
{
  va_list ap;
  char buf[256];
  int ret;

  /* Format the message */
  va_start(ap, fmt);
  ret = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  /* Output the formatted string */
  arm_lowputs(buf);

  return ret;
}

#ifdef CONFIG_DEBUG_FEATURES
#  define showprogress(c) arm_lowputc(c)
#else
#  define showprogress(c)
#endif



/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * ROM Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* g_idle_topstack: _sbss is the start of the BSS region as defined by the
 * linker script. _ebss lies at the end of the BSS region. The idle task
 * stack starts at the end of BSS and is of size CONFIG_IDLETHREAD_STACKSIZE.
 * The IDLE thread is the thread that the system boots on and, eventually,
 * becomes the IDLE, do nothing task that runs only when there is nothing
 * else to run.  The heap continues from there until the end of memory.
 * g_idle_topstack is a read-only variable the provides this computed
 * address.
 */

const uintptr_t g_idle_topstack = HEAP_BASE;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void sifli_lpsys_mpu_config(void)
{
  uint32_t region;

  ARM_MPU_Disable();

  for (region = 0; region < MPU_REGION_NUM; region++)
    {
      ARM_MPU_ClrRegion(region);
    }

  ARM_MPU_SetMemAttr(SIFLI_MPU_ATTR_RAM_IDX, SIFLI_MPU_ATTR_RAM);
  ARM_MPU_SetRegion(
    0,
    ARM_MPU_RBAR(SIFLI_LPSYS_RAM_BASE, ARM_MPU_SH_NON, 0, 1, 0),
    ARM_MPU_RLAR(SIFLI_LPSYS_RAM_LIMIT, SIFLI_MPU_ATTR_RAM_IDX));

  ARM_MPU_Enable(MPU_CTRL_HFNMIENA_Msk | MPU_CTRL_PRIVDEFENA_Msk);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __sifli_start
 ****************************************************************************/

void __start(void)
{
  uint32_t *dest;
  const uint32_t *src;

  /* Disable all interrupts at the very beginning to prevent any ISR
   * from firing during initialization. This is critical because HAL_Init()
   * and other early initialization code may trigger hardware interrupts
   * before NuttX interrupt system is ready.
   */
  __asm volatile ("cpsid i" : : : "memory");

  /* Configure Vector Table Offset Register (VTOR) for Cortex-M33.
   * The vector table is located at the start of flash (0x12010000).
   */
#define SCB_VTOR (*((volatile uint32_t *)0xE000ED08))
  SCB_VTOR = (uint32_t)_vectors;

  /* Configure FPU before any floating point operations */

  arm_fpuconfig();

  /* Clear BSS section - critical for proper variable initialization */

  for (dest = (uint32_t *)_sbss; dest < (uint32_t *)_ebss; )
    {
      *dest++ = 0;
    }

  /* Copy initialized data from flash to SRAM */

  for (src = (const uint32_t *)_eronly,
       dest = (uint32_t *)_sdata; dest < (uint32_t *)_edata; )
    {
      *dest++ = *src++;
    }

  /* Copy .ramfunc section from flash to SRAM */

  for (src = (const uint32_t *)&_siramfunc,
       dest = (uint32_t *)&_sramfunc; dest < (uint32_t *)&_eramfunc; )
    {
      *dest++ = *src++;
    }

  arm_lowputc('A'); /* data segment init done */

  /* The SiFli IPC rings are shared with LCPU and have producer and consumer
   * indices in the same cache line.  Establish their non-cacheable memory
   * attribute before either cache is enabled.
   */

  sifli_lpsys_mpu_config();

#ifdef CONFIG_ARMV8M_ICACHE
  up_enable_icache();
#endif

#ifdef CONFIG_ARMV8M_DCACHE
  up_enable_dcache();
#endif
    arm_lowputc('B'); /* cache enable done */

  /* Call HAL_Init() with interrupts disabled.
   * Some HAL functions may trigger hardware events that could
   * generate interrupts, but they won't fire while interrupts are disabled.
   */
  HAL_Init();
    arm_lowputc('C'); /* HAL init done */

  /* Disable SysTick that was enabled by HAL_Init().
   * NuttX uses its own timer system (LPTIM for tickless mode).
   * SysTick must be disabled to prevent unexpected interrupts.
   */
#define NVIC_SYSTICK_CTRL_REG   (*((volatile uint32_t *)0xE000E010))
  NVIC_SYSTICK_CTRL_REG = 0;  /* Disable SysTick completely */


  arm_lowputc('D'); /* about to start system */

  /* nx_start() will initialize the interrupt system and enable interrupts.
   * Interrupts remain disabled until the system is fully ready.
   */
  nx_start();
  
  showprogress('X'); /* should never reach here */

  for (; ; );
}
