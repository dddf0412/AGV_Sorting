#ifndef __G474_SERVO_H
#define __G474_SERVO_H

#include <stdint.h>

#define SERVO_PWM_MIN   500
#define SERVO_PWM_MAX   2500
#define SERVO_PWM_ARR   19999

void Servo_Init(void);

/* ch: 0=俯仰, 1=偏航; angle: 0~180 */
void Servo_SetAngle(uint8_t ch, uint8_t angle);

#endif
