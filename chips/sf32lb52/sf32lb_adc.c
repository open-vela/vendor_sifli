/*
 * SPDX-FileCopyrightText: 2019-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/param.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>
#include <string.h>

#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <nuttx/mutex.h>

#include "bf0_hal_adc.h"
#include "bf0_hal_lcpu_config.h"
#include "gpadc.h"

#include "sf32lb_adc.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct adc_info_s
{
    ADC_HandleTypeDef adc_handle;
    const struct adc_callback_s *cb;
    uint8_t channel;
    float adc_vol_offset;
    float adc_vol_ratio;
    uint32_t adc_thd_reg;
    uint32_t ref;
    uint8_t initialized;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int adc_bind(struct adc_dev_s *dev,
                    const struct adc_callback_s *callback);
static void adc_reset(struct adc_dev_s *dev);
static int adc_setup(struct adc_dev_s *dev);
static void adc_shutdown(struct adc_dev_s *dev);
static void adc_rxint(struct adc_dev_s *dev, bool enable);
static int adc_ioctl(struct adc_dev_s *dev, int cmd, unsigned long arg);
static void adc_read_work(struct adc_dev_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct adc_info_s g_adc_info;

static const struct adc_ops_s g_adcops =
{
    .ao_bind      = adc_bind,
    .ao_reset     = adc_reset,
    .ao_setup     = adc_setup,
    .ao_shutdown  = adc_shutdown,
    .ao_rxint     = adc_rxint,
    .ao_ioctl     = adc_ioctl,
};

static struct adc_dev_s g_adc_chan_dev =
{
    .ad_ops  = &g_adcops,
    .ad_priv = &g_adc_info,
};

static mutex_t g_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t adc_read(ADC_HandleTypeDef *adc_handle, uint8_t channel)
{
    int ret;
    uint32_t value;
    ADC_ChannelConfTypeDef chan_cfg;

    memset(&chan_cfg, 0, sizeof(chan_cfg));
    chan_cfg.pchnl_sel = channel;
    chan_cfg.slot_en = 1;
    chan_cfg.nchnl_sel = 0;
    chan_cfg.Channel = channel;
    chan_cfg.acc_num = 0;

    HAL_ADC_ConfigChannel(adc_handle, &chan_cfg);

    HAL_ADC_Start(adc_handle);

#if ADC_USE_AVERAGE
    int i;
    int j;
    uint32_t data[ADC_AVERAGE_COUNT];
    uint32_t total = 0;
    uint32_t tmp;

    for (i = 0; i < ADC_AVERAGE_COUNT; i++)
    {
        ADC_FRC_EN(adc_handle);
        HAL_Delay_us(200);

        __HAL_ADC_START_CONV(adc_handle);

        ret = HAL_ADC_PollForConversion(adc_handle, 100);
        if (ret != HAL_OK)
        {
            HAL_ADC_Stop(adc_handle);
            syslog(LOG_ERR, "Polling ADC fail %d\n", ret);
            return 0;
        }

        /* Read slot-0 result in single slot mode. */
        data[i] = (uint32_t)HAL_ADC_GetValue(adc_handle, 0);
        ADC_CLR_FRC_EN(adc_handle);

        total += data[i];
        HAL_Delay_us(10 * 1000);
    }

    for (i = 0; i < ADC_AVERAGE_COUNT - 1; i++)
    {
        for (j = 0; j < ADC_AVERAGE_COUNT - 1 - i; j++)
        {
            if (data[j] > data[j + 1])
            {
                tmp = data[j];
                data[j] = data[j + 1];
                data[j + 1] = tmp;
            }
        }
    }

    total -= data[0];
    total -= data[ADC_AVERAGE_COUNT - 1];
    value = total / (ADC_AVERAGE_COUNT - 2);
#else
    __HAL_ADC_START_CONV(adc_handle);

    ret = HAL_ADC_PollForConversion(adc_handle, 100);
    if (ret != HAL_OK)
    {
        HAL_ADC_Stop(adc_handle);
        syslog(LOG_ERR, "Polling ADC fail %d\n", ret);
        return 0;
    }

    value = (uint32_t)HAL_ADC_GetValue(adc_handle, 0);
#endif

    HAL_ADC_Stop(adc_handle);

    return value;
}

static void sf32lb_adc_calibrate(struct adc_info_s *priv)
{
    HAL_LCPU_CONFIG_ADC_T cfg;
    uint16_t len = (uint16_t)sizeof(HAL_LCPU_CONFIG_ADC_T);
    uint32_t reg_max;
    float gap1;
    float gap2;

    memset(&cfg, 0, sizeof(cfg));

    reg_max = GPADC_ADC_RDATA0_SLOT0_RDATA >> GPADC_ADC_RDATA0_SLOT0_RDATA_Pos;
    priv->adc_vol_ratio = 1000.0f;
    priv->adc_vol_offset = 0.0f;
    priv->adc_thd_reg = reg_max > 3 ? reg_max - 3 : reg_max;

    /* HAL_LCPU_CONFIG_get() forces an LCPU wake via HAL_HPAON_WakeCore(),
     * which busy-loops on HPSYS_AON_ISSR_LP_ACTIVE. Single-core boards
     * (e.g. LCKFB Huangshan / sf32lb52_lchspi_ulp) never run LCPU so the
     * call hangs HCPU forever. Skip the lookup and use factory defaults.
     */

    syslog(LOG_WARNING, "ADC calibration data missing, use defaults\n");
    cfg.vol10 = 1758;
    cfg.vol25 = 3162;
    cfg.low_mv = 1000;
    cfg.high_mv = 2500;

    cfg.vol10 &= 0x7fff;
    cfg.vol25 &= 0x7fff;

    gap1 = cfg.vol10 > cfg.vol25 ? (float)(cfg.vol10 - cfg.vol25)
                                 : (float)(cfg.vol25 - cfg.vol10);
    gap2 = cfg.low_mv > cfg.high_mv ? (float)(cfg.low_mv - cfg.high_mv)
                                    : (float)(cfg.high_mv - cfg.low_mv);

    if (gap1 < 1.0f)
    {
        return;
    }

    priv->adc_vol_ratio = gap2 * 1000.0f / gap1;
    if (priv->adc_vol_ratio < 1.0f)
    {
        priv->adc_vol_ratio = 1000.0f;
    }

    priv->adc_vol_offset = (float)cfg.vol10 -
                           ((float)cfg.low_mv * 1000.0f / priv->adc_vol_ratio);

    priv->adc_thd_reg = (uint32_t)(3300.0f * 1000.0f / priv->adc_vol_ratio +
                                   priv->adc_vol_offset);

    if (reg_max > 3 && priv->adc_thd_reg >= (reg_max - 3))
    {
        priv->adc_thd_reg = reg_max - 3;
    }

    syslog(LOG_INFO, "ADC calibration ratio=%d offset=%d\n",
           (int)priv->adc_vol_ratio, (int)priv->adc_vol_offset);
}

static void adc_read_work(struct adc_dev_s *dev)
{
    int ret;
    uint32_t value;
    int32_t adc_mv;
    float offset;
    float ratio;
    struct adc_info_s *ctx = (struct adc_info_s *)dev->ad_priv;
    ADC_HandleTypeDef *adc_handle = (ADC_HandleTypeDef *)&ctx->adc_handle;

    ret = nxmutex_lock(&g_lock);
    if (ret < 0)
    {
        syslog(LOG_ERR, "Failed to lock ADC mutex ret=%d\n", ret);
        return;
    }

    value = adc_read(adc_handle, ctx->channel);

    offset = ctx->adc_vol_offset;
    ratio = ctx->adc_vol_ratio;
    if (ratio <= 0.0f)
    {
        ratio = 1000.0f;
    }

    adc_mv = (int32_t)(((float)value - offset) * ratio / 1000.0f);
    if (adc_mv < 0)
    {
        adc_mv = 0;
    }

    if (ctx->cb != NULL && ctx->cb->au_receive != NULL)
    {
        ctx->cb->au_receive(dev, ctx->channel, adc_mv);
    }

    nxmutex_unlock(&g_lock);
}

static int adc_bind(struct adc_dev_s *dev,
                    const struct adc_callback_s *callback)
{
    struct adc_info_s *ctx = (struct adc_info_s *)dev->ad_priv;

    ctx->cb = callback;

    return OK;
}

static void adc_reset(struct adc_dev_s *dev)
{
    struct adc_info_s *ctx = (struct adc_info_s *)dev->ad_priv;

    if (ctx->ref > 0)
    {
        ctx->ref = 0;
    }
}

static int adc_setup(struct adc_dev_s *dev)
{
    struct adc_info_s *ctx = (struct adc_info_s *)dev->ad_priv;

    if (ctx->ref > 0)
    {
        ctx->ref++;
        return OK;
    }

    ctx->ref++;

    return OK;
}

static void adc_rxint(struct adc_dev_s *dev, bool enable)
{
    (void)dev;
    (void)enable;
}

static int adc_ioctl(struct adc_dev_s *dev, int cmd, unsigned long arg)
{
    int ret;

    (void)arg;

    switch (cmd)
    {
        case ANIOC_TRIGGER:
            adc_read_work(dev);
            ret = OK;
            break;

        case ANIOC_GET_NCHANNELS:
            ret = 1;
            break;

        case ANIOC_WDOG_UPPER:
            ret = 1;
            break;

        case ANIOC_WDOG_LOWER:
            ret = 1;
            break;

        default:
            syslog(LOG_ERR, "ERROR: Unknown cmd: %d\n", cmd);
            ret = -ENOTTY;
            break;
    }

    return ret;
}

static void adc_shutdown(struct adc_dev_s *dev)
{
    struct adc_info_s *ctx = (struct adc_info_s *)dev->ad_priv;

    if (ctx->ref > 0)
    {
        ctx->ref--;
    }
}

static void sf32lb_adc_default_config(ADC_HandleTypeDef *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    cfg->Instance = hwp_gpadc1;
    cfg->Init.atten3 = 0;
    cfg->Init.adc_se = 1;
    cfg->Init.adc_force_on = 0;
    cfg->Init.dma_en = 0;
    cfg->Init.op_mode = 0;
    cfg->Init.en_slot = 0;

#ifndef SF32LB55X
    cfg->Init.data_samp_delay = 2;
#if defined(SF32LB52X)
    cfg->Init.conv_width = 75;
    cfg->Init.sample_width = 71;
#else
    cfg->Init.conv_width = 24;
    cfg->Init.sample_width = 22;
#endif
    cfg->Init.avdd_v18_en = 0;
#else
    cfg->Init.clk_div = 0;
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int sf32lb_adc_init(const char *devpath)
{
    int ret = OK;
    struct adc_dev_s *dev;
    struct adc_info_s *ctx;
    ADC_HandleTypeDef *adc_handle;
    ADC_HandleTypeDef loc_ctx;

#ifndef CONFIG_ADC
    syslog(LOG_WARNING, "ADC %s not configured\n", devpath);
    return ret;
#endif

    dev = &g_adc_chan_dev;
    ctx = (struct adc_info_s *)dev->ad_priv;
    adc_handle = (ADC_HandleTypeDef *)&ctx->adc_handle;

    if (ctx->initialized != 1)
    {
        sf32lb_adc_default_config(&loc_ctx);
        memcpy(adc_handle, &loc_ctx, sizeof(ADC_HandleTypeDef));

        ctx->channel = ADC_CHAN_VBAT;
        ctx->ref = 0;

        if (HAL_ADC_Init(adc_handle) != HAL_OK)
        {
            syslog(LOG_ERR, "%s init failed\n", devpath);
            return -EIO;
        }

        sf32lb_adc_calibrate(ctx);

        ret = adc_register(devpath, dev);
        if (ret < 0)
        {
            syslog(LOG_ERR, "ADC register failed, devpath=%s, ret=%d\n",
                   devpath, ret);
            return ret;
        }

        ctx->initialized = 1;
    }

    syslog(LOG_INFO, "ADC %s init done, ret=%d\n", devpath, ret);

    HAL_Delay_us(300 * 1000);

    return ret;
}
