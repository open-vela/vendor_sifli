/****************************************************************************
 * boards/sf32lb52/sf32lb52_devkit_lcd/src/sf32lb52_audio.h
 *
 * SF32LB52 audio device driver (audio_lowerhalf implementation)
 * Registers /dev/audio0 and supports:
 *   - Playback (DAC + DMA): speaker/headphone output
 *   - Recording (ADC + DMA): on-board MEMS microphone input
 *   - Power amplifier (AW8155, PA10 = AU_PA_EN) control
 *
 ****************************************************************************/

#ifndef __BOARDS_SF32LB52_SF32LB52_DEVTKIT_LCD_SRC_SF32LB52_AUDIO_H
#define __BOARDS_SF32LB52_SF32LB52_DEVTKIT_LCD_SRC_SF32LB52_AUDIO_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/audio/audio.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: sf32lb52_audio_initialize
 *
 * Description:
 *   Initializes the SF32LB52 audio device (codec + amplifier) and registers it as /dev/audio0
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

int sf32lb52_audio_initialize(void);

#endif /* __BOARDS_SF32LB52_SF32LB52_DEVTKIT_LCD_SRC_SF32LB52_AUDIO_H */
