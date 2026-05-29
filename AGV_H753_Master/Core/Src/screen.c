#include "screen.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/*================ 硬件句柄 ================*/
extern UART_HandleTypeDef huart1;
static UART_HandleTypeDef *p_screen_uart = NULL;
uint8_t screen_rx_byte = 0;  /* UART1 RX 单字节缓冲 */

/*================ 接收缓冲 ================*/
static uint8_t rx_buf[SCREEN_RX_BUF_SIZE];
static volatile uint8_t rx_head = 0;
static volatile uint8_t rx_tail = 0;

/*================ 触摸回调 ================*/
static Screen_TouchCallback touch_cb = NULL;

/*================ 内部函数 ================*/
static void _send_frame(const char *cmd);
static void _parse_rx(void);
static inline uint8_t _rx_count(void);

/*================ 初始化 ================*/
void Screen_Init(UART_HandleTypeDef *huart)
{
    p_screen_uart = huart;
    memset(rx_buf, 0, sizeof(rx_buf));
    rx_head = 0;
    rx_tail = 0;

    /* 启动中断接收（单字节循环模式） */
    HAL_UART_Receive_IT(huart, &screen_rx_byte, 1);
    printf("[Screen] Init OK on USART1 (PB14/PB15, 115200)\r\n");
}

/*================ 页面切换 ================*/
void Screen_SwitchPage(uint8_t page_id)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "page %u", page_id);
    _send_frame(cmd);
    printf("[Screen] Switch to page %u\r\n", page_id);
}

void Screen_SwitchPageByName(const char *name)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "page %s", name);
    _send_frame(cmd);
    printf("[Screen] Switch to page \"%s\"\r\n", name);
}

/*================ 控件操作 ================*/
void Screen_SetText(const char *ctrl, const char *fmt, ...)
{
    char cmd[SCREEN_CMD_BUF_SIZE];
    va_list args;
    int prefix_len, txt_len;

    /* 拼接 "ctrl.txt=\"" */
    prefix_len = snprintf(cmd, sizeof(cmd), "%s.txt=\"", ctrl);

    /* 拼接格式化文本 */
    va_start(args, fmt);
    txt_len = vsnprintf(cmd + prefix_len,
                        sizeof(cmd) - prefix_len - 4,  /* 预留 "\"\xFF\xFF\xFF" */
                        fmt, args);
    va_end(args);

    /* 拼接结束符 "\"" + 3x0xFF */
    strcat(cmd, "\"");
    strcat(cmd, SCREEN_FRAME_END);

    HAL_UART_Transmit(p_screen_uart, (uint8_t *)cmd, strlen(cmd), 100);
    printf("[Screen] %s.txt updated\r\n", ctrl);
}

void Screen_SetVal(const char *ctrl, int32_t val)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "%s.val=%ld", ctrl, (long)val);
    _send_frame(cmd);
    printf("[Screen] %s.val = %ld\r\n", ctrl, (long)val);
}

void Screen_SendRaw(const char *cmd)
{
    _send_frame(cmd);
    printf("[Screen] Raw -> %s\r\n", cmd);
}

/*================ 主循环轮询 ================*/
void Screen_Task(void)
{
    _parse_rx();
}

/*================ 注册触摸回调 ================*/
void Screen_RegisterTouchCallback(Screen_TouchCallback cb)
{
    touch_cb = cb;
}

/*================ UART 中断处理 ================*/
void Screen_RxISR(uint8_t byte)
{
    uint8_t next = (rx_head + 1) % SCREEN_RX_BUF_SIZE;
    if (next != rx_tail) {
        rx_buf[rx_head] = byte;
        rx_head = next;
    }
}

/*=================== 内部实现 ===================*/

static void _send_frame(const char *cmd)
{
    if (p_screen_uart == NULL) return;

    /* 发送命令文本 */
    HAL_UART_Transmit(p_screen_uart, (uint8_t *)cmd, strlen(cmd), 100);

    /* 发送结束符 3x 0xFF */
    uint8_t end[3] = {0xFF, 0xFF, 0xFF};
    HAL_UART_Transmit(p_screen_uart, end, 3, 10);
}

static inline uint8_t _rx_count(void)
{
    return (rx_head - rx_tail) % SCREEN_RX_BUF_SIZE;
}

static void _parse_rx(void)
{
    /*
     * 淘晶驰 T1 触摸事件标准格式:
     *   0x65 + PageID + CtrlID + Event + 0xFF 0xFF 0xFF
     *   Event: 0x00 = 按下, 0x01 = 松开
     *
     * 实际数据格式可在 TJC 开发工具中自定义。
     * 当前按标准格式解析，如不一致请修改此函数。
     */

    while (_rx_count() >= 6)
    {
        uint8_t peek[8];
        for (int i = 0; i < 8; i++) {
            peek[i] = rx_buf[(rx_tail + i) % SCREEN_RX_BUF_SIZE];
        }

        /* 检查帧头 0x65 + 帧尾 FF FF FF */
        if (peek[0] == 0x65 && peek[4] == 0xFF && peek[5] == 0xFF && peek[6] == 0xFF)
        {
            uint8_t page  = peek[1];
            uint8_t ctrl  = peek[2];
            uint8_t event = peek[3];

            printf("[Screen Touch] page=%u ctrl=%u event=%s\r\n",
                   page, ctrl, event == 0x00 ? "PRESS" : "RELEASE");

            if (touch_cb) {
                touch_cb(page, ctrl, event);
            }

            /* 从缓冲区移除已处理的 7 字节 */
            rx_tail = (rx_tail + 7) % SCREEN_RX_BUF_SIZE;
        }
        else
        {
            /* 跳过无法识别的字节 */
            rx_tail = (rx_tail + 1) % SCREEN_RX_BUF_SIZE;
        }
    }
}
