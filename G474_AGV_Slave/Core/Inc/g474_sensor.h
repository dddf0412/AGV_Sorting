#ifndef __G474_SENSOR_H
#define __G474_SENSOR_H

#include <stdint.h>

void Sensor_Init(void);

/* ISR 安全, 无阻塞 — 读 DMA 环形缓存 */
uint16_t Sensor_GetRawLeft(void);
uint16_t Sensor_GetRawRight(void);

#endif
