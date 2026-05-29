#ifndef __SCREEN_H
#define __SCREEN_H

#include "main.h"
#include <stdint.h>

/*================ 串口屏型号 ================*/
/* 淘晶驰 T1 系列 4.3寸 电容触摸屏, 480x272, USART1 (PB14/PB15), 115200 */

/*================ 协议常量 ================*/
#define SCREEN_FRAME_END       "\xFF\xFF\xFF"
#define SCREEN_CMD_BUF_SIZE    256
#define SCREEN_RX_BUF_SIZE     128
#define SCREEN_MAX_CTRL_NAME    32

/*================ UART RX 字节缓冲（供 HAL 回调使用）================*/
extern uint8_t screen_rx_byte;

/*================ 触摸事件回调 ================*/
typedef void (*Screen_TouchCallback)(uint8_t page_id, uint8_t ctrl_id,
                                     uint8_t event_type);

/*================ API ================*/
void Screen_Init(UART_HandleTypeDef *huart);

/* 页面切换 */
void Screen_SwitchPage(uint8_t page_id);
void Screen_SwitchPageByName(const char *name);

/* 控件操作 */
void Screen_SetText(const char *ctrl, const char *fmt, ...);
void Screen_SetVal(const char *ctrl, int32_t val);
void Screen_SendRaw(const char *cmd);

/* 主循环轮询 */
void Screen_Task(void);

/* 注册触摸回调（屏→MCU）*/
void Screen_RegisterTouchCallback(Screen_TouchCallback cb);

/* UART 中断中调用 */
void Screen_RxISR(uint8_t byte);

#endif
