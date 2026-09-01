/*
 * SPDX-FileCopyrightText: 2019-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>

#include <debug.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/mutex.h>

#include "register.h"
#include "mem_map.h"

#include "bf0_hal_mpi_ex.h"
#include "flash_table.h"
#include "dma_config.h"

#include "sf32lb_flash.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SF32LB_NOR_ERASE_SIZE          QSPI_NOR_SECT_SIZE
#define SF32LB_NOR_PAGE_SHIFT          (12)
#define SF32LB_NOR_PAGE_SIZE           (1U << SF32LB_NOR_PAGE_SHIFT)
#define SF32LB_NOR_TOTAL_SIZE          (16U * 1024U * 1024U)
#define SF32LB_NOR_MIN_VALID_SIZE      (2U * 1024U * 1024U)
#define SF32LB_NOR_CLK_DIV             (2)

#define SF32LB_SFDP_MAGIC              (0x50444653U)
#define SF32LB_SFDP_BFPT_ID            (0xff00U)
#define SF32LB_SFDP_MAX_HEADERS        (8U)
#define SF32LB_SFDP_MAX_BFPT_DWORDS    (20U)

#define SF32LB_SFDP_QER_NONE           (0U)
#define SF32LB_SFDP_QER_S2B1_V1        (1U)
#define SF32LB_SFDP_QER_S1B6           (2U)
#define SF32LB_SFDP_QER_S2B7           (3U)
#define SF32LB_SFDP_QER_S2B1_V4        (4U)
#define SF32LB_SFDP_QER_S2B1_V5        (5U)
#define SF32LB_SFDP_QER_S2B1_V6        (6U)

#define SF32LB_XT25F128F_MANUF_ID       (0x0bU)
#define SF32LB_XT25F128F_MEM_TYPE       (0x40U)
#define SF32LB_XT25F128F_DEV_ID         (0x18U)
#define SF32LB_XT25F128F_DTR_OPCODE     (0xedU)
#define SF32LB_XT25F128F_DTR_DUMMY      (7U)
#define SF32LB_XT25F128F_DTR_DIV        (4U)
#define SF32LB_DTR_VERIFY_WORDS         (4U)
#define SF32LB_DTR_VERIFY_WINDOWS       (4U)

/* The controller currently has no board-calibrated DTR sampling point.
 * Keep the implementation available for controlled SRAM-only experiments,
 * but never enable it in the normal XIP boot path until calibration passes
 * across voltage, temperature and multiple boards.
 */

#define SF32LB_FLASH2_DTR_EXPERIMENTAL  (0)

#define SF32LB_NOR_PARENT_FMT          "/dev/config%d"

#if defined(__GNUC__)
#  define SF32LB_FLASH_RAMFUNC __attribute__((section(".ramfunc")))
#else
#  define SF32LB_FLASH_RAMFUNC
#endif

struct sf32lb_nor_dev_s
{
  struct mtd_dev_s mtd;
  uint32_t offset;
  uint32_t limit_blocks;
  uint32_t mem_base;
  uint32_t nsectors;
  FAR FLASH_HandleTypeDef *spi_flash_handle;
};

struct sf32lb_sfdp_info_s
{
  uint32_t size;
  uint8_t major;
  uint8_t minor;
  uint8_t read_opcode;
  uint8_t mode_clocks;
  uint8_t wait_states;
  uint8_t qer;
  bool read_144;
  bool qer_valid;
  bool dtr_clock;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static mutex_t g_lock = NXMUTEX_INITIALIZER;

static QSPI_FLASH_CTX_T g_spi_nor_flash_ctx;
static DMA_HandleTypeDef g_spi_nor_flash_dma_handle;
static SPI_FLASH_FACT_CFG_T g_spi_nor_cmd_table_cache;
static bool g_flash_hw_initialized;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int SF32LB_FLASH_RAMFUNC sf32lb_flash_preinit_runtime(void);
static int SF32LB_FLASH_RAMFUNC
sf32lb_flash_probe_sfdp(FAR FLASH_HandleTypeDef *hflash,
                        FAR uint8_t jedec[3],
                        FAR struct sf32lb_sfdp_info_s *info);
static int SF32LB_FLASH_RAMFUNC
sf32lb_flash_enable_quad(FAR FLASH_HandleTypeDef *hflash, uint8_t qer,
                         FAR uint8_t *sr1_out, FAR uint8_t *sr2_out);
static int SF32LB_FLASH_RAMFUNC
sf32lb_flash_try_xt25f128f_dtr(FAR FLASH_HandleTypeDef *hflash,
                               FAR const uint8_t jedec[3],
                               FAR const struct sf32lb_sfdp_info_s *sfdp);
static int sf32lb_nor_sync_geometry(FAR struct sf32lb_nor_dev_s *priv,
                                    bool allow_offset_fallback);
static bool sf32lb_flash_verify_erased(uint32_t addr, uint32_t size);
static bool sf32lb_flash_verify_written(uint32_t addr,
                                        FAR const uint8_t *buffer,
                                        uint32_t size);
static void sf32lb_flash_restore_ahb_read(FAR FLASH_HandleTypeDef *hflash);
static bool sf32lb_flash_is_xip_buffer(FAR const uint8_t *buffer,
                                       uint32_t size);
static int SF32LB_FLASH_RAMFUNC
sf32lb_flash_issue_raw_cmd(FAR FLASH_HandleTypeDef *hflash, uint8_t cmd);
static void SF32LB_FLASH_RAMFUNC
sf32lb_flash_try_global_unlock(FAR FLASH_HandleTypeDef *hflash);
static int SF32LB_FLASH_RAMFUNC
sf32lb_flash_raw_read_bytes(FAR FLASH_HandleTypeDef *hflash,
                            uint32_t offset,
                            FAR uint8_t *out,
                            uint32_t len);
static ssize_t SF32LB_FLASH_RAMFUNC
sf32lb_flash_raw_read_range(FAR FLASH_HandleTypeDef *hflash,
                            uint32_t addr,
                            FAR uint8_t *out,
                            uint32_t len);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void SF32LB_FLASH_RAMFUNC
sf32lb_flash_cache_invalidate(uint32_t addr, uint32_t size)
{
  SCB_InvalidateDCache_by_Addr((void *)addr, size);
  __DSB();
  __ISB();
}

static uint32_t SF32LB_FLASH_RAMFUNC
sf32lb_flash_get_le32(FAR const uint8_t *buf)
{
  return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
         ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static int SF32LB_FLASH_RAMFUNC
sf32lb_flash_raw_command_read(FAR FLASH_HandleTypeDef *hflash,
                              uint8_t opcode, uint32_t addr,
                              uint8_t addr_size, uint8_t dummy,
                              FAR uint8_t *out, uint32_t len)
{
  uint32_t word;
  uint32_t i;

  if (hflash == NULL || out == NULL || len == 0 || len > 32)
    {
      return -EINVAL;
    }

  HAL_FLASH_MANUAL_CMD(hflash, 0, 1, dummy, 0, 0,
                       addr_size, addr_size == 0 ? 0 : 1, 1);
  HAL_FLASH_WRITE_DLEN(hflash, len);
  if (HAL_FLASH_SET_CMD(hflash, opcode, addr) != HAL_OK)
    {
      return -EIO;
    }

  for (i = 0; i < len; i += 4)
    {
      word = HAL_FLASH_READ32(hflash);
      out[i] = (uint8_t)word;
      if (i + 1 < len)
        {
          out[i + 1] = (uint8_t)(word >> 8);
        }

      if (i + 2 < len)
        {
          out[i + 2] = (uint8_t)(word >> 16);
        }

      if (i + 3 < len)
        {
          out[i + 3] = (uint8_t)(word >> 24);
        }
    }

  return OK;
}

static int SF32LB_FLASH_RAMFUNC
sf32lb_flash_sfdp_read(FAR FLASH_HandleTypeDef *hflash, uint32_t addr,
                       FAR uint8_t *out, uint32_t len)
{
  uint32_t chunk;
  int ret;

  while (len > 0)
    {
      chunk = len > 32 ? 32 : len;
      ret = sf32lb_flash_raw_command_read(hflash, 0x5a, addr, 2, 8,
                                          out, chunk);
      if (ret < 0)
        {
          return ret;
        }

      addr += chunk;
      out += chunk;
      len -= chunk;
    }

  return OK;
}

static int SF32LB_FLASH_RAMFUNC
sf32lb_flash_probe_sfdp(FAR FLASH_HandleTypeDef *hflash,
                        FAR uint8_t jedec[3],
                        FAR struct sf32lb_sfdp_info_s *info)
{
  uint8_t header[8 + SF32LB_SFDP_MAX_HEADERS * 8];
  uint8_t bfpt[SF32LB_SFDP_MAX_BFPT_DWORDS * 4];
  FAR const uint8_t *ph;
  uint32_t bfpt_addr = 0;
  uint32_t density;
  uint32_t dw1;
  uint32_t dw3;
  uint32_t dw15;
  uint32_t nheaders;
  uint32_t len_dw = 0;
  uint32_t i;
  uint16_t param_id;
  int ret;

  if (hflash == NULL || jedec == NULL || info == NULL)
    {
      return -EINVAL;
    }

  memset(info, 0, sizeof(*info));
  ret = sf32lb_flash_raw_command_read(hflash, 0x9f, 0, 0, 0, jedec, 3);
  if (ret < 0)
    {
      return ret;
    }

  ret = sf32lb_flash_sfdp_read(hflash, 0, header, 8);
  if (ret < 0 || sf32lb_flash_get_le32(header) != SF32LB_SFDP_MAGIC ||
      header[5] == 0 || header[7] != 0xff)
    {
      return -ENODEV;
    }

  info->minor = header[4];
  info->major = header[5];
  nheaders = (uint32_t)header[6] + 1;
  if (nheaders > SF32LB_SFDP_MAX_HEADERS)
    {
      nheaders = SF32LB_SFDP_MAX_HEADERS;
    }

  ret = sf32lb_flash_sfdp_read(hflash, 8, header + 8, nheaders * 8);
  if (ret < 0)
    {
      return ret;
    }

  for (i = 0; i < nheaders; i++)
    {
      ph = header + 8 + i * 8;
      param_id = (uint16_t)ph[0] | ((uint16_t)ph[7] << 8);
      if (param_id == SF32LB_SFDP_BFPT_ID && ph[2] == 1 && ph[3] >= 9)
        {
          len_dw = ph[3];
          bfpt_addr = (uint32_t)ph[4] | ((uint32_t)ph[5] << 8) |
                      ((uint32_t)ph[6] << 16);
          info->minor = ph[1];
          info->major = ph[2];
          break;
        }
    }

  if (len_dw == 0)
    {
      return -ENOTSUP;
    }

  if (len_dw > SF32LB_SFDP_MAX_BFPT_DWORDS)
    {
      len_dw = SF32LB_SFDP_MAX_BFPT_DWORDS;
    }

  ret = sf32lb_flash_sfdp_read(hflash, bfpt_addr, bfpt, len_dw * 4);
  if (ret < 0)
    {
      return ret;
    }

  dw1 = sf32lb_flash_get_le32(bfpt);
  info->dtr_clock = (dw1 & (1U << 19)) != 0;
  density = sf32lb_flash_get_le32(bfpt + 4);
  if ((density & 0x80000000U) != 0)
    {
      uint32_t exponent = density & 0x7fffffffU;

      if (exponent < 3 || exponent > 34)
        {
          return -EFBIG;
        }

      info->size = exponent >= 32 ? 0xffffffffU : (1U << (exponent - 3));
    }
  else
    {
      info->size = (uint32_t)(((uint64_t)density + 1U) >> 3);
    }

  if (info->size < SF32LB_NOR_MIN_VALID_SIZE)
    {
      return -EINVAL;
    }

  if ((dw1 & (1U << 21)) != 0)
    {
      dw3 = sf32lb_flash_get_le32(bfpt + 8);
      info->read_opcode = (uint8_t)(dw3 >> 8);
      info->mode_clocks = (uint8_t)((dw3 >> 5) & 7);
      info->wait_states = (uint8_t)(dw3 & 0x1f);
      info->read_144 = info->read_opcode != 0;
    }

  if (len_dw >= 15)
    {
      dw15 = sf32lb_flash_get_le32(bfpt + 14 * 4);
      info->qer = (uint8_t)((dw15 >> 20) & 7);
      info->qer_valid = true;
    }

  return OK;
}

static int SF32LB_FLASH_RAMFUNC
sf32lb_flash_read_status(FAR FLASH_HandleTypeDef *hflash, uint8_t opcode,
                         FAR uint8_t *value)
{
  return sf32lb_flash_raw_command_read(hflash, opcode, 0, 0, 0, value, 1);
}

static int SF32LB_FLASH_RAMFUNC
sf32lb_flash_write_status(FAR FLASH_HandleTypeDef *hflash, uint8_t opcode,
                          uint16_t value, uint8_t len)
{
  uint8_t sr1;
  uint32_t retry;

  if (len == 0 || len > 2)
    {
      return -EINVAL;
    }

  if (sf32lb_flash_issue_raw_cmd(hflash, 0x06) < 0)
    {
      return -EIO;
    }

  HAL_FLASH_MANUAL_CMD(hflash, 1, 1, 0, 0, 0, 0, 0, 1);
  HAL_FLASH_WRITE_WORD(hflash, value);
  HAL_FLASH_WRITE_DLEN(hflash, len);
  if (HAL_FLASH_SET_CMD(hflash, opcode, 0) != HAL_OK)
    {
      return -EIO;
    }

  for (retry = 0; retry < 100000; retry++)
    {
      if (sf32lb_flash_read_status(hflash, 0x05, &sr1) < 0)
        {
          return -EIO;
        }

      if ((sr1 & 1) == 0)
        {
          return OK;
        }
    }

  return -ETIMEDOUT;
}

static int SF32LB_FLASH_RAMFUNC
sf32lb_flash_enable_quad(FAR FLASH_HandleTypeDef *hflash, uint8_t qer,
                         FAR uint8_t *sr1_out, FAR uint8_t *sr2_out)
{
  uint8_t sr1;
  uint8_t sr2 = 0;
  uint8_t sr2_read = 0x35;
  uint8_t mask;
  int ret;

  ret = sf32lb_flash_read_status(hflash, 0x05, &sr1);
  if (ret < 0)
    {
      return ret;
    }

  switch (qer)
    {
      case SF32LB_SFDP_QER_NONE:
        break;

      case SF32LB_SFDP_QER_S1B6:
        if ((sr1 & 0x40) == 0)
          {
            ret = sf32lb_flash_write_status(hflash, 0x01, sr1 | 0x40, 1);
          }
        break;

      case SF32LB_SFDP_QER_S2B7:
        sr2_read = 0x3f;
        ret = sf32lb_flash_read_status(hflash, sr2_read, &sr2);
        if (ret == OK && (sr2 & 0x80) == 0)
          {
            ret = sf32lb_flash_write_status(hflash, 0x3e, sr2 | 0x80, 1);
          }
        break;

      case SF32LB_SFDP_QER_S2B1_V1:
      case SF32LB_SFDP_QER_S2B1_V4:
      case SF32LB_SFDP_QER_S2B1_V5:
      case SF32LB_SFDP_QER_S2B1_V6:
        ret = sf32lb_flash_read_status(hflash, sr2_read, &sr2);
        if (ret == OK && (sr2 & 0x02) == 0)
          {
            if (qer == SF32LB_SFDP_QER_S2B1_V6)
              {
                ret = sf32lb_flash_write_status(hflash, 0x31,
                                                sr2 | 0x02, 1);
              }
            else
              {
                ret = sf32lb_flash_write_status(hflash, 0x01,
                    ((uint16_t)(sr2 | 0x02) << 8) | sr1, 2);
              }
          }
        break;

      default:
        return -ENOTSUP;
    }

  if (ret < 0)
    {
      return ret;
    }

  mask = qer == SF32LB_SFDP_QER_S1B6 ? 0x40 :
         qer == SF32LB_SFDP_QER_S2B7 ? 0x80 : 0x02;
  if (qer == SF32LB_SFDP_QER_S1B6)
    {
      ret = sf32lb_flash_read_status(hflash, 0x05, &sr1);
      if (ret == OK && (sr1 & mask) == 0)
        {
          ret = -EIO;
        }
    }
  else if (qer != SF32LB_SFDP_QER_NONE)
    {
      ret = sf32lb_flash_read_status(hflash, sr2_read, &sr2);
      if (ret == OK && (sr2 & mask) == 0)
        {
          ret = -EIO;
        }
    }

  if (sr1_out != NULL)
    {
      *sr1_out = sr1;
    }

  if (sr2_out != NULL)
    {
      *sr2_out = sr2;
    }

  return ret;
}

static int SF32LB_FLASH_RAMFUNC
sf32lb_flash_try_xt25f128f_dtr(FAR FLASH_HandleTypeDef *hflash,
                               FAR const uint8_t jedec[3],
                               FAR const struct sf32lb_sfdp_info_s *sfdp)
{
  uint32_t offsets[SF32LB_DTR_VERIFY_WINDOWS] =
  {
    0x000000U, 0x010000U, 0x100000U, 0x7f0000U
  };
  uint32_t reference[SF32LB_DTR_VERIFY_WINDOWS][SF32LB_DTR_VERIFY_WORDS];
  FAR volatile const uint32_t *src;
  FAR FLASH_CMD_CFG_T *dtr;
  uint32_t sdr_hcmdr;
  uint32_t sdr_hrccr;
  uint32_t sdr_miscr;
  uint32_t value;
  uint32_t i;
  uint32_t j;

  if (hflash == NULL || jedec == NULL || sfdp == NULL || !sfdp->dtr_clock ||
      jedec[0] != SF32LB_XT25F128F_MANUF_ID ||
      jedec[1] != SF32LB_XT25F128F_MEM_TYPE ||
      jedec[2] != SF32LB_XT25F128F_DEV_ID)
    {
      return -ENOTSUP;
    }

  /* JESD216 DW1 advertises DTR clocks but does not provide a portable
   * 4D-4D command description.  Restrict this path to the data-sheet
   * verified XT25F128F setup: EDh, DC1=0 and eight physical dummy clocks.
   * The MPI/SDK command table represents that latency with DCYC=7.
   */

  for (i = 0; i < SF32LB_DTR_VERIFY_WINDOWS; i++)
    {
      src = (FAR volatile const uint32_t *)(hflash->base + offsets[i]);
      for (j = 0; j < SF32LB_DTR_VERIFY_WORDS; j++)
        {
          reference[i][j] = src[j];
        }
    }

  sdr_hcmdr = hflash->Instance->HCMDR;
  sdr_hrccr = hflash->Instance->HRCCR;
  sdr_miscr = hflash->Instance->MISCR;

  dtr = &g_spi_nor_cmd_table_cache.cmd_cfg[SPI_FLASH_CMD_DTR4R];
  dtr->cmd = SF32LB_XT25F128F_DTR_OPCODE;
  dtr->func_mode = 0;
  dtr->data_mode = 7;
  dtr->dummy_cycle = SF32LB_XT25F128F_DTR_DUMMY;
  dtr->ab_size = 0;
  dtr->ab_mode = 7;
  dtr->addr_size = 2;
  dtr->addr_mode = 7;
  dtr->ins_mode = 1;

  /* The caller has disabled interrupts and this function is in SRAM.  Drop
   * to DLL2/4 before changing the active XIP read protocol: normally 72 MHz,
   * or 60 MHz when USB requires a 240 MHz DLL2 source.
   */

  hflash->Instance->PSCLR = SF32LB_XT25F128F_DTR_DIV;
  __DSB();

  HAL_FLASH_CFG_AHB_RCMD(hflash, dtr->data_mode, dtr->dummy_cycle,
                         dtr->ab_size, dtr->ab_mode, dtr->addr_size,
                         dtr->addr_mode, dtr->ins_mode);
  HAL_FLASH_SET_AHB_RCMD(hflash, dtr->cmd);

  value = hflash->Instance->MISCR;
  value |= MPI_MISCR_DTRPRE;
  value &= ~(MPI_MISCR_RXCLKDLY | MPI_MISCR_SCKDLY |
             MPI_MISCR_RXCLKINV | MPI_MISCR_SCKINV);
  value |= 0x0aU << MPI_MISCR_RXCLKDLY_Pos;
  hflash->Instance->MISCR = value;
  __DSB();
  __ISB();

  /* SCB cache maintenance APIs are CMSIS inline functions, not preprocessor
   * macros.  Do not guard them with #ifdef: doing so silently skipped cache
   * invalidation and made DTR verification compare stale SDR cache lines.
   * Drop all cached XIP instructions before any return to flash code.
   */

  SCB_InvalidateICache();
  __DSB();
  __ISB();

  for (i = 0; i < SF32LB_DTR_VERIFY_WINDOWS; i++)
    {
      sf32lb_flash_cache_invalidate(hflash->base + offsets[i],
                                    sizeof(reference[i]));
      src = (FAR volatile const uint32_t *)(hflash->base + offsets[i]);
      for (j = 0; j < SF32LB_DTR_VERIFY_WORDS; j++)
        {
          if (src[j] != reference[i][j])
            {
              /* Restore the complete known-good SDR register image while
               * retaining divider four.  Confirm SDR before returning to
               * any XIP-resident caller.
               */

              hflash->Instance->MISCR = sdr_miscr;
              hflash->Instance->HRCCR = sdr_hrccr;
              hflash->Instance->HCMDR = sdr_hcmdr;
              __DSB();
              __ISB();

              SCB_InvalidateICache();
              __DSB();
              __ISB();

              sf32lb_flash_cache_invalidate(hflash->base + offsets[i],
                                            sizeof(reference[i]));
              src = (FAR volatile const uint32_t *)
                    (hflash->base + offsets[i]);
              for (j = 0; j < SF32LB_DTR_VERIFY_WORDS; j++)
                {
                  if (src[j] != reference[i][j])
                    {
                      for (; ; )
                        {
                        }
                    }
                }

              hflash->buf_mode = 0;
              return -EIO;
            }
        }
    }

  hflash->buf_mode = 1;
  return OK;
}

static int sf32lb_flash_hw_init(void)
{
  HAL_StatusTypeDef status;
  qspi_configure_t flash_cfg;
  uintptr_t pc;
  int ret;
#ifdef CONFIG_BSP_QSPI2_USING_DMA
  struct dma_config flash_dma;
#endif

  memset(&g_spi_nor_flash_ctx, 0, sizeof(g_spi_nor_flash_ctx));
  g_spi_nor_flash_ctx.handle.Instance = FLASH2;
  g_spi_nor_flash_ctx.handle.base = FLASH2_BASE_ADDR;
  g_spi_nor_flash_ctx.handle.size = SF32LB_NOR_TOTAL_SIZE;
  g_spi_nor_flash_ctx.handle.freq = 24000000;
  g_spi_nor_flash_ctx.handle.buf_mode = 0;

  /* Running from FLASH2 XIP: avoid reinitializing active flash controller
   * during bringup, which can stall boot before shell is up.
   */

  pc = (uintptr_t)&sf32lb_flash_hw_init;
  if (pc >= FLASH2_BASE_ADDR && pc < (FLASH2_BASE_ADDR + SF32LB_NOR_TOTAL_SIZE))
    {
      /* HAL_FLASH_Init cannot safely run while executing from FLASH2.  The
       * runtime preinit and MPI/NOR command functions are linked into SRAM,
       * so use that path to identify the device, set QE, and atomically
       * change the AHB XIP command to SDK's 0xeb 1-4-4 read configuration.
       */

      ret = sf32lb_flash_preinit_runtime();
      if (ret < 0)
        {
          syslog(LOG_ERR,
                 "ERROR: FLASH2 XIP Quad initialization failed: %d\n", ret);
          return ret;
        }

      syslog(LOG_INFO,
             "FLASH2 XIP initialized from JEDEC/SFDP parameters\n");
      return OK;
    }

  memset(&flash_cfg, 0, sizeof(flash_cfg));
  flash_cfg.base = FLASH2_BASE_ADDR;
  flash_cfg.Instance = FLASH2;
  flash_cfg.line = 2;
  flash_cfg.msize = SF32LB_NOR_TOTAL_SIZE / (1024U * 1024U);
  flash_cfg.SpiMode = SPI_MODE_NOR;

#ifdef CONFIG_BSP_QSPI2_USING_DMA
  memset(&flash_dma, 0, sizeof(flash_dma));
  flash_dma.dma_irq_prio = FLASH2_DMA_IRQ_PRIO;
  flash_dma.dma_irq = FLASH2_DMA_IRQ;
  flash_dma.Instance = FLASH2_DMA_INSTANCE;
  flash_dma.request = FLASH2_DMA_REQUEST;

  status = HAL_FLASH_Init(&g_spi_nor_flash_ctx, &flash_cfg,
                          &g_spi_nor_flash_dma_handle,
                          &flash_dma, SF32LB_NOR_CLK_DIV);
#else
  status = HAL_FLASH_Init(&g_spi_nor_flash_ctx, &flash_cfg,
                          NULL, NULL, SF32LB_NOR_CLK_DIV);
#endif

  if (status == HAL_OK)
    {
      g_flash_hw_initialized = true;
      return OK;
    }

  g_flash_hw_initialized = false;
  return -EIO;
}

static int SF32LB_FLASH_RAMFUNC sf32lb_flash_preinit_runtime(void)
{
  FAR FLASH_HandleTypeDef *hflash;
  FAR const SPI_FLASH_FACT_CFG_T *template;
  struct sf32lb_sfdp_info_s sfdp;
  HAL_StatusTypeDef status;
  irqstate_t flags;
  uint32_t detected_size;
  uint8_t jedec[3];
  uint8_t sr1;
  uint8_t sr2;
  int dtr_ret;
  int sfdp_ret;

  if (g_flash_hw_initialized)
    {
      return OK;
    }

  hflash = &g_spi_nor_flash_ctx.handle;

  memset(&g_spi_nor_flash_ctx, 0, sizeof(g_spi_nor_flash_ctx));
  memset(&g_spi_nor_flash_dma_handle, 0, sizeof(g_spi_nor_flash_dma_handle));

  hflash->Instance = FLASH2;
  hflash->base = FLASH2_BASE_ADDR;
  hflash->size = SF32LB_NOR_TOTAL_SIZE;
  hflash->freq = 24000000;
  hflash->Mode = HAL_FLASH_NOR_MODE;
  hflash->isNand = 0;
  hflash->dma = NULL;

  sf32lb_flash_lock();
  flags = up_irq_save();
  sfdp_ret = sf32lb_flash_probe_sfdp(hflash, jedec, &sfdp);
  status = sfdp_ret < 0 ? HAL_FLASH_PreInit(hflash) : HAL_OK;
  up_irq_restore(flags);
  sf32lb_flash_unlock();

  if (sfdp_ret < 0)
    {
      syslog(LOG_ERR, "ERROR: FLASH2 SFDP probe failed: %d\n", sfdp_ret);
      if (status != HAL_OK)
        {
          return -ENODEV;
        }
    }

  if (sfdp_ret < 0 && hflash->ctable != NULL)
    {
      template = hflash->ctable;
    }
  else
    {
      /* Do not call HAL_FLASH_PreInit() after a successful SFDP probe: it
       * clears protection bits using a vendor command table before QER is
       * known.  Use the SDK table only as a command template and obtain
       * device-specific read timing and capacity from SFDP below.  The SDK
       * API returns its conservative type-0 table for unknown JEDEC IDs.
       */

      template = spi_flash_get_cmd_by_id(jedec[0], jedec[2], jedec[1]);
      if (template == NULL)
        {
          return -ENODEV;
        }
    }

  /* HAL stores command table in const rodata, which can live in XIP flash.
   * During erase/program busy states, repeatedly dereferencing an XIP table
   * can break status polling.  Keep a SRAM copy for runtime commands.
   */

  memcpy(&g_spi_nor_cmd_table_cache, template,
         sizeof(g_spi_nor_cmd_table_cache));
  hflash->ctable = &g_spi_nor_cmd_table_cache;

  if (sfdp_ret == OK)
    {
      hflash->size = sfdp.size;
      g_spi_nor_cmd_table_cache.manuf_id = jedec[0];
      g_spi_nor_cmd_table_cache.mem_type = jedec[1];
      g_spi_nor_cmd_table_cache.dev_id = jedec[2];

      if (sfdp.read_144)
        {
          FAR FLASH_CMD_CFG_T *quad;

          quad = &g_spi_nor_cmd_table_cache.cmd_cfg[SPI_FLASH_CMD_4READ];
          quad->cmd = sfdp.read_opcode;
          quad->data_mode = 3;
          quad->dummy_cycle = sfdp.wait_states;
          quad->ab_size = sfdp.mode_clocks == 0 ? 0 :
                          (sfdp.mode_clocks / 2) - 1;
          quad->ab_mode = sfdp.mode_clocks == 0 ? 0 : 3;
          quad->addr_size = 2;
          quad->addr_mode = 3;
          quad->ins_mode = 1;
        }
    }

  if (sfdp_ret < 0 || !sfdp.read_144 || !sfdp.qer_valid ||
      (sfdp.mode_clocks != 0 && (sfdp.mode_clocks & 1) != 0))
    {
      syslog(LOG_WARNING,
             "WARN: FLASH2 has no safely usable SFDP 1-4-4 mode; "
             "keeping 0x0b\n");
      hflash->Mode = HAL_FLASH_NOR_MODE;
      HAL_FLASH_CONFIG_AHB_READ(hflash, false);
      g_flash_hw_initialized = true;
      return OK;
    }

  if (hflash->size > NOR_FLASH_MAX_3B_SIZE)
    {
      syslog(LOG_ERR,
             "ERROR: SFDP Flash size %lu needs unsupported 4-byte setup\n",
             (unsigned long)hflash->size);
      hflash->Mode = HAL_FLASH_NOR_MODE;
      HAL_FLASH_CONFIG_AHB_READ(hflash, false);
      g_flash_hw_initialized = true;
      return OK;
    }

  /* Apply the SFDP QER procedure and switch AHB XIP from 0x0b 1-1-1 to
   * the discovered 1-4-4 command.  Do not issue 66h/99h here: reset
   * support is described separately in BFPT DW16 and is not universal.
   * No interrupt may fetch from FLASH2 while the protocol is changing.
   * Keep this complete critical sequence in SRAM; use a local delay loop
   * instead of the XIP-resident HAL_Delay_us().
   */

  sf32lb_flash_lock();
  flags = up_irq_save();
  status = sf32lb_flash_enable_quad(hflash, sfdp.qer, &sr1, &sr2);
  if (status != OK)
    {
      hflash->Mode = HAL_FLASH_NOR_MODE;
      HAL_FLASH_CONFIG_AHB_READ(hflash, false);
      up_irq_restore(flags);
      sf32lb_flash_unlock();
      syslog(LOG_ERR,
             "ERROR: SFDP QER %u failed (%d), SR1=%02x SR2=%02x; "
             "restored 0x0b\n", sfdp.qer, status, sr1, sr2);
      return -EIO;
    }

  hflash->Mode = HAL_FLASH_QMODE;
  HAL_FLASH_CONFIG_AHB_READ(hflash, true);
  HAL_FLASH_ENABLE_QSPI(hflash, 1);

  if (SF32LB_FLASH2_DTR_EXPERIMENTAL != 0)
    {
      dtr_ret = sf32lb_flash_try_xt25f128f_dtr(hflash, jedec, &sfdp);
    }
  else
    {
      dtr_ret = -ENOTSUP;
    }

  up_irq_restore(flags);
  sf32lb_flash_unlock();

  syslog(LOG_INFO,
      "FLASH2 SFDP %u.%u: JEDEC=%02x %02x %02x size=%lu "
      "1-4-4 cmd=%02x mode=%u wait=%u QER=%u SR1=%02x SR2=%02x "
      "HCMDR=%08lx HRCCR=%08lx\n",
      sfdp.major, sfdp.minor,
      hflash->ctable->manuf_id, hflash->ctable->mem_type,
      hflash->ctable->dev_id, (unsigned long)hflash->size,
      sfdp.read_opcode, sfdp.mode_clocks, sfdp.wait_states, sfdp.qer,
      sr1, sr2,
      (unsigned long)hflash->Instance->HCMDR,
      (unsigned long)hflash->Instance->HRCCR);

  if (dtr_ret == OK)
    {
      syslog(LOG_INFO,
             "FLASH2 DTR enabled: cmd=ed physical-dummy=8 div=%lu "
             "HCMDR=%08lx HRCCR=%08lx MISCR=%08lx\n",
             (unsigned long)(hflash->Instance->PSCLR & MPI_PSCLR_DIV_Msk),
             (unsigned long)hflash->Instance->HCMDR,
             (unsigned long)hflash->Instance->HRCCR,
             (unsigned long)hflash->Instance->MISCR);
    }
  else if (dtr_ret == -EIO)
    {
      syslog(LOG_WARNING,
             "WARN: FLASH2 DTR verification failed; restored 0xeb SDR "
             "at div=%lu\n",
             (unsigned long)(hflash->Instance->PSCLR & MPI_PSCLR_DIV_Msk));
    }
  else
    {
      syslog(LOG_INFO,
             "FLASH2 DTR skipped: no verified JEDEC/SFDP configuration; "
             "keeping SDR at div=%lu\n",
             (unsigned long)(hflash->Instance->PSCLR & MPI_PSCLR_DIV_Msk));
    }

  /* HAL_FLASH_PreInit sets hflash->dma = NULL but HAL_QSPIEX_WRITE_PAGE
   * requires DMA for reliable page-program operations.  Configure DMA
   * manually, mirroring what HAL_FLASH_Init would do.
   */

#ifdef CONFIG_BSP_QSPI2_USING_DMA
  hflash->dma = &g_spi_nor_flash_dma_handle;
  hflash->dma->Instance                 = FLASH2_DMA_INSTANCE;
  hflash->dma->Init.Request             = FLASH2_DMA_REQUEST;
  hflash->dma->Init.Direction           = DMA_MEMORY_TO_PERIPH;
  hflash->dma->Init.PeriphInc           = DMA_PINC_DISABLE;
  hflash->dma->Init.MemInc              = DMA_MINC_ENABLE;
  hflash->dma->Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hflash->dma->Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
  hflash->dma->Init.Mode                = DMA_NORMAL;
  hflash->dma->Init.Priority            = DMA_PRIORITY_MEDIUM;
  hflash->dma->Init.BurstSize           = 1;
  HAL_FLASH_SET_TXSLOT(hflash, hflash->dma->Init.BurstSize);

#else
  syslog(LOG_WARNING,
         "WARN: CONFIG_BSP_QSPI2_USING_DMA not enabled, NOR writes may fail\n");
#endif

  /* Ensure NOR is writable -- clear status register protection bits. */

  HAL_FLASH_CLR_PROTECT(hflash);
  sf32lb_flash_try_global_unlock(hflash);

  detected_size = hflash->size;
  if (detected_size < SF32LB_NOR_MIN_VALID_SIZE)
    {
      syslog(LOG_WARNING,
             "WARN: NOR detected size too small (%lu), fallback to configured %lu\n",
             (unsigned long)detected_size,
             (unsigned long)SF32LB_NOR_TOTAL_SIZE);
      hflash->size = SF32LB_NOR_TOTAL_SIZE;
    }

  g_flash_hw_initialized = true;

  return OK;
}

static int sf32lb_nor_sync_geometry(FAR struct sf32lb_nor_dev_s *priv,
                                    bool allow_offset_fallback)
{
  uint32_t flash_size;
  uint32_t offset_bytes;
  uint32_t fallback_bytes;
  uint32_t max_blocks;
  uint32_t region_size;

  if (priv == NULL || priv->spi_flash_handle == NULL)
    {
      return -EINVAL;
    }

  flash_size = priv->spi_flash_handle->size;
  if (flash_size < SF32LB_NOR_PAGE_SIZE)
    {
      syslog(LOG_ERR, "ERROR: NOR flash size invalid: %lu\n",
             (unsigned long)flash_size);
      return -ENOSPC;
    }

  offset_bytes = priv->offset * SF32LB_NOR_PAGE_SIZE;
  if (offset_bytes >= flash_size)
    {
      if (!allow_offset_fallback)
        {
          syslog(LOG_ERR,
                 "ERROR: NOR partition offset out of range: off=%lu size=%lu\n",
                 (unsigned long)offset_bytes,
                 (unsigned long)flash_size);
          return -ENOSPC;
        }

      /* If configured offset doesn't fit detected flash size, fallback to
       * half-flash split to keep app image and fs separated in most layouts.
       */

      fallback_bytes = (flash_size / 2U) & ~(SF32LB_NOR_PAGE_SIZE - 1U);
      if (fallback_bytes >= flash_size)
        {
          fallback_bytes = 0;
        }

      syslog(LOG_WARNING,
             "WARN: NOR offset %lu invalid for flash size %lu, fallback to %lu\n",
             (unsigned long)offset_bytes,
             (unsigned long)flash_size,
             (unsigned long)fallback_bytes);

      priv->offset = fallback_bytes / SF32LB_NOR_PAGE_SIZE;
      offset_bytes = fallback_bytes;
    }

  region_size = (flash_size - offset_bytes) & ~(SF32LB_NOR_PAGE_SIZE - 1U);
  priv->nsectors = region_size / SF32LB_NOR_PAGE_SIZE;

  max_blocks = priv->limit_blocks;
  if (max_blocks > 0 && priv->nsectors > max_blocks)
    {
      priv->nsectors = max_blocks;
    }

  priv->mem_base = priv->spi_flash_handle->base + offset_bytes;

  if (priv->nsectors == 0)
    {
      syslog(LOG_ERR,
             "ERROR: NOR partition has no usable sectors: off=%lu size=%lu\n",
             (unsigned long)offset_bytes,
             (unsigned long)flash_size);
      return -ENOSPC;
    }

  return OK;
}

static bool sf32lb_flash_verify_erased(uint32_t addr, uint32_t size)
{
  uint8_t checkbuf[32];
  uint32_t check_len;
  uint32_t flash_off;
  FAR FLASH_HandleTypeDef *hflash;
  uint32_t i;

  if (size == 0)
    {
      return true;
    }

  hflash = &g_spi_nor_flash_ctx.handle;
  if (hflash == NULL || addr < hflash->base)
    {
      return false;
    }

  flash_off = addr - hflash->base;
  check_len = size > sizeof(checkbuf) ? sizeof(checkbuf) : size;

  if (sf32lb_flash_raw_read_bytes(hflash, flash_off, checkbuf, check_len) < 0)
    {
      return false;
    }

  for (i = 0; i < check_len; i++)
    {
      if (checkbuf[i] != 0xFF)
        {
          return false;
        }
    }

  if (size > check_len)
    {
      flash_off = (addr + size - check_len) - hflash->base;
      if (sf32lb_flash_raw_read_bytes(hflash, flash_off, checkbuf,
                                      check_len) < 0)
        {
          return false;
        }

      for (i = 0; i < check_len; i++)
        {
          if (checkbuf[i] != 0xFF)
            {
              return false;
            }
        }
    }

  return true;
}

static bool sf32lb_flash_verify_written(uint32_t addr,
                                        FAR const uint8_t *buffer,
                                        uint32_t size)
{
  uint8_t checkbuf[32];
  FAR FLASH_HandleTypeDef *hflash;
  uint32_t flash_off;
  uint32_t check_len;

  if (buffer == NULL || size == 0)
    {
      return false;
    }

  hflash = &g_spi_nor_flash_ctx.handle;
  if (hflash == NULL || addr < hflash->base)
    {
      return false;
    }

  check_len = size > 32U ? 32U : size;
  flash_off = addr - hflash->base;

  if (sf32lb_flash_raw_read_bytes(hflash, flash_off, checkbuf, check_len) < 0)
    {
      return false;
    }

  if (memcmp(checkbuf, buffer, check_len) != 0)
    {
      syslog(LOG_ERR,
             "ERROR: verify mismatch at 0x%08lx: "
             "flash=%02x%02x%02x%02x%02x%02x%02x%02x "
             "expect=%02x%02x%02x%02x%02x%02x%02x%02x\n",
             (unsigned long)addr,
                  checkbuf[0], checkbuf[1], checkbuf[2], checkbuf[3],
                  checkbuf[4], checkbuf[5], checkbuf[6], checkbuf[7],
             buffer[0], buffer[1], buffer[2], buffer[3],
             buffer[4], buffer[5], buffer[6], buffer[7]);
      return false;
    }

  return true;
}

static void sf32lb_flash_restore_ahb_read(FAR FLASH_HandleTypeDef *hflash)
{
  bool qmode;

  if (hflash == NULL)
    {
      return;
    }

  qmode = hflash->Mode != 0;
  if (hflash->size > NOR_FLASH_MAX_3B_SIZE)
    {
      HAL_FLASH_CONFIG_FULL_AHB_READ(hflash, qmode);
    }
  else
    {
      HAL_FLASH_CONFIG_AHB_READ(hflash, qmode);
    }

  HAL_FLASH_ENABLE_QSPI(hflash, 1);
}

static int SF32LB_FLASH_RAMFUNC
sf32lb_flash_issue_raw_cmd(FAR FLASH_HandleTypeDef *hflash, uint8_t cmd)
{
  HAL_StatusTypeDef status;

  if (hflash == NULL)
    {
      return -EINVAL;
    }

  /* Raw single-line command with no address/data phase. */

  HAL_FLASH_MANUAL_CMD(hflash, 0, 0, 0, 0, 0, 0, 0, 1);
  status = HAL_FLASH_SET_CMD(hflash, cmd, 0);
  return status == HAL_OK ? OK : -EIO;
}

static void SF32LB_FLASH_RAMFUNC
sf32lb_flash_try_global_unlock(FAR FLASH_HandleTypeDef *hflash)
{
  irqstate_t flags;

  if (hflash == NULL)
    {
      return;
    }

  sf32lb_flash_lock();
  flags = up_irq_save();

  HAL_FLASH_ISSUE_CMD(hflash, SPI_FLASH_CMD_WREN, 0);
  (void)sf32lb_flash_issue_raw_cmd(hflash, 0x98); /* ULBPR/global unlock */

  up_irq_restore(flags);
  sf32lb_flash_unlock();
}

static int SF32LB_FLASH_RAMFUNC
sf32lb_flash_raw_read_bytes(FAR FLASH_HandleTypeDef *hflash,
                            uint32_t offset,
                            FAR uint8_t *out,
                            uint32_t len)
{
  irqstate_t flags;
  uint32_t word;
  uint32_t i;

  if (hflash == NULL || out == NULL || len == 0 || len > 32)
    {
      return -EINVAL;
    }

  sf32lb_flash_lock();
  flags = up_irq_save();

  HAL_FLASH_MANUAL_CMD(hflash, 0, 1, 0, 0, 0, 2, 1, 1);
  HAL_FLASH_WRITE_DLEN(hflash, len);
  if (HAL_FLASH_SET_CMD(hflash, 0x03, offset) != HAL_OK)
    {
      up_irq_restore(flags);
      sf32lb_flash_unlock();
      return -EIO;
    }

  for (i = 0; i < len; i += 4)
    {
      word = HAL_FLASH_READ32(hflash);
      out[i + 0] = (uint8_t)(word & 0xFF);
      if ((i + 1) < len)
        {
          out[i + 1] = (uint8_t)((word >> 8) & 0xFF);
        }

      if ((i + 2) < len)
        {
          out[i + 2] = (uint8_t)((word >> 16) & 0xFF);
        }

      if ((i + 3) < len)
        {
          out[i + 3] = (uint8_t)((word >> 24) & 0xFF);
        }
    }

  up_irq_restore(flags);
  sf32lb_flash_unlock();
  return OK;
}

static ssize_t SF32LB_FLASH_RAMFUNC
sf32lb_flash_raw_read_range(FAR FLASH_HandleTypeDef *hflash,
                            uint32_t addr,
                            FAR uint8_t *out,
                            uint32_t len)
{
  uint32_t offset;
  uint32_t done;
  uint32_t chunk;
  int ret;

  if (hflash == NULL || out == NULL)
    {
      return -EINVAL;
    }

  if (len == 0)
    {
      return 0;
    }

  offset = addr >= hflash->base ? (addr - hflash->base) : addr;
  done = 0;
  while (done < len)
    {
      chunk = len - done;
      if (chunk > 32)
        {
          chunk = 32;
        }

      ret = sf32lb_flash_raw_read_bytes(hflash, offset + done,
                                        out + done, chunk);
      if (ret < 0)
        {
          return ret;
        }

      done += chunk;
    }

  return (ssize_t)done;
}

static bool sf32lb_flash_is_xip_buffer(FAR const uint8_t *buffer,
                                       uint32_t size)
{
  uintptr_t start;
  uintptr_t end;
  uintptr_t xip_start;
  uintptr_t xip_end;

  if (buffer == NULL || size == 0)
    {
      return false;
    }

  start = (uintptr_t)buffer;
  end = start + (uintptr_t)size;
  xip_start = (uintptr_t)FLASH2_BASE_ADDR;
  xip_end = xip_start + (uintptr_t)SF32LB_NOR_TOTAL_SIZE;

  if (end < start)
    {
      return false;
    }

  return (start < xip_end) && (end > xip_start);
}

static int sf32lb_nor_prepare_io(FAR struct sf32lb_nor_dev_s *priv,
                                 uint32_t offset, uint32_t nbytes)
{
  uint64_t part_size;
  int ret;

  if (priv == NULL || priv->spi_flash_handle == NULL)
    {
      return -EINVAL;
    }

  if (!g_flash_hw_initialized)
    {
      ret = sf32lb_flash_preinit_runtime();
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = sf32lb_nor_sync_geometry(priv, true);
  if (ret < 0)
    {
      return ret;
    }

  part_size = (uint64_t)priv->nsectors * SF32LB_NOR_PAGE_SIZE;
  if (((uint64_t)offset + (uint64_t)nbytes) > part_size)
    {
      syslog(LOG_ERR,
             "ERROR: NOR io out of range: off=%lu nbytes=%lu part=%lu\n",
             (unsigned long)offset,
             (unsigned long)nbytes,
             (unsigned long)part_size);
      return -ENOSPC;
    }

  return OK;
}

static int SF32LB_FLASH_RAMFUNC sf32lb_nor_erase_range(FAR FLASH_HandleTypeDef *hflash,
                                                       uint32_t addr, uint32_t size)
{
  uint32_t erase_addr;
  uint32_t erase_size;
  uint32_t chunk_addr;
  uint32_t chunk_size;
  uint32_t phys_addr;
  int ret = OK;
  irqstate_t flags;

  if (hflash == NULL)
    {
      return -EINVAL;
    }

  if (!g_flash_hw_initialized)
    {
      ret = sf32lb_flash_preinit_runtime();
      if (ret < 0)
        {
          return ret;
        }
    }

  if (size == 0)
    {
      return OK;
    }

  if (size >= hflash->size)
    {
      sf32lb_flash_lock();
      flags = up_irq_save();
      ret = HAL_QSPIEX_CHIP_ERASE(hflash);
      up_irq_restore(flags);
      sf32lb_flash_unlock();
      return ret == 0 ? OK : -EIO;
    }

  if (addr >= hflash->base)
    {
      addr -= hflash->base;
    }

  if (!IS_ALIGNED(QSPI_NOR_SECT_SIZE, addr) ||
      !IS_ALIGNED(QSPI_NOR_SECT_SIZE, size))
    {
      return -EINVAL;
    }

  erase_addr = GET_ALIGNED_DOWN(QSPI_NOR_SECT_SIZE, addr);
  erase_size = GET_ALIGNED_UP(QSPI_NOR_SECT_SIZE, size);

  while (erase_size > 0)
    {
      chunk_addr = erase_addr;
      chunk_size = 0;
      sf32lb_flash_lock();
      flags = up_irq_save();
      if (IS_ALIGNED(QSPI_NOR_BLK64_SIZE, erase_addr) &&
          erase_size >= QSPI_NOR_BLK64_SIZE)
        {
          chunk_size = QSPI_NOR_BLK64_SIZE;
          if (hflash->size > NOR_FLASH_MAX_3B_SIZE)
            {
              ret = HAL_FLASH_PRE_CMD(hflash, SPI_FLASH_CMD_BE4BA);
            }
          else
            {
              ret = HAL_FLASH_PRE_CMD(hflash, SPI_FLASH_CMD_BE64);
            }

          if (ret != 0)
            {
              up_irq_restore(flags);
              sf32lb_flash_unlock();
              syslog(LOG_ERR,
                     "ERROR: NOR erase precmd failed: addr=0x%08lx cmd=BE ret=%d\n",
                     (unsigned long)erase_addr,
                     ret);
              return -EIO;
            }

          ret = HAL_QSPIEX_BLK64_ERASE(hflash, erase_addr);
          if (ret == 0)
            {
              erase_addr += QSPI_NOR_BLK64_SIZE;
              erase_size -= QSPI_NOR_BLK64_SIZE;
            }
        }
      else
        {
          chunk_size = QSPI_NOR_SECT_SIZE;
          if (hflash->size > NOR_FLASH_MAX_3B_SIZE)
            {
              ret = HAL_FLASH_PRE_CMD(hflash, SPI_FLASH_CMD_SE4BA);
            }
          else
            {
              ret = HAL_FLASH_PRE_CMD(hflash, SPI_FLASH_CMD_SE);
            }

          if (ret != 0)
            {
              up_irq_restore(flags);
              sf32lb_flash_unlock();
              syslog(LOG_ERR,
                     "ERROR: NOR erase precmd failed: addr=0x%08lx cmd=SE ret=%d\n",
                     (unsigned long)erase_addr,
                     ret);
              return -EIO;
            }

          ret = HAL_QSPIEX_SECT_ERASE(hflash, erase_addr);
          if (ret == 0)
            {
              erase_addr += QSPI_NOR_SECT_SIZE;
              erase_size -= QSPI_NOR_SECT_SIZE;
            }
        }
      up_irq_restore(flags);
      sf32lb_flash_unlock();

      if (ret != 0)
        {
          syslog(LOG_ERR,
                 "ERROR: NOR erase failed: addr=0x%08lx remaining=0x%08lx ret=%d\n",
                 (unsigned long)erase_addr,
                 (unsigned long)erase_size,
                 ret);
          return -EIO;
        }

      phys_addr = hflash->base + chunk_addr;
      sf32lb_flash_restore_ahb_read(hflash);
      sf32lb_flash_cache_invalidate(phys_addr, chunk_size);
      if (!sf32lb_flash_verify_erased(phys_addr, chunk_size))
        {
          syslog(LOG_ERR,
                 "ERROR: NOR erase verify failed: addr=0x%08lx size=0x%08lx\n",
                 (unsigned long)phys_addr,
                 (unsigned long)chunk_size);
          return -EIO;
        }
    }

  return OK;
}

static ssize_t SF32LB_FLASH_RAMFUNC sf32lb_nor_write_range(FAR FLASH_HandleTypeDef *hflash,
                                                           uint32_t addr,
                                                           FAR const uint8_t *buffer,
                                                           uint32_t size)
{
  uint32_t taddr;
  uint32_t tsize;
  uint32_t aligned_size;
  uint32_t start;
  uint32_t chunk_addr;
  uint32_t chunk_size;
  uint32_t phys_addr;
  ssize_t cnt;
  int ret;
  FAR const uint8_t *tbuf;
  FAR const uint8_t *chunk_buf;
  uint8_t stage_buf[QSPI_NOR_PAGE_SIZE];

  if (hflash == NULL || buffer == NULL || size == 0)
    {
      return -EINVAL;
    }

  if (!g_flash_hw_initialized)
    {
      if (sf32lb_flash_preinit_runtime() < 0)
        {
          return -EIO;
        }
    }

  cnt = 0;
  tsize = size;
  tbuf = buffer;

  if (addr >= hflash->base)
    {
      taddr = addr - hflash->base;
    }
  else
    {
      taddr = addr;
    }

  aligned_size = QSPI_NOR_PAGE_SIZE;
  start = taddr & (aligned_size - 1);

  if (start > 0)
    {
      start = aligned_size - start;
      if (start > tsize)
        {
          start = tsize;
        }

      chunk_addr = taddr;
      chunk_size = start;
      chunk_buf = tbuf;

      if (sf32lb_flash_is_xip_buffer(chunk_buf, chunk_size))
        {
          memcpy(stage_buf, chunk_buf, chunk_size);
          chunk_buf = stage_buf;
        }

      sf32lb_flash_lock();
      ret = HAL_QSPIEX_WRITE_PAGE(hflash, taddr, chunk_buf, start);
      sf32lb_flash_unlock();
      if (ret != (int)start)
        {
          syslog(LOG_ERR,
                 "ERROR: NOR write unaligned head failed: addr=0x%08lx size=%lu ret=%d\n",
                 (unsigned long)taddr,
                 (unsigned long)start,
                 ret);
          return -EIO;
        }

      phys_addr = hflash->base + chunk_addr;
      sf32lb_flash_restore_ahb_read(hflash);
      sf32lb_flash_cache_invalidate(phys_addr, chunk_size);
      if (!sf32lb_flash_verify_written(phys_addr, chunk_buf, chunk_size))
        {
          syslog(LOG_ERR,
                 "ERROR: NOR write verify failed: addr=0x%08lx size=%lu\n",
                 (unsigned long)phys_addr,
                 (unsigned long)chunk_size);
          return -EIO;
        }

      taddr += start;
      tbuf += start;
      tsize -= start;
      cnt += start;
    }

  while (tsize >= aligned_size)
    {
      chunk_addr = taddr;
      chunk_size = aligned_size;
      chunk_buf = tbuf;

      if (sf32lb_flash_is_xip_buffer(chunk_buf, chunk_size))
        {
          memcpy(stage_buf, chunk_buf, chunk_size);
          chunk_buf = stage_buf;
        }

      sf32lb_flash_lock();
      ret = HAL_QSPIEX_WRITE_PAGE(hflash, taddr, chunk_buf, aligned_size);
      sf32lb_flash_unlock();
      if (ret != (int)aligned_size)
        {
          syslog(LOG_ERR,
                 "ERROR: NOR write page failed: addr=0x%08lx size=%lu ret=%d\n",
                 (unsigned long)taddr,
                 (unsigned long)aligned_size,
                 ret);
          return -EIO;
        }

      phys_addr = hflash->base + chunk_addr;
      sf32lb_flash_restore_ahb_read(hflash);
      sf32lb_flash_cache_invalidate(phys_addr, chunk_size);
      if (!sf32lb_flash_verify_written(phys_addr, chunk_buf, chunk_size))
        {
          syslog(LOG_ERR,
                 "ERROR: NOR write verify failed: addr=0x%08lx size=%lu\n",
                 (unsigned long)phys_addr,
                 (unsigned long)chunk_size);
          return -EIO;
        }

      taddr += aligned_size;
      tbuf += aligned_size;
      tsize -= aligned_size;
      cnt += aligned_size;
    }

  if (tsize > 0)
    {
      chunk_addr = taddr;
      chunk_size = tsize;
      chunk_buf = tbuf;

      if (sf32lb_flash_is_xip_buffer(chunk_buf, chunk_size))
        {
          memcpy(stage_buf, chunk_buf, chunk_size);
          chunk_buf = stage_buf;
        }

      sf32lb_flash_lock();
      ret = HAL_QSPIEX_WRITE_PAGE(hflash, taddr, chunk_buf, tsize);
      sf32lb_flash_unlock();
      if (ret != (int)tsize)
        {
          syslog(LOG_ERR,
                 "ERROR: NOR write tail failed: addr=0x%08lx size=%lu ret=%d\n",
                 (unsigned long)taddr,
                 (unsigned long)tsize,
                 ret);
          return -EIO;
        }

      phys_addr = hflash->base + chunk_addr;
      sf32lb_flash_restore_ahb_read(hflash);
      sf32lb_flash_cache_invalidate(phys_addr, chunk_size);
      if (!sf32lb_flash_verify_written(phys_addr, chunk_buf, chunk_size))
        {
          syslog(LOG_ERR,
                 "ERROR: NOR write verify failed: addr=0x%08lx size=%lu\n",
                 (unsigned long)phys_addr,
                 (unsigned long)chunk_size);
          return -EIO;
        }

      cnt += tsize;
    }

  return cnt;
}

/****************************************************************************
 * MTD Callbacks
 ****************************************************************************/

static ssize_t sf32lb_nor_read(FAR struct mtd_dev_s *dev, off_t offset,
                               size_t nbytes, FAR uint8_t *buffer)
{
  FAR struct sf32lb_nor_dev_s *priv = (FAR struct sf32lb_nor_dev_s *)dev;
  int ret;

  if (offset < 0)
    {
      return -EINVAL;
    }

  ret = sf32lb_nor_prepare_io(priv, (uint32_t)offset, (uint32_t)nbytes);
  if (ret < 0)
    {
      return ret;
    }

  ret = sf32lb_flash_raw_read_range(priv->spi_flash_handle,
                                    priv->mem_base + offset,
                                    buffer,
                                    (uint32_t)nbytes);
  if (ret < 0)
    {
      return ret;
    }

  return (ssize_t)nbytes;
}

static ssize_t sf32lb_nor_write(FAR struct mtd_dev_s *dev, off_t offset,
                                size_t nbytes, FAR const uint8_t *buffer)
{
  FAR struct sf32lb_nor_dev_s *priv = (FAR struct sf32lb_nor_dev_s *)dev;
  uint32_t addr = priv->mem_base + offset;
  ssize_t written;
  int ret;

  if (offset < 0)
    {
      return -EINVAL;
    }

  ret = sf32lb_nor_prepare_io(priv, (uint32_t)offset, (uint32_t)nbytes);
  if (ret < 0)
    {
      return ret;
    }

  written = sf32lb_nor_write_range(priv->spi_flash_handle, addr, buffer,
                                   (uint32_t)nbytes);
  if (written < 0)
    {
      return written;
    }

  sf32lb_flash_cache_invalidate(addr, (uint32_t)nbytes);
  return written;
}

static int sf32lb_nor_erase(FAR struct mtd_dev_s *dev, off_t startblock,
                            size_t nblocks)
{
  FAR struct sf32lb_nor_dev_s *priv = (FAR struct sf32lb_nor_dev_s *)dev;
  uint32_t addr = priv->mem_base + ((uint32_t)startblock << SF32LB_NOR_PAGE_SHIFT);
  uint32_t nbytes = (uint32_t)nblocks << SF32LB_NOR_PAGE_SHIFT;
  int ret;

  if (startblock < 0)
    {
      return -EINVAL;
    }

  ret = sf32lb_nor_prepare_io(priv,
                              (uint32_t)startblock << SF32LB_NOR_PAGE_SHIFT,
                              nbytes);
  if (ret < 0)
    {
      return ret;
    }

  ret = sf32lb_nor_erase_range(priv->spi_flash_handle, addr, nbytes);
  if (ret < 0)
    {
      return ret;
    }

  sf32lb_flash_cache_invalidate(addr, nbytes);
  return (int)nblocks;
}

static ssize_t sf32lb_nor_bread(FAR struct mtd_dev_s *dev, off_t startblock,
                                size_t nblocks, FAR uint8_t *buffer)
{
  FAR struct sf32lb_nor_dev_s *priv = (FAR struct sf32lb_nor_dev_s *)dev;
  size_t nbytes = nblocks << SF32LB_NOR_PAGE_SHIFT;
  int ret;

  if (startblock < 0)
    {
      return -EINVAL;
    }

  ret = sf32lb_nor_prepare_io(priv,
                              (uint32_t)startblock << SF32LB_NOR_PAGE_SHIFT,
                              (uint32_t)nbytes);
  if (ret < 0)
    {
      return ret;
    }

  ret = sf32lb_flash_raw_read_range(
          priv->spi_flash_handle,
          priv->mem_base + ((uint32_t)startblock << SF32LB_NOR_PAGE_SHIFT),
          buffer,
          (uint32_t)nbytes);
  if (ret < 0)
    {
      return ret;
    }

  return (ssize_t)nblocks;
}

static ssize_t sf32lb_nor_bwrite(FAR struct mtd_dev_s *dev, off_t startblock,
                                 size_t nblocks, FAR const uint8_t *buffer)
{
  FAR struct sf32lb_nor_dev_s *priv = (FAR struct sf32lb_nor_dev_s *)dev;
  size_t nbytes = nblocks << SF32LB_NOR_PAGE_SHIFT;
  uint32_t addr = priv->mem_base + ((uint32_t)startblock << SF32LB_NOR_PAGE_SHIFT);
  ssize_t written;
  int ret;

  if (startblock < 0)
    {
      return -EINVAL;
    }

  ret = sf32lb_nor_prepare_io(priv,
                              (uint32_t)startblock << SF32LB_NOR_PAGE_SHIFT,
                              (uint32_t)nbytes);
  if (ret < 0)
    {
      return ret;
    }

  written = sf32lb_nor_write_range(priv->spi_flash_handle, addr, buffer,
                                   (uint32_t)nbytes);
  if (written < 0)
    {
      return written;
    }

  sf32lb_flash_cache_invalidate(addr, (uint32_t)nbytes);
  return (ssize_t)nblocks;
}

static int sf32lb_nor_ioctl(FAR struct mtd_dev_s *dev, int cmd,
                            unsigned long arg)
{
  FAR struct sf32lb_nor_dev_s *priv = (FAR struct sf32lb_nor_dev_s *)dev;
  int ret = -EINVAL;

  switch (cmd)
    {
      case MTDIOC_GEOMETRY:
        {
          FAR struct mtd_geometry_s *geo =
            (FAR struct mtd_geometry_s *)((uintptr_t)arg);

          if (geo != NULL)
            {
              if (!g_flash_hw_initialized)
                {
                  ret = sf32lb_flash_preinit_runtime();
                  if (ret < 0)
                    {
                      break;
                    }
                }

              ret = sf32lb_nor_sync_geometry(priv, true);
              if (ret < 0)
                {
                  break;
                }

              memset(geo, 0, sizeof(*geo));
              geo->blocksize = SF32LB_NOR_PAGE_SIZE;
              geo->erasesize = SF32LB_NOR_PAGE_SIZE;
              geo->neraseblocks = priv->nsectors;
              ret = OK;
            }
        }
        break;

      case BIOC_PARTINFO:
        {
          FAR struct partition_info_s *info =
            (FAR struct partition_info_s *)arg;

          if (info != NULL)
            {
              if (!g_flash_hw_initialized)
                {
                  ret = sf32lb_flash_preinit_runtime();
                  if (ret < 0)
                    {
                      break;
                    }
                }

              ret = sf32lb_nor_sync_geometry(priv, true);
              if (ret < 0)
                {
                  break;
                }

              info->numsectors = priv->nsectors;
              info->sectorsize = SF32LB_NOR_PAGE_SIZE;
              info->startsector = priv->offset;
              info->parent[0] = '\0';
              ret = OK;
            }
        }
        break;

      case MTDIOC_ERASESTATE:
        {
          FAR uint8_t *result = (FAR uint8_t *)arg;
          *result = 0xff;
          ret = OK;
        }
        break;

      case MTDIOC_BULKERASE:
        {
          ret = sf32lb_nor_erase(dev, 0, priv->nsectors) == (int)priv->nsectors ?
                OK : -EIO;
        }
        break;

      default:
        ret = -ENOTTY;
        break;
    }

  return ret;
}

static int sf32lb_nor_isbad(FAR struct mtd_dev_s *dev, off_t block)
{
  UNUSED(dev);
  UNUSED(block);
  return 0;
}

static int sf32lb_nor_markbad(FAR struct mtd_dev_s *dev, off_t block)
{
  UNUSED(dev);
  UNUSED(block);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int sf32lb_flash_unlock(void)
{
  int ret = nxmutex_unlock(&g_lock);
  return ret < 0 ? ret : OK;
}

int sf32lb_flash_lock(void)
{
  return nxmutex_lock(&g_lock);
}

FAR struct mtd_dev_s *sf32lb_nor_initialize(FAR struct spi_dev_s *spi,
                                            uint32_t spi_devid,
                                            int block_offset,
                                            int block_count)
{
  FAR struct sf32lb_nor_dev_s *priv;
  int ret;

  UNUSED(spi);
  UNUSED(spi_devid);

  priv = kmm_zalloc(sizeof(struct sf32lb_nor_dev_s));
  if (priv == NULL)
    {
      return NULL;
    }

  ret = sf32lb_flash_hw_init();
  if (ret < 0)
    {
      kmm_free(priv);
      return NULL;
    }

  priv->spi_flash_handle = &g_spi_nor_flash_ctx.handle;
  if (priv->spi_flash_handle == NULL)
    {
      kmm_free(priv);
      return NULL;
    }

  if (block_offset < 0)
    {
      block_offset = 0;
    }

  if (block_count < 0)
    {
      block_count = 0;
    }

  priv->mtd.erase = sf32lb_nor_erase;
  priv->mtd.bread = sf32lb_nor_bread;
  priv->mtd.bwrite = sf32lb_nor_bwrite;
  priv->mtd.read = sf32lb_nor_read;
#ifdef CONFIG_MTD_BYTE_WRITE
  priv->mtd.write = sf32lb_nor_write;
#endif
  priv->mtd.ioctl = sf32lb_nor_ioctl;
  priv->mtd.isbad = sf32lb_nor_isbad;
  priv->mtd.markbad = sf32lb_nor_markbad;
  priv->mtd.name = "config";

  priv->offset = (uint32_t)block_offset;
  priv->limit_blocks = (uint32_t)block_count;
  ret = sf32lb_nor_sync_geometry(priv, false);
  if (ret < 0)
    {
      kmm_free(priv);
      return NULL;
    }

  return (FAR struct mtd_dev_s *)priv;
}

int sf32lb_nor_automount(int minor, int block_offset, int block_count)
{
  FAR struct mtd_dev_s *mtd;
  FAR struct sf32lb_nor_dev_s *priv;
  static bool initialized;
  char devname[16];
  int ret;

  snprintf(devname, sizeof(devname), SF32LB_NOR_PARENT_FMT, minor);

  if (!initialized)
    {
      mtd = sf32lb_nor_initialize(NULL, 0, block_offset, block_count);
      if (mtd == NULL)
        {
          syslog(LOG_ERR, "ERROR: Failed to initialize NOR flash\n");
          return -ENODEV;
        }

      ret = register_mtddriver(devname, mtd, 0, mtd);
      if (ret < 0)
        {
          syslog(LOG_ERR, "ERROR: register_mtddriver(%s) failed: %d\n",
                 devname, ret);
          return ret;
        }

      initialized = true;
      priv = (FAR struct sf32lb_nor_dev_s *)mtd;
      syslog(LOG_INFO,
        "INFO: NOR MTD registered at %s (offset=%lu blocks=%lu)\n",
        devname,
        (unsigned long)priv->offset,
        (unsigned long)priv->nsectors);
    }

  return OK;
}

/****************************************************************************
 * Name: sf32lb_flash_get_handle
 *
 * Description:
 *   Get the FLASH_HandleTypeDef pointer for the NOR flash on MPI2 (QSPI2).
 *   Used by board power management code for deep power-down / release.
 *
 ****************************************************************************/

FLASH_HandleTypeDef *sf32lb_flash_get_handle(void)
{
    return &g_spi_nor_flash_ctx.handle;
}
