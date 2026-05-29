#ifndef __MICROPHONE_H
#define __MICROPHONE_H

#include <stdint.h>

#define MIC_BUFFER_SIZE     512

/**
 * @brief 音频数据就绪回调
 * @param buf  音频样本缓冲区首地址（有符号16bit，低16位有效）
 * @param len  本次回调的样本数（固定为 MIC_BUFFER_SIZE）
 * @note  回调运行在 DMA 中断上下文，尽量只做标记/拷贝，避免阻塞
 */
typedef void (*mic_callback_t)(int16_t *buf, uint16_t len);

/**
 * @brief 启动麦克风采集
 * @param cb  用户数据处理回调，传 NULL 则不回调
 */
void mic_start(mic_callback_t cb);

/**
 * @brief 主循环轮询处理（非阻塞）
 *        内部检查 DMA 半传输/全传输标志，就绪后调用用户回调
 */
void mic_process(void);

/**
 * @brief 停止麦克风采集并释放 DMA
 */
void mic_stop(void);

#endif /* __MICROPHONE_H */
