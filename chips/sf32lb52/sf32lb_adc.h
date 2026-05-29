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

/* Internal VBAT channel on SF32LB5x family */
#define ADC_CHAN_VBAT          ADC_CHAN_5

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
