#ifndef __WM8960_H
#define __WM8960_H

#include <stdint.h>

/* I2C 地址 */
#define WM8960_ADDR_7BIT    0x1A
#define WM8960_ADDR_8BIT    ((WM8960_ADDR_7BIT) << 1)

/*================ 寄存器地址 ================*/
#define WM8960_REG_LINVOL       0x00
#define WM8960_REG_RINVOL       0x01
#define WM8960_REG_LOUT1        0x02
#define WM8960_REG_ROUT1        0x03
#define WM8960_REG_CLOCK1       0x04
#define WM8960_REG_ADC_DAC      0x05
#define WM8960_REG_CLOCK2       0x06
#define WM8960_REG_AUDIO_IF     0x07
#define WM8960_REG_DAC_VOL_L    0x0A
#define WM8960_REG_DAC_VOL_R    0x0B
#define WM8960_REG_RESET        0x0F
#define WM8960_REG_3D           0x10
#define WM8960_REG_JACK1        0x17
#define WM8960_REG_JACK2        0x18
#define WM8960_REG_PWR1         0x19
#define WM8960_REG_PWR2         0x1A
#define WM8960_REG_LEFT_MIX     0x22
#define WM8960_REG_RIGHT_MIX    0x25
#define WM8960_REG_SPK_LVOL     0x28
#define WM8960_REG_SPK_RVOL     0x29
#define WM8960_REG_PWR3         0x2F
#define WM8960_REG_JACK3        0x30
#define WM8960_REG_CLASSD       0x31
#define WM8960_REG_PLL_N        0x34
#define WM8960_REG_PLL_K1       0x35
#define WM8960_REG_PLL_K2       0x36
#define WM8960_REG_PLL_K3       0x37

/*================ 关键位定义 ================*/

/* R4 CLOCK1 */
#define WM8960_CLOCK1_MS            (1 << 8)  /* Master mode */
#define WM8960_CLOCK1_MCLKDIV2      (1 << 4)  /* MCLK /2 */
#define WM8960_CLOCK1_SYSCLK_MCLK   (0 << 3)  /* SYSCLK from MCLK */

/* R5 ADC_DAC */
#define WM8960_DAC_UNMUTE           0x0000

/* R7 AUDIO_IF: MS=bit6, WL=bits[3:2], FORMAT=bits[1:0] */
#define WM8960_AUDIO_IF_I2S_16B_SLAVE   0x0002
#define WM8960_AUDIO_IF_I2S_16B_MASTER  0x0042

/* R25 PWR1: VMIDSEL[1:0]=bits[8:7], VREF=bit[6] */
#define WM8960_PWR1_VMID_5K     (0x03 << 7)
#define WM8960_PWR1_VMID_250K   (0x02 << 7)
#define WM8960_PWR1_VREF        (1 << 6)

/* R26 PWR2 */
#define WM8960_PWR2_DACL        (1 << 8)
#define WM8960_PWR2_DACR        (1 << 7)
#define WM8960_PWR2_LOUT1       (1 << 6)
#define WM8960_PWR2_ROUT1       (1 << 5)
#define WM8960_PWR2_SPKL        (1 << 4)
#define WM8960_PWR2_SPKR        (1 << 3)

/* R47 PWR3 */
#define WM8960_PWR3_SPK_EN      (1 << 2)

/* R34/R37 MIX */
#define WM8960_MIX_DACL_TO_L    (1 << 8)
#define WM8960_MIX_DACR_TO_R    (1 << 8)
#define WM8960_MIX_DACR_TO_L    (1 << 7)
#define WM8960_MIX_DACL_TO_R    (1 << 7)

/* R49 CLASSD */
#define WM8960_CLASSD_EN        0x00F7

/* 音量 */
#define WM8960_VOL_UPDATE       (1 << 8)
#define WM8960_VOL_0DB          0x6F
#define WM8960_SPK_6DB          0x7F

/*================ API ================*/
int      WM8960_Write_Reg(uint8_t reg, uint16_t dat);
uint16_t WM8960_Read_Reg(uint8_t reg);
int      WM8960_Init(void);
int      WM8960_Play(uint16_t *buf, uint32_t size_hw);
void     WM8960_Stop(void);
void     WM8960_SetVolume(uint8_t vol);         /* 0–100 */

#endif
