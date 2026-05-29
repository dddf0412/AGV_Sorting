#include "microphone.h"
#include "main.h"   // 包含 stm32h7xx_hal.h，确保 DFSDM 句柄可见

/* 引用 CubeMX 生成的 DFSDM Filter 句柄（默认名，勿改） */
extern DFSDM_Filter_HandleTypeDef hdfsdm1_filter0;

/* 内部双缓冲：DFSDM 寄存器为 32bit，DMA 按 Word 搬运 */
static int32_t mic_buffer[MIC_BUFFER_SIZE * 2];

/* 小端模式下，int32_t 数组内存可直接按 int16_t 解析，低16位即有效音频数据 */
static int16_t *mic_buf16 = (int16_t *)mic_buffer;

static volatile uint8_t mic_half_ready = 0;
static volatile uint8_t mic_full_ready = 0;
static mic_callback_t   mic_user_cb    = NULL;

/* ---------------- HAL 弱定义回调覆盖 ---------------- */

void HAL_DFSDM_FilterRegConvHalfCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm_filter)
{
    if(hdfsdm_filter == &hdfsdm1_filter0)
        mic_half_ready = 1;
}

void HAL_DFSDM_FilterRegConvCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm_filter)
{
    if(hdfsdm_filter == &hdfsdm1_filter0)
        mic_full_ready = 1;
}

/* ---------------- 对外接口 ---------------- */

void mic_start(mic_callback_t cb)
{
    mic_user_cb    = cb;
    mic_half_ready = 0;
    mic_full_ready = 0;

    HAL_DFSDM_FilterRegularStart_DMA(&hdfsdm1_filter0,
                                     mic_buffer,
                                     MIC_BUFFER_SIZE * 2);
}

void mic_process(void)
{
    if(mic_half_ready)
    {
        mic_half_ready = 0;
        if(mic_user_cb != NULL)
            mic_user_cb(&mic_buf16[0], MIC_BUFFER_SIZE);
    }

    if(mic_full_ready)
    {
        mic_full_ready = 0;
        if(mic_user_cb != NULL)
            mic_user_cb(&mic_buf16[MIC_BUFFER_SIZE], MIC_BUFFER_SIZE);
    }
}

void mic_stop(void)
{
    HAL_DFSDM_FilterRegularStop_DMA(&hdfsdm1_filter0);
    mic_user_cb = NULL;
}
