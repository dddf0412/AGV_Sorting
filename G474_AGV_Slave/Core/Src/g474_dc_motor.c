#include "g474_dc_motor.h"
#include "tim.h"

extern TIM_HandleTypeDef htim2;

void DC_Motor_Init(void)
{
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
}

void DC_Motor_Set(uint8_t pct, uint8_t dir)
{
    if (pct > 100) pct = 100;

    uint16_t pwm = (uint16_t)((uint32_t)pct * DC_MOTOR_PWM_MAX / 100);

    if (dir == 0) {
        HAL_GPIO_WritePin(MOTOR_DIR1_GPIO_Port, MOTOR_DIR1_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(MOTOR_DIR2_GPIO_Port, MOTOR_DIR2_Pin, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(MOTOR_DIR1_GPIO_Port, MOTOR_DIR1_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_DIR2_GPIO_Port, MOTOR_DIR2_Pin, GPIO_PIN_SET);
    }

    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pwm);
}

void DC_Motor_Stop(void)
{
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
    HAL_GPIO_WritePin(MOTOR_DIR1_GPIO_Port, MOTOR_DIR1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_DIR2_GPIO_Port, MOTOR_DIR2_Pin, GPIO_PIN_RESET);
}
