/****************************************************************************
 * vendor/sifli/chips/sf32lb52/include/sf32lb_audcodec.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SF32LB52 AUDCODEC ADC capture path (on-board MEMS microphone).
 *
 * The vendor HAL ships this block without any board bring-up: MspInit() is
 * empty and HAL_AUDCODEC_Config_ADCPath() is compiled out, so the audio PLL,
 * the ADC channel, the analog front end and the DMA engine all have to be
 * driven by the caller. This driver does that and exposes a plain
 * open/read/close capture interface.
 *
 ****************************************************************************/

#ifndef __VENDOR_SIFLI_CHIPS_SF32LB52_INCLUDE_SF32LB_AUDCODEC_H
#define __VENDOR_SIFLI_CHIPS_SF32LB52_INCLUDE_SF32LB_AUDCODEC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Public Constant Data
 ****************************************************************************/

/* Capacity of the circular DMA ring, in 32-bit words. The ADC deposits one
 * 16-bit sample per word, so this is also the number of samples in flight.
 *
 * This is slack, not latency: the reader only has to keep up on average, and
 * every sample that is still in the ring when the reader gets to it is
 * delivered intact. It is sized to cover one transmit frame plus that
 * frame's transmit latency -- 8192 words == 32 KB == 1.6 s at 5 kHz, against
 * the 1 s frames mic_stream sends.
 *
 * It does not want to be much larger: this is static SRAM on a part with
 * 512 KB total, and the firmware is already past 70% before the heap is
 * touched.
 */

#define SF32LB_AUDIO_DMA_WORDS 8192

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Statistics over a block of captured samples. */

struct sf32lb_audio_stats_s
{
  int16_t min;    /* Smallest sample value          */
  int16_t max;    /* Largest sample value           */
  int32_t mean;   /* Mean (DC offset)               */
  int32_t rms;    /* Root-mean-square amplitude     */
  size_t  nzero;  /* Number of exactly-zero samples */
  size_t  count;  /* Number of samples analysed     */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/* Clock source for the ADC. The vendor HAL documents both, and which one the
 * microphone actually needs is not settled yet, so both stay selectable.
 */

enum sf32lb_audio_clk_e
{
  SF32LB_AUDIO_CLK_PLL = 0,   /* audio PLL (bf0_enable_pll)  */
  SF32LB_AUDIO_CLK_XTAL = 1,  /* 48 MHz crystal, per HAL's reference ADC_CFG */
};

/* Register snapshot, for bring-up only. */

struct sf32lb_audcodec_regs_s
{
  uint32_t cfg;         /* AUDCODEC_CFG        */
  uint32_t adc_cfg;     /* AUDCODEC_ADC_CFG    */
  uint32_t adc_ch0_cfg; /* AUDCODEC_ADC_CH0_CFG */
  uint32_t adc_ch1_cfg; /* AUDCODEC_ADC_CH1_CFG */
  uint32_t adc_ana_cfg; /* AUDCODEC_ADC_ANA_CFG */
  uint32_t dma_cndtr;   /* DMA1 channel 4 counter */
  uint32_t dma_ccr;     /* DMA1 channel 4 config  */
  uint32_t dma_pos;     /* derived write position */
};

/****************************************************************************
 * Name: sf32lb_audcodec_open_ex
 *
 * Description:
 *   Bring up the AUDCODEC ADC path and start continuous circular DMA capture
 *   of the on-board MEMS microphone.
 *
 *   This configures the ADC channel (analog MICBIAS front end) and arms
 *   DMA1 channel 4 in circular mode. On success the driver starts producing
 *   16-bit mono samples at the requested rate.
 *
 * Input Parameters:
 *   sample_rate - Requested sample rate in Hz (16000).
 *   clk         - ADC clock source.
 *   channel     - ADC channel to capture from (0 or 1).
 *   volume_db   - Capture gain in dB, -60 to +30. The ADC channel reset
 *                 value is 0 dB, which leaves a MEMS microphone far too
 *                 quiet for speech recognition; SF32LB_AUDIO_DEFAULT_VOLUME
 *                 is the gain that works out of the box.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int sf32lb_audcodec_open_ex(uint32_t sample_rate,
                            enum sf32lb_audio_clk_e clk, int channel,
                            int volume_db);

#define SF32LB_AUDIO_VOLUME_MIN     (-60)
#define SF32LB_AUDIO_VOLUME_MAX     (30)
#define SF32LB_AUDIO_DEFAULT_VOLUME (30)

/****************************************************************************
 * Name: sf32lb_audcodec_set_clk
 *
 * Description:
 *   Override the ADC clock divider and oversampling ratio used by the next
 *   sf32lb_audcodec_open_ex(). The mapping from these two fields to an actual
 *   sample rate is not documented anywhere in the tree, and the values the
 *   HAL ships with do not land on 16 kHz, so they have to be calibrated
 *   against a measured rate rather than derived.
 *
 * Input Parameters:
 *   clk_div - value written to ADC_CFG.CLK_DIV.
 *   osr_sel - value written to ADC_CFG.OSR_SEL (0:200 1:300 2:400 3:600).
 *
 ****************************************************************************/

void sf32lb_audcodec_set_clk(uint8_t clk_div, uint8_t osr_sel);

/****************************************************************************
 * Name: sf32lb_audcodec_get_clk
 *
 * Description:
 *   Report the divider and OSR currently in use.
 *
 ****************************************************************************/

void sf32lb_audcodec_get_clk(uint8_t *clk_div, uint8_t *osr_sel);

/****************************************************************************
 * Name: sf32lb_audcodec_open
 *
 * Description:
 *   sf32lb_audcodec_open_ex() with the default clock source and channel.
 *
 ****************************************************************************/

int sf32lb_audcodec_open(uint32_t sample_rate);

/****************************************************************************
 * Name: sf32lb_audcodec_read
 *
 * Description:
 *   Copy captured mono samples into 'buf', blocking only until a useful
 *   amount is available -- not until the caller's whole request has been
 *   filled. The caller may therefore get back fewer samples than it asked
 *   for, and must use the return value rather than the request size.
 *
 *   The distinction matters for any caller that transmits what it reads.
 *   Waiting for a full block puts the wait for audio and the use of it in
 *   series, which on a 8192-sample ring means the reader laps the ring during
 *   its own transmit and silently loses part of every second of audio. The
 *   reader here keeps its position across calls, so it hands over everything
 *   the ring accumulated since the previous call and never discards audio it
 *   was not given.
 *
 *   The DMA engine keeps running regardless, so a caller that cannot keep up
 *   on average still misses audio rather than blocking the capture path.
 *
 *   The samples are compacted out of the DMA words: the ADC produces one
 *   16-bit sample per 32-bit DMA word.
 *
 * Input Parameters:
 *   buf     - Destination for 16-bit mono PCM.
 *   samples - Capacity of 'buf', in samples. A request larger than the DMA
 *             ring is truncated.
 *
 * Returned Value:
 *   The number of samples read (<= samples) on success; a negated errno
 *   value on failure. -ETIMEDOUT if the ADC produced nothing at all for
 *   about two seconds.
 *
 ****************************************************************************/

ssize_t sf32lb_audcodec_read(int16_t *buf, size_t samples);

/****************************************************************************
 * Name: sf32lb_audcodec_read_raw
 *
 * Description:
 *   Diagnostic variant of sf32lb_audcodec_read() that hands back the raw
 *   32-bit DMA words without compaction. Used to establish empirically
 *   where in the word the 16-bit sample actually sits.
 *
 * Input Parameters:
 *   buf   - Destination for raw DMA words.
 *   words - Number of words to read.
 *
 * Returned Value:
 *   The number of words read (== words) on success; a negated errno value
 *   on failure.
 *
 ****************************************************************************/

ssize_t sf32lb_audcodec_read_raw(uint32_t *buf, size_t words);

/****************************************************************************
 * Name: sf32lb_audcodec_close
 *
 * Description:
 *   Stop DMA capture and power the ADC path back down.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int sf32lb_audcodec_close(void);

/****************************************************************************
 * Name: sf32lb_audcodec_is_open
 *
 * Description:
 *   Report whether the capture path is currently running.
 *
 * Returned Value:
 *   true if capture is armed.
 *
 ****************************************************************************/

bool sf32lb_audcodec_is_open(void);

/****************************************************************************
 * Name: sf32lb_audcodec_get_regs
 *
 * Description:
 *   Read the AUDCODEC programming back out. Bring-up aid: this block has no
 *   other openvela reference, so when capture misbehaves the first question
 *   is always whether the registers actually took the values intended.
 *
 * Input Parameters:
 *   regs - Receives the register snapshot.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value if capture is not open.
 *
 ****************************************************************************/

int sf32lb_audcodec_get_regs(struct sf32lb_audcodec_regs_s *regs);

/****************************************************************************
 * Name: sf32lb_audcodec_analyze
 *
 * Description:
 *   Compute min/max/mean/RMS/zero-count over a block of samples. Used by
 *   the bring-up self test to decide whether the microphone is really
 *   picking up sound (as opposed to returning silence or a stuck value).
 *
 * Input Parameters:
 *   buf     - Samples to analyse.
 *   samples - Number of samples.
 *   stats   - Receives the result.
 *
 ****************************************************************************/

void sf32lb_audcodec_analyze(const int16_t *buf, size_t samples,
                             struct sf32lb_audio_stats_s *stats);

#ifdef __cplusplus
}
#endif

#endif /* __VENDOR_SIFLI_CHIPS_SF32LB52_INCLUDE_SF32LB_AUDCODEC_H */
