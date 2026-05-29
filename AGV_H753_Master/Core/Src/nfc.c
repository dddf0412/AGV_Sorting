/* USER CODE BEGIN Header */
/**
  * @file    nfc.c
  * @brief   MFRC-522 driver + state machine + card manager + UART command parser
  *          All-in-one module, main.c only calls NFC_InitSystem() and NFC_Task()
  */
/* USER CODE END Header */

#include "nfc.h"
#include "spi.h"
#include "gpio.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

/*================ 私有变量 =================*/

static NFC_Mode_t g_nfc_mode = NFC_MODE_IDLE;
static NFC_Card_t g_card_list[NFC_MAX_CARDS];
static uint8_t g_card_count = 0;

/*================ UART 环形缓冲区（模块私有）================*/

#define UART_RX_BUF_SIZE  128
#define UART_CMD_MAX_LEN  32

typedef struct {
    uint8_t  buf[UART_RX_BUF_SIZE];
    uint16_t head;
    uint16_t tail;
} UART_RingBuf_t;

static UART_RingBuf_t rx_ring = {0};
static uint8_t rx_byte;

/*================ 私有函数原型 =================*/

static void NFC_CS_Low(void);
static void NFC_CS_High(void);
static const char* NFC_ModeStr(NFC_Mode_t mode);
static void UART_RxStart(void);
static uint16_t UART_RxCount(void);
static uint8_t UART_RxRead(void);
static void UART_ProcessCmd(void);

/*================ SPI 片选控制 =================*/

static void NFC_CS_Low(void)
{
    HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_RESET);
}

static void NFC_CS_High(void)
{
    HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_SET);
}

/*================ 写寄存器 =================*/

void NFC_WriteReg(uint8_t addr, uint8_t val)
{
    uint8_t tx[2];
    tx[0] = ((addr << 1) & 0x7E);
    tx[1] = val;

    NFC_CS_Low();
    HAL_SPI_Transmit(&hspi1, tx, 2, 100);
    NFC_CS_High();
}

/*================ 读寄存器 =================*/

uint8_t NFC_ReadReg(uint8_t addr)
{
    uint8_t tx, rx;
    tx = ((addr << 1) & 0x7E) | 0x80;

    NFC_CS_Low();
    HAL_SPI_Transmit(&hspi1, &tx, 1, 100);
    HAL_SPI_Receive(&hspi1, &rx, 1, 100);
    NFC_CS_High();

    return rx;
}

/*================ 置位/清零 =================*/

void NFC_SetBitMask(uint8_t reg, uint8_t mask)
{
    uint8_t tmp = NFC_ReadReg(reg);
    NFC_WriteReg(reg, tmp | mask);
}

void NFC_ClearBitMask(uint8_t reg, uint8_t mask)
{
    uint8_t tmp = NFC_ReadReg(reg);
    NFC_WriteReg(reg, tmp & ~mask);
}

/*================ 天线控制 =================*/

void NFC_AntennaOn(void)
{
    uint8_t i = NFC_ReadReg(MFRC522_REG_TX_CONTROL);
    if (!(i & 0x03)) {
        NFC_WriteReg(MFRC522_REG_TX_CONTROL, i | 0x03);
    }
}

void NFC_AntennaOff(void)
{
    NFC_ClearBitMask(MFRC522_REG_TX_CONTROL, 0x03);
}

/*================ CRC 计算 =================*/

void NFC_CalculateCRC(uint8_t* pIndata, uint8_t len, uint8_t* pOutData)
{
    uint8_t i, n;

    NFC_ClearBitMask(MFRC522_REG_DIV_IRQ, 0x04);
    NFC_WriteReg(MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    NFC_SetBitMask(MFRC522_REG_FIFO_LEVEL, 0x80);

    for (i = 0; i < len; i++) {
        NFC_WriteReg(MFRC522_REG_FIFO_DATA, pIndata[i]);
    }

    NFC_WriteReg(MFRC522_REG_COMMAND, MFRC522_CMD_CALCCRC);

    i = 0xFF;
    do {
        n = NFC_ReadReg(MFRC522_REG_DIV_IRQ);
        i--;
    } while ((i != 0) && !(n & 0x04));

    pOutData[0] = NFC_ReadReg(MFRC522_REG_CRC_RESULT_L);
    pOutData[1] = NFC_ReadReg(MFRC522_REG_CRC_RESULT_M);
}

/*================ 与卡片通信 =================*/

uint8_t NFC_ToCard(uint8_t cmd, uint8_t* sendData, uint8_t sendLen, uint8_t* backData, uint8_t* backLen)
{
    uint8_t irqEn = 0x00;
    uint8_t waitFor = 0x00;
    uint8_t lastBits;
    uint8_t n;
    uint8_t status = MI_ERR;
    uint16_t i;

    switch (cmd) {
        case MFRC522_CMD_AUTHENT:
            irqEn = 0x12;
            waitFor = 0x10;
            break;
        case MFRC522_CMD_TRANSCEIVE:
            irqEn = 0x77;
            waitFor = 0x30;
            break;
        default:
            break;
    }

    NFC_WriteReg(MFRC522_REG_COM_IEN, irqEn | 0x80);
    NFC_ClearBitMask(MFRC522_REG_COM_IRQ, 0x80);
    NFC_WriteReg(MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    NFC_SetBitMask(MFRC522_REG_FIFO_LEVEL, 0x80);

    for (i = 0; i < sendLen; i++) {
        NFC_WriteReg(MFRC522_REG_FIFO_DATA, sendData[i]);
    }

    NFC_WriteReg(MFRC522_REG_COMMAND, cmd);

    if (cmd == MFRC522_CMD_TRANSCEIVE) {
        NFC_SetBitMask(MFRC522_REG_BIT_FRAMING, 0x80);
    }

    i = 2000;
    do {
        n = NFC_ReadReg(MFRC522_REG_COM_IRQ);
        i--;
    } while ((i != 0) && !(n & 0x01) && !(n & waitFor));

    NFC_ClearBitMask(MFRC522_REG_BIT_FRAMING, 0x80);

    if (i != 0) {
        if (!(NFC_ReadReg(MFRC522_REG_ERROR) & 0x1B)) {
            status = MI_OK;

            if (n & irqEn & 0x01) {
                status = MI_NOTAGERR;
            }

            if (cmd == MFRC522_CMD_TRANSCEIVE) {
                n = NFC_ReadReg(MFRC522_REG_FIFO_LEVEL);
                lastBits = NFC_ReadReg(MFRC522_REG_CONTROL) & 0x07;

                if (lastBits) {
                    *backLen = (n - 1) * 8 + lastBits;
                } else {
                    *backLen = n * 8;
                }

                if (n == 0) n = 1;
                if (n > DEF_FIFO_LENGTH) n = DEF_FIFO_LENGTH;

                for (i = 0; i < n; i++) {
                    backData[i] = NFC_ReadReg(MFRC522_REG_FIFO_DATA);
                }
            }
        } else {
            status = MI_ERR;
        }
    }

    NFC_SetBitMask(MFRC522_REG_CONTROL, 0x80);
    NFC_WriteReg(MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);

    return status;
}

/*================ 读取版本 =================*/

uint8_t NFC_ReadVersion(void)
{
    return NFC_ReadReg(MFRC522_REG_VERSION);
}

/*================ 初始化 MFRC-522 =================*/

uint8_t NFC_Init(void)
{
    uint8_t version;

    NFC_WriteReg(MFRC522_REG_COMMAND, MFRC522_CMD_SOFTRESET);
    HAL_Delay(1);

    while (NFC_ReadReg(MFRC522_REG_COMMAND) & 0x08);

    version = NFC_ReadVersion();
    if (version != 0x92 && version != 0x91 && version != 0x90) {
        return MI_ERR;
    }

    NFC_WriteReg(MFRC522_REG_MODE, 0x3D);
    NFC_WriteReg(MFRC522_REG_TX_MODE, 0x00);
    NFC_WriteReg(MFRC522_REG_RX_MODE, 0x00);
    NFC_WriteReg(MFRC522_REG_TX_CONTROL, 0x80);
    HAL_Delay(10);
    NFC_WriteReg(MFRC522_REG_TX_AUTO, 0x40);
    NFC_WriteReg(MFRC522_REG_RX_SEL, 0x86);
    NFC_WriteReg(MFRC522_REG_RX_THRESHOLD, 0x84);
    NFC_WriteReg(MFRC522_REG_DEMOD, 0x4D);
    NFC_WriteReg(MFRC522_REG_RFCFG, 0x7F);
    NFC_WriteReg(MFRC522_REG_GSN, 0x88);
    NFC_WriteReg(MFRC522_REG_CWGSCFG, 0x20);
    NFC_WriteReg(MFRC522_REG_MODGSCFG, 0x20);
    NFC_WriteReg(MFRC522_REG_TMODE, 0x8D);
    NFC_WriteReg(MFRC522_REG_TPRESCALER, 0x3E);
    NFC_WriteReg(MFRC522_REG_TRELOAD_H, 0x00);
    NFC_WriteReg(MFRC522_REG_TRELOAD_L, 0x1E);
    NFC_WriteReg(MFRC522_REG_COM_IRQ, 0x7F);
    NFC_WriteReg(MFRC522_REG_DIV_IRQ, 0x7F);

    NFC_AntennaOn();
    HAL_Delay(1);

    return MI_OK;
}

/*================ 寻卡 =================*/

uint8_t NFC_Request(uint8_t reqMode, uint8_t* tagType)
{
    uint8_t status;
    uint8_t len;

    NFC_ClearBitMask(MFRC522_REG_STATUS2, 0x08);
    NFC_WriteReg(MFRC522_REG_BIT_FRAMING, 0x07);
    NFC_SetBitMask(MFRC522_REG_TX_CONTROL, 0x03);

    tagType[0] = reqMode;
    status = NFC_ToCard(MFRC522_CMD_TRANSCEIVE, tagType, 1, tagType, &len);

    if ((status == MI_OK) && (len == 0x10)) {
        return MI_OK;
    }

    return MI_ERR;
}

/*================ 防冲撞 =================*/

uint8_t NFC_Anticoll(uint8_t* uid)
{
    uint8_t status;
    uint8_t len;
    uint8_t i, snr_check;
    uint8_t buf[DEF_FIFO_LENGTH];

    NFC_ClearBitMask(MFRC522_REG_STATUS2, 0x08);
    NFC_WriteReg(MFRC522_REG_BIT_FRAMING, 0x00);
    NFC_ClearBitMask(MFRC522_REG_COLL, 0x80);

    buf[0] = PICC_ANTICOLL1;
    buf[1] = 0x20;

    status = NFC_ToCard(MFRC522_CMD_TRANSCEIVE, buf, 2, buf, &len);

    if (status == MI_OK) {
        snr_check = 0;
        for (i = 0; i < 4; i++) {
            uid[i] = buf[i];
            snr_check ^= buf[i];
        }
        if (snr_check != buf[4]) {
            status = MI_ERR;
        }
    }

    NFC_SetBitMask(MFRC522_REG_COLL, 0x80);

    return status;
}

/*================ 选卡 =================*/

uint8_t NFC_SelectTag(uint8_t* uid)
{
    uint8_t status;
    uint8_t len;
    uint8_t i;
    uint8_t bcc;
    uint8_t buf[9];

    buf[0] = PICC_ANTICOLL1;
    buf[1] = 0x70;

    for (i = 0; i < 4; i++) {
        buf[i + 2] = uid[i];
    }

    bcc = uid[0] ^ uid[1] ^ uid[2] ^ uid[3];
    buf[6] = bcc;

    NFC_CalculateCRC(buf, 7, &buf[7]);

    NFC_ClearBitMask(MFRC522_REG_STATUS2, 0x08);

    status = NFC_ToCard(MFRC522_CMD_TRANSCEIVE, buf, 9, buf, &len);

    if ((status == MI_OK) && (len == 0x18)) {
        return MI_OK;
    }

    return MI_ERR;
}

/*================ 认证 =================*/

uint8_t NFC_AuthState(uint8_t authMode, uint8_t addr, uint8_t* key, uint8_t* uid)
{
    uint8_t status;
    uint8_t len;
    uint8_t i;
    uint8_t buf[12];

    buf[0] = authMode;
    buf[1] = addr;

    for (i = 0; i < 6; i++) {
        buf[i + 2] = key[i];
    }

    for (i = 0; i < 6; i++) {
        buf[i + 8] = uid[i];
    }

    status = NFC_ToCard(MFRC522_CMD_AUTHENT, buf, 12, buf, &len);

    if ((status != MI_OK) || !(NFC_ReadReg(MFRC522_REG_STATUS2) & 0x08)) {
        status = MI_ERR;
    }

    return status;
}

/*================ 读块 =================*/

uint8_t NFC_ReadBlock(uint8_t blockAddr, uint8_t* data)
{
    uint8_t status;
    uint8_t len;
    uint8_t i;
    uint8_t buf[18];

    buf[0] = PICC_READ;
    buf[1] = blockAddr;

    NFC_CalculateCRC(buf, 2, &buf[2]);

    status = NFC_ToCard(MFRC522_CMD_TRANSCEIVE, buf, 4, buf, &len);

    if ((status == MI_OK) && (len == 0x90)) {
        for (i = 0; i < 16; i++) {
            data[i] = buf[i];
        }
        return MI_OK;
    }

    return MI_ERR;
}

/*================ 写块 =================*/

uint8_t NFC_WriteBlock(uint8_t blockAddr, uint8_t* data)
{
    uint8_t status;
    uint8_t len;
    uint8_t i;
    uint8_t buf[18];

    buf[0] = PICC_WRITE;
    buf[1] = blockAddr;

    NFC_CalculateCRC(buf, 2, &buf[2]);

    status = NFC_ToCard(MFRC522_CMD_TRANSCEIVE, buf, 4, buf, &len);

    if ((status != MI_OK) || (len != 4) || ((buf[0] & 0x0F) != 0x0A)) {
        return MI_ERR;
    }

    for (i = 0; i < 16; i++) {
        buf[i] = data[i];
    }

    NFC_CalculateCRC(buf, 16, &buf[16]);

    status = NFC_ToCard(MFRC522_CMD_TRANSCEIVE, buf, 18, buf, &len);

    if ((status != MI_OK) || (len != 4) || ((buf[0] & 0x0F) != 0x0A)) {
        return MI_ERR;
    }

    return MI_OK;
}

/*================ 休眠 =================*/

uint8_t NFC_Halt(void)
{
    uint8_t len;
    uint8_t buf[4];

    buf[0] = PICC_HALT;
    buf[1] = 0;

    NFC_CalculateCRC(buf, 2, &buf[2]);

    NFC_ToCard(MFRC522_CMD_TRANSCEIVE, buf, 4, buf, &len);

    return MI_OK;
}

/*================ 模式名称字符串 =================*/

static const char* NFC_ModeStr(NFC_Mode_t mode)
{
    switch (mode) {
        case NFC_MODE_IDLE:   return "IDLE";
        case NFC_MODE_ADD_CARD: return "ADD";
        case NFC_MODE_VERIFY_CARD: return "VERIFY";
        default: return "UNKNOWN";
    }
}

/*================ 模式设置/获取 =================*/

void NFC_SetMode(NFC_Mode_t mode)
{
    g_nfc_mode = mode;
    printf("[NFC] Mode switched to: %s\r\n", NFC_ModeStr(mode));
}

NFC_Mode_t NFC_GetMode(void)
{
    return g_nfc_mode;
}

/*================ 卡片库管理 =================*/

uint8_t NFC_AddCard(uint8_t* uid)
{
    uint8_t i;

    for (i = 0; i < g_card_count; i++) {
        if (g_card_list[i].valid &&
            memcmp(g_card_list[i].uid, uid, 4) == 0) {
            return MI_ERR;
        }
    }

    if (g_card_count >= NFC_MAX_CARDS) {
        return MI_ERR;
    }

    memcpy(g_card_list[g_card_count].uid, uid, 4);
    g_card_list[g_card_count].valid = 1;
    g_card_count++;

    return MI_OK;
}

uint8_t NFC_VerifyCard(uint8_t* uid)
{
    uint8_t i;
    for (i = 0; i < g_card_count; i++) {
        if (g_card_list[i].valid &&
            memcmp(g_card_list[i].uid, uid, 4) == 0) {
            return MI_OK;
        }
    }
    return MI_ERR;
}

uint8_t NFC_RemoveCard(uint8_t* uid)
{
    uint8_t i, j;
    for (i = 0; i < g_card_count; i++) {
        if (g_card_list[i].valid &&
            memcmp(g_card_list[i].uid, uid, 4) == 0) {

            for (j = i; j < g_card_count - 1; j++) {
                g_card_list[j] = g_card_list[j + 1];
            }
            g_card_count--;
            return MI_OK;
        }
    }
    return MI_ERR;
}

uint8_t NFC_GetCardCount(void)
{
    return g_card_count;
}

void NFC_ClearAllCards(void)
{
    memset(g_card_list, 0, sizeof(g_card_list));
    g_card_count = 0;
    printf("[NFC] All cards cleared.\r\n");
}

void NFC_ListCards(void)
{
    uint8_t i;
    printf("[NFC] Card list (%d/%d):\r\n", g_card_count, NFC_MAX_CARDS);
    for (i = 0; i < g_card_count; i++) {
        printf("  [%d] UID: %02X%02X%02X%02X\r\n", i + 1,
               g_card_list[i].uid[0], g_card_list[i].uid[1],
               g_card_list[i].uid[2], g_card_list[i].uid[3]);
    }
}

/*================ UART 接收管理（模块私有）================*/

static void UART_RxStart(void)
{
    HAL_UART_Receive_IT(&huart3, &rx_byte, 1);
}

static uint16_t UART_RxCount(void)
{
    return (rx_ring.head - rx_ring.tail + UART_RX_BUF_SIZE) % UART_RX_BUF_SIZE;
}

static uint8_t UART_RxRead(void)
{
    uint8_t ch = 0;
    if (rx_ring.head != rx_ring.tail) {
        ch = rx_ring.buf[rx_ring.tail];
        rx_ring.tail = (rx_ring.tail + 1) % UART_RX_BUF_SIZE;
    }
    return ch;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3) {
        uint16_t next = (rx_ring.head + 1) % UART_RX_BUF_SIZE;
        if (next != rx_ring.tail) {
            rx_ring.buf[rx_ring.head] = rx_byte;
            rx_ring.head = next;
        }
        UART_RxStart();
    }
}

static void UART_ProcessCmd(void)
{
    static char cmd[UART_CMD_MAX_LEN];
    static uint8_t idx = 0;
    uint8_t ch;

    while (UART_RxCount() > 0) {
        ch = UART_RxRead();

        if (ch == '\r' || ch == '\n') {
            if (idx > 0) {
                cmd[idx] = '\0';

                if (strncmp(cmd, "NFC:", 4) == 0) {
                    const char *sub = cmd + 4;

                    if (strcmp(sub, "IDLE") == 0) {
                        NFC_SetMode(NFC_MODE_IDLE);
                    }
                    else if (strcmp(sub, "ADD") == 0) {
                        NFC_SetMode(NFC_MODE_ADD_CARD);
                        printf("[UART] Enter ADD mode. Swipe ONE card to add, then auto-switch to VERIFY.\r\n");
                    }
                    else if (strcmp(sub, "VERIFY") == 0) {
                        NFC_SetMode(NFC_MODE_VERIFY_CARD);
                        printf("[UART] Enter VERIFY mode.\r\n");
                    }
                    else if (strcmp(sub, "LIST") == 0) {
                        NFC_ListCards();
                    }
                    else if (strcmp(sub, "CLEAR") == 0) {
                        NFC_ClearAllCards();
                    }
                    else {
                        printf("[UART] Unknown NFC cmd: %s\r\n", sub);
                    }
                }
                else if (strcmp(cmd, "HELP") == 0) {
                    printf("----- UART Command List -----\r\n");
                    printf("  NFC:IDLE    -> NFC idle mode\r\n");
                    printf("  NFC:ADD     -> NFC add card mode (auto-switch to VERIFY after one card)\r\n");
                    printf("  NFC:VERIFY  -> NFC verify card mode\r\n");
                    printf("  NFC:LIST    -> List authorized cards\r\n");
                    printf("  NFC:CLEAR   -> Clear all cards\r\n");
                    printf("  HELP        -> Show this list\r\n");
                    printf("-----------------------------\r\n");
                }
                else {
                    printf("[UART] Unknown cmd: %s\r\n", cmd);
                }

                idx = 0;
            }
        }
        else if (ch == '\b' || ch == 0x7F) {
            if (idx > 0) idx--;
        }
        else if (idx < UART_CMD_MAX_LEN - 1) {
            cmd[idx++] = ch;
        }
    }
}

/*================ 对外接口：系统初始化 =================*/

uint8_t NFC_InitSystem(void)
{
    uint8_t status;

    printf("\r\n========================================\r\n");
    printf("NFC System Initializing...\r\n");
    printf("========================================\r\n");

    /* 启动 UART 中断接收 */
    UART_RxStart();

    /* 初始化 MFRC-522 */
    status = NFC_Init();
    if (status == MI_OK) {
        uint8_t ver = NFC_ReadVersion();
        printf("[NFC] MFRC-522 OK! Version: 0x%02X\r\n", ver);
    } else {
        printf("[NFC] MFRC-522 FAILED!\r\n");
        return MI_ERR;
    }

    /* 默认进入关闭模式 */
    NFC_SetMode(NFC_MODE_IDLE);

    printf("========================================\r\n\r\n");
    return MI_OK;
}

/*================ 对外接口：主循环任务 =================*/

void NFC_Task(void)
{
    static uint32_t last_tick = 0;
    static uint32_t cooldown_tick = 0;
    uint8_t tagType[2];
    uint8_t uid[4];
    uint8_t status;
    uint32_t now = HAL_GetTick();

    /* 处理串口命令（放在前面，保证模式切换及时生效） */
    UART_ProcessCmd();

    /* 冷却期 */
    if (now < cooldown_tick) {
        return;
    }

    /* 空闲模式：不寻卡 */
    if (g_nfc_mode == NFC_MODE_IDLE) {
        return;
    }

    /* 500ms 检测间隔 */
    if (now - last_tick < 500) {
        return;
    }
    last_tick = now;

    /* 寻卡 */
    status = NFC_Request(PICC_REQALL, tagType);
    if (status != MI_OK) {
        return;
    }

    /* 防冲撞读UID */
    status = NFC_Anticoll(uid);
    if (status != MI_OK) {
        printf("[NFC][%s] Anticoll failed\r\n", NFC_ModeStr(g_nfc_mode));
        return;
    }

    /* 选卡 */
    NFC_SelectTag(uid);

    /* 根据模式处理 */
    if (g_nfc_mode == NFC_MODE_ADD_CARD) {
        if (NFC_AddCard(uid) == MI_OK) {
            printf("[NFC][%s] >>> Card ADDED! UID=%02X%02X%02X%02X (Total:%d)\r\n",
                   NFC_ModeStr(g_nfc_mode),
                   uid[0], uid[1], uid[2], uid[3], g_card_count);

            /* 一次只添加一张：成功后自动切回 VERIFY */
            printf("[NFC][%s] Auto-switching to VERIFY mode.\r\n", NFC_ModeStr(g_nfc_mode));
            NFC_SetMode(NFC_MODE_VERIFY_CARD);

            cooldown_tick = now + 1500;
        } else {
            printf("[NFC][%s] Add failed! Card exists or list full. (Total:%d)\r\n",
                   NFC_ModeStr(g_nfc_mode), g_card_count);
            cooldown_tick = now + 1000;
        }
    }
    else if (g_nfc_mode == NFC_MODE_VERIFY_CARD) {
        if (NFC_VerifyCard(uid) == MI_OK) {
            printf("[NFC][%s] >>> Access GRANTED! UID=%02X%02X%02X%02X\r\n",
                   NFC_ModeStr(g_nfc_mode),
                   uid[0], uid[1], uid[2], uid[3]);
            /* TODO: CAN解锁 + 语音播报 + 屏幕绿色提示 */
            cooldown_tick = now + 1500;
        } else {
            printf("[NFC][%s] >>> Access DENIED! UID=%02X%02X%02X%02X\r\n",
                   NFC_ModeStr(g_nfc_mode),
                   uid[0], uid[1], uid[2], uid[3]);
            /* TODO: 保持锁定 + 语音警告 + 屏幕红色提示 */
            cooldown_tick = now + 1500;
        }
    }

    NFC_Halt();
}

/* USER CODE END 0 */
