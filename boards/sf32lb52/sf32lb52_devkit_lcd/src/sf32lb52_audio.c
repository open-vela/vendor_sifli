/****************************************************************************
 * boards/sf32lb52/sf32lb52_devkit_lcd/src/sf32lb52_audio.c
 *
 * SF32LB52 audio device driver (audio_lowerhalf implementation)
 *
 * Architecture (see the SiFli SDK audprc examples; verified on the RT-Thread side):
 *   - AUDCODEC: analog front end (DAC/ADC analog path, no DMA)
 *   - AUDPRC  : digital audio processor, data path (TX0=playback DMA / RX0=recording DMA)
 *
 * Data flow:
 *   Playback: memory -> AUDPRC TX0 DMA -> AUDPRC -> codec DAC analog -> AW8155 power amplifier -> speaker
 *   Recording: microphone -> codec ADC analog -> AUDPRC -> AUDPRC RX0 DMA -> memory
 *
 * Registered as the NuttX audio device /dev/audio0.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/ioctl.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>
#include <syslog.h>

#include <nuttx/kmalloc.h>
#include <nuttx/audio/audio.h>
#include <nuttx/semaphore.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>

#include "bf0_hal_audcodec.h"
#include "bf0_hal_audprc.h"
#include "bf0_hal_pmu.h"
#include "bf0_hal_rcc.h"
#include "bf0_hal_gpio.h"
#include "register.h"
#include "dma_config.h"
#include "sf32lb52_audio.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SF32LB52_AUDIO_CLK_TAB_NUM  9

/* Channel selection: single channel (DAC0 playback / ADC0 recording; AUDPRC TX0 / RX0)
 * Note: the codec Config_TChanel/RChanel take channel indexes (0/1),
 * not the HAL_AUDCODEC_*_CHx enums (DAC_CH0=0, ADC_CH0=2) */

#define SF32LB52_AUDIO_DAC_CH       0
#define SF32LB52_AUDIO_ADC_CH       0
#define SF32LB52_AUDIO_PRC_TX_CH    HAL_AUDPRC_TX_CH0
#define SF32LB52_AUDIO_PRC_RX_CH    HAL_AUDPRC_RX_CH0

/* Power amplifier enable GPIO: PA10 = AU_PA_EN (configured as GPIO by bsp_pinmux) */

#define SF32LB52_AUDIO_PA_GPIO      ((GPIO_TypeDef *)hwp_gpio1)
#define SF32LB52_AUDIO_PA_PIN       10

/* AUDPRC path selection (MUX/MIX sources); see the SDK audprc example, out_sel=0x5050 */

#define SF32LB52_AUDIO_OUT_SEL      0x5050

/* Default volume (dB) */

#define SF32LB52_AUDIO_DEFAULT_VOL  (-18)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sf32lb52_audio_s
{
  struct audio_lowerhalf_s dev;         /* NuttX audio lower-half device */
  AUDCODEC_HandleTypeDef  codec;        /* audio codec (analog front end) */
  AUDPRC_HandleTypeDef    aprc;         /* audio processor (data-path DMA) */
  bool                    running;      /* whether the device is running */
  bool                    playback;     /* current direction: true=playback false=recording */
  int                     samplerate;   /* current sample rate */
  int                     nchannels;    /* current channel count */
  int                     bpsamp;       /* current bits per sample */
  FAR struct ap_buffer_s *tx_apb;       /* buffer being played (queued mode) */
  FAR struct ap_buffer_s *rx_apb;       /* buffer being recorded */
  sem_t                   wr_sem;       /* write() synchronization semaphore */
  bool                    wr_busy;      /* write() DMA in progress */
  sem_t                   rx_sem;       /* read() synchronization semaphore */
  bool                    rx_busy;      /* read() DMA in progress */
  int                     irq_attach_ret; /* irq_attach return value (debug) */
  volatile int            irq_count;    /* DMA interrupt trigger count (debug) */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  sf32lb52_audio_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                                   FAR struct audio_caps_s *caps);
static int  sf32lb52_audio_configure(FAR struct audio_lowerhalf_s *dev,
                                     FAR const struct audio_caps_s *caps);
static int  sf32lb52_audio_shutdown(FAR struct audio_lowerhalf_s *dev);
static int  sf32lb52_audio_start(FAR struct audio_lowerhalf_s *dev);
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
static int  sf32lb52_audio_stop(FAR struct audio_lowerhalf_s *dev);
#endif
static int  sf32lb52_audio_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                         FAR struct ap_buffer_s *apb);
static int  sf32lb52_audio_cancelbuffer(FAR struct audio_lowerhalf_s *dev,
                                        FAR struct ap_buffer_s *apb);
static int  sf32lb52_audio_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                                 unsigned long arg);
static ssize_t sf32lb52_audio_write(FAR struct audio_lowerhalf_s *dev,
                                    FAR const char *buffer, size_t buflen);
static ssize_t sf32lb52_audio_read(FAR struct audio_lowerhalf_s *dev,
                                   FAR char *buffer, size_t buflen);
static int  sf32lb52_audio_reserve(FAR struct audio_lowerhalf_s *dev);
static int  sf32lb52_audio_release(FAR struct audio_lowerhalf_s *dev);

static int  sf32lb52_audio_hw_init(FAR struct sf32lb52_audio_s *priv);
static int  sf32lb52_audio_hw_configure(FAR struct sf32lb52_audio_s *priv,
                                        int samplerate, int nchannels,
                                        int bpsamp);
static int  sf32lb52_audio_hw_start(FAR struct sf32lb52_audio_s *priv,
                                    bool playback);
static int  sf32lb52_audio_hw_stop(FAR struct sf32lb52_audio_s *priv);
static void sf32lb52_audio_pa_enable(bool enable);
static void sf32lb52_audio_tx_complete(FAR struct sf32lb52_audio_s *priv);
static void sf32lb52_audio_rx_complete(FAR struct sf32lb52_audio_s *priv);
static int  sf32lb52_audio_dma1_irq(int irq, FAR void *context,
                                    FAR void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* SF32LB52X codec clock configuration table (sample rate -> internal PLL/divider parameters; see SDK) */

static const AUDCODE_DAC_CLK_CONFIG_TYPE codec_dac_clk_config[SF32LB52_AUDIO_CLK_TAB_NUM] =
{
  {48000, 0, 1, 0, 0x14D, 0,  5, 4, 2, 20, 20, 0},
  {32000, 0, 1, 1, 0x14D, 0,  5, 4, 2, 20, 20, 0},
  {24000, 0, 1, 5, 0x14D, 0, 10, 2, 2, 10, 10, 1},
  {16000, 0, 1, 4, 0x14D, 0,  5, 4, 2, 20, 20, 0},
  {12000, 0, 1, 7, 0x14D, 0, 20, 2, 1,  5,  5, 1},
  { 8000, 0, 1, 8, 0x14D, 0, 10, 2, 2, 10, 10, 1},
  {44100, 1, 1, 0, 0x14D, 1,  5, 4, 2, 20, 20, 0},
  {22050, 1, 1, 5, 0x14D, 1, 10, 2, 2, 10, 10, 1},
  {11025, 1, 1, 7, 0x14D, 1, 20, 2, 1,  5,  5, 1},
};

static const AUDCODE_ADC_CLK_CONFIG_TYPE codec_adc_clk_config[SF32LB52_AUDIO_CLK_TAB_NUM] =
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

static const struct audio_ops_s g_sf32lb52_audio_ops =
{
  sf32lb52_audio_getcaps,       /* getcaps        */
  sf32lb52_audio_configure,     /* configure      */
  sf32lb52_audio_shutdown,      /* shutdown       */
  sf32lb52_audio_start,         /* start          */
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  sf32lb52_audio_stop,          /* stop           */
#endif
  NULL,                         /* pause          */
  NULL,                         /* resume         */
  NULL,                         /* allocbuffer    */
  NULL,                         /* freebuffer     */
  sf32lb52_audio_enqueuebuffer, /* enqueue_buffer */
  sf32lb52_audio_cancelbuffer,  /* cancel_buffer  */
  sf32lb52_audio_ioctl,         /* ioctl          */
  sf32lb52_audio_read,          /* read           */
  sf32lb52_audio_write,         /* write          */
  sf32lb52_audio_reserve,       /* reserve        */
  sf32lb52_audio_release        /* release        */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sf32lb52_audio_pa_enable
 *
 * Description: Controls the board power amplifier (AW8155, PA10 = AU_PA_EN)
 *
 ****************************************************************************/

static void sf32lb52_audio_pa_enable(bool enable)
{
  HAL_GPIO_WritePin(SF32LB52_AUDIO_PA_GPIO, SF32LB52_AUDIO_PA_PIN,
                    enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/****************************************************************************
 * Name: sf32lb52_audio_tx_complete / sf32lb52_audio_rx_complete
 *
 * Description:
 *   DMA completion processing (invoked by the HAL in the DMA interrupt; see the HAL_AUDPRC_*
 *   CpltCallback overrides at the bottom of the file), returning the buffer to the NuttX upper layer.
 *
 ****************************************************************************/

static void sf32lb52_audio_tx_complete(FAR struct sf32lb52_audio_s *priv)
{
  FAR struct ap_buffer_s *apb = priv->tx_apb;
  bool final;

  priv->tx_apb = NULL;

  if (apb == NULL)
    {
      return;
    }

  final = (apb->flags & AUDIO_APB_FINAL) != 0;

  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);
  if (final)
    {
      priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);
    }
}

static void sf32lb52_audio_rx_complete(FAR struct sf32lb52_audio_s *priv)
{
  FAR struct ap_buffer_s *apb = priv->rx_apb;
  bool final;

  priv->rx_apb = NULL;

  if (apb == NULL)
    {
      return;
    }

  final = (apb->flags & AUDIO_APB_FINAL) != 0;

  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);
  if (final)
    {
      priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);
    }
}

/****************************************************************************
 * Name: sf32lb52_audio_dma1_irq
 *
 * Description:
 *   DMA1 channel-interrupt unified entry (forwards the NuttX IRQ into the HAL channel pool for handling, and
 *   HAL invokes the matching XferCpltCallback -> this driver's Tx/RxCpltCallback)
 *
 ****************************************************************************/

static int sf32lb52_audio_dma1_irq(int irq, FAR void *context,
                                   FAR void *arg)
{
  int idx = irq - NVIC_IRQ_FIRST - DMAC1_CH1_IRQn;

  switch (idx)
    {
      case 0:  HAL_DMAC1_CH1_IRQHandler();  break;
      case 1:  HAL_DMAC1_CH2_IRQHandler();  break;
      case 2:  HAL_DMAC1_CH3_IRQHandler();  break;
      case 3:  HAL_DMAC1_CH4_IRQHandler();  break;
      case 4:  HAL_DMAC1_CH5_IRQHandler();  break;
      case 5:  HAL_DMAC1_CH6_IRQHandler();  break;
      case 6:  HAL_DMAC1_CH7_IRQHandler();  break;
      case 7:  HAL_DMAC1_CH8_IRQHandler();  break;
      default: break;
    }

  return OK;
}

/****************************************************************************
 * Name: sf32lb52_audio_hw_init
 *
 * Description:
 *   codec + AUDPRC hardware initialization:
 *   power/clock enable -> HAL Init -> AUDPRC TX/RX DMA handle configuration -> DMA interrupt registration
 *
 ****************************************************************************/

static int sf32lb52_audio_hw_init(FAR struct sf32lb52_audio_s *priv)
{
  HAL_StatusTypeDef res;
  int i;

  memset(&priv->codec, 0, sizeof(priv->codec));
  memset(&priv->aprc, 0, sizeof(priv->aprc));

  /* Register base addresses (the HAL init accesses Instance directly) */

  priv->codec.Instance = (AUDCODEC_TypeDef *)AUDCODEC_BASE;
  priv->aprc.Instance  = (AUDPRC_TypeDef *)AUDPRC_BASE;

  /* Enable the audio power and codec/AUDPRC clocks */

  syslog(LOG_ERR, "AUDIO: PMU enable\n");
  HAL_PMU_EnableAudio(1);
  HAL_RCC_EnableModule(RCC_MOD_AUDCODEC_HP);
  HAL_RCC_EnableModule(RCC_MOD_AUDCODEC_LP);
  HAL_RCC_EnableModule(RCC_MOD_AUDPRC);
  syslog(LOG_ERR, "AUDIO: clocks ok\n");

  /* codec basic configuration (analog front end) */

  priv->codec.Init.en_dly_sel      = 0;
  priv->codec.Init.dac_cfg.opmode  = 1;
  priv->codec.Init.adc_cfg.opmode  = 1;
  priv->codec.Init.samplerate_index = 3;   /* 16k default, updated during configure */

  syslog(LOG_ERR, "AUDIO: codec init...\n");
  res = HAL_AUDCODEC_Init(&priv->codec);
  syslog(LOG_ERR, "AUDIO: codec init res=%d\n", res);
  if (res != HAL_OK)
    {
      return -EIO;
    }

  /* AUDPRC initialization (data path) */

  priv->aprc.Init.clk_div = 1;   /* audio master clock divider (non-ASIC chip) */
  priv->aprc.Init.adc_div = 1;
  priv->aprc.Init.dac_div = 1;

  syslog(LOG_ERR, "AUDIO: aprc init...\n");
  res = HAL_AUDPRC_Init(&priv->aprc);
  syslog(LOG_ERR, "AUDIO: aprc init res=%d\n", res);
  if (res != HAL_OK)
    {
      return -EIO;
    }

  /* AUDPRC TX0 (playback) / RX0 (recording) DMA handles */

  priv->aprc.hdma[HAL_AUDPRC_TX_CH0] = kmm_zalloc(sizeof(DMA_HandleTypeDef));
  priv->aprc.hdma[HAL_AUDPRC_RX_CH0] = kmm_zalloc(sizeof(DMA_HandleTypeDef));
  if (priv->aprc.hdma[HAL_AUDPRC_TX_CH0] == NULL ||
      priv->aprc.hdma[HAL_AUDPRC_RX_CH0] == NULL)
    {
      auderr("ERROR: Failed to alloc DMA handle\n");
      return -ENOMEM;
    }

  priv->aprc.hdma[HAL_AUDPRC_TX_CH0]->Instance     = AUDPRC_TX0_DMA_INSTANCE;
  priv->aprc.hdma[HAL_AUDPRC_TX_CH0]->Init.Request = AUDPRC_TX0_DMA_REQUEST;
  priv->aprc.hdma[HAL_AUDPRC_TX_CH0]->Parent       = &priv->aprc;
  priv->aprc.hdma[HAL_AUDPRC_TX_CH0]->Init.Direction = DMA_MEMORY_TO_PERIPH;
  priv->aprc.hdma[HAL_AUDPRC_TX_CH0]->Init.PeriphInc = DMA_PINC_DISABLE;
  priv->aprc.hdma[HAL_AUDPRC_TX_CH0]->Init.MemInc    = DMA_MINC_ENABLE;
  priv->aprc.hdma[HAL_AUDPRC_TX_CH0]->Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  priv->aprc.hdma[HAL_AUDPRC_TX_CH0]->Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
  priv->aprc.hdma[HAL_AUDPRC_TX_CH0]->Init.Mode      = DMA_NORMAL;
  priv->aprc.hdma[HAL_AUDPRC_TX_CH0]->Init.Priority  = DMA_PRIORITY_LOW;
  HAL_DMA_Init(priv->aprc.hdma[HAL_AUDPRC_TX_CH0]);

  priv->aprc.hdma[HAL_AUDPRC_RX_CH0]->Instance     = AUDPRC_RX0_DMA_INSTANCE;
  priv->aprc.hdma[HAL_AUDPRC_RX_CH0]->Init.Request = AUDPRC_RX0_DMA_REQUEST;
  priv->aprc.hdma[HAL_AUDPRC_RX_CH0]->Parent       = &priv->aprc;
  priv->aprc.hdma[HAL_AUDPRC_RX_CH0]->Init.Direction = DMA_PERIPH_TO_MEMORY;
  priv->aprc.hdma[HAL_AUDPRC_RX_CH0]->Init.PeriphInc = DMA_PINC_DISABLE;
  priv->aprc.hdma[HAL_AUDPRC_RX_CH0]->Init.MemInc    = DMA_MINC_ENABLE;
  priv->aprc.hdma[HAL_AUDPRC_RX_CH0]->Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  priv->aprc.hdma[HAL_AUDPRC_RX_CH0]->Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
  priv->aprc.hdma[HAL_AUDPRC_RX_CH0]->Init.Mode      = DMA_NORMAL;
  priv->aprc.hdma[HAL_AUDPRC_RX_CH0]->Init.Priority  = DMA_PRIORITY_LOW;
  HAL_DMA_Init(priv->aprc.hdma[HAL_AUDPRC_RX_CH0]);

  /* Register DMA1 channel interrupts (AUDPRC TX/RX data-transfer completion depends on DMA interrupts) */

  for (i = 0; i < 8; i++)
    {
      int irq = DMAC1_CH1_IRQn + i + NVIC_IRQ_FIRST;
      int r1;

      r1 = irq_attach(irq, sf32lb52_audio_dma1_irq, priv);
      up_enable_irq(irq);
    }

  audinfo("SF32LB52 audio hw initialized\n");
  return OK;
}

/****************************************************************************
 * Name: sf32lb52_audio_hw_configure
 *
 * Description: Configure the codec paths according to sample rate / channels / bits per sample (see SDK bf0_audio_configure)
 *
 ****************************************************************************/

static int sf32lb52_audio_hw_configure(FAR struct sf32lb52_audio_s *priv,
                                       int samplerate, int nchannels,
                                       int bpsamp)
{
  AUDCODEC_HandleTypeDef *codec = &priv->codec;
  int i;

  for (i = 0; i < SF32LB52_AUDIO_CLK_TAB_NUM; i++)
    {
      if (samplerate == codec_dac_clk_config[i].samplerate)
        {
          codec->Init.samplerate_index = i;
          codec->Init.dac_cfg.dac_clk = (FAR AUDCODE_DAC_CLK_CONFIG_TYPE *)
                                        &codec_dac_clk_config[i];
          codec->Init.adc_cfg.adc_clk = (FAR AUDCODE_ADC_CLK_CONFIG_TYPE *)
                                        &codec_adc_clk_config[i];
          break;
        }
    }

  if (i >= SF32LB52_AUDIO_CLK_TAB_NUM)
    {
      auderr("ERROR: Unsupported samplerate %d\n", samplerate);
      return -EINVAL;
    }

  /* Configure the codec DAC (playback) and ADC (recording) channels */

  HAL_AUDCODEC_Config_TChanel(codec, SF32LB52_AUDIO_DAC_CH,
                              &codec->Init.dac_cfg);
  HAL_AUDCODEC_Config_RChanel(codec, SF32LB52_AUDIO_ADC_CH,
                              &codec->Init.adc_cfg);

  /* Configure the AUDPRC TX (playback) and RX (recording) data channels
   * (see the SDK drv_audprc configure: channel enable + data format)
   */

  {
    AUDPRC_ChnlCfgTypeDef cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.dma_mask = 0;
    cfg.en       = 1;
    cfg.format   = (bpsamp == 16) ? 0 : 1;
    cfg.mode     = (nchannels == 1) ? 0 : 1;

    HAL_AUDPRC_Config_TChanel(&priv->aprc, SF32LB52_AUDIO_PRC_TX_CH, &cfg);
    HAL_AUDPRC_Config_RChanel(&priv->aprc, SF32LB52_AUDIO_PRC_RX_CH, &cfg);
  }

  /* AUDPRC sample clock divider (see SDK bf0_audprc_src: table lookup by sample rate,
   * 16k -> clk_div 3000, xtal clock source)
   */

  priv->aprc.Init.adc_div = 3000;
  priv->aprc.Init.dac_div = 3000;
  priv->aprc.Init.clk_sel = 0;
  __HAL_AUDPRC_CLK_XTAL(&priv->aprc);
  __HAL_AUDPRC_STB_DIV_CLK(&priv->aprc, priv->aprc.Init.adc_div,
                           priv->aprc.Init.dac_div);

  /* AUDPRC path initialization (see SDK bf0_adc_dac_path_cfg_init:
   * Init.adc_cfg / Init.dac_cfg fields -> write the Config_ADCPath/DACPath registers)
   */

  priv->aprc.Init.adc_cfg.src_hbf3_mode = 0;
  priv->aprc.Init.adc_cfg.src_hbf3_en   = 0;
  priv->aprc.Init.adc_cfg.src_hbf2_mode = 0;
  priv->aprc.Init.adc_cfg.src_hbf2_en   = 0;
  priv->aprc.Init.adc_cfg.src_hbf1_mode = 0;
  priv->aprc.Init.adc_cfg.src_hbf1_en   = 0;
  priv->aprc.Init.adc_cfg.src_ch_en     = 0;
  priv->aprc.Init.adc_cfg.rx2tx_loopback = 0;
  priv->aprc.Init.adc_cfg.data_swap     = 0;
  priv->aprc.Init.adc_cfg.src_sel       = 0;
  priv->aprc.Init.adc_cfg.vol_l         = 0;
  priv->aprc.Init.adc_cfg.vol_r         = 0;
  priv->aprc.Init.adc_cfg.src_sinc_en   = 0;
  priv->aprc.Init.adc_cfg.sinc_ratio    = 0;

  priv->aprc.Init.dac_cfg.dst_sel       = 0;
  priv->aprc.Init.dac_cfg.vol_l         = 0;
  priv->aprc.Init.dac_cfg.vol_r         = 0;
  priv->aprc.Init.dac_cfg.src_hbf3_mode = 0;
  priv->aprc.Init.dac_cfg.src_hbf3_en   = 0;
  priv->aprc.Init.dac_cfg.src_hbf2_mode = 0;
  priv->aprc.Init.dac_cfg.src_hbf2_en   = 0;
  priv->aprc.Init.dac_cfg.src_hbf1_mode = 0;
  priv->aprc.Init.dac_cfg.src_hbf1_en   = 0;
  priv->aprc.Init.dac_cfg.src_ch_en     = 0;
  priv->aprc.Init.dac_cfg.eq_clr        = 0;
  priv->aprc.Init.dac_cfg.eq_stage      = 1;
  priv->aprc.Init.dac_cfg.eq_ch_en      = 0;
  priv->aprc.Init.dac_cfg.src_sinc_en   = 0;
  priv->aprc.Init.dac_cfg.sinc_ratio    = 0;

  /* MUX/MIX source selection (playback output select; see the example, out_sel=0x5050) */

  priv->aprc.Init.dac_cfg.muxrsrc1 = (SF32LB52_AUDIO_OUT_SEL >> 12) & 0xF;
  priv->aprc.Init.dac_cfg.muxrsrc0 = (SF32LB52_AUDIO_OUT_SEL >> 8) & 0xF;
  priv->aprc.Init.dac_cfg.muxlsrc1 = (SF32LB52_AUDIO_OUT_SEL >> 4) & 0xF;
  priv->aprc.Init.dac_cfg.muxlsrc0 = SF32LB52_AUDIO_OUT_SEL & 0xF;
  priv->aprc.Init.dac_cfg.mixrsrc1 = (SF32LB52_AUDIO_OUT_SEL >> 12) & 0xF;
  priv->aprc.Init.dac_cfg.mixrsrc0 = (SF32LB52_AUDIO_OUT_SEL >> 8) & 0xF;
  priv->aprc.Init.dac_cfg.mixlsrc1 = (SF32LB52_AUDIO_OUT_SEL >> 4) & 0xF;
  priv->aprc.Init.dac_cfg.mixlsrc0 = SF32LB52_AUDIO_OUT_SEL & 0xF;

  HAL_AUDPRC_Config_ADCPath(&priv->aprc, &priv->aprc.Init.adc_cfg);
  HAL_AUDPRC_Config_DACPath(&priv->aprc, &priv->aprc.Init.dac_cfg);

  priv->samplerate = samplerate;
  priv->nchannels  = nchannels;
  priv->bpsamp     = bpsamp;

  audinfo("codec configured: %d Hz %d ch %d bit\n",
          samplerate, nchannels, bpsamp);
  return OK;
}

/****************************************************************************
 * Name: sf32lb52_audio_hw_start
 *
 * Description:
 *   Start the playback or recording path (see the SDK audprc example start_tx/start_rx):
 *   Playback: codec DAC analog -> AUDPRC TX path -> power amplifier
 *   Recording: codec ADC analog -> AUDPRC RX path
 *
 ****************************************************************************/

static int sf32lb52_audio_hw_start(FAR struct sf32lb52_audio_s *priv,
                                   bool playback)
{
  AUDCODEC_HandleTypeDef *codec = &priv->codec;
  AUDPRC_HandleTypeDef   *aprc  = &priv->aprc;

  /* Audio PLL clock configuration (must be called before playback/recording; see SDK bf0_audio_pll_config) */

  bf0_enable_pll(priv->samplerate, 1);

  if (playback)
    {
      /* AUDPRC: DAC output to codec, configure the MUX/MIX path (0x5050) */

      __HAL_AUDPRC_DAC_DST_CODEC(aprc);

      aprc->Init.dac_cfg.muxrsrc1 = (SF32LB52_AUDIO_OUT_SEL >> 12) & 0xF;
      aprc->Init.dac_cfg.muxrsrc0 = (SF32LB52_AUDIO_OUT_SEL >> 8) & 0xF;
      aprc->Init.dac_cfg.muxlsrc1 = (SF32LB52_AUDIO_OUT_SEL >> 4) & 0xF;
      aprc->Init.dac_cfg.muxlsrc0 = SF32LB52_AUDIO_OUT_SEL & 0xF;

      aprc->Init.dac_cfg.mixrsrc1 = (SF32LB52_AUDIO_OUT_SEL >> 12) & 0xF;
      aprc->Init.dac_cfg.mixrsrc0 = (SF32LB52_AUDIO_OUT_SEL >> 8) & 0xF;
      aprc->Init.dac_cfg.mixlsrc1 = (SF32LB52_AUDIO_OUT_SEL >> 4) & 0xF;
      aprc->Init.dac_cfg.mixlsrc0 = SF32LB52_AUDIO_OUT_SEL & 0xF;

      HAL_AUDPRC_Config_DACPath(aprc, &aprc->Init.dac_cfg);
      __HAL_AUDPRC_ENABLE(aprc);

      /* codec: DAC analog-path output */

      __HAL_AUDCODEC_DAC_ENABLE(codec);
      HAL_AUDCODEC_Config_Analog_DACPath(codec->Init.dac_cfg.dac_clk);
      HAL_AUDCODEC_Config_DACPath(codec, 0);
      HAL_AUDCODEC_Config_DACPath_Volume(codec, SF32LB52_AUDIO_DAC_CH,
                                         SF32LB52_AUDIO_DEFAULT_VOL);

      /* Power on the amplifier */

      sf32lb52_audio_pa_enable(true);
      audinfo("DAC path started\n");
    }
  else
    {
      /* AUDPRC: ADC input comes from the codec (overall enable is deferred until after DMA startup,
       * see the SDK audprc start: configure and start DMA -> __HAL_AUDPRC_ENABLE) */

      __HAL_AUDPRC_ADC_SRC_CODEC(aprc);

      /* codec: ADC channel reconfiguration + analog path (see the SDK codec start) */

      HAL_AUDCODEC_Config_RChanel(codec, SF32LB52_AUDIO_ADC_CH,
                                  &codec->Init.adc_cfg);
      HAL_AUDCODEC_Config_Analog_ADCPath(codec->Init.adc_cfg.adc_clk);
      __HAL_AUDCODEC_ADC_ENABLE(codec);
      audinfo("ADC path started\n");
    }

  priv->playback = playback;
  priv->running  = true;
  return OK;
}

/****************************************************************************
 * Name: sf32lb52_audio_hw_stop
 *
 * Description: Stops playback/recording and disables the paths and the power amplifier
 *
 ****************************************************************************/

static int sf32lb52_audio_hw_stop(FAR struct sf32lb52_audio_s *priv)
{
  HAL_AUDPRC_DMAStop(&priv->aprc, HAL_AUDPRC_TX_CH0);
  HAL_AUDPRC_DMAStop(&priv->aprc, HAL_AUDPRC_RX_CH0);
  __HAL_AUDPRC_DISABLE(&priv->aprc);

  if (priv->playback)
    {
      HAL_AUDCODEC_Close_Analog_DACPath();
      sf32lb52_audio_pa_enable(false);
    }
  else
    {
      HAL_AUDCODEC_Close_Analog_ADCPath();
    }

  priv->running = false;
  audinfo("audio stopped\n");
  return OK;
}

/****************************************************************************
 * Name: sf32lb52_audio_getcaps
 *
 * Description: Reports device capabilities (input+output, PCM, sample rates)
 *
 ****************************************************************************/

static int sf32lb52_audio_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                                  FAR struct audio_caps_s *caps)
{
  DEBUGASSERT(caps && caps->ac_len >= sizeof(struct audio_caps_s));

  caps->ac_format.hw  = 0;
  caps->ac_controls.w = 0;

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_QUERY:
        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            /* Supports input (microphone) and output (speaker), PCM format */

            caps->ac_controls.b[0] = AUDIO_TYPE_INPUT |
                                     AUDIO_TYPE_OUTPUT;
            caps->ac_format.hw     = 1 << (AUDIO_FMT_PCM - 1);
          }
        else
          {
            caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
          }
        break;

      case AUDIO_TYPE_INPUT:
      case AUDIO_TYPE_OUTPUT:
        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            caps->ac_controls.hw[0] = AUDIO_SAMP_RATE_8K |
                                      AUDIO_SAMP_RATE_16K |
                                      AUDIO_SAMP_RATE_44K |
                                      AUDIO_SAMP_RATE_48K;
            caps->ac_channels = 1;
          }
        else
          {
            caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
          }
        break;

      case AUDIO_TYPE_FEATURE:
        if (caps->ac_subtype == AUDIO_FU_UNDEF)
          {
            caps->ac_controls.b[0] = AUDIO_FU_VOLUME;
          }
        break;

      default:
        caps->ac_subtype = 0;
        caps->ac_channels = 0;
        break;
    }

  return caps->ac_len;
}

/****************************************************************************
 * Name: sf32lb52_audio_configure
 *
 * Description: Handles the upper-layer AUDIOIOC_CONFIGURE request
 *
 ****************************************************************************/

static int sf32lb52_audio_configure(FAR struct audio_lowerhalf_s *dev,
                                    FAR const struct audio_caps_s *caps)
{
  FAR struct sf32lb52_audio_s *priv =
    (FAR struct sf32lb52_audio_s *)dev;
  int ret = OK;

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_INPUT:
        priv->playback = false;
        ret = sf32lb52_audio_hw_configure(priv,
                                          caps->ac_controls.hw[0] |
                                          (caps->ac_controls.b[3] << 16),
                                          caps->ac_channels,
                                          caps->ac_controls.b[2]);
        break;

      case AUDIO_TYPE_OUTPUT:
        priv->playback = true;
        ret = sf32lb52_audio_hw_configure(priv,
                                          caps->ac_controls.hw[0] |
                                          (caps->ac_controls.b[3] << 16),
                                          caps->ac_channels,
                                          caps->ac_controls.b[2]);
        break;

      case AUDIO_TYPE_FEATURE:
        if (caps->ac_format.hw == AUDIO_FU_VOLUME)
          {
            HAL_AUDCODEC_Config_DACPath_Volume(&priv->codec,
                                               SF32LB52_AUDIO_DAC_CH,
                                               caps->ac_controls.hw[0]);
          }
        break;

      default:
        audwarn("WARNING: Unsupported configure type %d\n", caps->ac_type);
        break;
    }

  return ret;
}

/****************************************************************************
 * Name: sf32lb52_audio_shutdown
 *
 ****************************************************************************/

static int sf32lb52_audio_shutdown(FAR struct audio_lowerhalf_s *dev)
{
  return sf32lb52_audio_hw_stop((FAR struct sf32lb52_audio_s *)dev);
}

/****************************************************************************
 * Name: sf32lb52_audio_start
 *
 * Description: Handles the upper-layer AUDIOIOC_START request
 *
 ****************************************************************************/

static int sf32lb52_audio_start(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct sf32lb52_audio_s *priv =
    (FAR struct sf32lb52_audio_s *)dev;

  if (priv->running)
    {
      return OK;
    }

  return sf32lb52_audio_hw_start(priv, priv->playback);
}

/****************************************************************************
 * Name: sf32lb52_audio_stop
 *
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
static int sf32lb52_audio_stop(FAR struct audio_lowerhalf_s *dev)
{
  return sf32lb52_audio_hw_stop((FAR struct sf32lb52_audio_s *)dev);
}
#endif

/****************************************************************************
 * Name: sf32lb52_audio_enqueuebuffer
 *
 * Description:
 *   The upper layer enqueues a buffer:
 *   - Playback: data sent to the DAC via AUDPRC TX0 DMA
 *   - Recording: AUDPRC RX0 DMA captures ADC data into the buffer
 *   Completion is notified by the DMA interrupt callbacks (HAL_AUDPRC_Tx/RxCpltCallback).
 *
 ****************************************************************************/

static int sf32lb52_audio_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                        FAR struct ap_buffer_s *apb)
{
  FAR struct sf32lb52_audio_s *priv =
    (FAR struct sf32lb52_audio_s *)dev;
  HAL_StatusTypeDef res;

  DEBUGASSERT(priv && apb);

  if (priv->playback)
    {
      priv->tx_apb = apb;
      res = HAL_AUDPRC_Transmit_DMA(&priv->aprc, apb->samp, apb->nbytes,
                                    SF32LB52_AUDIO_PRC_TX_CH);
    }
  else
    {
      priv->rx_apb = apb;
      res = HAL_AUDPRC_Receive_DMA(&priv->aprc, apb->samp, apb->nbytes,
                                   SF32LB52_AUDIO_PRC_RX_CH);
    }

  if (res != HAL_OK)
    {
      auderr("ERROR: DMA enqueue failed: %d\n", res);
      priv->tx_apb = NULL;
      priv->rx_apb = NULL;
      return -EIO;
    }

  return OK;
}

/****************************************************************************
 * Name: sf32lb52_audio_cancelbuffer
 *
 ****************************************************************************/

static int sf32lb52_audio_cancelbuffer(FAR struct audio_lowerhalf_s *dev,
                                       FAR struct ap_buffer_s *apb)
{
  FAR struct sf32lb52_audio_s *priv =
    (FAR struct sf32lb52_audio_s *)dev;

  if (priv->tx_apb == apb)
    {
      HAL_AUDPRC_DMAStop(&priv->aprc, SF32LB52_AUDIO_PRC_TX_CH);
      priv->tx_apb = NULL;
    }

  if (priv->rx_apb == apb)
    {
      HAL_AUDPRC_DMAStop(&priv->aprc, SF32LB52_AUDIO_PRC_RX_CH);
      priv->rx_apb = NULL;
    }

  return OK;
}

/****************************************************************************
 * Name: sf32lb52_audio_ioctl
 *
 ****************************************************************************/

static int sf32lb52_audio_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                                unsigned long arg)
{
  int ret = OK;

  switch (cmd)
    {
      case AUDIOIOC_HWRESET:
        audinfo("AUDIOIOC_HWRESET\n");
        break;

      default:
        ret = -ENOTTY;
        break;
    }

  return ret;
}

/****************************************************************************
 * Name: sf32lb52_audio_write
 *
 * Description:
 *   Direct write: sends application data via AUDPRC TX0 DMA and waits for playback to complete.
 *   (a simple synchronous playback API outside the standard NuttX buffer-queue flow)
 *
 ****************************************************************************/

static ssize_t sf32lb52_audio_write(FAR struct audio_lowerhalf_s *dev,
                                    FAR const char *buffer, size_t buflen)
{
  FAR struct sf32lb52_audio_s *priv =
    (FAR struct sf32lb52_audio_s *)dev;
  HAL_StatusTypeDef res;
  int ret;

  if (buffer == NULL || buflen == 0 || !priv->running)
    {
      return 0;
    }

  priv->wr_busy = true;
  res = HAL_AUDPRC_Transmit_DMA(&priv->aprc, (FAR uint8_t *)buffer, buflen,
                                SF32LB52_AUDIO_PRC_TX_CH);
  if (res != HAL_OK)
    {
      priv->wr_busy = false;
      auderr("ERROR: write DMA start failed: %d\n", res);
      return 0;
    }

  ret = nxsem_tickwait_uninterruptible(&priv->wr_sem, MSEC2TICK(5000));
  priv->wr_busy = false;

  /* Stop the circular DMA transfer (single-shot playback complete) */

  HAL_AUDPRC_DMAStop(&priv->aprc, SF32LB52_AUDIO_PRC_TX_CH);

  if (ret < 0)
    {
      auderr("ERROR: write DMA wait failed: %d\n", ret);
      return 0;
    }

  return buflen;
}

/****************************************************************************
 * Name: sf32lb52_audio_read
 *
 * Description:
 *   Direct read: starts AUDPRC RX0 DMA to capture microphone data and waits for completion.
 *
 ****************************************************************************/

static ssize_t sf32lb52_audio_read(FAR struct audio_lowerhalf_s *dev,
                                   FAR char *buffer, size_t buflen)
{
  FAR struct sf32lb52_audio_s *priv =
    (FAR struct sf32lb52_audio_s *)dev;
  HAL_StatusTypeDef res;
  int ret;

  if (buffer == NULL || buflen == 0 || !priv->running)
    {
      syslog(LOG_ERR, "R: early ret buf=%p len=%zu run=%d\n",
             buffer, buflen, priv->running);
      return 0;
    }

  priv->rx_busy = true;
  res = HAL_AUDPRC_Receive_DMA(&priv->aprc, (FAR uint8_t *)buffer, buflen,
                               SF32LB52_AUDIO_PRC_RX_CH);

  /* Overall enable after the DMA starts (see the SDK audprc start sequence) */

  __HAL_AUDPRC_ENABLE(&priv->aprc);

  if (res != HAL_OK)
    {
      priv->rx_busy = false;
      auderr("ERROR: read DMA start failed: %d\n", res);
      return 0;
    }

  ret = nxsem_tickwait_uninterruptible(&priv->rx_sem, MSEC2TICK(5000));
  priv->rx_busy = false;

  /* Stop the circular DMA transfer (single-shot capture complete) */

  HAL_AUDPRC_DMAStop(&priv->aprc, SF32LB52_AUDIO_PRC_RX_CH);

  if (ret < 0)
    {
      auderr("ERROR: read DMA wait failed: %d\n", ret);
      return 0;
    }

  return buflen;
}

/****************************************************************************
 * Name: sf32lb52_audio_reserve / sf32lb52_audio_release
 *
 ****************************************************************************/

static int sf32lb52_audio_reserve(FAR struct audio_lowerhalf_s *dev)
{
  return OK;
}

static int sf32lb52_audio_release(FAR struct audio_lowerhalf_s *dev)
{
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sf32lb52_audio_initialize
 *
 ****************************************************************************/

int sf32lb52_audio_initialize(void)
{
  FAR struct sf32lb52_audio_s *priv;
  int ret;

  syslog(LOG_ERR, "AUDIO: initialize start\n");
  priv = kmm_zalloc(sizeof(struct sf32lb52_audio_s));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  ret = sf32lb52_audio_hw_init(priv);
  syslog(LOG_ERR, "AUDIO: hw_init ret=%d\n", ret);
  if (ret < 0)
    {
      kmm_free(priv);
      return ret;
    }

  nxsem_init(&priv->wr_sem, 0, 0);
  nxsem_init(&priv->rx_sem, 0, 0);

  priv->dev.ops = &g_sf32lb52_audio_ops;

  ret = audio_register("audio0", &priv->dev);
  syslog(LOG_ERR, "AUDIO: register ret=%d\n", ret);
  if (ret < 0)
    {
      auderr("ERROR: audio_register failed: %d\n", ret);
      kmm_free(priv);
      return ret;
    }

  audinfo("/dev/audio0 registered\n");
  return OK;
}

/****************************************************************************
 * HAL weak-symbol callback overrides (DMA interrupt -> this driver)
 *
 ****************************************************************************/

void HAL_AUDPRC_TxCpltCallback(AUDPRC_HandleTypeDef *haprc, int cid)
{
  FAR struct sf32lb52_audio_s *priv;

  if (haprc == NULL)
    {
      return;
    }

  priv = (FAR struct sf32lb52_audio_s *)
         ((FAR char *)haprc - offsetof(struct sf32lb52_audio_s, aprc));

  if (priv->wr_busy)
    {
      priv->wr_busy = false;
      nxsem_post(&priv->wr_sem);
    }
  else
    {
      sf32lb52_audio_tx_complete(priv);
    }
}

void HAL_AUDPRC_RxCpltCallback(AUDPRC_HandleTypeDef *haprc, int cid)
{
  FAR struct sf32lb52_audio_s *priv;

  if (haprc == NULL)
    {
      return;
    }

  priv = (FAR struct sf32lb52_audio_s *)
         ((FAR char *)haprc - offsetof(struct sf32lb52_audio_s, aprc));

  if (priv->rx_busy)
    {
      priv->rx_busy = false;
      nxsem_post(&priv->rx_sem);
    }
  else
    {
      sf32lb52_audio_rx_complete(priv);
    }
}
