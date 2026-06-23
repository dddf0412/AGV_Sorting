#include "g474_sensor.h"
#include "adc.h"

static uint16_t adc1_dma_buf[1];
static uint16_t adc2_dma_buf[1];

/* CubeMX 已配置: ADC1/ADC2 -> TIM6 TRGO 触发, DMA_CIRCULAR, 已 LINK.
 * 这里只需启动 DMA。 */
void Sensor_Init(void)
{
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc1_dma_buf, 1) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_ADC_Start_DMA(&hadc2, (uint32_t *)adc2_dma_buf, 1) != HAL_OK) {
        Error_Handler();
    }
}

uint16_t Sensor_GetRawLeft(void)
{
    return adc1_dma_buf[0];
}

uint16_t Sensor_GetRawRight(void)
{
    return adc2_dma_buf[0];
}
