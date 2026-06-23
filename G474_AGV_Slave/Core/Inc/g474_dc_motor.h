#ifndef __G474_DC_MOTOR_H
#define __G474_DC_MOTOR_H

#include <stdint.h>

#define DC_MOTOR_PWM_MAX  999  /* TIM2 ARR */

void DC_Motor_Init(void);

/* pct: 0~100, dir: 0=正转/1=反转 */
void DC_Motor_Set(uint8_t pct, uint8_t dir);
void DC_Motor_Stop(void);

#endif
