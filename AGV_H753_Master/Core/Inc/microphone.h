#ifndef __MICROPHONE_H
#define __MICROPHONE_H

#include <stdint.h>

#define MIC_BUFFER_SIZE     512
#define MIC_CHUNK_SAMPLES   256   /* 每帧样本数 (~6.7ms @ 38.4kHz) */

typedef void (*mic_callback_t)(int16_t *buf, uint16_t len);

void mic_start_monitor(mic_callback_t cb);  /* 回调模式 (调试用) */
void mic_start_stream(void);                /* 实时流模式 → USART3 */
void mic_process(void);
void mic_stop(void);

#endif
