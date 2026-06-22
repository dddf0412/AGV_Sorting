#include "wm8960.h"
#include "i2c.h"
#include "sai.h"
#include <stdio.h>

extern I2C_HandleTypeDef hi2c1;
extern SAI_HandleTypeDef hsai_BlockA1;

/* 寄存器缓存 */
static uint16_t wm8960_regs[56];

/* DMA 播放状态 */
static volatile uint8_t tx_state; /* 0=idle, 1=TxHalf, 2=TxCplt */

/*================ I2C 读写 ================*/

int WM8960_Write_Reg(uint8_t reg, uint16_t dat)
{
    uint8_t buf[2];
    buf[0] = (reg << 1) | ((dat >> 8) & 0x01);
    buf[1] = (uint8_t)(dat & 0xFF);

    HAL_StatusTypeDef rc = HAL_I2C_Master_Transmit(&hi2c1,
        WM8960_ADDR_8BIT, buf, 2, 100);
    if (rc == HAL_OK) {
        wm8960_regs[reg] = dat;
        return 0;
    }
    printf("[WM8960] I2C err=%d @ reg=0x%02X\r\n", rc, reg);
    return -1;
}

uint16_t WM8960_Read_Reg(uint8_t reg)
{
    return wm8960_regs[reg];
}

/*================ 初始化 ================*/

int WM8960_Init(void)
{
    int rc;

    /* 软件复位 */
    rc = WM8960_Write_Reg(WM8960_REG_RESET, 0x0000);
    if (rc) {
        printf("[WM8960] Reset FAIL\r\n");
        return -1;
    }
    HAL_Delay(10);

    /* 上电 */
    WM8960_Write_Reg(WM8960_REG_PWR1,
        WM8960_PWR1_VMID_5K | WM8960_PWR1_VREF);
    WM8960_Write_Reg(WM8960_REG_PWR2,
        WM8960_PWR2_DACL  | WM8960_PWR2_DACR  |
        WM8960_PWR2_LOUT1 | WM8960_PWR2_ROUT1 |
        WM8960_PWR2_SPKL  | WM8960_PWR2_SPKR);
    WM8960_Write_Reg(WM8960_REG_PWR3,
        WM8960_PWR3_SPK_EN | (1 << 3));

    WM8960_Write_Reg(WM8960_REG_ADC_DAC, 0x0000);

    /* 音量 */
    WM8960_Write_Reg(WM8960_REG_LOUT1,
        WM8960_VOL_0DB | WM8960_VOL_UPDATE);
    WM8960_Write_Reg(WM8960_REG_ROUT1,
        WM8960_VOL_0DB | WM8960_VOL_UPDATE);
    WM8960_Write_Reg(WM8960_REG_SPK_LVOL,
        WM8960_SPK_6DB | WM8960_VOL_UPDATE);
    WM8960_Write_Reg(WM8960_REG_SPK_RVOL,
        WM8960_SPK_6DB | WM8960_VOL_UPDATE);
    WM8960_Write_Reg(WM8960_REG_CLASSD, WM8960_CLASSD_EN);
    WM8960_Write_Reg(WM8960_REG_DAC_VOL_L,
        0x00FF | WM8960_VOL_UPDATE);
    WM8960_Write_Reg(WM8960_REG_DAC_VOL_R,
        0x00FF | WM8960_VOL_UPDATE);
    WM8960_Write_Reg(WM8960_REG_LEFT_MIX,
        WM8960_MIX_DACL_TO_L | WM8960_MIX_DACR_TO_L);
    WM8960_Write_Reg(WM8960_REG_RIGHT_MIX,
        WM8960_MIX_DACR_TO_R | WM8960_MIX_DACL_TO_R);

    WM8960_Write_Reg(WM8960_REG_JACK2, (1 << 6) | (0 << 5));
    WM8960_Write_Reg(WM8960_REG_JACK1, 0x01C3);
    WM8960_Write_Reg(WM8960_REG_JACK3, 0x0009);

    /* 时钟: MCLK 直通 → SYSCLK=24MHz */
    WM8960_Write_Reg(WM8960_REG_CLOCK1,
        WM8960_CLOCK1_SYSCLK_MCLK);
    /* 音频接口: I2S, 16-bit, Slave */
    WM8960_Write_Reg(WM8960_REG_AUDIO_IF,
        WM8960_AUDIO_IF_I2S_16B_SLAVE);

    printf("[WM8960] Init OK\r\n");
    return 0;
}

/*================ 音量控制 ================*/

void WM8960_SetVolume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    uint16_t h = (vol == 0) ? 0x2F : (0x30 + ((uint32_t)vol * 0x50 / 100));
    uint16_t s = (vol == 0) ? 0x37 : (0x38 + ((uint32_t)vol * 0x47 / 100));
    WM8960_Write_Reg(WM8960_REG_LOUT1, h | WM8960_VOL_UPDATE);
    WM8960_Write_Reg(WM8960_REG_ROUT1, h | WM8960_VOL_UPDATE);
    WM8960_Write_Reg(WM8960_REG_SPK_LVOL, s | WM8960_VOL_UPDATE);
    WM8960_Write_Reg(WM8960_REG_SPK_RVOL, s | WM8960_VOL_UPDATE);
}

/*================ 播放 (Circular DMA) ================*/

int WM8960_Play(uint16_t *buf, uint32_t size_hw)
{
    tx_state = 0;

    /* 启动 PLL2 (CubeMX SAI MspInit 不含 PLL2) */
    if ((RCC->CR & RCC_CR_PLL2RDY) == 0) {
        RCC_PeriphCLKInitTypeDef clk = {0};
        clk.PeriphClockSelection = RCC_PERIPHCLK_PLL2_DIVP;
        clk.PLL2.PLL2M = 8;
        clk.PLL2.PLL2N = 172;
        clk.PLL2.PLL2P = 2;
        clk.PLL2.PLL2Q = 4;
        clk.PLL2.PLL2R = 2;
        clk.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_0;
        clk.PLL2.PLL2VCOSEL = RCC_PLL2VCOMEDIUM;
        clk.PLL2.PLL2FRACN = 0;
        HAL_RCCEx_PeriphCLKConfig(&clk);
    }

    /* 清除 SAI 错误标志 */
    hsai_BlockA1.Instance->CLRFR = 0xFFFFFFFF;

    HAL_StatusTypeDef rc = HAL_SAI_Transmit_DMA(&hsai_BlockA1,
        (uint8_t *)buf, size_hw);
    if (rc != HAL_OK) {
        printf("[WM8960] SAI DMA start failed: %d\r\n", rc);
        return -1;
    }
    return 0;
}

void WM8960_Stop(void)
{
    HAL_SAI_DMAStop(&hsai_BlockA1);
    tx_state = 0;
}

/*================ SAI DMA 回调 ================*/

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == &hsai_BlockA1) tx_state = 1;
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == &hsai_BlockA1) tx_state = 2;
}

void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == &hsai_BlockA1) {
        printf("[WM8960] SAI Error %lu\r\n", hsai->ErrorCode);
    }
}
