/* Core/Inc/motor_control.h */
#ifndef __MOTOR_CONTROL_H
#define __MOTOR_CONTROL_H

#include "agv_g474.h"

/*================ 函数声明 ================*/

/* 初始化 */
void Motor_Init(void);

/* 直流电机 (L298N) */
void DC_Motor_SetSpeed(int8_t speed);       /* -100~100 */
void DC_Motor_Stop(void);

/* 步进电机 (DRV8825) */
void Stepper_SetSpeed(uint8_t speed);        /* 0~255 */
void Stepper_SetDirection(uint8_t dir);      /* 0正/1反 */
void Stepper_Stop(void);

/* 舵机 (MG996R) */
void Servo_SetAngle(uint8_t ch, uint8_t angle); /* ch:0俯仰/1偏航, angle:0~180 */

/* 编码器 */
int16_t Encoder_GetCount(void);
void Encoder_Reset(void);

/* 传感器 */
void Sensor_Read(uint8_t* left, uint8_t* right);

/* 1kHz控制循环入口 */
void Control_Loop_1kHz(void);

#endif
