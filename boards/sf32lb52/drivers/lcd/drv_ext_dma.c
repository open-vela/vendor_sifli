/*
 * SPDX-FileCopyrightText: 2019-2022 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "drv_lcd_nuttx.h"
#include <nuttx/cache.h>
#include <stdio.h>
#include <string.h>
#include "drv_ext_dma.h"
#include "drv_io.h"
#include "bf0_hal_ext_dma.h"

pCallback full_cb = NULL;
pCallback err_cb = NULL;

EXT_DMA_HandleTypeDef gExtDma = {0};

rt_sem_t ExtDma_sema = NULL;

static void EXT_DMA_CPLT_CB(EXT_DMA_HandleTypeDef *_hdma);
static void EXT_DMA_ERR_CB(EXT_DMA_HandleTypeDef *_hdma);
void EXTDMA_IRQHandler(void);

static int extdma_nuttx_isr(int irq, FAR void *context, FAR void *arg)
{
    UNUSED(irq);
    UNUSED(context);
    UNUSED(arg);
    EXTDMA_IRQHandler();
    return OK;
}

int EXT_DMA_Init(void)
{
    int ret;

    if (!ExtDma_sema)
    {
        ExtDma_sema = calloc(1, sizeof(*ExtDma_sema));
        if (ExtDma_sema != NULL)
        {
            ret = nxsem_init(&ExtDma_sema->sem, 0, 1);
            if (ret < 0)
            {
                free(ExtDma_sema);
                ExtDma_sema = NULL;
                return ret;
            }
            ExtDma_sema->value = 1;
        }
        RT_ASSERT(ExtDma_sema != NULL);

        ret = irq_attach(NX_IRQ(EXTDMA_IRQn), extdma_nuttx_isr, NULL);
        if (ret < 0) return ret;
        up_enable_irq(NX_IRQ(EXTDMA_IRQn));
    }

    return 0;
}

static void EXT_DMA_Lock(void)
{
    RT_ASSERT(ExtDma_sema != NULL);

    rt_err_t err;
    err = nxsem_tickwait_uninterruptible(&ExtDma_sema->sem,
                                         MSEC2TICK(1000));
    if (err == 0) ExtDma_sema->value = 0;
    RT_ASSERT(RT_EOK == err);
}

static void EXT_DMA_Unlock(void)
{
    RT_ASSERT(ExtDma_sema != NULL);

    rt_err_t err;
    err = nxsem_post(&ExtDma_sema->sem);
    if (err == 0) ExtDma_sema->value = 1;
    RT_ASSERT(RT_EOK == err);
}

/**
 * @brief EXT DMA Interrupt handler.
 */
void EXTDMA_IRQHandler(void)
{
    HAL_EXT_DMA_IRQHandler(&gExtDma);
}

rt_err_t EXT_DMA_ConfigCmpr(uint8_t src_inc, uint8_t dst_inc, const EXT_DMA_CmprTypeDef *cmpr)
{
    rt_err_t res = 0;
    if (!cmpr)
    {
        return -RT_ERROR;
    }

    EXT_DMA_Lock();

    if (src_inc)
        gExtDma.Init.SrcInc = HAL_EXT_DMA_SRC_INC | HAL_EXT_DMA_SRC_BURST16;
    else
        gExtDma.Init.SrcInc = HAL_EXT_DMA_SRC_BURST1;

    if (dst_inc)
        gExtDma.Init.DstInc = HAL_EXT_DMA_DST_INC | HAL_EXT_DMA_DST_BURST16;
    else
        gExtDma.Init.DstInc = HAL_EXT_DMA_DST_BURST1;

    gExtDma.Init.cmpr_en = cmpr->cmpr_en;
    gExtDma.Init.src_format = cmpr->src_format;
    gExtDma.Init.cmpr_rate = cmpr->cmpr_rate;
    gExtDma.Init.col_num = cmpr->col_num;
    gExtDma.Init.row_num = cmpr->row_num;

    return res;
}

rt_err_t EXT_DMA_Config(uint8_t src_inc, uint8_t dst_inc)
{
    const EXT_DMA_CmprTypeDef cmpr_none =
    {
        .cmpr_en = false,
    };
    if (ExtDma_sema == NULL)
        return -RT_ERROR;
    return EXT_DMA_ConfigCmpr(src_inc, dst_inc, &cmpr_none);
}

rt_err_t EXT_DMA_START_ASYNC(uint32_t src, uint32_t dst, uint32_t len)
{
    HAL_StatusTypeDef res;

#ifdef CONFIG_PM
    pm_stay(PM_IDLE_DOMAIN, PM_IDLE);
#endif  /* CONFIG_PM */

    /* reset extdma to make CMPRDR.MAXBUF is updated if overflow happens */
    HAL_RCC_ResetModule(RCC_MOD_EXTDMA);
    res = HAL_EXT_DMA_Init(&gExtDma);
    if (HAL_OK == res)
    {
        /* NVIC configuration for DMA transfer complete interrupt */
        HAL_NVIC_SetPriority(EXTDMA_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(EXTDMA_IRQn);
        HAL_EXT_DMA_RegisterCallback(&gExtDma, HAL_EXT_DMA_XFER_CPLT_CB_ID, EXT_DMA_CPLT_CB);
        HAL_EXT_DMA_RegisterCallback(&gExtDma, HAL_EXT_DMA_XFER_ERROR_CB_ID, EXT_DMA_ERR_CB);

        up_clean_dcache((uintptr_t)src,
                        (uintptr_t)src + (uintptr_t)len * sizeof(uint32_t));
        res = HAL_EXT_DMA_Start_IT(&gExtDma, src, dst, len);
    }
    else
    {
        //Act like EXTDMA error: err irq + cmplt irq.
        EXT_DMA_ERR_CB(&gExtDma);
        EXT_DMA_CPLT_CB(&gExtDma);
    }

    return HAL_OK == res ?  RT_EOK : (rt_err_t)res;
}

rt_err_t EXT_DMA_TRANS_SYNC(uint32_t src, uint32_t dst, uint32_t len, uint32_t timeout)
{
    HAL_StatusTypeDef res = 0;

    /* reset extdma to make CMPRDR.MAXBUF is updated if overflow happens */
    HAL_RCC_ResetModule(RCC_MOD_EXTDMA);
    res = HAL_EXT_DMA_Init(&gExtDma);

    res = HAL_EXT_DMA_Start(&gExtDma, src, dst, len);
    if (HAL_OK == res)
    {
        res = HAL_EXT_DMA_PollForTransfer(&gExtDma, HAL_EXT_DMA_FULL_TRANSFER, timeout);
    }
    if (res != HAL_OK)
    {
        res = HAL_EXT_DMA_GetError(&gExtDma);
    }

    EXT_DMA_Unlock();

    return HAL_OK == res ?  RT_EOK : (rt_err_t)res;
}

uint32_t EXT_DMA_GetError(void)
{
    return HAL_EXT_DMA_GetError(&gExtDma);
}

void EXT_DMA_Register_Callback(EXT_DMA_CallbackIDTypeDef cid, pCallback cb)
{
    if (cid == EXT_DMA_XFER_CPLT_CB_ID)
    {
        full_cb = cb;
    }

    if (cid == EXT_DMA_XFER_ERROR_CB_ID)
    {
        err_cb = cb;
    }
}

void EXT_DMA_Wait_ASYNC_Done(void)
{
    EXT_DMA_Lock();
    EXT_DMA_Unlock();
}

static void EXT_DMA_CPLT_CB(EXT_DMA_HandleTypeDef *_hdma)
{
    HAL_NVIC_DisableIRQ(EXTDMA_IRQn);

#ifdef CONFIG_PM
    pm_relax(PM_IDLE_DOMAIN, PM_IDLE);
#endif  /* CONFIG_PM */

    if (full_cb)
        full_cb();

    EXT_DMA_Unlock();
}

static void EXT_DMA_ERR_CB(EXT_DMA_HandleTypeDef *_hdma)
{
#if 0//def CONFIG_PM  The CPLT_CB always come, although there are error occurs
    pm_relax(PM_IDLE_DOMAIN, PM_IDLE);
#endif  /* CONFIG_PM */
    if (err_cb)
        err_cb();
}

INIT_BOARD_EXPORT(EXT_DMA_Init);

//#define DRV_EXT_DMA_TEST
#ifdef DRV_EXT_DMA_TEST

#include "drv_flash.h"
#include "drv_psram.h"

#define EXT_DMA_IT

#define FLASH_TEST_ADDR         FLASH_BASE_ADDR
#define PSRAM_TEST_ADDR         PSRAM_BASE

#ifdef EXT_DMA_IT

static uint32_t endflag = 0;
void psram_dma_done_cb()
{
    endflag = 1;
    LOG_I("psram with ext dma interrupt done\n");
}

void psram_dma_err_cb()
{
    endflag = 2;
    LOG_I("psram with ext dma interrupt error\n");
}

#endif

/*****************************************************************
 ***** support memory to memory, it should include 4 cases ****
 *
 * 1. sram to psram
 * 2. psram to sram
 * 3. flash to sram
 * 4. flash to psram
******************************************************************/

void edma_help()
{
    LOG_I("*** edma test command parameter: ***\n");
    LOG_I("*** 1 ---- sram to psram, sram use fixed address\n");
    LOG_I("*** 2 ---- psram to sram, sram use fixed address\n");
    LOG_I("*** 3 ---- flash to sram, sram use fixed address\n");
    LOG_I("*** 4 ---- flash to psram, address auto increased\n");
    LOG_I("*** 5 ---- flash to psram without dma\n");
}

int cmd_edma(int argc, char *argv[])
{
    int cmd, i;
    rt_uint32_t start, end;
    if (argc >= 2)
    {
        cmd = atoi(argv[1]);
        switch (cmd)
        {
        case 1: // sram use fixed address, psram address increase auto
        {
            EXT_DMA_Config(0, 1);

            HAL_StatusTypeDef res = HAL_OK;
            rt_uint32_t sram_data = 0x5aa5a55a;
            rt_uint32_t psram_addr = PSRAM_TEST_ADDR;
            endflag = 0;

            EXT_DMA_Register_Callback(EXT_DMA_XFER_CPLT_CB_ID, psram_dma_done_cb);
            EXT_DMA_Register_Callback(EXT_DMA_XFER_ERROR_CB_ID, psram_dma_err_cb);
            start = ((uint32_t)clock_systime_ticks());
            res = EXT_DMA_START_ASYNC((rt_uint32_t)(&sram_data), psram_addr, 0x80000);
            if (res != 0)
            {
                LOG_I("EXT_DMA_START_ASYNC fail with %d\n", res);
                return 1;
            }
            i = 0;
            while (endflag == 0)
            {
                usleep(10 * CONFIG_USEC_PER_TICK);
                i++;
                if (i > 1000)
                {
                    LOG_I("WRITE psram dma with it time out!\n");
                    break;
                }
            }
            if (endflag == 1)
            {
                LOG_I("transfer done\n");
                end = ((uint32_t)clock_systime_ticks());
                LOG_I("sram to psram with 0x%x data use %d tick, speed %d kbps\n", 0x80000 * 4, end - start, (0x80000 * 4 * 8) / (end - start));
            }
            else if (endflag == 2)
                LOG_I("transfer error\n");
            else
                LOG_I("tranfer timeout\n");
            break;
        }
        case 2: // psram address increased, to fixed sram address
        {
            EXT_DMA_Config(1, 0);

            HAL_StatusTypeDef res = HAL_OK;
            rt_uint32_t sram_data = 0xdeadbeaf;
            rt_uint32_t psram_addr = PSRAM_TEST_ADDR;
            endflag = 0;

            EXT_DMA_Register_Callback(EXT_DMA_XFER_CPLT_CB_ID, psram_dma_done_cb);
            EXT_DMA_Register_Callback(EXT_DMA_XFER_ERROR_CB_ID, psram_dma_err_cb);
            start = ((uint32_t)clock_systime_ticks());
            res = EXT_DMA_START_ASYNC(psram_addr, (rt_uint32_t)(&sram_data), 0x80000);
            if (res != 0)
            {
                LOG_I("EXT_DMA_START_ASYNC fail with %d\n", res);
                return 1;
            }
            i = 0;
            while (endflag == 0)
            {
                usleep(10 * CONFIG_USEC_PER_TICK);
                i++;
                if (i > 1000)
                {
                    LOG_I("READ psram dma with it time out!\n");
                    break;
                }
            }
            if (endflag == 1)
            {
                LOG_I("transfer done\n");
                end = ((uint32_t)clock_systime_ticks());
                LOG_I("psram to sram with 0x%x data use %d tick, speed %d kbps\n", 0x80000 * 4, end - start, (0x80000 * 4 * 8) / (end - start));
            }
            else if (endflag == 2)
                LOG_I("transfer error\n");
            else
                LOG_I("tranfer timeout\n");
            break;
        }
        case 3: // flash read, flash address increased to fixed sram address
        {
            EXT_DMA_Config(1, 0);

            HAL_StatusTypeDef res = HAL_OK;
            rt_uint32_t sram_data = 0xdeadbeaf;
            rt_uint32_t flash_addr = FLASH_TEST_ADDR;
            rt_uint8_t tbuf[256];
            endflag = 0;

            // initial flash data to randam
            for (i = 0; i < 256; i++)
                tbuf[i] = (i + 0x3721 * ((uint32_t)clock_systime_ticks())) & 0xff;

            start = ((uint32_t)clock_systime_ticks());
            BSP_Nor_erase(0x10000000, 0x200000);
            end = ((uint32_t)clock_systime_ticks());
            LOG_I("Flash full chip erase used %d tick\n", end - start);
            for (i = 0; i < 0x200000 / 256; i++)
                BSP_Nor_write(flash_addr + i * 256, tbuf, 256);
            start = ((uint32_t)clock_systime_ticks());
            LOG_I("Flash full chip page write used %d tick, speed %d kbps\n", (start - end) * 2, (0x200000 * 8) / (start - end));

            EXT_DMA_Register_Callback(EXT_DMA_XFER_CPLT_CB_ID, psram_dma_done_cb);
            EXT_DMA_Register_Callback(EXT_DMA_XFER_ERROR_CB_ID, psram_dma_err_cb);
            start = ((uint32_t)clock_systime_ticks());
            res = EXT_DMA_START_ASYNC(flash_addr, (rt_uint32_t)(&sram_data), 0x80000);
            if (res != 0)
            {
                LOG_I("EXT_DMA_START_ASYNC fail with %d\n", res);
                return 1;
            }
            i = 0;
            while (endflag == 0)
            {
                usleep(10 * CONFIG_USEC_PER_TICK);
                i++;
                if (i > 1000)
                {
                    LOG_I("READ flash dma with it time out!\n");
                    break;
                }
            }
            if (endflag == 1)
            {
                LOG_I("transfer done\n");
                end = ((uint32_t)clock_systime_ticks());
                LOG_I("flash to sram with 0x%x data use %d tick, speed %d kbps\n", 0x80000 * 4, end - start, (0x80000 * 4 * 8) / (end - start));
                // data check
                LOG_I("src 0x%02x%02x%02x%02x\n", tbuf[255], tbuf[254], tbuf[253], tbuf[252]);
                LOG_I("dst 0x%08x\n", sram_data);
            }
            else if (endflag == 2)
                LOG_I("transfer error\n");
            else
                LOG_I("tranfer timeout\n");
            break;
        }
        case 4: // flash to psram, all address auto increased
        {
            EXT_DMA_Config(1, 1);

            HAL_StatusTypeDef res = HAL_OK;
            rt_uint32_t psram_data = PSRAM_TEST_ADDR;
            rt_uint32_t flash_addr = FLASH_TEST_ADDR;
            rt_uint8_t tbuf[256];
            endflag = 0;

            // initial flash data to randam
            for (i = 0; i < 256; i++)
                tbuf[i] = (i + 0x3721 * ((uint32_t)clock_systime_ticks())) & 0xff;

            start = ((uint32_t)clock_systime_ticks());
            BSP_Nor_erase(0x10000000, 0x200000);
            end = ((uint32_t)clock_systime_ticks());
            LOG_I("Flash full chip erase used %d tick\n", end - start);
            for (i = 0; i < 0x200000 / 256; i++)
                BSP_Nor_write(flash_addr + i * 256, tbuf, 256);
            start = ((uint32_t)clock_systime_ticks());
            LOG_I("Flash full chip page write used %d tick, speed %d kbps\n", (start - end) * 2, (0x200000 * 8) / (start - end));

            EXT_DMA_Register_Callback(EXT_DMA_XFER_CPLT_CB_ID, psram_dma_done_cb);
            EXT_DMA_Register_Callback(EXT_DMA_XFER_ERROR_CB_ID, psram_dma_err_cb);
            start = ((uint32_t)clock_systime_ticks());
            res = EXT_DMA_START_ASYNC(flash_addr, psram_data, 0x80000);
            if (res != 0)
            {
                LOG_I("EXT_DMA_START_ASYNC fail with %d\n", res);
                return 1;
            }
            i = 0;
            while (endflag == 0)
            {
                usleep(10 * CONFIG_USEC_PER_TICK);
                i++;
                if (i > 1000)
                {
                    LOG_I("Flash to Psram time out!\n");
                    break;
                }
            }
            if (endflag == 1)
            {
                LOG_I("transfer done\n");
                end = ((uint32_t)clock_systime_ticks());
                LOG_I("flash to psram with 0x%x data use %d tick, speed %d kbps\n", 0x80000 * 4, end - start, (0x80000 * 4 * 8) / (end - start));
                // data check
                rt_uint32_t *fptr = (rt_uint32_t *)FLASH_TEST_ADDR;
                rt_uint32_t *pptr = (rt_uint32_t *)PSRAM_TEST_ADDR;
                start = ((uint32_t)clock_systime_ticks());
                for (i = 0; i < 0x80000; i++)
                {
                    if (*(fptr + i) != *(pptr + i))
                    {
                        LOG_I("data check fail at pos %d: 0x%x vs 0x%s\n", i, *(fptr + i), *(pptr + i));
                        break;
                    }
                }
                end = ((uint32_t)clock_systime_ticks());
                if (i == 0x80000)
                    LOG_I("flash to psram data check pass, flash/psram read speed %d kbps\n", (0x80000 * 4 * 8) / (end - start));
            }
            else if (endflag == 2)
                LOG_I("transfer error\n");
            else
                LOG_I("tranfer timeout\n");
            break;
        }
        case 5:
        {
            HAL_StatusTypeDef res = HAL_OK;
            //rt_uint32_t psram_data = PSRAM_TEST_ADDR;
            rt_uint32_t flash_addr = FLASH_TEST_ADDR;
            rt_uint32_t *fptr = (rt_uint32_t *)FLASH_TEST_ADDR;
            rt_uint32_t *pptr = (rt_uint32_t *)PSRAM_TEST_ADDR;
            rt_uint8_t tbuf[256];

            // initial flash data to randam
            for (i = 0; i < 256; i++)
                tbuf[i] = (i + 0x3721 * ((uint32_t)clock_systime_ticks())) & 0xff;

            BSP_Nor_erase(0x10000000, 0x200000);
            for (i = 0; i < 0x200000 / 256; i++)
                BSP_Nor_write(flash_addr + i * 256, tbuf, 256);
            start = ((uint32_t)clock_systime_ticks());
            for (i = 0; i < 0x80000; i++)
                *(pptr + i) = *(fptr + i);
            end = ((uint32_t)clock_systime_ticks());
            LOG_I("CPU: flash to psram with 0x%x data use %d tick, speed %d kbps\n", 0x80000 * 4, end - start, (0x80000 * 4 * 8) / (end - start));
            break;
        }
        default:
            LOG_I("Invalid parameter %s\n", argv[1]);
            edma_help();
        }
    }
    else
    {
        LOG_I("Invalid parameter\n");
        edma_help();
    }
    return 0;
}

FINSH_FUNCTION_EXPORT_ALIAS(cmd_edma, __cmd_edma, Test ext_dma driver);

#endif  // DRV_EXT_DMA_TEST
