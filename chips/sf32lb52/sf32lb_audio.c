/****************************************************************************
 * vendor/sifli/chips/sf32lb52/sf32lb_audio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * NuttX audio lower-half driver for the SiFli SF32LB52 internal audio
 * codec (AUDCODEC, codec-only path / opmode=1).
 *
 * Ported from the RT-Thread reference driver drv_audcodec_m.c.  It exposes
 * two devices:
 *   /dev/audio/pcm0p - playback via DAC channel 0 (mono, 16-bit)
 *   /dev/audio/pcm0c - capture  via ADC channel 0 (mono, 16-bit)
 *
 * The DMA runs in circular (double-buffer) mode over a contiguous region of
 * audio-pipeline buffers, mirroring nuttx/drivers/audio/audio_dma.c.  The
 * HAL fires a half-complete and a complete interrupt per cycle, so the
 * number of buffers is forced to two (one period == one buffer).
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <string.h>
#include <errno.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/queue.h>
#include <nuttx/spinlock.h>

#include <nuttx/audio/audio.h>

#include "bf0_hal.h"
#include "bf0_hal_audcodec.h"
#include "bf0_hal_audprc.h"
#include "dma_config.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* AUDPRC TX0 DMA — used for playback (AUDPRC → AUDCODEC DAC path). */

#ifndef AUDPRC_TX0_DMA_INSTANCE
#  define AUDPRC_TX0_DMA_INSTANCE   DMA1_Channel1
#endif
#ifndef AUDPRC_TX0_DMA_IRQ
#  define AUDPRC_TX0_DMA_IRQ        DMAC1_CH1_IRQn
#endif
#ifndef AUDPRC_TX0_DMA_REQUEST
#  define AUDPRC_TX0_DMA_REQUEST    DMA_REQUEST_51
#endif

/* AUDCODEC ADC0 DMA — used for recording (direct codec ADC, opmode=1). */

#ifndef AUDCODEC_ADC0_DMA_INSTANCE
#  define AUDCODEC_ADC0_DMA_INSTANCE   DMA1_Channel4
#endif
#ifndef AUDCODEC_ADC0_DMA_IRQ
#  define AUDCODEC_ADC0_DMA_IRQ        DMAC1_CH4_IRQn
#endif
#ifndef AUDCODEC_ADC0_DMA_REQUEST
#  define AUDCODEC_ADC0_DMA_REQUEST    DMA_REQUEST_39
#endif

/* NuttX IRQ numbers are the Cortex-M exception number, i.e. the SiFli IRQn
 * plus the 16 system exceptions.
 */

#define SIFLI_IRQ(n)                  ((int)(n) + 16)

/* Audio PA: AW8155 amplifier on PA42 (hwp_gpio1, pin 42).
 * The AW8155 uses a pulse-count protocol to select the operating mode:
 *   1 pulse  = mode 1,  2 pulses = mode 2,  3 pulses = mode 3,  4 pulses = mode 4.
 * Each pulse is HIGH 5µs / LOW 5µs.  After the last pulse the pin stays HIGH
 * to keep the amplifier enabled.  Driving it LOW powers it down.
 */

#define AUDIO_PA_GPIO                 hwp_gpio1
#define AUDIO_PA_PIN                  42
#define AW8155_WORK_MODE              1       /* 1–4, matches SDK default */
#define AW8155_PULSE_US               5       /* µs per half-cycle */

/* Default pipeline buffer geometry.  buffer_num is forced to 2 to match the
 * HAL half/complete double-buffer interrupt model.
 */

#define SF32LB_AUDIO_BUFFER_SIZE      4096
#define SF32LB_AUDIO_BUFFER_NUM       2

/* SINC gain used by the codec clock tables (matches the RT-Thread default
 * when AVDD_V18 is not enabled).
 */

#define SF32LB_SINC_GAIN              0x14d

/* PLL state tracking. */

#define SF32LB_PLL_CLOSED             0
#define SF32LB_PLL_OPEN               1   /* xtal path, PLL bandgap on */
#define SF32LB_PLL_ENABLE             2   /* fractional PLL enabled    */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sf32lb_audio_s
{
  struct audio_lowerhalf_s dev;        /* NuttX lower-half (must be first) */
  bool                     playback;   /* true: DAC/pcm0p, false: ADC/pcm0c */
  uint8_t                  did;        /* HAL channel id (DAC_CH0 / ADC_CH0) */
  uint8_t                 *alloc_addr; /* contiguous apb backing store      */
  uint8_t                  alloc_index;
  bool                     xrun;
  bool                     running;
  struct dq_queue_s        pendq;      /* enqueued buffers in flight        */
  apb_samp_t               buffer_size;
  apb_samp_t               buffer_num;
  spinlock_t               lock;
  int                      samplerate_index;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int sf32lb_audio_getcaps(struct audio_lowerhalf_s *dev, int type,
                                struct audio_caps_s *caps);
static int sf32lb_audio_shutdown(struct audio_lowerhalf_s *dev);
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int sf32lb_audio_configure(struct audio_lowerhalf_s *dev,
                                  void *session,
                                  const struct audio_caps_s *caps);
static int sf32lb_audio_start(struct audio_lowerhalf_s *dev, void *session);
#  ifndef CONFIG_AUDIO_EXCLUDE_STOP
static int sf32lb_audio_stop(struct audio_lowerhalf_s *dev, void *session);
#  endif
#  ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
static int sf32lb_audio_pause(struct audio_lowerhalf_s *dev, void *session);
static int sf32lb_audio_resume(struct audio_lowerhalf_s *dev, void *session);
#  endif
static int sf32lb_audio_reserve(struct audio_lowerhalf_s *dev,
                                void **session);
static int sf32lb_audio_release(struct audio_lowerhalf_s *dev,
                                void *session);
#else
static int sf32lb_audio_configure(struct audio_lowerhalf_s *dev,
                                  const struct audio_caps_s *caps);
static int sf32lb_audio_start(struct audio_lowerhalf_s *dev);
#  ifndef CONFIG_AUDIO_EXCLUDE_STOP
static int sf32lb_audio_stop(struct audio_lowerhalf_s *dev);
#  endif
#  ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
static int sf32lb_audio_pause(struct audio_lowerhalf_s *dev);
static int sf32lb_audio_resume(struct audio_lowerhalf_s *dev);
#  endif
static int sf32lb_audio_reserve(struct audio_lowerhalf_s *dev);
static int sf32lb_audio_release(struct audio_lowerhalf_s *dev);
#endif
static int sf32lb_audio_allocbuffer(struct audio_lowerhalf_s *dev,
                                    struct audio_buf_desc_s *bufdesc);
static int sf32lb_audio_freebuffer(struct audio_lowerhalf_s *dev,
                                   struct audio_buf_desc_s *bufdesc);
static int sf32lb_audio_enqueuebuffer(struct audio_lowerhalf_s *dev,
                                      struct ap_buffer_s *apb);
static int sf32lb_audio_ioctl(struct audio_lowerhalf_s *dev, int cmd,
                              unsigned long arg);

static void sf32lb_audio_period_done(struct sf32lb_audio_s *priv);

int sf32lb_audio_audprc_tx0_dma_isr(int irq, void *context, void *arg);
int sf32lb_audio_adc0_dma_isr(int irq, void *context, void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct audio_ops_s g_sf32lb_audio_ops =
{
  .getcaps       = sf32lb_audio_getcaps,
  .configure     = sf32lb_audio_configure,
  .shutdown      = sf32lb_audio_shutdown,
  .start         = sf32lb_audio_start,
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  .stop          = sf32lb_audio_stop,
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
  .pause         = sf32lb_audio_pause,
  .resume        = sf32lb_audio_resume,
#endif
  .allocbuffer   = sf32lb_audio_allocbuffer,
  .freebuffer    = sf32lb_audio_freebuffer,
  .enqueuebuffer = sf32lb_audio_enqueuebuffer,
  .ioctl         = sf32lb_audio_ioctl,
  .reserve       = sf32lb_audio_reserve,
  .release       = sf32lb_audio_release,
};

/* Codec DAC clock configuration table (xtal-based; the 44.1k family entries
 * use the fractional PLL).  Indexed identically to the ADC table.
 */

static const AUDCODE_DAC_CLK_CONFIG_TYPE g_dac_clk_cfg[9] =
{
  {48000, 0, 1, 0, SF32LB_SINC_GAIN, 0,  5, 4, 2, 20, 20, 0},
  {32000, 0, 1, 1, SF32LB_SINC_GAIN, 0,  5, 4, 2, 20, 20, 0},
  {24000, 0, 1, 5, SF32LB_SINC_GAIN, 0, 10, 2, 2, 10, 10, 1},
  {16000, 0, 1, 4, SF32LB_SINC_GAIN, 0,  5, 4, 2, 20, 20, 0},
  {12000, 0, 1, 7, SF32LB_SINC_GAIN, 0, 20, 2, 1,  5,  5, 1},
  { 8000, 0, 1, 8, SF32LB_SINC_GAIN, 0, 10, 2, 2, 10, 10, 1},
  {44100, 1, 1, 0, SF32LB_SINC_GAIN, 1,  5, 4, 2, 20, 20, 0},
  {22050, 1, 1, 5, SF32LB_SINC_GAIN, 1, 10, 2, 2, 10, 10, 1},
  {11025, 1, 1, 7, SF32LB_SINC_GAIN, 1, 20, 2, 1,  5,  5, 1},
};

static const AUDCODE_ADC_CLK_CONFIG_TYPE g_adc_clk_cfg[9] =
{
  {48000, 0,  5, 0, 0, 1, 5, 0},
  {32000, 0,  5, 1, 0, 1, 5, 0},
  {24000, 0, 10, 0, 0, 0, 5, 2},
  {16000, 0, 10, 1, 0, 0, 5, 2},
  {12000, 0, 10, 2, 0, 0, 5, 2},
  { 8000, 0, 10, 3, 0, 0, 5, 2},
  {44100, 1,  5, 0, 1, 1, 5, 1},
  {22050, 1,  5, 2, 1, 1, 5, 1},
  {11025, 1, 10, 2, 1, 0, 5, 3},
};

/* Shared codec hardware state. */

static AUDCODEC_HandleTypeDef g_haudcodec;
static AUDPRC_HandleTypeDef   g_haudprc;
static DMA_HandleTypeDef      g_hdma_dac0;
static DMA_HandleTypeDef      g_hdma_adc0;
static DMA_HandleTypeDef      g_hdma_audprc_tx0;
static bool                   g_codec_inited;
static int                    g_pll_state = SF32LB_PLL_CLOSED;

/* Separate instance pointers — AUDPRC and AUDCODEC channel ids overlap. */

static struct sf32lb_audio_s *g_playback_priv;
static struct sf32lb_audio_s *g_capture_priv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sf32lb_audio_pa_init
 *
 * Description:
 *   Initialise the AW8155 amplifier GPIO.  The pin is configured as a
 *   push-pull output with pull-down and initially driven LOW (amplifier
 *   off).
 *
 ****************************************************************************/

static void sf32lb_audio_pa_init(void)
{
  GPIO_InitTypeDef cfg;

  memset(&cfg, 0, sizeof(cfg));
  cfg.Pin  = AUDIO_PA_PIN;
  cfg.Mode = GPIO_MODE_OUTPUT;
  cfg.Pull = GPIO_PULLDOWN;            /* match SDK bsp_pinmux default */
  HAL_GPIO_Init(AUDIO_PA_GPIO, &cfg);
  HAL_GPIO_WritePin(AUDIO_PA_GPIO, AUDIO_PA_PIN, GPIO_PIN_RESET);
  HAL_Delay_us(550);                   /* power-on settling */

  auderr("AW8155 PA init: gpio1 pin%d OUTPUT PULLDOWN, LOW\n", AUDIO_PA_PIN);
}

/****************************************************************************
 * Name: sf32lb_audio_pa_set
 *
 * Description:
 *   Turn the AW8155 amplifier on (pulse-count protocol) or off.
 *
 ****************************************************************************/

static void sf32lb_audio_pa_set(int on)
{
  if (on)
    {
      int remaining = AW8155_WORK_MODE;

      /* Match sifli_aw8155_start(): toggle the pin to generate the
       * required number of pulses.  Leave HIGH on exit.
       */

      while (1)
        {
          HAL_GPIO_WritePin(AUDIO_PA_GPIO, AUDIO_PA_PIN, GPIO_PIN_SET);
          HAL_Delay_us(AW8155_PULSE_US);

          if (--remaining > 0)
            {
              HAL_GPIO_WritePin(AUDIO_PA_GPIO, AUDIO_PA_PIN, GPIO_PIN_RESET);
              HAL_Delay_us(AW8155_PULSE_US);
            }
          else
            {
              break;
            }
        }

      auderr("AW8155 PA ON (mode %d)\n", AW8155_WORK_MODE);
    }
  else
    {
      HAL_GPIO_WritePin(AUDIO_PA_GPIO, AUDIO_PA_PIN, GPIO_PIN_RESET);
      HAL_Delay_us(550);
      auderr("AW8155 PA OFF\n");
    }
}

/****************************************************************************
 * Name: sf32lb_audio_find_rate
 *
 * Description:
 *   Map a sample rate (Hz) to an index in the codec clock tables.
 *
 ****************************************************************************/

static int sf32lb_audio_find_rate(uint32_t samplerate)
{
  int i;

  for (i = 0; i < 9; i++)
    {
      if (g_dac_clk_cfg[i].samplerate == samplerate)
        {
          return i;
        }
    }

  return -1;
}

/****************************************************************************
 * Name: sf32lb_audio_pll_config
 ****************************************************************************/

static void sf32lb_audio_pll_config(uint8_t clk_src_sel, uint32_t samplerate)
{
  if (clk_src_sel)
    {
      /* Fractional PLL path (44.1k family). */

      bf0_enable_pll(samplerate, 1);
      g_pll_state = SF32LB_PLL_ENABLE;
    }
  else
    {
      /* Crystal path: the codec PLL bandgap still needs to be turned on. */

      if (g_pll_state == SF32LB_PLL_CLOSED)
        {
          HAL_TURN_ON_PLL();
          g_pll_state = SF32LB_PLL_OPEN;
        }
    }
}

/****************************************************************************
 * Name: sf32lb_audio_hw_init
 *
 * Description:
 *   One-time initialisation of the shared codec hardware: power, clocks,
 *   DMA handles and the HAL core.
 *
 ****************************************************************************/

static int sf32lb_audio_hw_init(void)
{
  if (g_codec_inited)
    {
      return OK;
    }

  memset(&g_haudcodec, 0, sizeof(g_haudcodec));
  memset(&g_haudprc,    0, sizeof(g_haudprc));
  memset(&g_hdma_adc0,  0, sizeof(g_hdma_adc0));
  memset(&g_hdma_audprc_tx0, 0, sizeof(g_hdma_audprc_tx0));

  /* ── AUDCODEC: ADC0 for recording (direct codec, opmode=1) ── */

  g_hdma_adc0.Instance     = AUDCODEC_ADC0_DMA_INSTANCE;
  g_hdma_adc0.Init.Request = AUDCODEC_ADC0_DMA_REQUEST;
  g_haudcodec.hdma[HAL_AUDCODEC_ADC_CH0] = &g_hdma_adc0;

  g_haudcodec.Instance        = hwp_audcodec;
  g_haudcodec.Init.en_dly_sel = 0;
  g_haudcodec.Init.dac_cfg.opmode = 0;  /* playback via AUDPRC */
  g_haudcodec.Init.adc_cfg.opmode = 1;  /* capture direct codec */

  HAL_PMU_EnableAudio(1);
  HAL_RCC_EnableModule(RCC_MOD_AUDCODEC_HP);
  HAL_RCC_EnableModule(RCC_MOD_AUDCODEC_LP);

  if (HAL_AUDCODEC_Init(&g_haudcodec) != HAL_OK)
    {
      auderr("HAL_AUDCODEC_Init failed\n");
      return -EIO;
    }

  auderr("AUDCODEC init OK: ADC0 DMA=ch%d IRQ=%d\n",
         AUDCODEC_ADC0_DMA_INSTANCE, AUDCODEC_ADC0_DMA_IRQ);

  /* ── AUDPRC: TX0 for playback (AUDPRC → AUDCODEC DAC) ── */

  g_hdma_audprc_tx0.Instance     = AUDPRC_TX0_DMA_INSTANCE;
  g_hdma_audprc_tx0.Init.Request = AUDPRC_TX0_DMA_REQUEST;
  g_haudprc.hdma[HAL_AUDPRC_TX_CH0] = &g_hdma_audprc_tx0;

  g_haudprc.Instance = hwp_audprc;

  /* Use xtal clock source, div=1 (÷2) to get ~24MHz for AUDPRC. */

  g_haudprc.Init.clk_sel = 0;
  g_haudprc.Init.clk_div = 1;

  /* Default DAC path: match SDK bf0_adc_dac_path_cfg_init +
   * audio_server override (0x5050 = src0=0, src1=5 mute). */

  g_haudprc.Init.dac_cfg.dst_sel    = 0;   /* to codec */
  g_haudprc.Init.dac_cfg.mixrsrc1   = 5;   /* mute */
  g_haudprc.Init.dac_cfg.mixrsrc0   = 0;   /* TX0 → right mixer */
  g_haudprc.Init.dac_cfg.mixlsrc1   = 5;   /* mute */
  g_haudprc.Init.dac_cfg.mixlsrc0   = 0;   /* TX0 → left mixer */
  g_haudprc.Init.dac_cfg.vol_r      = 0;
  g_haudprc.Init.dac_cfg.vol_l      = 0;
  g_haudprc.Init.dac_cfg.src_ch_en  = 1;   /* stereo enable */
  g_haudprc.Init.dac_cfg.src_hbf1_en = 0;
  g_haudprc.Init.dac_cfg.src_hbf2_en = 0;
  g_haudprc.Init.dac_cfg.src_hbf3_en = 0;
  g_haudprc.Init.dac_cfg.eq_clr     = 0;
  g_haudprc.Init.dac_cfg.eq_stage   = 1;
  g_haudprc.Init.dac_cfg.eq_ch_en   = 0;
  g_haudprc.Init.dac_cfg.muxrsrc1   = 5;   /* mute */
  g_haudprc.Init.dac_cfg.muxrsrc0   = 0;   /* TX0 → mux right */
  g_haudprc.Init.dac_cfg.muxlsrc1   = 5;   /* mute */
  g_haudprc.Init.dac_cfg.muxlsrc0   = 0;   /* TX0 → mux left */
  g_haudprc.Init.dac_cfg.src_sinc_en = 0;
  g_haudprc.Init.dac_cfg.sinc_ratio = 0;

  g_haudprc.Init.adc_div = 1;
  g_haudprc.Init.dac_div = 1;

  HAL_RCC_EnableModule(RCC_MOD_AUDPRC);

  if (HAL_AUDPRC_Init(&g_haudprc) != HAL_OK)
    {
      auderr("HAL_AUDPRC_Init failed\n");
      return -EIO;
    }

  /* HAL_AUDPRC_Init leaves the block disabled.  Enable it in
   * start() after DMA is configured to match the SDK order. */

  __HAL_AUDPRC_CLK_XTAL(&g_haudprc);

  auderr("AUDPRC init OK: TX0 DMA=ch%d IRQ=%d\n",
         AUDPRC_TX0_DMA_INSTANCE, AUDPRC_TX0_DMA_IRQ);

  /* ── ISRs ── */

  irq_attach(SIFLI_IRQ(AUDPRC_TX0_DMA_IRQ),
             sf32lb_audio_audprc_tx0_dma_isr, NULL);
  irq_attach(SIFLI_IRQ(AUDCODEC_ADC0_DMA_IRQ),
             sf32lb_audio_adc0_dma_isr, NULL);

  sf32lb_audio_pa_init();

  g_codec_inited = true;
  return OK;
}

/****************************************************************************
 * Name: sf32lb_audio_getcaps
 ****************************************************************************/

static int sf32lb_audio_getcaps(struct audio_lowerhalf_s *dev, int type,
                                struct audio_caps_s *caps)
{
  struct sf32lb_audio_s *priv = (struct sf32lb_audio_s *)dev;

  DEBUGASSERT(caps && caps->ac_len >= sizeof(struct audio_caps_s));

  caps->ac_format.hw  = 0;
  caps->ac_controls.w = 0;

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_QUERY:
        caps->ac_channels = 1;

        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            if (priv->playback)
              {
                caps->ac_controls.b[0] = AUDIO_TYPE_OUTPUT;
              }
            else
              {
                caps->ac_controls.b[0] = AUDIO_TYPE_INPUT;
              }

            caps->ac_format.hw = 1 << (AUDIO_FMT_PCM - 1);
          }
        else
          {
            caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
          }
        break;

      case AUDIO_TYPE_OUTPUT:
      case AUDIO_TYPE_INPUT:
        caps->ac_channels = 1;

        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            caps->ac_controls.hw[0] = AUDIO_SAMP_RATE_8K  |
                                      AUDIO_SAMP_RATE_11K |
                                      AUDIO_SAMP_RATE_12K |
                                      AUDIO_SAMP_RATE_16K |
                                      AUDIO_SAMP_RATE_22K |
                                      AUDIO_SAMP_RATE_24K |
                                      AUDIO_SAMP_RATE_32K |
                                      AUDIO_SAMP_RATE_44K |
                                      AUDIO_SAMP_RATE_48K;
            caps->ac_controls.b[2] = AUDIO_BIT_RATE_22K | AUDIO_BIT_RATE_44K;
          }
        break;

      default:
        break;
    }

  return caps->ac_len;
}

/****************************************************************************
 * Name: sf32lb_audio_configure
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int sf32lb_audio_configure(struct audio_lowerhalf_s *dev,
                                  void *session,
                                  const struct audio_caps_s *caps)
#else
static int sf32lb_audio_configure(struct audio_lowerhalf_s *dev,
                                  const struct audio_caps_s *caps)
#endif
{
  struct sf32lb_audio_s *priv = (struct sf32lb_audio_s *)dev;
  uint32_t samplerate;
  int index;

  DEBUGASSERT(priv && caps);

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_OUTPUT:
      case AUDIO_TYPE_INPUT:
        samplerate = caps->ac_controls.hw[0] |
                     (caps->ac_controls.b[3] << 16);

        index = sf32lb_audio_find_rate(samplerate);
        if (index < 0)
          {
            auderr("unsupported samplerate %lu\n",
                   (unsigned long)samplerate);
            return -EINVAL;
          }

        priv->samplerate_index = index;
        g_haudcodec.Init.samplerate_index = (uint8_t)index;

        if (priv->playback)
          {
            g_haudcodec.Init.dac_cfg.dac_clk =
              (AUDCODE_DAC_CLK_CONFIG_TYPE *)&g_dac_clk_cfg[index];
            HAL_AUDCODEC_Config_TChanel(&g_haudcodec, 0,
                                        &g_haudcodec.Init.dac_cfg);

            /* Set AUDPRC clock dividers for this sample rate. */

            {
              /* clk_div = 48MHz / samplerate (from SDK audprc_clk_cfg_tb_xtal) */
              static const uint16_t audprc_div[9] = {
                1000, 1500, 2000, 3000, 4000, 6000, 1000, 2000, 4000
              };

              g_haudprc.Init.adc_div = audprc_div[index];
              g_haudprc.Init.dac_div = audprc_div[index];

              __HAL_AUDPRC_STB_DIV_CLK(&g_haudprc,
                                       g_haudprc.Init.adc_div,
                                       g_haudprc.Init.dac_div);
            }
          }
        else
          {
            g_haudcodec.Init.adc_cfg.adc_clk =
              (AUDCODE_ADC_CLK_CONFIG_TYPE *)&g_adc_clk_cfg[index];
          }

        audinfo("configure %s rate=%lu index=%d\n",
                priv->playback ? "out" : "in",
                (unsigned long)samplerate, index);
        return OK;

      default:
        return -ENOTTY;
    }
}

/****************************************************************************
 * Name: sf32lb_audio_shutdown
 ****************************************************************************/

static int sf32lb_audio_shutdown(struct audio_lowerhalf_s *dev)
{
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
#  ifdef CONFIG_AUDIO_MULTI_SESSION
  sf32lb_audio_stop(dev, NULL);
#  else
  sf32lb_audio_stop(dev);
#  endif
#endif
  return OK;
}

/****************************************************************************
 * Name: sf32lb_audio_start
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int sf32lb_audio_start(struct audio_lowerhalf_s *dev, void *session)
#else
static int sf32lb_audio_start(struct audio_lowerhalf_s *dev)
#endif
{
  struct sf32lb_audio_s *priv = (struct sf32lb_audio_s *)dev;
  uint32_t size;

  if (priv->running)
    {
      return OK;
    }

  if (!priv->alloc_addr)
    {
      return -EINVAL;
    }

  size = priv->buffer_num * priv->buffer_size;
  priv->xrun = false;

  if (priv->playback)
    {
      AUDCODE_DAC_CLK_CONFIG_TYPE *dac_clk =
        g_haudcodec.Init.dac_cfg.dac_clk;


      if (!dac_clk)
        {
              return -EINVAL;
        }

      sf32lb_audio_pll_config(dac_clk->clk_src_sel, dac_clk->samplerate);

      up_clean_dcache((uintptr_t)priv->alloc_addr,
                      (uintptr_t)priv->alloc_addr + size);

      /* ── AUDPRC path: CPU → AUDPRC TX0 → AUDCODEC DAC ── */

      /* 1. Configure TX0 channel and start DMA. */

      g_haudprc.cfg.mode   = 0;  /* mono */
      g_haudprc.cfg.format = 0;  /* 16-bit */
      g_haudprc.cfg.en     = 1;
      g_haudprc.cfg.dma_mask = 1;

      HAL_AUDPRC_Config_TChanel(&g_haudprc, 0, &g_haudprc.cfg);

      if (HAL_AUDPRC_Transmit_DMA(&g_haudprc, priv->alloc_addr, size,
                                   HAL_AUDPRC_TX_CH0) != HAL_OK)
        {
          return -EIO;
        }

      up_enable_irq(SIFLI_IRQ(AUDPRC_TX0_DMA_IRQ));

      /* 2. Enable AUDPRC (must be AFTER DMA start, matching SDK). */

      __HAL_AUDPRC_ENABLE(&g_haudprc);

      /* 3. Enable AUDCODEC DAC → unmute → PA. */

      __HAL_AUDCODEC_DAC_ENABLE(&g_haudcodec);
      HAL_AUDCODEC_Config_DACPath(&g_haudcodec, 1);
      HAL_AUDCODEC_Config_Analog_DACPath(dac_clk);
      HAL_AUDCODEC_Config_DACPath_Volume(&g_haudcodec, 0, 54);
      HAL_AUDCODEC_Config_DACPath(&g_haudcodec, 0);

      sf32lb_audio_pa_set(1);
    }
  else
    {
      AUDCODE_ADC_CLK_CONFIG_TYPE *adc_clk =
        g_haudcodec.Init.adc_cfg.adc_clk;

      sf32lb_audio_pll_config(adc_clk->clk_src_sel, adc_clk->samplerate);

      HAL_AUDCODEC_Config_RChanel(&g_haudcodec, 0,
                                  &g_haudcodec.Init.adc_cfg);
      HAL_AUDCODEC_Config_ADCPath_Volume(&g_haudcodec, 0, 12);

      if (HAL_AUDCODEC_Receive_DMA(&g_haudcodec, priv->alloc_addr, size,
                                    HAL_AUDCODEC_ADC_CH0) != HAL_OK)
        {
          auderr("Receive_DMA failed\n");
          return -EIO;
        }

      up_enable_irq(SIFLI_IRQ(AUDCODEC_ADC0_DMA_IRQ));

      HAL_AUDCODEC_Config_Analog_ADCPath(adc_clk);
      __HAL_AUDCODEC_ADC_ENABLE(&g_haudcodec);
    }

  priv->running = true;
  return OK;
}

/****************************************************************************
 * Name: sf32lb_audio_stop
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int sf32lb_audio_stop(struct audio_lowerhalf_s *dev, void *session)
#else
static int sf32lb_audio_stop(struct audio_lowerhalf_s *dev)
#endif
{
  struct sf32lb_audio_s *priv = (struct sf32lb_audio_s *)dev;
  struct ap_buffer_s *apb;

  if (priv->running)
    {
      if (priv->playback)
        {
          /* Mute the PA first to avoid an audible pop. */

          sf32lb_audio_pa_set(0);

          /* Stop AUDPRC TX0 DMA (feeds the DAC pipeline). */

          up_disable_irq(SIFLI_IRQ(AUDPRC_TX0_DMA_IRQ));
          HAL_AUDPRC_DMAStop(&g_haudprc, HAL_AUDPRC_TX_CH0);

          g_haudprc.State[HAL_AUDPRC_TX_CH0] =
            HAL_AUDPRC_STATE_READY;

          /* Shut down the AUDCODEC DAC output path. */

          HAL_AUDCODEC_Config_DACPath(&g_haudcodec, 1);
          HAL_AUDCODEC_Close_Analog_DACPath();
          __HAL_AUDCODEC_DAC_DISABLE(&g_haudcodec);
          HAL_AUDCODEC_Clear_All_Channel(&g_haudcodec, 1);
        }
      else
        {
          up_disable_irq(SIFLI_IRQ(AUDCODEC_ADC0_DMA_IRQ));
          HAL_AUDCODEC_DMAStop(&g_haudcodec, HAL_AUDCODEC_ADC_CH0);

          /* HAL DMAStop omits state reset — clear it ourselves so the next
           * Receive_DMA call does not return HAL_BUSY.
           */

          g_haudcodec.State[HAL_AUDCODEC_ADC_CH0] =
            HAL_AUDCODEC_STATE_READY;

          __HAL_AUDCODEC_ADC_DISABLE(&g_haudcodec);
          HAL_AUDCODEC_Close_Analog_ADCPath();
          HAL_AUDCODEC_Clear_All_Channel(&g_haudcodec, 2);
        }

      priv->running = false;
    }

  while (!dq_empty(&priv->pendq))
    {
      apb = (struct ap_buffer_s *)dq_remfirst(&priv->pendq);
#ifdef CONFIG_AUDIO_MULTI_SESSION
      priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK, NULL);
#else
      priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);
#endif
    }

#ifdef CONFIG_AUDIO_MULTI_SESSION
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK, NULL);
#else
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);
#endif

  priv->xrun = false;
  return OK;
}
#endif

/****************************************************************************
 * Name: sf32lb_audio_pause / sf32lb_audio_resume
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int sf32lb_audio_pause(struct audio_lowerhalf_s *dev, void *session)
#else
static int sf32lb_audio_pause(struct audio_lowerhalf_s *dev)
#endif
{
  /* The codec DMA runs in circular mode and cannot be cleanly paused per
   * period; nothing to do here for the bring-up driver.
   */

  return OK;
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int sf32lb_audio_resume(struct audio_lowerhalf_s *dev, void *session)
#else
static int sf32lb_audio_resume(struct audio_lowerhalf_s *dev)
#endif
{
  return OK;
}
#endif

/****************************************************************************
 * Name: sf32lb_audio_allocbuffer
 ****************************************************************************/

static int sf32lb_audio_allocbuffer(struct audio_lowerhalf_s *dev,
                                    struct audio_buf_desc_s *bufdesc)
{
  struct sf32lb_audio_s *priv = (struct sf32lb_audio_s *)dev;
  struct ap_buffer_s *apb;

  if (bufdesc->numbytes != priv->buffer_size)
    {
      return -EINVAL;
    }

  if (priv->alloc_index == priv->buffer_num)
    {
      return -ENOMEM;
    }

  if (!priv->alloc_addr)
    {
      priv->alloc_addr = kumm_memalign(32,
                                       priv->buffer_num * priv->buffer_size);
      if (!priv->alloc_addr)
        {
          return -ENOMEM;
        }
    }

  apb = kumm_zalloc(sizeof(struct ap_buffer_s));
  if (apb == NULL)
    {
      return -ENOMEM;
    }

  *bufdesc->u.pbuffer = apb;

  apb->i.channels = 1;
  apb->crefs      = 1;
  apb->nmaxbytes  = priv->buffer_size;
  apb->samp       = priv->alloc_addr +
                    priv->alloc_index * priv->buffer_size;
  priv->alloc_index++;
  nxmutex_init(&apb->lock);

  return sizeof(struct audio_buf_desc_s);
}

/****************************************************************************
 * Name: sf32lb_audio_freebuffer
 ****************************************************************************/

static int sf32lb_audio_freebuffer(struct audio_lowerhalf_s *dev,
                                   struct audio_buf_desc_s *bufdesc)
{
  struct sf32lb_audio_s *priv = (struct sf32lb_audio_s *)dev;
  struct ap_buffer_s *apb;

  apb = bufdesc->u.buffer;
  priv->alloc_index--;
  nxmutex_destroy(&apb->lock);
  kumm_free(apb);

  if (priv->alloc_index == 0)
    {
      kumm_free(priv->alloc_addr);
      priv->alloc_addr = NULL;
    }

  return sizeof(struct audio_buf_desc_s);
}

/****************************************************************************
 * Name: sf32lb_audio_enqueuebuffer
 ****************************************************************************/

static int sf32lb_audio_enqueuebuffer(struct audio_lowerhalf_s *dev,
                                      struct ap_buffer_s *apb)
{
  struct sf32lb_audio_s *priv = (struct sf32lb_audio_s *)dev;
  irqstate_t flags;

  if (priv->playback)
    {
      up_clean_dcache((uintptr_t)apb->samp,
                      (uintptr_t)apb->samp + apb->nbytes);
    }

  apb->flags |= AUDIO_APB_OUTPUT_ENQUEUED;

  flags = spin_lock_irqsave(&priv->lock);
  dq_addlast(&apb->dq_entry, &priv->pendq);
  priv->xrun = false;
  spin_unlock_irqrestore(&priv->lock, flags);

  return OK;
}

/****************************************************************************
 * Name: sf32lb_audio_ioctl
 ****************************************************************************/

static int sf32lb_audio_ioctl(struct audio_lowerhalf_s *dev, int cmd,
                              unsigned long arg)
{
  struct sf32lb_audio_s *priv = (struct sf32lb_audio_s *)dev;
  struct ap_buffer_info_s *bufinfo;

  switch (cmd)
    {
      case AUDIOIOC_GETBUFFERINFO:
        bufinfo              = (struct ap_buffer_info_s *)arg;
        bufinfo->buffer_size = priv->buffer_size;
        bufinfo->nbuffers    = priv->buffer_num;
        return OK;

      case AUDIOIOC_SETBUFFERINFO:
        bufinfo = (struct ap_buffer_info_s *)arg;

        /* buffer_num is fixed at two to match the HAL double-buffer
         * interrupt model; only honour a new buffer size.
         */

        priv->buffer_size = bufinfo->buffer_size;
        kumm_free(priv->alloc_addr);
        priv->alloc_addr  = NULL;
        priv->alloc_index = 0;
        return OK;

      default:
        return -ENOTTY;
    }
}

/****************************************************************************
 * Name: sf32lb_audio_reserve / sf32lb_audio_release
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int sf32lb_audio_reserve(struct audio_lowerhalf_s *dev,
                                void **session)
#else
static int sf32lb_audio_reserve(struct audio_lowerhalf_s *dev)
#endif
{
  return OK;
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int sf32lb_audio_release(struct audio_lowerhalf_s *dev, void *session)
#else
static int sf32lb_audio_release(struct audio_lowerhalf_s *dev)
#endif
{
  return OK;
}

/****************************************************************************
 * Name: sf32lb_audio_period_done
 *
 * Description:
 *   Common per-period handler invoked from the HAL DMA half/complete
 *   callbacks.  Dequeues the oldest in-flight buffer and hands it back to
 *   the upper half.
 *
 ****************************************************************************/

static void sf32lb_audio_period_done(struct sf32lb_audio_s *priv)
{
  struct ap_buffer_s *apb;
  bool final = false;
  irqstate_t flags;

  if (priv == NULL)
    {
      return;
    }

  flags = spin_lock_irqsave(&priv->lock);
  apb = (struct ap_buffer_s *)dq_remfirst(&priv->pendq);
  if (apb == NULL)
    {
      priv->xrun = true;
    }

  spin_unlock_irqrestore(&priv->lock, flags);

  if (apb == NULL)
    {
      /* Under/overrun: the circular DMA keeps running over the existing
       * buffers until the upper half catches up.
       */

      return;
    }

  if (!priv->playback)
    {
      /* The codec filled this period; expose the full buffer. */

      apb->nbytes = priv->buffer_size;
      up_invalidate_dcache((uintptr_t)apb->samp,
                           (uintptr_t)apb->samp + apb->nbytes);
    }

  if ((apb->flags & AUDIO_APB_FINAL) != 0)
    {
      final = true;
    }

#ifdef CONFIG_AUDIO_MULTI_SESSION
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK, NULL);
#else
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);
#endif

  if (final)
    {
#ifdef CONFIG_AUDIO_MULTI_SESSION
      sf32lb_audio_stop(&priv->dev, NULL);
#else
      sf32lb_audio_stop(&priv->dev);
#endif
    }
}

/****************************************************************************
 * Name: HAL DMA interrupt service routines
 ****************************************************************************/

int sf32lb_audio_audprc_tx0_dma_isr(int irq, void *context, void *arg)
{
  HAL_DMA_IRQHandler(g_haudprc.hdma[HAL_AUDPRC_TX_CH0]);
  return OK;
}

int sf32lb_audio_adc0_dma_isr(int irq, void *context, void *arg)
{
  HAL_DMA_IRQHandler(g_haudcodec.hdma[HAL_AUDCODEC_ADC_CH0]);
  return OK;
}

/****************************************************************************
 * Name: HAL weak callback overrides (AUDPRC TX — playback)
 ****************************************************************************/

void HAL_AUDPRC_TxHalfCpltCallback(AUDPRC_HandleTypeDef *haprc, int cid)
{
  (void)cid;
  sf32lb_audio_period_done(g_playback_priv);
}

void HAL_AUDPRC_TxCpltCallback(AUDPRC_HandleTypeDef *haprc, int cid)
{
  (void)cid;
  sf32lb_audio_period_done(g_playback_priv);
}

/****************************************************************************
 * Name: HAL weak callback overrides (AUDCODEC RX — recording)
 ****************************************************************************/

void HAL_AUDCODEC_RxHalfCpltCallback(AUDCODEC_HandleTypeDef *hacodec, int cid)
{
  (void)cid;
  sf32lb_audio_period_done(g_capture_priv);
}

void HAL_AUDCODEC_RxCpltCallback(AUDCODEC_HandleTypeDef *hacodec, int cid)
{
  (void)cid;
  sf32lb_audio_period_done(g_capture_priv);
}

/****************************************************************************
 * Name: sf32lb_audio_create
 ****************************************************************************/

static struct sf32lb_audio_s *sf32lb_audio_create(bool playback)
{
  struct sf32lb_audio_s *priv;

  priv = kmm_zalloc(sizeof(struct sf32lb_audio_s));
  if (priv == NULL)
    {
      return NULL;
    }

  priv->dev.ops     = &g_sf32lb_audio_ops;
  priv->playback    = playback;
  priv->did         = playback ? HAL_AUDCODEC_DAC_CH0 : HAL_AUDCODEC_ADC_CH0;
  priv->buffer_size = SF32LB_AUDIO_BUFFER_SIZE;
  priv->buffer_num  = SF32LB_AUDIO_BUFFER_NUM;
  dq_init(&priv->pendq);
  spin_lock_init(&priv->lock);

  if (priv->playback)
    g_playback_priv = priv;
  else
    g_capture_priv = priv;

  return priv;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sf32lb_audio_initialize
 *
 * Description:
 *   Initialise the SF32LB52 codec and register the playback (/dev/audio/
 *   pcm0p) and capture (/dev/audio/pcm0c) devices.
 *
 ****************************************************************************/

int sf32lb_audio_initialize(void)
{
  struct sf32lb_audio_s *play;
  struct sf32lb_audio_s *capture;
  int ret;

  ret = sf32lb_audio_hw_init();
  if (ret < 0)
    {
      return ret;
    }

  play = sf32lb_audio_create(true);
  if (play == NULL)
    {
      return -ENOMEM;
    }

  capture = sf32lb_audio_create(false);
  if (capture == NULL)
    {
      kmm_free(play);
      return -ENOMEM;
    }

  ret = audio_register("pcm0p", &play->dev);
  if (ret < 0)
    {
      auderr("failed to register pcm0p: %d\n", ret);
      goto err;
    }

  ret = audio_register("pcm0c", &capture->dev);
  if (ret < 0)
    {
      auderr("failed to register pcm0c: %d\n", ret);
      goto err;
    }

  return OK;

err:
  g_playback_priv = NULL;
  g_capture_priv = NULL;
  kmm_free(play);
  kmm_free(capture);
  return ret;
}
