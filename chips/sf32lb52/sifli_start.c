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

/* Early syslog implementation before full syslog is initialized */
int __arch_syslog(int priority, const char *fmt, ...)
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

  arm_lowputc('A'); /* 表示数据段初始化完成 */

#ifdef CONFIG_ARMV8M_ICACHE
  up_enable_icache();
#endif

#ifdef CONFIG_ARMV8M_DCACHE
  up_enable_dcache();
#endif
    arm_lowputc('B'); /* 表示cache启用完成 */

  /* Call HAL_Init() with interrupts disabled.
   * Some HAL functions may trigger hardware events that could
   * generate interrupts, but they won't fire while interrupts are disabled.
   */
  HAL_Init();
    arm_lowputc('C'); /* 表示HAL初始化完成 */

  /* Disable SysTick that was enabled by HAL_Init().
   * NuttX uses its own timer system (LPTIM for tickless mode).
   * SysTick must be disabled to prevent unexpected interrupts.
   */
#define NVIC_SYSTICK_CTRL_REG   (*((volatile uint32_t *)0xE000E010))
  NVIC_SYSTICK_CTRL_REG = 0;  /* Disable SysTick completely */


  arm_lowputc('D'); /* 表示准备启动系统 */

  /* nx_start() will initialize the interrupt system and enable interrupts.
   * Interrupts remain disabled until the system is fully ready.
   */
  nx_start();
  
  showprogress('X'); /* 不应该到这里 */

  for (; ; );
}
