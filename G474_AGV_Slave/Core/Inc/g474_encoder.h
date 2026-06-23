#ifndef __G474_ENCODER_H
#define __G474_ENCODER_H

#include <stdint.h>

void Encoder_Init(void);

/* ISR 安全 — 读 TIM3 CNT */
int16_t Encoder_GetCount(void);
void    Encoder_Reset(void);

#endif
