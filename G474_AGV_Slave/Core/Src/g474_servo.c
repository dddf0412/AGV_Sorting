#include "g474_servo.h"
#include "tim.h"

extern TIM_HandleTypeDef htim4;

void Servo_Init(void)
{
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);

    Servo_SetAngle(0, 90);
    Servo_SetAngle(1, 90);
}

void Servo_SetAngle(uint8_t ch, uint8_t angle)
{
    if (angle > 180) angle = 180;

    uint16_t pulse = SERVO_PWM_MIN
                   + ((uint32_t)angle * (SERVO_PWM_MAX - SERVO_PWM_MIN) / 180);

    if (ch == 0) {
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, pulse);
    } else {
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, pulse);
    }
}
