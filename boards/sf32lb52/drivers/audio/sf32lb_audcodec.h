/****************************************************************************
 * vendor/sifli/boards/sf32lb52/drivers/audio/sf32lb_audcodec.h
 *
 * Minimal driver for the SF32LB52 on-chip AUDCODEC DAC playback path
 * (speaker output).
 *
 * The tone modes below form a staged bring-up ladder: each stage rules out
 * one class of uncertainty, so whichever stage fails tells you where the
 * problem is.
 *   Mode 1 (DEBUG) - write constants straight into the DAC, bypassing the
 *                    FIFO and the sample packing format.  Proves the analog
 *                    path, the amplifier and the speaker wiring.
 *   Mode 2 (PIO)   - feed real PCM through the FIFO with CPU polling.
 *                    Proves the FIFO packing and the sinc gain, no DMA.
 *   Mode 4 (DMA)   - the production path: circular DMA, no underrun.
 *
 * Note that mode 0 (the built-in CFG.DAC_1K_MODE tone) is NOT a standalone
 * hardware tone generator: it needs the digital path to be actively clocked
 * with data.  With no data stream only the enable/disable DC step reaches
 * the speaker, which sounds like two pops.  Do not use it to validate the
 * analog path.
 ****************************************************************************/

#ifndef __SF32LB52_DRIVERS_AUDIO_SF32LB_AUDCODEC_H
#define __SF32LB52_DRIVERS_AUDIO_SF32LB_AUDCODEC_H

#include <stdint.h>
#include <stdbool.h>

#define SF32LB_TONE_1K_BUILTIN 0  /* CFG.DAC_1K_MODE built-in 1kHz    */
#define SF32LB_TONE_DEBUG_SQ   1  /* DAC_CH0_DEBUG.BYPASS square wave */
#define SF32LB_TONE_PIO_SINE   2  /* CPU polled FIFO sine             */
#define SF32LB_TONE_SWEEP      3  /* sweep volume / sinc_gain         */
#define SF32LB_TONE_DMA_SINE   4  /* DMA circular sine (no underrun)  */

/* AUDIO_PA_CTRL pin.  It is PA10 on sf32lb52_devkit_lcd, but PA10 is
 * I2C2_SCL on sf32lb52_lchspi_ulp and lckfb_huangshan_pi, hence the knob.
 */

#ifdef CONFIG_SIFLI_AUDCODEC_PA_PIN
#  define SF32LB_AUDIO_PA_PIN CONFIG_SIFLI_AUDCODEC_PA_PIN
#else
#  define SF32LB_AUDIO_PA_PIN 10
#endif

struct sf32lb_audcodec_cfg_s
{
  uint32_t samplerate;  /* passed to bf0_enable_pll to pick the band */
  uint8_t  clk_src_sel; /* DAC_CFG[7]    0 = xtal48M, 1 = PLL        */
  uint8_t  clk_div;     /* DAC_CFG[15:8] always 1 on SF32LB52X       */
  uint8_t  osr_sel;     /* DAC_CFG[3:0]  0:100 1:150 2:300 4:64 ...  */
  uint16_t sinc_gain;   /* DAC_CH0_CFG[25:17], 9 bits                */
  int      volume;      /* Q15.1, 0.5dB per LSB, 0 = 0dB             */
};

int  sf32lb_audcodec_open(const struct sf32lb_audcodec_cfg_s *cfg);
void sf32lb_audcodec_close(void);
void sf32lb_audcodec_pa(bool on);
int  sf32lb_audcodec_tone(int mode, uint32_t tone_hz, uint32_t ms, int amp);
void sf32lb_audcodec_dumpreg(const char *tag);

/* Fill a cfg for the given sample rate from the official SF32LB52X clock
 * table.  Returns OK when the rate is supported, -ENOTSUP otherwise.
 * Supported: 48000 32000 24000 16000 12000 8000 (xtal)
 *            44100 22050 11025 (PLL)
 */

int  sf32lb_audcodec_cfg_for_rate(struct sf32lb_audcodec_cfg_s *cfg,
                                  uint32_t samplerate);

/* Play a block of 16-bit mono PCM over DMA, blocking until it is done.
 * The codec must already be open.  nsamples is a sample count, not bytes.
 */

int  sf32lb_audcodec_play_pcm(const int16_t *pcm, uint32_t nsamples);

#endif /* __SF32LB52_DRIVERS_AUDIO_SF32LB_AUDCODEC_H */
