/*
 * SPDX-FileCopyrightText: 2019-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __VENDOR_SIFLI_SF32LB52_ADC_H
#define __VENDOR_SIFLI_SF32LB52_ADC_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ADC_CHAN_0             0
#define ADC_CHAN_1             1
#define ADC_CHAN_2             2
#define ADC_CHAN_3             3
#define ADC_CHAN_4             4
#define ADC_CHAN_5             5
#define ADC_CHAN_6             6
#define ADC_CHAN_7             7

/* Battery sense channel.
 *
 * Upstream comments this as the "internal VBAT channel" and points it at
 * channel 5. On this board that is wrong: measured across two boots with the
 * cell in and out, channel 5 does not move at all (3653/3654 raw both ways),
 * while channel 7 moves 2913 -> 2736 raw and is rock steady between
 * consecutive conversions. Channel 7 is therefore the external divider that
 * the LCKFB Huangshan Pi wires to the battery connector.
 */

#define ADC_CHAN_VBAT          ADC_CHAN_7

#define ADC_USE_AVERAGE        1
#if ADC_USE_AVERAGE
#define ADC_AVERAGE_COUNT      20
#else
#define ADC_AVERAGE_COUNT      1
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int sf32lb_adc_init(const char *devpath);

#endif /* __VENDOR_SIFLI_SF32LB52_ADC_H */
