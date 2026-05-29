/* Core/Inc/agv_g474.h */
#ifndef __AGV_G474_H
#define __AGV_G474_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/*================ 系统参数 ================*/
#define CONTROL_FREQ_HZ     1000        /* 1kHz控制频率 */
#define CAN_TX_FREQ_HZ      100         /* 100Hz状态上报 */
#define HEARTBEAT_TIMEOUT_MS 500       /* 通信超时500ms */

/*================ 电机参数 ================*/
#define DC_MOTOR_PWM_MAX    999         /* TIM2 ARR值 */
#define STEPPER_PWM_MAX     999         /* TIM1 ARR值 */
#define SERVO_PWM_MIN       500         /* 0度: 0.5ms */
#define SERVO_PWM_MID       1500        /* 90度: 1.5ms */
#define SERVO_PWM_MAX       2500        /* 180度: 2.5ms */
#define SERVO_PWM_ARR       19999       /* TIM4 ARR值 (50Hz) */

/*================ 系统状态字 ================*/
typedef union {
    uint8_t byte;
    struct {
        uint8_t derail_flag     : 1;    /* Bit0: 脱轨标志 */
        uint8_t comm_timeout    : 1;    /* Bit1: 通信超时 */
        uint8_t motor_overcurrent: 1;   /* Bit2: 电机过流(预留) */
        uint8_t reserved        : 5;
    } bits;
} SystemStatus_t;

/*================ 全局数据结构 ================*/
typedef struct {
    /* 指令接收区 (来自H753) */
    struct {
        int8_t  agv_speed;          /* -128~127 */
        uint8_t agv_direction;      /* 0正转/1反转 */
        uint8_t conveyor_enable;    /* 0停/1启 */
        uint8_t conveyor_speed;     /* 0~255 */
        uint8_t conveyor_dir;       /* 0正/1反 */
        uint8_t servo_pitch;        /* 0~180 */
        uint8_t servo_yaw;          /* 0~180 */
    } cmd;

    /* 状态反馈区 (上报H753) */
    struct {
        int8_t  dc_speed;           /* 当前直流速度 */
        uint8_t step_speed;         /* 当前步进速度 */
        int16_t encoder_count;      /* 编码器计数 */
        uint8_t sensor_left;        /* 左传感器灰度 */
        uint8_t sensor_right;       /* 右传感器灰度 */
        SystemStatus_t status;      /* 系统状态 */
    } feedback;

    /* 运行时 */
    uint32_t heartbeat_rx_tick;     /* 上次收到主站心跳时间 */
    uint32_t heartbeat_tx_tick;   /* 上次发送状态时间 */
    bool     motor_locked;          /* 运动锁定(未授权) */
} AGV_State_t;

extern AGV_State_t g_agv;

/*================ 函数声明 ================*/
void AGV_Init(void);
void AGV_LockMotors(void);
void AGV_UnlockMotors(void);
void G474_CAN_ClearLastRxID(void);
#endif
