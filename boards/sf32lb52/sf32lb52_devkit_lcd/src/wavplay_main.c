/****************************************************************************
 * vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/src/wavplay_main.c
 *
 * NSH command: wavplay <file.wav>
 *
 * Parse the WAV header, configure the on-chip AUDCODEC for the sample rate
 * found in the file and play it over DMA.  Supports 16-bit PCM, mono or
 * stereo (stereo is downmixed by taking the left channel).
 * The sample rate must be one of those in the official SF32LB52X table:
 *   48000 32000 24000 16000 12000 8000 (xtal)/ 44100 22050 11025 (PLL)
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "sf32lb_audcodec.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct wav_fmt_s
{
  uint16_t audio_format;
  uint16_t num_channels;
  uint32_t sample_rate;
  uint32_t byte_rate;
  uint16_t block_align;
  uint16_t bits_per_sample;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t rd32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* Parse the WAV header and locate the fmt and data chunks.  Returns OK and
 * fills in fmt, the data offset and the data length.
 */

static int wav_parse(int fd, struct wav_fmt_s *fmt,
                     off_t *data_off, uint32_t *data_len)
{
  uint8_t hdr[12];
  uint8_t ck[8];
  bool    got_fmt = false;

  if (read(fd, hdr, 12) != 12)
    {
      return -EIO;
    }

  if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
    {
      fprintf(stderr, "wavplay: not a RIFF/WAVE file\n");
      return -EINVAL;
    }

  /* Walk the chunks, skipping anything that is not fmt or data */

  for (; ; )
    {
      uint32_t cksz;

      if (read(fd, ck, 8) != 8)
        {
          return -EIO;
        }

      cksz = rd32(ck + 4);

      if (memcmp(ck, "fmt ", 4) == 0)
        {
          uint8_t f[16];

          if (cksz < 16 || read(fd, f, 16) != 16)
            {
              return -EIO;
            }

          fmt->audio_format    = rd16(f + 0);
          fmt->num_channels    = rd16(f + 2);
          fmt->sample_rate     = rd32(f + 4);
          fmt->byte_rate       = rd32(f + 8);
          fmt->block_align     = rd16(f + 12);
          fmt->bits_per_sample = rd16(f + 14);
          got_fmt = true;

          if (cksz > 16)
            {
              lseek(fd, cksz - 16, SEEK_CUR);
            }
        }
      else if (memcmp(ck, "data", 4) == 0)
        {
          if (!got_fmt)
            {
              return -EINVAL;
            }

          *data_off = lseek(fd, 0, SEEK_CUR);
          *data_len = cksz;
          return OK;
        }
      else
        {
          /* Odd-sized chunks carry one pad byte */

          if (lseek(fd, cksz + (cksz & 1), SEEK_CUR) < 0)
            {
              return -EIO;
            }
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  struct sf32lb_audcodec_cfg_s cfg;
  struct wav_fmt_s fmt;
  off_t    data_off = 0;
  uint32_t data_len = 0;
  uint8_t  *raw     = NULL;
  int16_t  *mono    = NULL;
  uint32_t  nsamples;
  int       fd;
  int       ret = 1;

  if (argc < 2)
    {
      printf("usage: wavplay <file.wav>\n");
      return 1;
    }

  fd = open(argv[1], O_RDONLY);
  if (fd < 0)
    {
      printf("wavplay: cannot open %s: %d\n", argv[1], errno);
      return 1;
    }

  if (wav_parse(fd, &fmt, &data_off, &data_len) < 0)
    {
      printf("wavplay: bad WAV header\n");
      goto out_close;
    }

  printf("wavplay: %s  %luHz %uch %ubit  %lu bytes\n",
         argv[1], (unsigned long)fmt.sample_rate, fmt.num_channels,
         fmt.bits_per_sample, (unsigned long)data_len);

  if (fmt.audio_format != 1 || fmt.bits_per_sample != 16 ||
      fmt.num_channels < 1 || fmt.num_channels > 2)
    {
      printf("wavplay: only PCM 16bit mono/stereo supported\n");
      goto out_close;
    }

  if (sf32lb_audcodec_cfg_for_rate(&cfg, fmt.sample_rate) < 0)
    {
      printf("wavplay: unsupported rate %lu "
             "(48k/32k/24k/16k/12k/8k/44.1k/22.05k/11.025k)\n",
             (unsigned long)fmt.sample_rate);
      goto out_close;
    }

  /* Read the whole thing up front and play it in one go so that there is
   * no gap between chunks.
   */

  raw = malloc(data_len);
  if (raw == NULL)
    {
      printf("wavplay: out of memory (%lu bytes)\n", (unsigned long)data_len);
      goto out_close;
    }

  lseek(fd, data_off, SEEK_SET);
  if (read(fd, raw, data_len) != (ssize_t)data_len)
    {
      printf("wavplay: short read\n");
      goto out_free;
    }

  /* Downmix stereo to mono by keeping the left channel */

  if (fmt.num_channels == 2)
    {
      uint32_t i;

      nsamples = data_len / 4;
      mono = malloc(nsamples * sizeof(int16_t));
      if (mono == NULL)
        {
          printf("wavplay: out of memory\n");
          goto out_free;
        }

      for (i = 0; i < nsamples; i++)
        {
          mono[i] = (int16_t)rd16(raw + i * 4);
        }
    }
  else
    {
      nsamples = data_len / 2;
      mono = (int16_t *)raw;
    }

  if (sf32lb_audcodec_open(&cfg) < 0)
    {
      printf("wavplay: codec open failed\n");
      goto out_free_mono;
    }

  printf("wavplay: playing %lu samples (%lu ms) ...\n",
         (unsigned long)nsamples,
         (unsigned long)((uint64_t)nsamples * 1000 / fmt.sample_rate));

  ret = sf32lb_audcodec_play_pcm(mono, nsamples);
  if (ret < 0)
    {
      printf("wavplay: play failed: %d\n", ret);
    }

  sf32lb_audcodec_close();
  printf("wavplay: done\n");
  ret = 0;

out_free_mono:
  if (fmt.num_channels == 2 && mono != NULL)
    {
      free(mono);
    }

out_free:
  free(raw);

out_close:
  close(fd);
  return ret;
}
