/****************************************************************************
 * vendor/sifli/chips/sf32lb52/sf32lb_audcodec.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SF32LB52 AUDCODEC ADC capture path (on-board MEMS microphone).
 *
 * Bring-up notes, because the vendor HAL leaves all of this to the caller:
 *
 *   - SF32LB52X builds bf0_hal_audcodec_m.c, not bf0_hal_audcodec.c.
 *   - HAL_AUDCODEC_MspInit() is empty: no clock, pin or DMA setup happens
 *     for us. HAL_DMA_Init() is invoked by HAL_AUDCODEC_Init() through
 *     HAL_AUDCODEC_DMA_Init(), but only after the caller has filled in
 *     Instance and Init.Request on the DMA handle.
 *   - HAL_AUDCODEC_Config_ADCPath() is compiled out (#if 0), so the analog
 *     front end is set up with HAL_AUDCODEC_Config_Analog_ADCPath(), which
 *     also enables the microphone bias.
 *   - The ADC needs the audio PLL: ADC_CFG.CLK_SRC_SEL is programmed to 1.
 *
 * Capture is a plain circular DMA into a single buffer; the reader derives
 * the DMA write position from CNDTR, so no DMA interrupt handler has to run
 * at audio rate and a reader that falls behind loses samples rather than
 * stalling the capture path.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/clock.h>

#include "bf0_hal.h"
#include "bf0_hal_audcodec.h"
#include "dma_config.h"

#include "sf32lb_audcodec.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Words of circular DMA buffer; see SF32LB_AUDIO_DMA_WORDS. Must be a power
 * of two: the write position is masked rather than wrapped.
 */

#define MIC_DMA_WORDS    SF32LB_AUDIO_DMA_WORDS
#define MIC_DMA_BYTES    (MIC_DMA_WORDS * 4)
#define MIC_DMA_MASK     (MIC_DMA_WORDS - 1)

/* Poll interval while waiting for the DMA to advance. */

#define MIC_POLL_US      2000

/* Least amount of audio a read waits for before returning, when the caller
 * asked for more. A read that insists on the caller's whole request before
 * returning puts the wait for audio and the transmission of it in series:
 * at 8 kHz a 4096-sample request blocks for 512 ms and the transmit that
 * follows blocks for longer still, while the ADC keeps filling the ring the
 * whole time. The ring is only 8192 samples, so the reader laps it on the
 * first frame and then silently loses about a quarter of every second of
 * audio. Returning as soon as this much is in hand lets a caller that is
 * slower than the ADC still drain the ring continuously, at the cost of
 * shorter frames. 1024 samples == 128 ms at 8 kHz == 8 frames/s.
 */

#define MIC_MIN_READ     1024

/* Give up on a read after this many consecutive polls with no DMA progress
 * (2000 us each, so ~2 s). Guards against wedging the caller when the ADC is
 * silent, which is the expected symptom of a misconfigured capture path.
 */

#define MIC_STALL_LIMIT  1000

/* Nominal rate used when bringing the PLL up. bf0_enable_pll() type 0 selects
 * the 1024-series frequency, 49.152 MHz == 16000 * 3072.
 */

#define MIC_SAMPLE_RATE  16000

/* The audible rate is set by g_clk_div / g_osr_sel, not by this parameter,
 * so it is only a sanity bound: the caller states what it believes the rate
 * to be and the driver refuses values that cannot be right.
 */

#define MIC_RATE_MIN     2000
#define MIC_RATE_MAX     48000

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* The DMA target. Aligned so that the cache maintenance below operates on
 * whole cache lines.
 */

static uint32_t g_dma_buf[MIC_DMA_WORDS] aligned_data(32);

static DMA_HandleTypeDef      g_hdma_adc;
static AUDCODEC_HandleTypeDef g_codec;
static bool                   g_open;
static uint32_t               g_did;      /* HAL_AUDCODEC_ADC_CH0/CH1 */
static bool                   g_pll;      /* audio PLL was enabled */

/* Where the reader got to in the ring. The ADC fills the ring continuously
 * and does not care whether anyone is reading, so the reader has to remember
 * its own position: re-deriving it from the DMA write pointer on every call
 * would aim the next read at "now", throwing away everything captured during
 * the previous transmit and making each call wait for a full fresh block.
 */

static unsigned int           g_read_pos;
static bool                   g_read_started;

/* ADC_CFG divider and OSR.
 *
 * Calibrated by measurement, not derived: nothing in the tree documents how
 * these two fields map onto a sample rate, and the values the HAL ships with
 * (5 / 0) actually produce about 20 kHz. Measured rates, PLL clock source:
 *
 *     clk_div/osr_sel   5/0    6/0    7/0    8/0    9/0    12/0
 *     Hz                19986  19851  15920  13333  11431  10000
 *
 *     clk_div/osr_sel   7/1    9/1    8/3    7/3    9/3
 *     Hz                10000  8000   5000   5801   4211
 *
 * The 7/3 entry originally read 5333 and was wrong by 9%; the delivered rate
 * end to end agreed with the re-measured figure, so the table is what moved,
 * not the hardware. Treat every entry as approximate and re-measure with
 * mic_rate -- announcing the wrong rate to a recogniser shifts its whole
 * spectrum rather than failing outright.
 *
 * The default is 7 / 3 == 5801 Hz, set by what the transport actually
 * delivers rather than by what the recogniser would like. The PPP link over
 * the CH340 carries about 6 KB/s of payload at the TCP MSS this board
 * negotiates (256), so 8 kHz of mu-law -- 8 KB/s before framing -- does not
 * fit, and running the ADC faster than the link can carry does not degrade
 * gracefully: the reader falls behind, the DMA ring laps, and the samples
 * that go missing are replaced by whatever came a second later. 5801 Hz of
 * mu-law is 5.8 KB/s and arrives contiguous.
 *
 * 5801 Hz is a 2.9 kHz audio band. That is thin -- the fricatives live above
 * it -- but it is within reach of the 3.4 kHz the telephone network gives a
 * recogniser, and it is better than twice the band the previous 2667 Hz
 * setting managed.
 *
 * The delivered byte rate equals the capture rate exactly while nothing is
 * dropped, which is how to tell the two apart. Re-check with mic_rate if the
 * PLL frequency or the clock source changes.
 */

static uint8_t                g_clk_div = 7;
static uint8_t                g_osr_sel = 3;

void sf32lb_audcodec_set_clk(uint8_t clk_div, uint8_t osr_sel)
{
  g_clk_div = clk_div;
  g_osr_sel = (uint8_t)(osr_sel & 0x3);
}

void sf32lb_audcodec_get_clk(uint8_t *clk_div, uint8_t *osr_sel)
{
  if (clk_div != NULL)
    {
      *clk_div = g_clk_div;
    }

  if (osr_sel != NULL)
    {
      *osr_sel = g_osr_sel;
    }
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mic_dma_pos
 *
 * Description:
 *   Current DMA write position, in words, within the circular buffer.
 *   CNDTR counts down as transfers complete, so the number of words already
 *   written is MIC_DMA_WORDS - CNDTR. When CNDTR has just reloaded the
 *   position reads back as MIC_DMA_WORDS, which the mask maps to 0.
 *
 ****************************************************************************/

static unsigned int mic_dma_pos(void)
{
  return (MIC_DMA_WORDS - __HAL_DMA_GET_COUNTER(&g_hdma_adc)) & MIC_DMA_MASK;
}

/****************************************************************************
 * Name: mic_dma_start
 *
 * Description:
 *   Fill in the DMA handle and the codec handle, then let the HAL arm the
 *   channel. Instance and Init.Request are the caller's job; everything
 *   else is overwritten by HAL_AUDCODEC_DMA_Init().
 *
 ****************************************************************************/

static int mic_dma_start(void)
{
  memset(&g_hdma_adc, 0, sizeof(g_hdma_adc));
  memset(&g_codec, 0, sizeof(g_codec));

  g_hdma_adc.Instance         = AUDCODEC_ADC0_DMA_INSTANCE;
  g_hdma_adc.Init.Request     = AUDCODEC_ADC0_DMA_REQUEST;
  g_hdma_adc.Init.IrqPrio     = AUDCODEC_ADC0_DMA_IRQ_PRIO;

  g_codec.Instance            = hwp_audcodec;
  g_codec.hdma[HAL_AUDCODEC_ADC_CH0] = &g_hdma_adc;

  /* HAL_AUDCODEC_Init() runs HAL_AUDCODEC_DMA_Init() over hdma[], which is
   * what actually calls HAL_DMA_Init().
   */

  if (HAL_AUDCODEC_Init(&g_codec) != HAL_OK)
    {
      _err("ERROR: HAL_AUDCODEC_Init failed\n");
      return -EIO;
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* Not declared in bf0_hal_audcodec.h even though it is exported, so pull it
 * in here rather than duplicating the rough/fine volume encoding.
 */

extern HAL_StatusTypeDef
HAL_AUDCODEC_Config_ADCPath_Volume(AUDCODEC_HandleTypeDef *hacodec,
                                   int channel, int volume);

int sf32lb_audcodec_open_ex(uint32_t sample_rate,
                            enum sf32lb_audio_clk_e clk, int channel,
                            int volume_db)
{
  AUDCODE_ADC_CLK_CONFIG_TYPE adc_clk;
  AUDCODEC_ADCCfgTypeDef      adc_cfg;
  int                         ret;
  int                         use_pll = (clk == SF32LB_AUDIO_CLK_PLL);

  if (g_open)
    {
      return -EBUSY;
    }

  if (sample_rate < MIC_RATE_MIN || sample_rate > MIC_RATE_MAX)
    {
      _err("ERROR: implausible capture rate %lu (expected %u..%u)\n",
           (unsigned long)sample_rate, MIC_RATE_MIN, MIC_RATE_MAX);
      return -EINVAL;
    }

  if (channel != 0 && channel != 1)
    {
      _err("ERROR: ADC channel must be 0 or 1 (asked for %d)\n", channel);
      return -EINVAL;
    }

  memset(g_dma_buf, 0, sizeof(g_dma_buf));

  if (use_pll && bf0_enable_pll(MIC_SAMPLE_RATE, 0) != 0)
    {
      _err("ERROR: bf0_enable_pll failed (audio PLL did not lock)\n");
      return -EIO;
    }

  ret = mic_dma_start();
  if (ret < 0)
    {
      if (use_pll)
        {
          bf0_disable_pll();
        }

      return ret;
    }

  /* clk_div 5 and osr_sel 0 reproduce the reference configuration the HAL
   * documents as ADC_CFG == 0x508 (OP_MODE 1). Note that 0x508 has
   * CLK_SRC_SEL == 0, i.e. the 48 MHz crystal rather than the PLL, so the
   * clock source is caller-selectable.
   */

  memset(&adc_clk, 0, sizeof(adc_clk));
  adc_clk.samplerate         = MIC_SAMPLE_RATE;
  adc_clk.clk_src_sel        = (uint8_t)(use_pll ? 1 : 0);
  adc_clk.clk_div            = g_clk_div;
  adc_clk.osr_sel            = g_osr_sel;
  adc_clk.sel_clk_adc_source = (uint8_t)(use_pll ? 1 : 0);
  adc_clk.sel_clk_adc        = 1;
  adc_clk.diva_clk_adc       = 5;
  adc_clk.fsp                = 0;

  adc_cfg.opmode  = 1;
  adc_cfg.adc_clk = &adc_clk;

  if (HAL_AUDCODEC_Config_RChanel(&g_codec, channel, &adc_cfg) != HAL_OK)
    {
      _err("ERROR: HAL_AUDCODEC_Config_RChanel failed\n");
      ret = -EIO;
      goto err_out;
    }

  g_did = (channel == 0) ? HAL_AUDCODEC_ADC_CH0 : HAL_AUDCODEC_ADC_CH1;

  /* Gain, after the channel config that resets these fields. */

  if (volume_db < SF32LB_AUDIO_VOLUME_MIN)
    {
      volume_db = SF32LB_AUDIO_VOLUME_MIN;
    }

  if (volume_db > SF32LB_AUDIO_VOLUME_MAX)
    {
      volume_db = SF32LB_AUDIO_VOLUME_MAX;
    }

  if (HAL_AUDCODEC_Config_ADCPath_Volume(&g_codec, channel, volume_db)
      != HAL_OK)
    {
      _err("ERROR: HAL_AUDCODEC_Config_ADCPath_Volume failed\n");
    }

  /* Make sure stale cache lines are not what the reader ends up seeing. */

  up_invalidate_dcache((uintptr_t)g_dma_buf,
                       (uintptr_t)g_dma_buf + sizeof(g_dma_buf));

  if (HAL_AUDCODEC_Receive_DMA(&g_codec, (uint8_t *)g_dma_buf,
                               MIC_DMA_BYTES, g_did) != HAL_OK)
    {
      _err("ERROR: HAL_AUDCODEC_Receive_DMA failed\n");
      ret = -EIO;
      goto err_out;
    }

  /* Reference generator. This is the piece the vendor HAL leaves to its
   * caller and that nothing else in the tree calls: without REFGEN_CFG.EN
   * the converter has no reference to compare against and every sample
   * comes back as zero, with the DMA running happily the whole time.
   */

  HAL_AUCODEC_Refgen_Init();

  /* Analog front end. This switches the microphone bias on and brings
   * ADC1/ADC2 out of reset; it blocks for ~20 ms internally.
   */

  HAL_AUDCODEC_Config_Analog_ADCPath(&adc_clk);

  /* Finally turn the ADC block on. The HAL never does this itself: both
   * call sites in bf0_hal_audcodec_m.c are commented out and only the
   * disable path is live.
   */

  __HAL_AUDCODEC_ADC_ENABLE(&g_codec);

  g_open         = true;
  g_pll          = use_pll;
  g_read_started = false;

  syslog(LOG_INFO,
         "[audcodec] mic capture started: %lu Hz nominal, div=%u osr=%u, "
         "ch%d %s clk, %u-word DMA ring\n",
         (unsigned long)sample_rate, g_clk_div, g_osr_sel, channel,
         use_pll ? "PLL" : "xtal", (unsigned)MIC_DMA_WORDS);

  return OK;

err_out:
  __HAL_AUDCODEC_ADC_DISABLE(&g_codec);
  HAL_AUDCODEC_Close_Analog_ADCPath();
  HAL_AUDCODEC_Clear_All_Channel(&g_codec, 0x2);

  if (use_pll)
    {
      bf0_disable_pll();
    }

  return ret;
}

int sf32lb_audcodec_open(uint32_t sample_rate)
{
  return sf32lb_audcodec_open_ex(sample_rate, SF32LB_AUDIO_CLK_PLL, 0,
                                 SF32LB_AUDIO_DEFAULT_VOLUME);
}

/****************************************************************************
 * Name: mic_copy_words
 *
 * Description:
 *   Copy 'words' words out of the circular DMA buffer starting at 'first',
 *   splitting the copy when the range wraps.
 *
 ****************************************************************************/

static void mic_copy_words(uint32_t *dst, unsigned int first,
                           unsigned int words)
{
  unsigned int head;

  up_invalidate_dcache((uintptr_t)&g_dma_buf[first],
                       (uintptr_t)&g_dma_buf[first] + words * sizeof(uint32_t));

  head = MIC_DMA_WORDS - first;
  if (head >= words)
    {
      memcpy(dst, &g_dma_buf[first], words * sizeof(uint32_t));
      return;
    }

  memcpy(dst, &g_dma_buf[first], head * sizeof(uint32_t));
  memcpy(dst + head, g_dma_buf, (words - head) * sizeof(uint32_t));
}

ssize_t sf32lb_audcodec_read_raw(uint32_t *buf, size_t words)
{
  size_t       got    = 0;
  unsigned int last;
  unsigned int stalls = 0;

  if (!g_open)
    {
      return -EINVAL;
    }

  last = mic_dma_pos();

  while (got < words)
    {
      unsigned int now = mic_dma_pos();
      unsigned int avail;

      if (now == last)
        {
          if (++stalls > MIC_STALL_LIMIT)
            {
              _err("ERROR: DMA stalled at word %u, no audio from the ADC\n",
                   last);
              return got > 0 ? (ssize_t)got : -ETIMEDOUT;
            }

          usleep(MIC_POLL_US);
          continue;
        }

      stalls = 0;

      avail = (now - last) & MIC_DMA_MASK;
      if (avail > words - got)
        {
          avail = words - got;
        }

      mic_copy_words(buf + got, last, avail);

      got += avail;
      last = (last + avail) & MIC_DMA_MASK;
    }

  return (ssize_t)got;
}

ssize_t sf32lb_audcodec_read(int16_t *buf, size_t samples)
{
  size_t       got    = 0;
  size_t       want;
  unsigned int stalls = 0;

  if (!g_open)
    {
      return -EINVAL;
    }

  if (samples == 0)
    {
      return 0;
    }

  /* A request that spans the whole ring cannot be served: the write position
   * is tracked modulo the ring, so an advance of exactly one ring is
   * indistinguishable from no advance, and the caller would wait forever on a
   * capture path that is working perfectly. Truncate rather than refuse --
   * the caller states an upper bound on how much it can take, not a demand.
   */

  if (samples > MIC_DMA_WORDS - 1)
    {
      samples = MIC_DMA_WORDS - 1;
    }

  /* First read after arming starts from the write position as it stands, so
   * the caller is not handed audio captured before it asked for any. */

  if (!g_read_started)
    {
      g_read_pos     = mic_dma_pos();
      g_read_started = true;
    }

  /* Return as soon as this much is in hand. Waiting for the caller's whole
   * request instead would serialise capture and transmission -- see
   * MIC_MIN_READ. */

  want = (samples < MIC_MIN_READ) ? samples : MIC_MIN_READ;

  while (got < samples)
    {
      unsigned int now = mic_dma_pos();
      unsigned int avail;
      unsigned int i;

      if (now == g_read_pos)
        {
          /* Everything captured so far is handed over; the ring simply has
           * not refilled yet. That is the normal case for a reader faster
           * than the ADC, and it is not a stall. */

          if (got >= want)
            {
              break;
            }

          /* Nothing at all for two seconds, though, means the ADC is not
           * producing -- a configuration bug, not something to wait out. */

          if (++stalls > MIC_STALL_LIMIT)
            {
              _err("ERROR: DMA stalled at word %u, no audio from the ADC\n",
                   g_read_pos);
              return got > 0 ? (ssize_t)got : -ETIMEDOUT;
            }

          usleep(MIC_POLL_US);
          continue;
        }

      stalls = 0;

      /* Everything the ring accumulated since the last read, up to what the
       * caller can hold. */
      avail = (now - g_read_pos) & MIC_DMA_MASK;
      if (avail > samples - got)
        {
          avail = samples - got;
        }

      /* The ADC drops one 16-bit sample into the low half of each 32-bit
       * DMA word; compact them so the caller sees packed mono PCM.
       */

      up_invalidate_dcache((uintptr_t)&g_dma_buf[g_read_pos],
                           (uintptr_t)&g_dma_buf[g_read_pos] +
                           avail * sizeof(uint32_t));

      for (i = 0; i < avail; i++)
        {
          unsigned int idx = (g_read_pos + i) & MIC_DMA_MASK;
          buf[got + i] = (int16_t)(g_dma_buf[idx] & 0xffff);
        }

      got += avail;
      g_read_pos = (g_read_pos + avail) & MIC_DMA_MASK;
    }

  return (ssize_t)got;
}

int sf32lb_audcodec_close(void)
{
  if (!g_open)
    {
      return -EINVAL;
    }

  g_open         = false;
  g_read_started = false;

  HAL_AUDCODEC_DMAStop(&g_codec, g_did);
  __HAL_AUDCODEC_ADC_DISABLE(&g_codec);
  HAL_AUDCODEC_Close_Analog_ADCPath();
  HAL_AUDCODEC_Clear_All_Channel(&g_codec, 0x2);

  if (g_pll)
    {
      bf0_disable_pll();
      g_pll = false;
    }

  syslog(LOG_INFO, "[audcodec] mic capture stopped\n");

  return OK;
}

bool sf32lb_audcodec_is_open(void)
{
  return g_open;
}

int sf32lb_audcodec_get_regs(struct sf32lb_audcodec_regs_s *regs)
{
  if (!g_open || regs == NULL)
    {
      return -EINVAL;
    }

  regs->cfg         = hwp_audcodec->CFG;
  regs->adc_cfg     = hwp_audcodec->ADC_CFG;
  regs->adc_ch0_cfg = hwp_audcodec->ADC_CH0_CFG;
  regs->adc_ch1_cfg = hwp_audcodec->ADC_CH1_CFG;
  regs->adc_ana_cfg = hwp_audcodec->ADC_ANA_CFG;
  regs->dma_cndtr   = __HAL_DMA_GET_COUNTER(&g_hdma_adc);
  regs->dma_ccr     = g_hdma_adc.Instance->CCR;
  regs->dma_pos     = mic_dma_pos();

  return OK;
}

void sf32lb_audcodec_analyze(const int16_t *buf, size_t samples,
                             struct sf32lb_audio_stats_s *stats)
{
  int64_t sum  = 0;
  int64_t sum2 = 0;
  size_t  i;

  stats->min   = 0;
  stats->max   = 0;
  stats->mean  = 0;
  stats->rms   = 0;
  stats->nzero = 0;
  stats->count = samples;

  if (samples == 0)
    {
      return;
    }

  stats->min = buf[0];
  stats->max = buf[0];

  for (i = 0; i < samples; i++)
    {
      int32_t v = buf[i];

      if (v < stats->min)
        {
          stats->min = v;
        }

      if (v > stats->max)
        {
          stats->max = v;
        }

      if (v == 0)
        {
          stats->nzero++;
        }

      sum  += v;
      sum2 += (int64_t)v * v;
    }

  stats->mean = (int32_t)(sum / (int64_t)samples);
  stats->rms  = (int32_t)__builtin_sqrt((double)sum2 / (double)samples);
}
