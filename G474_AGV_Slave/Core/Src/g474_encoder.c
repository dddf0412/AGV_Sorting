#include "g474_encoder.h"
#include "tim.h"

extern TIM_HandleTypeDef htim3;

void Encoder_Init(void)
{
    HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);
}

int16_t Encoder_GetCount(void)
{
    return (int16_t)__HAL_TIM_GET_COUNTER(&htim3);
}

void Encoder_Reset(void)
{
    __HAL_TIM_SET_COUNTER(&htim3, 0);
}
