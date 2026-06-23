#ifndef __G474_STEPPER_H
#define __G474_STEPPER_H

#include <stdint.h>

void Stepper_Init(void);

/* speed: 0=停, 1~255 */
void Stepper_SetSpeed(uint8_t speed);
void Stepper_SetDirection(uint8_t dir);
void Stepper_Stop(void);

#endif
