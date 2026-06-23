#include "g474_stepper.h"
#include "tim.h"

extern TIM_HandleTypeDef htim1;

void Stepper_Init(void)
{
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
}

void Stepper_SetSpeed(uint8_t speed)
{
    if (speed == 0) {
        Stepper_Stop();
        return;
    }

    /* 1~255 -> 100Hz~2kHz */
    uint32_t freq = 100 + (uint32_t)speed * 75 / 10;
    uint16_t arr = (uint16_t)(1000000UL / freq) - 1;
    if (arr > 9999) arr = 9999;
    if (arr < 499) arr = 499;

    __HAL_TIM_SET_AUTORELOAD(&htim1, arr);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, arr / 2);

    if (!(htim1.Instance->CR1 & TIM_CR1_CEN)) {
        __HAL_TIM_ENABLE(&htim1);
    }
}

void Stepper_SetDirection(uint8_t dir)
{
    HAL_GPIO_WritePin(STEP_DIR_GPIO_Port, STEP_DIR_Pin,
                      dir ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Stepper_Stop(void)
{
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
    __HAL_TIM_DISABLE(&htim1);
}
