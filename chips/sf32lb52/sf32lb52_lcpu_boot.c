/****************************************************************************
 * vendor/sifli/chips/sf32lb52/sf32lb52_lcpu_boot.c
 ****************************************************************************/

#include <sfconfig.h>
#include <bf0_hal.h>
#include <bf0_hal_patch.h>
#include <nuttx/cache.h>
#include <syslog.h>
#include <string.h>

#include "mem_map.h"
#include "sf32lb52_bt_adapter.h"

#if (!defined(SF32LB52X_REV_B)) && !defined(LCPU_RUN_ROM_ONLY)
#  define g_lcpu_bin g_lcpu_bin_legacy
#  include "../../middleware/bluetooth/patch/sf32lb52/sf32lb52_lcpu.h"
#  undef g_lcpu_bin
#  define g_lcpu_patch_list g_lcpu_patch_list_legacy
#  define g_lcpu_patch_bin g_lcpu_patch_bin_legacy
#  include "../../middleware/bluetooth/patch/sf32lb52/sf32lb52_lcpu_patch_sync.h"
#  undef g_lcpu_patch_list
#  undef g_lcpu_patch_bin
#endif

#if defined(APP_BSP_TEST)
#  define bt_rf_cal()
#else
#  if defined(FPGA)
#    define bt_rf_cal() bt_rf_cal_9364()
#  endif
extern void bt_rf_cal(void);
#endif

#if defined(SOC_BF0_HCPU)
extern void lcpu_patch_install_rev_b(void);
extern uint16_t LCPU_CONFIG_get_total_size(void);

static uint8_t g_lcpu_rf_cal_disable;

static void sf32lb52_lcpu_boot_clean_range(uint32_t addr, uint32_t size)
{
  if (size == 0)
    {
      return;
    }

  up_clean_dcache((uintptr_t)addr, (uintptr_t)addr + size);
}

__WEAK void adc_resume(void)
{
}

#if (!defined(SF32LB52X_REV_B)) && !defined(LCPU_RUN_ROM_ONLY)
void lcpu_img_install(void)
{
  if (__HAL_SYSCFG_GET_REVID() < HAL_CHIP_REV_ID_A4)
    {
      memcpy((void *)HCPU_LCPU_CODE_START_ADDR,
             g_lcpu_bin_legacy,
             sizeof(g_lcpu_bin_legacy));
      sf32lb52_lcpu_boot_clean_range(HCPU_LCPU_CODE_START_ADDR,
                                     sizeof(g_lcpu_bin_legacy));
    }
}

static void lcpu_patch_install_legacy(void)
{
  memcpy((void *)LCPU_PATCH_RECORD_ADDR,
         g_lcpu_patch_list_legacy,
         sizeof(g_lcpu_patch_list_legacy));
  sf32lb52_lcpu_boot_clean_range(LCPU_PATCH_RECORD_ADDR,
                                 sizeof(g_lcpu_patch_list_legacy));
  HAL_PATCH_install();
  memset((void *)LCPU_PATCH_START_ADDR_S, 0, LCPU_PATCH_TOTAL_SIZE);
  memcpy((void *)LCPU_PATCH_START_ADDR_S,
         g_lcpu_patch_bin_legacy,
         sizeof(g_lcpu_patch_bin_legacy));
  sf32lb52_lcpu_boot_clean_range(LCPU_PATCH_START_ADDR_S,
                                 LCPU_PATCH_TOTAL_SIZE);
}
#else
#  define lcpu_img_install()
#endif

void lcpu_rom_config_default(void)
{
  uint8_t rev_id = __HAL_SYSCFG_GET_REVID();
  uint8_t is_enable_lxt;
  uint8_t is_lcpu_rccal = 0;
  uint32_t wdt_staus = 0xFF;
  uint32_t wdt_time = 10;
  uint16_t wdt_clk = 32768;

  if (HAL_LXT_DISABLED())
    {
      is_lcpu_rccal = 1;
    }

  is_enable_lxt = 1 - is_lcpu_rccal;
  HAL_LCPU_CONFIG_set(HAL_LCPU_CONFIG_XTAL_ENABLED, &is_enable_lxt, 1);
  HAL_LCPU_CONFIG_set(HAL_LCPU_CONFIG_WDT_STATUS, &wdt_staus, 4);
  HAL_LCPU_CONFIG_set(HAL_LCPU_CONFIG_WDT_TIME, &wdt_time, 4);
  HAL_LCPU_CONFIG_set(HAL_LCPU_CONFIG_WDT_CLK_FEQ, &wdt_clk, 2);
  HAL_LCPU_CONFIG_set(HAL_LCPU_CONFIG_BT_RC_CAL_IN_L, &is_lcpu_rccal, 1);

#if defined(SF32LB52X_REV_B) || defined(SF32LB52X_REV_AUTO)
  if (rev_id >= HAL_CHIP_REV_ID_A4)
    {
      uint32_t tx_queue = HCPU2LCPU_MB_CH1_BUF_START_ADDR;
      hal_lcpu_bluetooth_rom_config_t config = {0};
      hal_lcpu_ble_mem_config_t ble_config = {0};

      config.bit_valid |= 1 << 10 | 1 << 6 | 1 << 2;
      config.lld_prog_delay = 3;
      config.is_fpga = 0;
      config.default_xtal_enabled = is_enable_lxt;
      HAL_LCPU_CONFIG_set(HAL_LCPU_CONFIG_HCPU_TX_QUEUE, &tx_queue, 4);
      HAL_LCPU_CONFIG_set(HAL_LCPU_CONFIG_BT_CONFIG, &config, sizeof(config));

      ble_config.max_nb_of_hci_completed = 6;
      ble_config.bit_valid = 1 << 6;
      HAL_LCPU_CONFIG_set(HAL_LCPU_CONFIG_BT_KE_BUF,
                          &ble_config,
                          sizeof(ble_config));
    }
#else
  (void)rev_id;
#endif
}

__WEAK void lcpu_rom_config(void)
{
  lcpu_rom_config_default();
}

static void lcpu_ble_patch_install(void)
{
  uint8_t rev_id = __HAL_SYSCFG_GET_REVID();

  syslog(LOG_INFO, "lcpu patch: revid=%u -> %s patch\n",
         (unsigned)rev_id,
         rev_id < HAL_CHIP_REV_ID_A4 ? "legacy" : "rev_b");

  /* Read the LCPU firmware's assert record (patch-area tail, 32 B) BEFORE
   * reloading the blob.  A fatal (_ke/ke_/plat) assert in the previous
   * boot leaves flag/name/line here and the stub's reset path resets the
   * whole SoC before the host can log anything, so the first lines of the
   * NEXT successful boot are the only chance to read it.  Clean boots
   * print flag=0. */
  sf32lb52_lcpu_boot_dump_fault_record();

  if (rev_id < HAL_CHIP_REV_ID_A4)
    {
#if !defined(SF32LB52X_REV_B)
      lcpu_patch_install_legacy();
#else
      HAL_ASSERT(0 && "Wrongly config");
#endif
    }
  else
    {
#if !defined(SF32LB52X_REV_A)
      memset((void *)0x20400000, 0, 0x500);
      lcpu_patch_install_rev_b();
#else
      HAL_ASSERT(0 && "Wrongly config");
#endif
    }

  if (g_lcpu_rf_cal_disable == 0)
    {
      syslog(LOG_INFO, "cal: begin\n");
      bt_rf_cal();
      syslog(LOG_INFO, "cal: end mac0=%08x\n",
             *(volatile uint32_t *)0x40090000);

      /* r6 P0 / C2: the 0x5938 hook's first BLE IRQ waits (unbounded,
       * FUN_2040720C step 4) for MAC+0x00 bit31 (wake request) to
       * clear.  Releasing the LCPU right after cal left the MAC not
       * ready and every first 0x0C03 timed out (22/22 tonight with
       * cal vs 3/3 without).  Give the RF/MAC a settle window and
       * hold release until the wake-request bit reads clear. */

      HAL_Delay_us(100 * 1000);
      for (int i = 0; i < 50; i++)
        {
          if ((*(volatile uint32_t *)0x40090000 & 0x80000000) == 0)
            {
              break;
            }

          HAL_Delay_us(1000);
        }
      syslog(LOG_INFO, "cal: settled mac0=%08x\n",
             *(volatile uint32_t *)0x40090000);
    }

  syslog(LOG_INFO, "cal: lcpu release\n");
  adc_resume();
  memset((void *)0x20408000, 0, 0x5000);
  sf32lb52_lcpu_boot_clean_range(0x20408000, 0x5000);
}

void lcpu_disable_rf_cal(uint8_t is_disable)
{
  g_lcpu_rf_cal_disable = is_disable;
}

/* Dump controller-death evidence before the self-heal clears it: chip
 * revision (selects the LCPU patch path) and the LCPU assert record at
 * LCPU_ASSERT_INFO_ADDR / LCPU_CONFIG_START_ADDR (the assert window is
 * zeroed by HAL_LCPU_ASSERT_INFO_clear at every BT enable).
 */
void sf32lb52_lcpu_boot_dump_evidence(void)
{
  volatile uint32_t *assert_p = (volatile uint32_t *)0x2040FDA0;
  volatile uint32_t *cfg_p = (volatile uint32_t *)0x2040FDC0;
  uint32_t revid = __HAL_SYSCFG_GET_REVID();

  /* The LCPU wrote this window over IPC; the HCPU cache may hold stale
   * lines from a previous BT enable - invalidate before reading. */

  up_invalidate_dcache((uintptr_t)0x2040FDA0, (uintptr_t)0x2040FDE0);

  syslog(LOG_ERR,
         "lcpu evidence: revid=%u (%s patch path) assert_over=%d "
         "ai=%08x %08x %08x %08x cfg=%08x %08x %08x %08x\n",
         (unsigned)revid,
         revid >= HAL_CHIP_REV_ID_A4 ? "rev_b" : "legacy",
         (int)HAL_LCPU_ASSERT_INFO_get(),
         (unsigned)assert_p[0], (unsigned)assert_p[1],
         (unsigned)assert_p[2], (unsigned)assert_p[3],
         (unsigned)cfg_p[0], (unsigned)cfg_p[1],
         (unsigned)cfg_p[2], (unsigned)cfg_p[3]);
}

/* The controller firmware's real assert record lives in the last 32
 * bytes of the patch area (0x20407FE0): flag(u32) / name(u32, ROM-local
 * address of the assert name string) / line(u32).  The assert handler
 * always writes name/line; only asserts whose name starts with "_ke",
 * "ke_" or "plat" also set flag=1 and reset the SoC.  Read this
 * IMMEDIATELY after a controller fault, before anything power-cycles
 * the LCPU.  Callers: the BLE bridge recovery entry. */

void sf32lb52_lcpu_boot_dump_fault_record(void)
{
  volatile uint32_t *rec = (volatile uint32_t *)0x20407FE0;
  volatile uint32_t *hook1 = (volatile uint32_t *)0x2040E424;
  volatile uint32_t *hook2 = (volatile uint32_t *)0x2040E4E0;
  volatile uint32_t *hook3 = (volatile uint32_t *)0x2040E4E4;
  volatile uint32_t *cb = (volatile uint32_t *)0x2040E5C0;
  uint32_t flag;
  uint32_t name;
  uint32_t line;

  up_invalidate_dcache((uintptr_t)0x20407FE0, (uintptr_t)0x20408000);
  up_invalidate_dcache((uintptr_t)0x2040E420, (uintptr_t)0x2040E5C4);

  flag = rec[0];
  name = rec[1];
  line = rec[2];

  syslog(LOG_ERR, "lcpu assert: flag=%u name=0x%08lx line=%lu\n",
         (unsigned)flag, (unsigned long)name, (unsigned long)line);

  if (name != 0 && name < 0x60000)
    {
      /* Resolve the name string through the HCPU ROM alias
       * (LCPU_ROM_ADDR_2_HCPU_ADDR, offset 0x20800000). */

      const volatile char *s = (const volatile char *)
                               (0x20800000UL + name);
      char buf[25];
      int i;

      for (i = 0; i < 24; i++)
        {
          char c = s[i];

          if (c == '\0')
            {
              break;
            }

          buf[i] = c;
        }
      buf[i] = '\0';
      syslog(LOG_ERR, "lcpu assert name str: \"%s\"\n", buf);
    }

  syslog(LOG_ERR,
         "lcpu hooks: e424=%08lx e4e0=%08lx e4e4=%08lx e5c0=%08lx\n",
         (unsigned long)*hook1, (unsigned long)*hook2,
         (unsigned long)*hook3, (unsigned long)*cb);
}

__WEAK __NOINLINE void lcpu_nvds_config(void)
{
}

uint8_t lcpu_power_on(void)
{
  HAL_HPAON_WakeCore(CORE_ID_LCPU);
  HAL_RCC_Reset_and_Halt_LCPU(0);

  lcpu_nvds_config();
  lcpu_rom_config();
  sf32lb52_lcpu_boot_clean_range(LCPU_CONFIG_START_ADDR,
                                 LCPU_CONFIG_get_total_size());

  if (HAL_RCC_GetHCLKFreq(CORE_ID_LCPU) > 24000000)
    {
      HAL_RCC_LCPU_SetDiv(2, 1, 5);
      HAL_ASSERT(HAL_RCC_GetHCLKFreq(CORE_ID_LCPU) <= 24000000);
    }

  if (__HAL_SYSCFG_GET_REVID() < HAL_CHIP_REV_ID_A4)
    {
      lcpu_img_install();
    }

  HAL_LPAON_ConfigStartAddr((uint32_t *)HCPU_LCPU_CODE_START_ADDR);
  lcpu_ble_patch_install();

  if (sf32lb52_bt_prepare_lcpu_boot() < 0)
    {
      HAL_HPAON_CANCEL_LP_ACTIVE_REQUEST();
      return 1;
    }

  HAL_RCC_ReleaseLCPU();
  HAL_HPAON_CANCEL_LP_ACTIVE_REQUEST();
#ifdef USING_SEC_ENV
  HAL_SECU_SetAttr(SECU_MOD_HCPU, SECU_ROLE_MASTER, SECU_FLAG_NONE);
  HAL_SECU_Apply(SECU_GROUP_HPMST);
#endif
  HAL_Delay_us(5000);
  return 0;
}

uint8_t lcpu_power_off(void)
{
  HAL_RCC_Reset_and_Halt_LCPU(0);
  return 0;
}

#else
uint8_t lcpu_power_on(void)
{
  return 0;
}
#endif
