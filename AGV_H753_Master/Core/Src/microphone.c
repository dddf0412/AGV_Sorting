#include "microphone.h"
#include "main.h"

extern DFSDM_Filter_HandleTypeDef hdfsdm1_filter0;
extern UART_HandleTypeDef huart3;

static int32_t mic_raw[MIC_BUFFER_SIZE];
static int16_t mic_out[MIC_CHUNK_SAMPLES];

static volatile uint8_t mic_half_ready;
static volatile uint8_t mic_full_ready;
static uint8_t      mic_stream_mode;
static mic_callback_t mic_user_cb;

/* ---------------- 协议帧发送 ---------------- */

static void mic_send_chunk(int16_t *buf, uint16_t n)
{
    uint8_t hdr[12];
    uint32_t dlen = n * 2;

    hdr[0]  = 0xAA;
    hdr[1]  = 0x55;
    hdr[2]  = 2;                    /* type: 2 = PCM */
    hdr[3]  = 1;                    /* channels: 1 */
    hdr[4]  = 0x00; hdr[5] = 0x96; /* 38400 Hz */
    hdr[6]  = 0x10; hdr[7] = 0x00; /* 16 bit */
    hdr[8]  = (dlen >>  0) & 0xFF;
    hdr[9]  = (dlen >>  8) & 0xFF;
    hdr[10] = (dlen >> 16) & 0xFF;
    hdr[11] = (dlen >> 24) & 0xFF;

    HAL_UART_Transmit(&huart3, hdr, 12, 100);
    HAL_UART_Transmit(&huart3, (uint8_t *)buf, dlen, 500);
}

/* ---------------- HAL 回调 ---------------- */

void HAL_DFSDM_FilterRegConvHalfCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm_filter)
{
    if (hdfsdm_filter == &hdfsdm1_filter0) {
        SCB_InvalidateDCache_by_Addr(
            (uint32_t *)((uint32_t)&mic_raw[MIC_BUFFER_SIZE / 2] & ~0x1F),
            (MIC_BUFFER_SIZE / 2 * sizeof(int32_t) + 31) & ~0x1F);
        mic_half_ready = 1;
    }
}

void HAL_DFSDM_FilterRegConvCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm_filter)
{
    if (hdfsdm_filter == &hdfsdm1_filter0) {
        SCB_InvalidateDCache_by_Addr(
            (uint32_t *)((uint32_t)&mic_raw[0] & ~0x1F),
            (MIC_BUFFER_SIZE / 2 * sizeof(int32_t) + 31) & ~0x1F);
        mic_full_ready = 1;
    }
}

/* ---------------- 数据转换 ---------------- */

static void mic_convert(int32_t *raw, int16_t *out, uint16_t offset, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++)
        out[i] = (int16_t)raw[offset + i];
}

/* ---------------- 对外接口 ---------------- */

static void mic_start_dma(void)
{
    mic_half_ready = 0;
    mic_full_ready = 0;
    HAL_DFSDM_FilterRegularStart_DMA(&hdfsdm1_filter0,
                                     (int32_t *)mic_raw, MIC_BUFFER_SIZE);
}

void mic_start_monitor(mic_callback_t cb)
{
    mic_stream_mode = 0;
    mic_user_cb    = cb;
    mic_start_dma();
}

void mic_start_stream(void)
{
    mic_stream_mode = 1;
    mic_user_cb    = NULL;
    mic_start_dma();
}

void mic_process(void)
{
    if (mic_half_ready) {
        mic_half_ready = 0;
        mic_convert(mic_raw, mic_out, MIC_BUFFER_SIZE / 2, MIC_CHUNK_SAMPLES);
        if (mic_stream_mode)
            mic_send_chunk(mic_out, MIC_CHUNK_SAMPLES);
        else if (mic_user_cb)
            mic_user_cb(mic_out, MIC_CHUNK_SAMPLES);
    }

    if (mic_full_ready) {
        mic_full_ready = 0;
        mic_convert(mic_raw, mic_out, 0, MIC_CHUNK_SAMPLES);
        if (mic_stream_mode)
            mic_send_chunk(mic_out, MIC_CHUNK_SAMPLES);
        else if (mic_user_cb)
            mic_user_cb(mic_out, MIC_CHUNK_SAMPLES);
    }
}

void mic_stop(void)
{
    HAL_DFSDM_FilterRegularStop_DMA(&hdfsdm1_filter0);
    mic_user_cb    = NULL;
    mic_stream_mode = 0;
}
