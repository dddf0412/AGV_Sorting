#ifndef __H753_CAN_H
#define __H753_CAN_H

#include "main.h"
#include "stm32h7xx_hal.h"

#define CAN_ID_AGV_CMD      0x201
#define CAN_ID_CONVEYOR_CMD 0x202
#define CAN_ID_SERVO_CMD    0x203
#define CAN_ID_HEARTBEAT    0x2FF
#define CAN_ID_STATUS       0x301

/* 解析后的状态帧 */
typedef struct {
    int8_t  dc_speed;
    uint8_t step_speed;
    int16_t encoder_cnt;
    uint8_t sensor_left;
    uint8_t sensor_right;
    uint8_t sys_status;
    uint8_t tail;
} H753_StatusFrame_t;

void H753_CAN_Init(void);

uint8_t H753_CAN_SendHeartbeat(void);
uint8_t H753_CAN_SendAgvCmd(uint8_t speed, uint8_t dir);
uint8_t H753_CAN_SendConveyorCmd(uint8_t run, uint8_t speed, uint8_t dir);
uint8_t H753_CAN_SendRaw(uint32_t id, uint8_t *data, uint8_t len);

void H753_CAN_RxHandler(void);

uint8_t  H753_CAN_IsStatusFrameValid(void);
void     H753_CAN_GetStatus(H753_StatusFrame_t *out);
void     H753_CAN_ClearStatusFlag(void);

#endif
