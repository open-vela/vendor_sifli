/****************************************************************************
 * vendor/sifli/boards/sf32lb52/drivers/audio/sf32lb_audcodec.c
 *
 * Minimal driver for the SF32LB52 on-chip AUDCODEC DAC playback path.
 *
 * A few things the HAL does NOT do for the caller (each verified against
 * chips/drivers/hal/bf0_hal_audcodec_m.c):
 *   1. HAL_AUDCODEC_MspInit() is empty, so the module clock must be
 *      enabled by the caller.
 *   2. Nothing ever calls HAL_AUCODEC_Refgen_Init(); the corresponding
 *      code inside Config_Analog_DACPath is commented out.
 *   3. The handle must be zeroed: State[0] has to be
 *      HAL_AUDCODEC_STATE_RESET(0) or the MspInit branch is skipped.
 *   4. Volume must be set after Config_TChanel, which rewrites the whole
 *      DAC_CH0_CFG register.
 *   5. The HAL only ever clears CFG.DAC_ENABLE, never sets it.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/cache.h>
#include <nuttx/kmalloc.h>

#include "bf0_hal.h"
#include "drv_io.h"
#include "dma_config.h"          /* AUDCODEC_DAC0_DMA_REQUEST = DMA_REQUEST_41 */
#include "sf32lb_audcodec.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The DAC FIFO depth is not given in any header (APB_STAT.DAC_CH0_FIFO_CNT
 * is 4 bits wide).  Use a conservative high-water mark of 4: polling a bit
 * more often is better than overflowing.
 */

#define AUD_FIFO_HIWM 4

/* DMA for DAC0.  The official 52x dma_config.h gives the request line (41)
 * but no channel assignment.  DMA1 usage: Ch2=FLASH2, Ch3=FLASH1/SPI1,
 * Ch4=ADC0, Ch5=I2C2.  Ch1 is nominally reserved for AUDPRC, which we
 * bypass via opmode=1, so it is free in practice.
 */

#define AUD_DMA_INSTANCE  DMA1_Channel1
#define AUD_DMA_IRQn      DMAC1_CH1_IRQn      /* = 50, needs NX_IRQ() on the NuttX side */

/* Q15 sine table, 64 points per period.  A table is used instead of sin()
 * so that libm is not pulled in.
 */

#define SINE_LUT_N 64

/****************************************************************************
 * Private Data
 ****************************************************************************/

static AUDCODEC_HandleTypeDef      g_hcodec;
static DMA_HandleTypeDef           g_hdma0;
static AUDCODE_DAC_CLK_CONFIG_TYPE g_dac_clk;
static AUDCODEC_DACCfgTypeDef      g_dac_cfg;
static uint32_t                   *g_pcm;
static bool                        g_opened;

/* Official SF32LB52X DAC clock table, taken from the SiFli SDK
 * (drv_audcodec_m.c).  Field order is documented in
 * bf0_hal_audcodec.h:71-87.  Note that clk_div is always 1 on 52x, which
 * differs from the 56x/58x tables.  The 44.1kHz family must run off the
 * PLL (clk_src_sel=1 and sel_clk_dac_source=1).
 * SINC_GAIN is 0x14D for a 3.3V AVDD and 0xa0 for 1.8V; AVDD_V18_ENABLE is
 * not defined for this board, so the 3.3V value applies.
 */

#define SF32LB_SINC_GAIN 0x14D

struct audcodec_rate_s
{
  uint32_t fs;
  uint8_t  clk_src_sel;
  uint8_t  clk_div;
  uint8_t  osr_sel;
  uint8_t  sel_clk_dac_source;
  uint8_t  diva_clk_dac;
  uint8_t  diva_clk_chop_dac;
  uint8_t  divb_clk_chop_dac;
  uint8_t  diva_clk_chop_bg;
  uint8_t  diva_clk_chop_refgen;
  uint8_t  sel_clk_dac;
};

static const struct audcodec_rate_s g_rate_tab[] =
{
  { 48000, 0, 1, 0, 0,  5, 4, 2, 20, 20, 0 },
  { 32000, 0, 1, 1, 0,  5, 4, 2, 20, 20, 0 },
  { 24000, 0, 1, 5, 0, 10, 2, 2, 10, 10, 1 },
  { 16000, 0, 1, 4, 0,  5, 4, 2, 20, 20, 0 },
  { 12000, 0, 1, 7, 0, 20, 2, 1,  5,  5, 1 },
  {  8000, 0, 1, 8, 0, 10, 2, 2, 10, 10, 1 },
  { 44100, 1, 1, 0, 1,  5, 4, 2, 20, 20, 0 },
  { 22050, 1, 1, 5, 1, 10, 2, 2, 10, 10, 1 },
  { 11025, 1, 1, 7, 1, 20, 2, 1,  5,  5, 1 },
};

/* sin(2*pi*i/64) * 32767, Q15 */

static const int16_t g_sine_lut[SINE_LUT_N] =
{
       0,   3212,   6393,   9512,  12539,  15446,  18204,  20787,
   23170,  25330,  27245,  28898,  30273,  31356,  32138,  32610,
   32767,  32610,  32138,  31356,  30273,  28898,  27245,  25330,
   23170,  20787,  18204,  15446,  12539,   9512,   6393,   3212,
       0,  -3212,  -6393,  -9512, -12539, -15446, -18204, -20787,
  -23170, -25330, -27245, -28898, -30273, -31356, -32138, -32610,
  -32767, -32610, -32138, -31356, -30273, -28898, -27245, -25330,
  -23170, -20787, -18204, -15446, -12539,  -9512,  -6393,  -3212,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Generate an integral number of sine periods into 32-bit FIFO words using
 * the Q15 table.
 *
 * How a 32-bit FIFO word packs 16-bit samples is not documented anywhere,
 * so the same sample is duplicated into both halves: if one word carries
 * one sample the result is tone_hz, if it carries two the result is
 * tone_hz/2.  Either way it is a clean single tone, and the pitch tells
 * you which packing is in use.  Measured on hardware: one word, one sample.
 */

static void audcodec_fill_sine(uint32_t *buf, uint32_t words,
                               uint32_t fs, uint32_t hz, int amp)
{
  uint32_t i;

  for (i = 0; i < words; i++)
    {
      /* Phase in fixed point to avoid floats: i * hz * N / fs */

      uint32_t idx = (uint32_t)(((uint64_t)i * hz * SINE_LUT_N) / fs)
                     % SINE_LUT_N;
      int32_t  s   = ((int32_t)g_sine_lut[idx] * amp) >> 15;
      uint16_t u   = (uint16_t)(int16_t)s;

      buf[i] = ((uint32_t)u << 16) | u;
    }
}

/* Forward the DMA interrupt to the HAL, exactly as the SDK driver does */

static int audcodec_dma_isr(int irq, void *ctx, void *arg)
{
  HAL_DMA_IRQHandler(&g_hdma0);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void sf32lb_audcodec_pa(bool on)
{
  /* Use the board GPIO helper, the same way bsp_lcd_tp.c drives
   * LCD_VADD_EN.  The third argument selects the GPIO1 (PA) bank.
   */

  BSP_GPIO_Set(SF32LB_AUDIO_PA_PIN, on ? 1 : 0, 1);
}

int sf32lb_audcodec_open(const struct sf32lb_audcodec_cfg_s *cfg)
{
  unsigned i;

  if (g_opened)
    {
      return OK;
    }

  /* 1) Module clock: MspInit is empty, so enable it here */

  HAL_RCC_EnableModule(RCC_MOD_AUDCODEC);

  /* 2) HXT audio buffer, bandgap and PLL calibration.  Even when
   *    clk_src_sel=0 selects xtal48M this cannot be skipped, because
   *    PMUC_HXT_CR1_BUF_AUD_EN is only ever set here.
   */

  if (bf0_enable_pll(cfg->samplerate, 0) != 0)
    {
      syslog(LOG_ERR, "audcodec: bf0_enable_pll(%lu) failed\n",
             (unsigned long)cfg->samplerate);
      return -EIO;
    }

  /* 3) Reference generator: nothing else turns this on */

  HAL_AUCODEC_Refgen_Init();

  /* 4) Clock and path configuration */

  memset(&g_dac_clk, 0, sizeof(g_dac_clk));
  g_dac_clk.samplerate         = cfg->samplerate;
  g_dac_clk.clk_src_sel        = cfg->clk_src_sel;
  g_dac_clk.clk_div            = cfg->clk_div;
  g_dac_clk.osr_sel            = cfg->osr_sel;
  g_dac_clk.sinc_gain          = cfg->sinc_gain;
  g_dac_clk.sel_clk_dac_source = cfg->clk_src_sel;
  g_dac_clk.sel_clk_dac        = 0;

  /* Fill the remaining divider/chop fields from the official table.  They
   * happen to work when left at 0 for 48kHz, but the other rates need the
   * tabulated values.
   */

  for (i = 0; i < sizeof(g_rate_tab) / sizeof(g_rate_tab[0]); i++)
    {
      if (g_rate_tab[i].fs == cfg->samplerate)
        {
          g_dac_clk.diva_clk_dac         = g_rate_tab[i].diva_clk_dac;
          g_dac_clk.diva_clk_chop_dac    = g_rate_tab[i].diva_clk_chop_dac;
          g_dac_clk.divb_clk_chop_dac    = g_rate_tab[i].divb_clk_chop_dac;
          g_dac_clk.diva_clk_chop_bg     = g_rate_tab[i].diva_clk_chop_bg;
          g_dac_clk.diva_clk_chop_refgen = g_rate_tab[i].diva_clk_chop_refgen;
          g_dac_clk.sel_clk_dac          = g_rate_tab[i].sel_clk_dac;
          break;
        }
    }

  /* 5) Power up the analog DAC path (VCM -> AMP -> OS_DAC -> EN_DAC) */

  HAL_AUDCODEC_Config_Analog_DACPath(&g_dac_clk);

  /* 6) The handle must be zeroed so that State[0] is RESET(0), otherwise
   *    the MspInit branch is skipped.  The DMA handle is attached here as
   *    well, matching the SDK driver which fills hdma[] before Init.
   */

  HAL_RCC_EnableModule(RCC_MOD_DMAC1);

  memset(&g_hcodec, 0, sizeof(g_hcodec));
  memset(&g_hdma0,  0, sizeof(g_hdma0));

  g_hdma0.Instance     = AUD_DMA_INSTANCE;
  g_hdma0.Init.Request = AUDCODEC_DAC0_DMA_REQUEST;
  g_hdma0.Init.IrqPrio = 5;   /* left at 0 DMA_AllocChannel would use priority 0 */

  g_hcodec.Instance = hwp_audcodec;
  g_hcodec.hdma[HAL_AUDCODEC_DAC_CH0] = &g_hdma0;

  if (HAL_AUDCODEC_Init(&g_hcodec) != HAL_OK)
    {
      syslog(LOG_ERR, "audcodec: HAL_AUDCODEC_Init failed\n");
      return -EIO;
    }

  /* 7) Channel setup: opmode = 1 means "mem tx to audcodec", bypassing
   *    the AUDPRC block.
   */

  memset(&g_dac_cfg, 0, sizeof(g_dac_cfg));
  g_dac_cfg.opmode  = 1;
  g_dac_cfg.dac_clk = &g_dac_clk;

  if (HAL_AUDCODEC_Config_TChanel(&g_hcodec, 0, &g_dac_cfg) != HAL_OK)
    {
      syslog(LOG_ERR, "audcodec: Config_TChanel failed\n");
      return -EINVAL;
    }

  /* 7.5) Disable the volume ramp and zero-crossing adjust.
   *
   * Config_TChanel unconditionally writes CH0_EXT=0x17 (RAMP_EN=1,
   * ZERO_ADJUST_EN=1).  The ramp engine only advances on zero crossings of
   * the data stream, so with the built-in 1kHz source - which is injected
   * inside the path and has no PCM stream - it never gets a tick and the
   * steady-state output stays muted.  The SDK self-test writes both bits
   * as 0 for the same reason.
   */

  hwp_audcodec->DAC_CH0_CFG_EXT =
      (0 << AUDCODEC_DAC_CH0_CFG_EXT_RAMP_EN_Pos)       |
      (1 << AUDCODEC_DAC_CH0_CFG_EXT_RAMP_MODE_Pos)     |
      (0 << AUDCODEC_DAC_CH0_CFG_EXT_ZERO_ADJUST_EN_Pos) |
      (2 << AUDCODEC_DAC_CH0_CFG_EXT_RAMP_INTERVAL_Pos);  /* = 0x12 */

  /* 8) Volume must come after Config_TChanel, which rewrites the whole
   *    DAC_CH0_CFG register.
   */

  HAL_AUDCODEC_Config_DACPath_Volume(&g_hcodec, 0, cfg->volume);

  /* 9) Global DAC enable (CFG bit1); the HAL never sets it */

  __HAL_AUDCODEC_DAC_ENABLE(&g_hcodec);

  /* 10) The DMA interrupt has to be attached before Transmit_DMA:
   *     DMA_AllocChannel calls HAL_NVIC_EnableIRQ internally, so without a
   *     NuttX handler the first interrupt is an unexpected ISR.
   */

  irq_attach(NX_IRQ(AUD_DMA_IRQn), audcodec_dma_isr, NULL);
  up_enable_irq(NX_IRQ(AUD_DMA_IRQn));

  g_opened = true;
  return OK;
}

void sf32lb_audcodec_close(void)
{
  if (!g_opened)
    {
      return;
    }

  hwp_audcodec->CFG &= ~AUDCODEC_CFG_DAC_1K_MODE;
  hwp_audcodec->DAC_CH0_DEBUG = 0;
  __HAL_AUDCODEC_DAC_DISABLE(&g_hcodec);
  HAL_AUDCODEC_Close_Analog_DACPath();

  up_disable_irq(NX_IRQ(AUD_DMA_IRQn));
  irq_detach(NX_IRQ(AUD_DMA_IRQn));

  if (g_pcm != NULL)
    {
      kmm_free(g_pcm);
      g_pcm = NULL;
    }

  sf32lb_audcodec_pa(false);
  g_opened = false;
}

int sf32lb_audcodec_tone(int mode, uint32_t tone_hz, uint32_t ms, int amp)
{
  uint32_t fs = g_dac_clk.samplerate;
  uint32_t i;

  if (!g_opened)
    {
      return -EPERM;
    }

  if (tone_hz == 0)
    {
      tone_hz = 1000;
    }

  switch (mode)
    {
      /* ---- Built-in 1kHz source (see the header: needs a data stream) ---- */

      case SF32LB_TONE_1K_BUILTIN:
        {
          sf32lb_audcodec_pa(true);
          up_mdelay(50);  /* let the amplifier settle */

          hwp_audcodec->CFG |= AUDCODEC_CFG_DAC_1K_MODE;

          /* Dump while the tone is active so that the 1K bit and the PA
           * pin state are actually observable.
           */

          sf32lb_audcodec_dumpreg("during-tone");

          up_mdelay(ms);
          hwp_audcodec->CFG &= ~AUDCODEC_CFG_DAC_1K_MODE;

          /* Leave the amplifier on for further manual probing; close()
           * turns it off.
           */

          return OK;
        }

      /* ---- Write 16-bit constants straight into the DAC via
       *      DEBUG.BYPASS and toggle them in software to form a square wave
       */

      case SF32LB_TONE_DEBUG_SQ:
        {
          uint32_t half_us = 500000u / tone_hz;
          uint32_t n       = (ms * 1000u) / (2 * half_us);

          sf32lb_audcodec_pa(true);
          up_mdelay(50);

          for (i = 0; i < n; i++)
            {
              hwp_audcodec->DAC_CH0_DEBUG =
                  AUDCODEC_DAC_CH0_DEBUG_BYPASS |
                  (uint32_t)(uint16_t)(int16_t)(+amp);
              up_udelay(half_us);
              hwp_audcodec->DAC_CH0_DEBUG =
                  AUDCODEC_DAC_CH0_DEBUG_BYPASS |
                  (uint32_t)(uint16_t)(int16_t)(-amp);
              up_udelay(half_us);
            }

          hwp_audcodec->DAC_CH0_DEBUG = 0; /* BYPASS=0, back to normal */
          sf32lb_audcodec_pa(false);
          return OK;
        }

      /* ---- Feed the FIFO with CPU polling, DMA_EN stays 0 ---- */

      case SF32LB_TONE_PIO_SINE:
        {
          uint32_t  total        = (uint32_t)(((uint64_t)fs * ms) / 1000);
          uint32_t  period_words = fs / tone_hz;
          uint32_t *pcm;

          if (period_words < 2)
            {
              return -EINVAL;
            }

          pcm = kmm_malloc(period_words * sizeof(uint32_t));
          if (pcm == NULL)
            {
              return -ENOMEM;
            }

          audcodec_fill_sine(pcm, period_words, fs, tone_hz, amp);

          sf32lb_audcodec_pa(true);
          up_mdelay(50);

          for (i = 0; i < total; i++)
            {
              uint32_t guard = 0;

              while (((hwp_audcodec->APB_STAT &
                       AUDCODEC_APB_STAT_DAC_CH0_FIFO_CNT_Msk) >>
                      AUDCODEC_APB_STAT_DAC_CH0_FIFO_CNT_Pos) >= AUD_FIFO_HIWM)
                {
                  /* Wait while the FIFO is full; the guard count keeps a
                   * hardware fault from hanging us forever.
                   */

                  if (++guard > 1000000)
                    {
                      syslog(LOG_ERR, "audcodec: FIFO stuck at %lu\n",
                             (unsigned long)i);
                      goto pio_done;
                    }
                }

              hwp_audcodec->DAC_CH0_ENTRY = pcm[i % period_words];
            }

pio_done:
          sf32lb_audcodec_pa(false);
          kmm_free(pcm);
          return OK;
        }

      /* ---- Circular DMA sine; no underrun unlike CPU polling ---- */

      case SF32LB_TONE_DMA_SINE:
        {
          uint32_t period_words = fs / tone_hz;
          uint32_t words;

          if (period_words < 2)
            {
              return -EINVAL;
            }

          /* Hold an integral number of periods so the circular wrap does
           * not produce a phase jump.  Size is in bytes (the HAL shifts it
           * right by 2) and the word count must stay <= 65535.
           */

          words = period_words * 16;
          if (words == 0 || words > 65535)
            {
              return -EINVAL;
            }

          g_pcm = kmm_memalign(4, words * sizeof(uint32_t));
          if (g_pcm == NULL)
            {
              return -ENOMEM;
            }

          audcodec_fill_sine(g_pcm, words, fs, tone_hz, amp);
          up_clean_dcache((uintptr_t)g_pcm,
                          (uintptr_t)g_pcm + words * sizeof(uint32_t));

          if (HAL_AUDCODEC_Transmit_DMA(&g_hcodec, (uint8_t *)g_pcm,
                                        words * sizeof(uint32_t),
                                        HAL_AUDCODEC_DAC_CH0) != HAL_OK)
            {
              syslog(LOG_ERR, "audcodec: Transmit_DMA failed\n");
              kmm_free(g_pcm);
              g_pcm = NULL;
              return -EIO;
            }

          /* DMA_AllocChannel may migrate the channel, which would leave
           * the IRQ number pointing at the wrong one.
           */

          if ((g_hdma0.ChannelIndex >> 2) != 0)
            {
              syslog(LOG_WARNING,
                     "audcodec: DMA migrated to ch%d, IRQ may mismatch\n",
                     (int)(g_hdma0.ChannelIndex >> 2) + 1);
            }

          sf32lb_audcodec_pa(true);
          syslog(LOG_INFO, "  DMA playing: %lu words, %lu periods\n",
                 (unsigned long)words, (unsigned long)(words / period_words));

          up_mdelay(ms);

          hwp_audcodec->DAC_CH0_CFG &= ~AUDCODEC_DAC_CH0_CFG_DMA_EN;
          HAL_DMA_Abort(&g_hdma0);
          g_hcodec.State[HAL_AUDCODEC_DAC_CH0] = HAL_AUDCODEC_STATE_READY;

          sf32lb_audcodec_pa(false);
          kmm_free(g_pcm);
          g_pcm = NULL;
          return OK;
        }

      /* ---- Sweep: neither sinc_gain nor volume has any documented
       *      correct value, so sweep them.  Each step plays for 700ms and
       *      prints its index.
       */

      case SF32LB_TONE_SWEEP:
        {
          static const int      vols[]  = { 0, 20, 40, 60, 80, 100, 120 };
          static const uint16_t gains[] = { 1, 8, 32, 64, 128, 256, 511 };
          uint32_t cfg;
          int      step = 0;
          unsigned k;

          sf32lb_audcodec_pa(true);
          up_mdelay(50);

          /* A) fixed sinc_gain, sweep volume (0dB .. +60dB, 0.5dB/LSB) */

          for (k = 0; k < sizeof(vols) / sizeof(vols[0]); k++)
            {
              HAL_AUDCODEC_Config_DACPath_Volume(&g_hcodec, 0, vols[k]);
              syslog(LOG_INFO,
                     "  step %2d: volume=%3d (%+ddB) sinc_gain=128 CH0_CFG=%08lx\n",
                     ++step, vols[k], vols[k] / 2,
                     (unsigned long)hwp_audcodec->DAC_CH0_CFG);

              hwp_audcodec->CFG |= AUDCODEC_CFG_DAC_1K_MODE;
              up_mdelay(700);
              hwp_audcodec->CFG &= ~AUDCODEC_CFG_DAC_1K_MODE;
              up_mdelay(300);
            }

          /* B) volume at +30dB, sweep sinc_gain over its full range */

          HAL_AUDCODEC_Config_DACPath_Volume(&g_hcodec, 0, 60);

          for (k = 0; k < sizeof(gains) / sizeof(gains[0]); k++)
            {
              cfg  = hwp_audcodec->DAC_CH0_CFG;
              cfg &= ~AUDCODEC_DAC_CH0_CFG_SINC_GAIN_Msk;
              cfg |= ((uint32_t)gains[k] << AUDCODEC_DAC_CH0_CFG_SINC_GAIN_Pos)
                     & AUDCODEC_DAC_CH0_CFG_SINC_GAIN_Msk;
              hwp_audcodec->DAC_CH0_CFG = cfg;

              syslog(LOG_INFO,
                     "  step %2d: volume=60 (+30dB) sinc_gain=%3u CH0_CFG=%08lx\n",
                     ++step, gains[k],
                     (unsigned long)hwp_audcodec->DAC_CH0_CFG);

              hwp_audcodec->CFG |= AUDCODEC_CFG_DAC_1K_MODE;
              up_mdelay(700);
              hwp_audcodec->CFG &= ~AUDCODEC_CFG_DAC_1K_MODE;
              up_mdelay(300);
            }

          syslog(LOG_INFO, "  sweep done, %d steps\n", step);
          return OK;
        }

      default:
        return -EINVAL;
    }
}

int sf32lb_audcodec_cfg_for_rate(struct sf32lb_audcodec_cfg_s *cfg,
                                 uint32_t samplerate)
{
  unsigned i;

  for (i = 0; i < sizeof(g_rate_tab) / sizeof(g_rate_tab[0]); i++)
    {
      if (g_rate_tab[i].fs == samplerate)
        {
          cfg->samplerate  = samplerate;
          cfg->clk_src_sel = g_rate_tab[i].clk_src_sel;
          cfg->clk_div     = g_rate_tab[i].clk_div;
          cfg->osr_sel     = g_rate_tab[i].osr_sel;
          cfg->sinc_gain   = SF32LB_SINC_GAIN;
          cfg->volume      = 0;
          return OK;
        }
    }

  return -ENOTSUP;
}

/* Play a block of 16-bit mono PCM over DMA, blocking until it is done.
 *
 * The FIFO takes 32-bit words.  Duplicating the same 16-bit sample into
 * both halves yields the correct pitch on hardware, so one word carries
 * one sample.
 */

int sf32lb_audcodec_play_pcm(const int16_t *pcm, uint32_t nsamples)
{
  uint32_t chunk;
  uint32_t done = 0;
  uint32_t fs   = g_dac_clk.samplerate;
  int      ret  = OK;

  if (!g_opened || pcm == NULL || nsamples == 0)
    {
      return -EINVAL;
    }

  /* Size is in bytes and the HAL shifts it right by 2, with a 65535 word
   * ceiling, so submit the data in chunks.
   */

  sf32lb_audcodec_pa(true);
  up_mdelay(20);

  while (done < nsamples)
    {
      uint32_t i;
      uint32_t ms;

      chunk = nsamples - done;
      if (chunk > 32768)
        {
          chunk = 32768;
        }

      g_pcm = kmm_memalign(4, chunk * sizeof(uint32_t));
      if (g_pcm == NULL)
        {
          ret = -ENOMEM;
          break;
        }

      for (i = 0; i < chunk; i++)
        {
          uint16_t u = (uint16_t)pcm[done + i];
          g_pcm[i] = ((uint32_t)u << 16) | u;
        }

      up_clean_dcache((uintptr_t)g_pcm,
                      (uintptr_t)g_pcm + chunk * sizeof(uint32_t));

      if (HAL_AUDCODEC_Transmit_DMA(&g_hcodec, (uint8_t *)g_pcm,
                                    chunk * sizeof(uint32_t),
                                    HAL_AUDCODEC_DAC_CH0) != HAL_OK)
        {
          kmm_free(g_pcm);
          g_pcm = NULL;
          ret = -EIO;
          break;
        }

      /* Transmit_DMA hardcodes CIRCULAR mode, so wait out the chunk and
       * stop it explicitly, otherwise it would loop forever.
       */

      ms = (uint32_t)(((uint64_t)chunk * 1000) / fs);
      up_mdelay(ms);

      hwp_audcodec->DAC_CH0_CFG &= ~AUDCODEC_DAC_CH0_CFG_DMA_EN;
      HAL_DMA_Abort(&g_hdma0);
      g_hcodec.State[HAL_AUDCODEC_DAC_CH0] = HAL_AUDCODEC_STATE_READY;

      kmm_free(g_pcm);
      g_pcm = NULL;
      done += chunk;
    }

  sf32lb_audcodec_pa(false);
  return ret;
}

void sf32lb_audcodec_dumpreg(const char *tag)
{
  syslog(LOG_INFO,
         "audcodec[%s]: PA10=%d 1K=%d DACEN=%d\n"
         "  CFG=%08lx DAC_CFG=%08lx CH0_CFG=%08lx CH0_EXT=%08lx\n"
         "  APB_STAT=%08lx IRQ=%08lx PLL_STAT=%08lx REFGEN=%08lx\n"
         "  BG_CFG0=%08lx DAC1_CFG=%08lx PLL_CFG4=%08lx\n",
         tag ? tag : "",
         (int)HAL_GPIO_ReadPin(hwp_gpio1, SF32LB_AUDIO_PA_PIN),
         (int)((hwp_audcodec->CFG & AUDCODEC_CFG_DAC_1K_MODE) ? 1 : 0),
         (int)((hwp_audcodec->CFG & AUDCODEC_CFG_DAC_ENABLE) ? 1 : 0),
         (unsigned long)hwp_audcodec->CFG,
         (unsigned long)hwp_audcodec->DAC_CFG,
         (unsigned long)hwp_audcodec->DAC_CH0_CFG,
         (unsigned long)hwp_audcodec->DAC_CH0_CFG_EXT,
         (unsigned long)hwp_audcodec->APB_STAT,
         (unsigned long)hwp_audcodec->IRQ,
         (unsigned long)hwp_audcodec->PLL_STAT,
         (unsigned long)hwp_audcodec->REFGEN_CFG,
         (unsigned long)hwp_audcodec->BG_CFG0,
         (unsigned long)hwp_audcodec->DAC1_CFG,
         (unsigned long)hwp_audcodec->PLL_CFG4);
}
