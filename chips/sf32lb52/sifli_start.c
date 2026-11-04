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
#ifdef CONFIG_ARM_MPU
#  include "mpu.h"                /* BT HCI mailbox rings: non-cacheable MPU regions */
#endif
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

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __sifli_start
 ****************************************************************************/

static void early_puthex32(uint32_t v)
{
  static const char h[] = "0123456789ABCDEF";
  int i;
  for (i = 28; i >= 0; i -= 4)
    {
      arm_lowputc(h[(v >> i) & 0xF]);
    }
}

static void early_puts(const char *s)
{
  while (*s)
    {
      arm_lowputc(*s++);
    }
}

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

  /* v114b：复位原因可见化（AON 域常供电，复位后 WSR/CR 仍可读）。
   * WSR：bit5=IWDT, bit1/2=WDT1/2, bit3/4=PIN, bit6=PWRKEY, bit7=LOWBAT。 */
  {
    uint32_t cr = hwp_pmuc->CR;
    uint32_t wsr = hwp_pmuc->WSR;
    early_puts("\r\n[RSET] CR=");
    early_puthex32(cr);
    early_puts(" WSR=");
    early_puthex32(wsr);
    early_puts("\r\n");
  }

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

  /* BT HCI mailbox rings 走 MPU 非缓存 ------------------------------------
   *
   * 根因（docs_ble/30 §22 根因 2，真机 2026-10-25 重验）：
   * struct circular_buf 里 read_idx_mirror(HCPU 写, 偏移 8..11) 与
   * write_idx_mirror(LCPU 写, 偏移 12..15) 同一条 cache line。HCPU 发布
   * 读指针后 up_clean_dcache() 会把本端缓存的**陈旧写指针**一起写回 →
   * LCPU 的写指针回退 → 覆盖尚未 drain 的字节 → H4 失步（真机现象：
   * 大流量时 drop invalid h4 上百条 → 网络挂 → 静默复位）。
   *
   * 已有一层缓解（sf32lb52_bt_adapter.c 里发布前重新 invalidate+重读），
   * 只把窗口从 µs 缩到几条指令；这里从机制上根治：把两个 512B ring 配成
   * MPU normal non-cacheable（0 RAM），HCPU 对 ring 的访问不再进 D-cache，
   * 也就没有"写回陈旧写指针"这回事。MPU 其余地址由 PRIVDEFENA 走默认
   * 内存映射（SRAM/PSRAM 可缓存，peripheral 走 device）。
   */
#ifdef CONFIG_ARM_MPU
  mpu_control(false, false, false);        /* 配置前先保持 MPU 关闭 */
  mpu_configure_region(0x2007fe00, 512,    /* HCPU->LCPU ring (MB CH1) */
                       MPU_RBAR_AP_RWNO,
                       MPU_RLAR_NONCACHEABLE | MPU_RLAR_PXN);
  mpu_configure_region(0x20405c00, 512,    /* LCPU->HCPU ring (MB CH1) */
                       MPU_RBAR_AP_RWNO,
                       MPU_RLAR_NONCACHEABLE | MPU_RLAR_PXN);
  mpu_control(true, false, true);          /* enable + PRIVDEFENA */
  arm_lowputc('m');                        /* BT rings non-cacheable */
#endif

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
