/* USER CODE BEGIN Header */
/**
  * @file    nfc.h
  * @brief   MFRC-522 NFC driver + state machine + card manager + UART command
  *          for STM32H753
  */
/* USER CODE END Header */

#ifndef __NFC_H
#define __NFC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/*================ 寄存器与命令定义（原有，保持不变）================*/

#define MFRC522_REG_COMMAND         0x01
#define MFRC522_REG_COM_IEN         0x02
#define MFRC522_REG_DIV_IEN         0x03
#define MFRC522_REG_COM_IRQ         0x04
#define MFRC522_REG_DIV_IRQ         0x05
#define MFRC522_REG_ERROR           0x06
#define MFRC522_REG_STATUS1         0x07
#define MFRC522_REG_STATUS2         0x08
#define MFRC522_REG_FIFO_DATA       0x09
#define MFRC522_REG_FIFO_LEVEL      0x0A
#define MFRC522_REG_WATER_LEVEL     0x0B
#define MFRC522_REG_CONTROL         0x0C
#define MFRC522_REG_BIT_FRAMING     0x0D
#define MFRC522_REG_COLL            0x0E
#define MFRC522_REG_MODE            0x11
#define MFRC522_REG_TX_MODE         0x12
#define MFRC522_REG_RX_MODE         0x13
#define MFRC522_REG_TX_CONTROL      0x14
#define MFRC522_REG_TX_AUTO         0x15
#define MFRC522_REG_TX_SEL          0x16
#define MFRC522_REG_RX_SEL          0x17
#define MFRC522_REG_RX_THRESHOLD    0x18
#define MFRC522_REG_DEMOD           0x19
#define MFRC522_REG_MIFARE          0x1C
#define MFRC522_REG_SERIAL_SPEED    0x1F
#define MFRC522_REG_CRC_RESULT_M    0x21
#define MFRC522_REG_CRC_RESULT_L    0x22
#define MFRC522_REG_MOD_WIDTH       0x24
#define MFRC522_REG_RFCFG           0x26
#define MFRC522_REG_GSN             0x27
#define MFRC522_REG_CWGSCFG         0x28
#define MFRC522_REG_MODGSCFG        0x29
#define MFRC522_REG_TMODE           0x2A
#define MFRC522_REG_TPRESCALER      0x2B
#define MFRC522_REG_TRELOAD_H       0x2C
#define MFRC522_REG_TRELOAD_L       0x2D
#define MFRC522_REG_TCOUNTVAL_H     0x2E
#define MFRC522_REG_TCOUNTVAL_L     0x2F
#define MFRC522_REG_TEST_SEL1       0x31
#define MFRC522_REG_TEST_SEL2       0x32
#define MFRC522_REG_TEST_PIN_EN     0x33
#define MFRC522_REG_TEST_PIN_VALUE  0x34
#define MFRC522_REG_TEST_BUS        0x35
#define MFRC522_REG_AUTO_TEST       0x36
#define MFRC522_REG_VERSION         0x37
#define MFRC522_REG_ANALOG_TEST     0x38
#define MFRC522_REG_TEST_DAC1       0x39
#define MFRC522_REG_TEST_DAC2       0x3A
#define MFRC522_REG_TEST_ADC        0x3B

#define MFRC522_CMD_IDLE            0x00
#define MFRC522_CMD_MEM             0x01
#define MFRC522_CMD_GENID           0x02
#define MFRC522_CMD_CALCCRC         0x03
#define MFRC522_CMD_TRANSMIT        0x04
#define MFRC522_CMD_NOCMDCH         0x07
#define MFRC522_CMD_RECEIVE         0x08
#define MFRC522_CMD_TRANSCEIVE      0x0C
#define MFRC522_CMD_AUTHENT         0x0E
#define MFRC522_CMD_SOFTRESET       0x0F

#define PICC_REQIDL                 0x26
#define PICC_REQALL                 0x52
#define PICC_ANTICOLL1              0x93
#define PICC_ANTICOLL2              0x95
#define PICC_AUTHENT1A              0x60
#define PICC_AUTHENT1B              0x61
#define PICC_READ                   0x30
#define PICC_WRITE                  0xA0
#define PICC_DECREMENT              0xC0
#define PICC_INCREMENT              0xC1
#define PICC_RESTORE                0xC2
#define PICC_TRANSFER               0xB0
#define PICC_HALT                   0x50

#define DEF_FIFO_LENGTH             64
#define MI_OK                       0
#define MI_NOTAGERR                 (-1)
#define MI_ERR                      (-2)

/*================ 工作模式定义 =================*/

typedef enum {
    NFC_MODE_IDLE = 0,
    NFC_MODE_ADD_CARD,
    NFC_MODE_VERIFY_CARD
} NFC_Mode_t;

#define NFC_MAX_CARDS  10

typedef struct {
    uint8_t uid[4];
    uint8_t valid;
} NFC_Card_t;

/*================ 对外接口：main.c 只需调用这两个 =================*/

uint8_t NFC_InitSystem(void);   /* 初始化 MFRC-522 + 启动串口接收 */
void NFC_Task(void);            /* 主循环中周期性调用（建议 10ms）*/

/* 以下函数供内部/调试使用，main.c 不需要调用 */
void NFC_SetMode(NFC_Mode_t mode);
NFC_Mode_t NFC_GetMode(void);
uint8_t NFC_AddCard(uint8_t* uid);
uint8_t NFC_VerifyCard(uint8_t* uid);
uint8_t NFC_RemoveCard(uint8_t* uid);
uint8_t NFC_GetCardCount(void);
void NFC_ClearAllCards(void);
void NFC_ListCards(void);

/* 底层驱动（内部使用） */
uint8_t NFC_ReadVersion(void);
void NFC_WriteReg(uint8_t addr, uint8_t val);
uint8_t NFC_ReadReg(uint8_t addr);
void NFC_SetBitMask(uint8_t reg, uint8_t mask);
void NFC_ClearBitMask(uint8_t reg, uint8_t mask);
void NFC_AntennaOn(void);
void NFC_AntennaOff(void);
uint8_t NFC_ToCard(uint8_t cmd, uint8_t* sendData, uint8_t sendLen, uint8_t* backData, uint8_t* backLen);
uint8_t NFC_Request(uint8_t reqMode, uint8_t* tagType);
uint8_t NFC_Anticoll(uint8_t* uid);
uint8_t NFC_SelectTag(uint8_t* uid);
uint8_t NFC_ReadBlock(uint8_t blockAddr, uint8_t* data);
uint8_t NFC_WriteBlock(uint8_t blockAddr, uint8_t* data);
uint8_t NFC_AuthState(uint8_t authMode, uint8_t addr, uint8_t* key, uint8_t* uid);
uint8_t NFC_Halt(void);
void NFC_CalculateCRC(uint8_t* pIndata, uint8_t len, uint8_t* pOutData);

#ifdef __cplusplus
}
#endif

#endif /* __NFC_H */
