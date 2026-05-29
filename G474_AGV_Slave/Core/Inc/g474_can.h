#ifndef __G474_CAN_H
#define __G474_CAN_H

#include "main.h"
#include "stm32g4xx_hal.h"

/* CAN ID 定义 */
#define CAN_ID_AGV_CMD      0x201
#define CAN_ID_CONVEYOR_CMD 0x202
#define CAN_ID_SERVO_CMD    0x203
#define CAN_ID_HEARTBEAT    0x2FF
#define CAN_ID_STATUS       0x301

/* 状态字节位定义 */
#define STATUS_BIT_OFFTRACK (1 << 0)
#define STATUS_BIT_COM_LOST (1 << 1)

/* 状态帧数据结构 */
typedef struct {
    int8_t  dc_speed;       // Data[0]: 直流电机速度
    uint8_t step_speed;      // Data[1]: 步进电机速度
    int16_t encoder_cnt;    // Data[2-3]: 编码器值，小端
    uint8_t sensor_left;     // Data[4]: 左传感器
    uint8_t sensor_right;    // Data[5]: 右传感器
    uint8_t sys_status;      // Data[6]: 系统状态字
    uint8_t tail;            // Data[7]: 帧尾校验，固定0xAA
} G474_StatusFrame_t;

/* 初始化 */
void G474_CAN_Init(void);

/* 发送接口 */
uint8_t G474_CAN_SendStatus(const G474_StatusFrame_t *status);
uint8_t G474_CAN_SendRaw(uint32_t id, uint8_t *data, uint8_t len);

/* 接收处理（在中断回调内调用，也可在主循环轮询） */
void G474_CAN_RxHandler(void);

/* 获取最新接收到的指令ID（供主循环查询） */
uint32_t G474_CAN_GetLastRxID(void);
uint8_t* G474_CAN_GetLastRxData(void);
uint8_t  G474_CAN_IsHeartbeatReceived(void);
void     G474_CAN_ClearHeartbeatFlag(void);
void G474_CAN_ClearLastRxID(void);

#endif
