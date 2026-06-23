#ifndef __G474_CONTROL_H
#define __G474_CONTROL_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/*================ 时序参数 ================*/
#define CONTROL_FREQ_HZ         1000
#define CAN_TX_FREQ_HZ          100
#define HEARTBEAT_TIMEOUT_MS    500

/*================ 传感器阈值 ================*/
#define LINE_SENSOR_THRESHOLD   50

/*================ 系统状态字 ================*/
typedef union {
    uint8_t byte;
    struct {
        uint8_t derail_flag       : 1;
        uint8_t comm_timeout      : 1;
        uint8_t motor_overcurrent : 1;
        uint8_t reserved          : 5;
    } bits;
} SystemStatus_t;

/*================ 全局状态结构 ================*/
typedef struct {
    /* 指令区 (main loop 写, ISR 读) */
    struct {
        uint8_t dc_speed_raw;
        uint8_t dc_direction;
        uint8_t conveyor_enable;
        uint8_t conveyor_speed;
        uint8_t conveyor_dir;
        uint8_t servo_pitch;
        uint8_t servo_yaw;
    } cmd;

    /* 反馈区 (ISR 写, main loop 读) */
    struct {
        uint8_t  dc_speed_pct;
        uint8_t  step_speed;
        int16_t  encoder_count;
        uint8_t  sensor_left;
        uint8_t  sensor_right;
        uint8_t  status_byte;
    } fdb;

    /* 安全状态 */
    bool     motor_locked;
    uint32_t heartbeat_rx_tick;
} AGV_State_t;

extern volatile AGV_State_t g_agv;

/*================ API ================*/
void Control_Init(void);

void AGV_LockMotors(void);
void AGV_UnlockMotors(void);

#endif
