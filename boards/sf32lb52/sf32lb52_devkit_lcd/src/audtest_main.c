/****************************************************************************
 * vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/src/audtest_main.c
 *
 * NSH command: audtest - staged speaker bring-up test for SF32LB52.
 *
 *   audtest -m 1 -f 500   square wave via DEBUG bypass
 *   audtest -m 2 -f 1000  sine fed to the FIFO by CPU polling
 *   audtest -m 4 -f 1000  sine played over DMA (the production path)
 *   audtest -m 3          sweep volume and sinc_gain
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sf32lb_audcodec.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void audtest_usage(void)
{
  printf("usage: audtest [-m mode] [-f hz] [-t ms] [-a amp]\n"
         "               [-g sinc_gain] [-d clk_div] [-o osr_sel]\n"
         "               [-r samplerate] [-v vol]\n"
         "  -m 0  built-in 1kHz (needs a data stream, see driver header)\n"
         "  -m 1  square wave via DEBUG bypass\n"
         "  -m 2  sine fed to the FIFO by CPU polling\n"
         "  -m 3  sweep volume and sinc_gain\n"
         "  -m 4  sine played over DMA\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  /* Defaults taken from the official SF32LB52X clock table
   * (codec_dac_clk_config_xtal[0] in the SiFli SDK).  clk_div is always 1
   * on 52x - the 10/20/40 values belong to 56x/58x and would run the DAC
   * clock ten times too slow.  SINC_GAIN is 0x14D for a 3.3V AVDD.
   */

  struct sf32lb_audcodec_cfg_s cfg =
    {
      .samplerate  = 48000,
      .clk_src_sel = 0,     /* xtal 48M */
      .clk_div     = 1,
      .osr_sel     = 0, /* 52x: 48k=0 32k=1 24k=5 16k=4 12k=7 8k=8 */
      .sinc_gain   = 0x14D, /* 333 */
      .volume      = 0,     /* 0 dB */
    };

  int mode = SF32LB_TONE_1K_BUILTIN;
  int hz   = 1000;
  int ms   = 2000;
  int amp  = 6000;
  int ret;
  int i;

  for (i = 1; i < argc; i++)
    {
      if (i + 1 >= argc)
        {
          audtest_usage();
          return 1;
        }

      if      (strcmp(argv[i], "-m") == 0) mode           = atoi(argv[++i]);
      else if (strcmp(argv[i], "-f") == 0) hz             = atoi(argv[++i]);
      else if (strcmp(argv[i], "-t") == 0) ms             = atoi(argv[++i]);
      else if (strcmp(argv[i], "-a") == 0) amp            = atoi(argv[++i]);
      else if (strcmp(argv[i], "-g") == 0) cfg.sinc_gain  = atoi(argv[++i]);
      else if (strcmp(argv[i], "-d") == 0) cfg.clk_div    = atoi(argv[++i]);
      else if (strcmp(argv[i], "-o") == 0) cfg.osr_sel    = atoi(argv[++i]);
      else if (strcmp(argv[i], "-r") == 0) cfg.samplerate = atoi(argv[++i]);
      else if (strcmp(argv[i], "-v") == 0) cfg.volume     = atoi(argv[++i]);
      else
        {
          audtest_usage();
          return 1;
        }
    }

  printf("audtest: open (fs=%lu div=%u osr=%u gain=%u vol=%d)\n",
         (unsigned long)cfg.samplerate, cfg.clk_div, cfg.osr_sel,
         cfg.sinc_gain, cfg.volume);

  ret = sf32lb_audcodec_open(&cfg);
  if (ret < 0)
    {
      printf("audtest: open failed: %d\n", ret);
      return 1;
    }

  sf32lb_audcodec_dumpreg("after-open");

  printf("audtest: tone mode=%d f=%dHz t=%dms amp=%d ...\n",
         mode, hz, ms, amp);

  ret = sf32lb_audcodec_tone(mode, hz, ms, amp);
  if (ret < 0)
    {
      printf("audtest: tone failed: %d\n", ret);
    }

  sf32lb_audcodec_dumpreg("after-tone");
  sf32lb_audcodec_close();

  printf("audtest: done\n");
  return 0;
}
