/****************************************************************************
 * vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/src/sf32lb_audio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * NuttX audio lower-half driver for the SF32LB52 onboard audio:
 *   AUDCODEC DAC → PA42 Class-D PA → speaker connector.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include "sfconfig.h"
#include "dma_config.h"

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/audio/audio.h>
#include <nuttx/kmalloc.h>
#include <nuttx/irq.h>

#include "bf0_hal.h"
#include "bf0_hal_audcodec.h"
#include "bf0_hal_dma.h"
#include "drv_io.h"
#include "sifli_gpio.h"

/* PA42 = AUDIO_PA_CTRL on Huangshan Pi */

#define AUDIO_PA_PIN        GET_PIN_2(hwp_gpio1, 42)

/* DAC channel index used for playback */

#define AUDIO_DAC_CHANNEL   HAL_AUDCODEC_DAC_CH0

/* DMA channel for DAC TX -- must match dma_config.h or be defined here */

#ifndef AUDCODEC_DAC0_DMA_INSTANCE
#  define AUDCODEC_DAC0_DMA_INSTANCE  DMA2_Channel1
#endif

#ifndef AUDCODEC_DAC0_DMA_IRQ
#  define AUDCODEC_DAC0_DMA_IRQ       DMAC2_CH1_IRQn
#endif

/* Default audio parameters */

#define AUDIO_DEFAULT_RATE    16000
#define AUDIO_DEFAULT_CHANNELS 1
#define AUDIO_DEFAULT_BPS     16

/* Number of ping-pong DMA buffers */

#define AUDIO_NUM_BUFFERS     4
#define AUDIO_BUFFER_SAMPLES  1024
#define AUDIO_BUFFER_SIZE     (AUDIO_BUFFER_SAMPLES * sizeof(uint16_t))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sf32lb_audio_s
{
  struct audio_lowerhalf_s lower;

  /* SiFli HAL handles */

  AUDCODEC_HandleTypeDef  codec;
  DMA_HandleTypeDef       dma_tx;
  AUDCODE_DAC_CLK_CONFIG_TYPE dac_clk_cfg;

  /* State */

  bool                    playing;
  uint32_t                sample_rate;
  uint8_t                 channels;
  uint8_t                 bps;

  /* DMA ping-pong buffers (must be word-aligned for DMA) */

  uint32_t                dma_buf[AUDIO_NUM_BUFFERS]
                                 [AUDIO_BUFFER_SIZE / sizeof(uint32_t)];
  uint8_t                 buf_head;
  uint8_t                 buf_tail;
  uint8_t                 buf_count;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  sf32lb_audio_getcaps(FAR struct audio_lowerhalf_s *dev,
                                 int type, FAR struct audio_caps_s *caps);
static int  sf32lb_audio_configure(FAR struct audio_lowerhalf_s *dev,
                                   FAR const struct audio_caps_s *caps);
static int  sf32lb_audio_shutdown(FAR struct audio_lowerhalf_s *dev);
static int  sf32lb_audio_start(FAR struct audio_lowerhalf_s *dev);
static int  sf32lb_audio_stop(FAR struct audio_lowerhalf_s *dev);
static int  sf32lb_audio_allocbuffer(FAR struct audio_lowerhalf_s *dev,
                                     FAR struct audio_buf_desc_s *bufdesc);
static int  sf32lb_audio_freebuffer(FAR struct audio_lowerhalf_s *dev,
                                    FAR struct audio_buf_desc_s *bufdesc);
static int  sf32lb_audio_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                       FAR struct ap_buffer_s *apb);
static int  sf32lb_audio_ioctl(FAR struct audio_lowerhalf_s *dev,
                               int cmd, unsigned long arg);
static ssize_t sf32lb_audio_write(FAR struct audio_lowerhalf_s *dev,
                                  FAR const char *buffer, size_t buflen);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct audio_ops_s g_sf32lb_audio_ops =
{
  sf32lb_audio_getcaps,       /* getcaps        */
  sf32lb_audio_configure,     /* configure      */
  sf32lb_audio_shutdown,      /* shutdown       */
  sf32lb_audio_start,         /* start          */
  sf32lb_audio_stop,          /* stop           */
  NULL,                       /* pause          */
  NULL,                       /* resume         */
  sf32lb_audio_allocbuffer,   /* allocbuffer    */
  sf32lb_audio_freebuffer,    /* freebuffer     */
  sf32lb_audio_enqueuebuffer, /* enqueue_buffer */
  NULL,                       /* cancel_buffer  */
  sf32lb_audio_ioctl,         /* ioctl          */
  NULL,                       /* read           */
  sf32lb_audio_write,         /* write          */
  NULL,                       /* reserve        */
  NULL                        /* release        */
};

/****************************************************************************
 * Private Functions -- HAL callbacks (weak symbols from bf0_hal_audcodec.c)
 ****************************************************************************/

void HAL_AUDCODEC_TxCpltCallback(AUDCODEC_HandleTypeDef *hacodec, int cid)
{
  /* Nothing to do -- the upper-half handles buffer dequeue via our
   * enqueuebuffer callback mechanism.
   */

  UNUSED(hacodec);
  UNUSED(cid);
}

void HAL_AUDCODEC_TxHalfCpltCallback(AUDCODEC_HandleTypeDef *hacodec,
                                     int cid)
{
  UNUSED(hacodec);
  UNUSED(cid);
}

/****************************************************************************
 * Private Functions -- PA control
 ****************************************************************************/

static void audio_pa_enable(void)
{
  sifli_gpio_config(AUDIO_PA_PIN, GPIO_OUTPUT);
  sifli_gpio_write(AUDIO_PA_PIN, true);
}

static void audio_pa_disable(void)
{
  sifli_gpio_write(AUDIO_PA_PIN, false);
}

/****************************************************************************
 * Private Functions -- codec init / deinit
 ****************************************************************************/

static int audio_codec_init(FAR struct sf32lb_audio_s *priv)
{
  AUDCODEC_HandleTypeDef *hc = &priv->codec;
  int ret;

  memset(hc, 0, sizeof(*hc));
  hc->Instance = (AUDCODEC_TypeDef *)AUDCODEC_BASE;

  /* Enable AUDCODEC and AUDPRC module clocks via HPSYS_RCC_ENR2 */

  hwp_hpsys_rcc->ENR2 |= HPSYS_RCC_ENR2_AUDCODEC | HPSYS_RCC_ENR2_AUDPRC;
  usleep(1000);

  /* Configure DAC clock for the requested sample rate */

  memset(&priv->dac_clk_cfg, 0, sizeof(priv->dac_clk_cfg));
  priv->dac_clk_cfg.samplerate = priv->sample_rate;

  /* Select clock source and divider based on sample rate */

  if (priv->sample_rate == 44100 || priv->sample_rate == 22050)
    {
      priv->dac_clk_cfg.clk_src_sel = 1;  /* PLL 44.1MHz */
    }
  else
    {
      priv->dac_clk_cfg.clk_src_sel = 0;  /* XTAL 48MHz */
    }

  priv->dac_clk_cfg.osr_sel = 4;  /* OSR=64 for good quality */
  priv->dac_clk_cfg.sinc_gain = 0;
  priv->dac_clk_cfg.diva_clk_dac = 0;
  priv->dac_clk_cfg.diva_clk_chop_dac = 0;
  priv->dac_clk_cfg.divb_clk_chop_dac = 0;
  priv->dac_clk_cfg.diva_clk_chop_bg = 0;
  priv->dac_clk_cfg.diva_clk_chop_refgen = 0;
#ifdef SF32LB52X
  priv->dac_clk_cfg.sel_clk_dac = 0;
#endif

  hc->Init.samplerate_index = 0;
  hc->Init.dac_cfg.opmode = 0;  /* audprc tx to audcode */
  hc->Init.dac_cfg.dac_clk = &priv->dac_clk_cfg;

  /* Initialize the codec */

  syslog(LOG_INFO, "AUDIO: HAL_AUDCODEC_Init start, sample_rate=%lu\n",
         (unsigned long)priv->sample_rate);
  ret = HAL_AUDCODEC_Init(hc);
  syslog(LOG_INFO, "AUDIO: HAL_AUDCODEC_Init returned: %d\n", ret);
  if (ret != HAL_OK)
    {
      auderr("ERROR: HAL_AUDCODEC_Init failed: %d\n", ret);
      return -EIO;
    }

  /* Enable PLL for DAC clock */

  syslog(LOG_INFO, "AUDIO: Enabling PLL for %lu Hz\n",
         (unsigned long)priv->sample_rate);
  ret = bf0_enable_pll(priv->sample_rate, 0);
  syslog(LOG_INFO, "AUDIO: bf0_enable_pll returned: %d\n", ret);

  /* Configure analog DAC path (clock routing, enable bits) */

  ret = HAL_AUDCODEC_Config_Analog_DACPath(&priv->dac_clk_cfg);
  syslog(LOG_INFO, "AUDIO: Config_Analog_DACPath returned: %d\n", ret);

  /* Configure DAC channel 0 */

  ret = HAL_AUDCODEC_Config_TChanel(hc, 0, &hc->Init.dac_cfg);
  syslog(LOG_INFO, "AUDIO: HAL_AUDCODEC_Config_TChanel returned: %d\n", ret);
  if (ret != HAL_OK)
    {
      auderr("ERROR: HAL_AUDCODEC_Config_TChanel failed: %d\n", ret);
      return -EIO;
    }

  /* Unmute DAC path */

  HAL_AUDCODEC_Mute_DACPath(hc, 0);

  /* Enable the overall DAC in CFG register */

  hc->Instance->CFG |= AUDCODEC_CFG_DAC_ENABLE;
  syslog(LOG_INFO, "AUDIO: DAC enabled in CFG register\n");
  syslog(LOG_INFO, "AUDIO: DAC path unmuted\n");

  return OK;
}

static void audio_codec_deinit(FAR struct sf32lb_audio_s *priv)
{
  AUDCODEC_HandleTypeDef *hc = &priv->codec;

  HAL_AUDCODEC_Clear_All_Channel(hc, 0x1);  /* disable DAC channels */
  HAL_AUDCODEC_DeInit(hc);
}

/****************************************************************************
 * Private Functions -- DMA
 ****************************************************************************/

static int audio_dma_init(FAR struct sf32lb_audio_s *priv)
{
  /* DMA not used - write directly to DAC FIFO */

  UNUSED(priv);
  return OK;
}

static void audio_dma_deinit(FAR struct sf32lb_audio_s *priv)
{
  /* DMA not used - nothing to deinit */
  UNUSED(priv);
}

/****************************************************************************
 * NuttX audio lower-half callbacks
 ****************************************************************************/

static int sf32lb_audio_getcaps(FAR struct audio_lowerhalf_s *dev,
                                int type, FAR struct audio_caps_s *caps)
{
  DEBUGASSERT(caps && caps->ac_len >= sizeof(struct audio_caps_s));

  caps->ac_format.hw = 0;
  caps->ac_controls.w = 0;

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_QUERY:
        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            caps->ac_controls.b[0] = AUDIO_TYPE_OUTPUT;
            caps->ac_format.hw = 1 << (AUDIO_FMT_PCM - 1);
          }
        break;

      case AUDIO_TYPE_OUTPUT:
        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            caps->ac_controls.hw[0] = AUDIO_SAMP_RATE_DEF_ALL;
            caps->ac_channels = 2;
          }
        break;

      default:
        break;
    }

  return caps->ac_len;
}

/****************************************************************************
 * Write raw PCM data to the DAC via DMA
 ****************************************************************************/

static ssize_t sf32lb_audio_write(FAR struct audio_lowerhalf_s *dev,
                                  FAR const char *buffer, size_t buflen)
{
  FAR struct sf32lb_audio_s *priv = (FAR struct sf32lb_audio_s *)dev;
  AUDCODEC_HandleTypeDef *hc = &priv->codec;
  int16_t *samples = (int16_t *)buffer;
  size_t nsamples = buflen / sizeof(int16_t);
  size_t i;

  if (!priv->playing || buffer == NULL || buflen == 0)
    {
      return -EINVAL;
    }

  /* Write samples directly to DAC FIFO */

  for (i = 0; i < nsamples; i++)
    {
      /* Write 16-bit sample to DAC entry register */

#ifdef SF32LB52X
      hc->Instance->DAC_CH0_ENTRY = (uint32_t)(uint16_t)samples[i];
#else
      hc->Instance_hp->DAC_CH0_ENTRY = (uint32_t)(uint16_t)samples[i];
#endif

      /* Pace output to match sample rate */

      usleep(1000000 / priv->sample_rate);
    }

  return buflen;
}

static int sf32lb_audio_configure(FAR struct audio_lowerhalf_s *dev,
                                  FAR const struct audio_caps_s *caps)
{
  FAR struct sf32lb_audio_s *priv = (FAR struct sf32lb_audio_s *)dev;

  DEBUGASSERT(caps);

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_OUTPUT:
        priv->sample_rate = caps->ac_controls.hw[0] |
                            (caps->ac_controls.b[3] << 16);
        priv->channels = caps->ac_channels;
        priv->bps = caps->ac_controls.b[2];

        audinfo("configure: rate=%lu ch=%u bps=%u\n",
                (unsigned long)priv->sample_rate,
                priv->channels, priv->bps);
        break;

      default:
        break;
    }

  return OK;
}

static int sf32lb_audio_shutdown(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct sf32lb_audio_s *priv = (FAR struct sf32lb_audio_s *)dev;

  if (priv->playing)
    {
      sf32lb_audio_stop(dev);
    }

  audio_pa_disable();
  audio_codec_deinit(priv);
  return OK;
}

static int sf32lb_audio_start(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct sf32lb_audio_s *priv = (FAR struct sf32lb_audio_s *)dev;
  int ret;

  if (priv->playing)
    {
      syslog(LOG_INFO, "AUDIO: start - already playing\n");
      return OK;
    }

  syslog(LOG_INFO, "AUDIO: start - initializing codec...\n");

  /* Initialize codec */

  ret = audio_codec_init(priv);
  syslog(LOG_INFO, "AUDIO: codec_init returned: %d\n", ret);
  if (ret < 0)
    {
      return ret;
    }

  /* Initialize DMA */

  syslog(LOG_INFO, "AUDIO: start - initializing DMA...\n");
  ret = audio_dma_init(priv);
  syslog(LOG_INFO, "AUDIO: dma_init returned: %d\n", ret);
  if (ret < 0)
    {
      audio_codec_deinit(priv);
      return ret;
    }

  /* Enable the Class-D PA */

  syslog(LOG_INFO, "AUDIO: start - enabling PA42\n");
  audio_pa_enable();

  priv->playing = true;
  audinfo("audio started: rate=%lu\n", (unsigned long)priv->sample_rate);
  return OK;
}

static int sf32lb_audio_stop(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct sf32lb_audio_s *priv = (FAR struct sf32lb_audio_s *)dev;

  if (!priv->playing)
    {
      return OK;
    }

  /* Stop DMA first, then disable PA to avoid pop */

  audio_dma_deinit(priv);
  audio_pa_disable();
  audio_codec_deinit(priv);

  priv->playing = false;
  audinfo("audio stopped\n");
  return OK;
}

static int sf32lb_audio_allocbuffer(FAR struct audio_lowerhalf_s *dev,
                                    FAR struct audio_buf_desc_s *bufdesc)
{
  FAR struct ap_buffer_s *apb;

  DEBUGASSERT(bufdesc && bufdesc->u.pbuffer);

  apb = kmm_zalloc(sizeof(*apb) + bufdesc->numbytes);
  if (apb == NULL)
    {
      return -ENOMEM;
    }

  apb->samp = (FAR uint8_t *)(apb + 1);
  apb->nmaxbytes = bufdesc->numbytes;
  bufdesc->u.buffer = apb;

  return sizeof(*apb) + bufdesc->numbytes;
}

static int sf32lb_audio_freebuffer(FAR struct audio_lowerhalf_s *dev,
                                   FAR struct audio_buf_desc_s *bufdesc)
{
  DEBUGASSERT(bufdesc && bufdesc->u.buffer);
  kmm_free(bufdesc->u.buffer);
  return OK;
}

static int sf32lb_audio_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                      FAR struct ap_buffer_s *apb)
{
  FAR struct sf32lb_audio_s *priv = (FAR struct sf32lb_audio_s *)dev;
  AUDCODEC_HandleTypeDef *hc = &priv->codec;
  uint32_t *data = (uint32_t *)apb->samp;
  uint32_t size_words = apb->nbytes / sizeof(uint32_t);

  DEBUGASSERT(apb && apb->samp);

  if (!priv->playing)
    {
      return -EPIPE;
    }

  /* Start DMA transmission from this buffer */

  HAL_AUDCODEC_Transmit_DMA(hc, (uint8_t *)data, apb->nbytes,
                            AUDIO_DAC_CHANNEL);

  /* Notify upper-half that the buffer has been consumed */

  if (dev->upper)
    {
      dev->upper(dev->priv, AUDIO_CALLBACK_DEQUEUE, apb, 0);
    }

  return OK;
}

static int sf32lb_audio_ioctl(FAR struct audio_lowerhalf_s *dev,
                              int cmd, unsigned long arg)
{
  int ret = -ENOTTY;

  switch (cmd)
    {
      case AUDIOIOC_SETBUFFERINFO:
        {
          /* Accept buffer info from upper-half */

          ret = OK;
        }
        break;

      default:
        break;
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct audio_lowerhalf_s *sf32lb_audio_initialize(void)
{
  FAR struct sf32lb_audio_s *priv;

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      auderr("ERROR: failed to allocate audio driver\n");
      return NULL;
    }

  priv->lower.ops = &g_sf32lb_audio_ops;
  priv->sample_rate = AUDIO_DEFAULT_RATE;
  priv->channels = AUDIO_DEFAULT_CHANNELS;
  priv->bps = AUDIO_DEFAULT_BPS;

  audinfo("SF32LB audio driver initialized\n");
  return &priv->lower;
}
