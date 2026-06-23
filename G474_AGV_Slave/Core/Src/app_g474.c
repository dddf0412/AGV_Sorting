#include "app_g474.h"
#include "g474_can.h"
#include "g474_debug.h"
#include "g474_control.h"
#include "g474_sensor.h"
#include "g474_encoder.h"
#include "g474_dc_motor.h"
#include "g474_stepper.h"
#include "g474_servo.h"
#include <stdio.h>
#include <string.h>

/* 传感器测试阶段: 屏蔽 CAN, 关闭电机 PWM 初始化 */
#define CAN_ENABLE   0
#define MOTOR_ENABLE 0

/*================ App_Init ================*/

void App_Init(void)
{
    G474_Debug_Init();

#if CAN_ENABLE
    G474_CAN_Init();
#endif

    Sensor_Init();
    Encoder_Init();

#if MOTOR_ENABLE
    DC_Motor_Init();
    Stepper_Init();
    Servo_Init();
#endif

    Control_Init();

    printf("\r\n========== G474 Slave Running ==========\r\n");
#if CAN_ENABLE
    printf("CAN 1Mbps | ");
#else
    printf("Sensor Test | ");
#endif
    printf("Control 1kHz | ADC DMA\r\n");
#if !CAN_ENABLE
    printf("L_sensor R_sensor  Encoder  Status\r\n");
#endif
}

/*================ App_Run ================*/

void App_Run(void)
{
#if CAN_ENABLE
    /* ---- 1. CAN 指令分发 ---- */
    uint32_t rxId = G474_CAN_GetLastRxID();
    if (rxId != 0) {
        uint8_t *data = G474_CAN_GetLastRxData();
        switch (rxId) {
            case CAN_ID_AGV_CMD:
                g_agv.cmd.dc_speed_raw     = data[0];
                g_agv.cmd.dc_direction     = data[1];
                break;
            case CAN_ID_CONVEYOR_CMD:
                g_agv.cmd.conveyor_enable  = data[0];
                g_agv.cmd.conveyor_speed   = data[1];
                g_agv.cmd.conveyor_dir     = data[2];
                break;
            case CAN_ID_SERVO_CMD:
                g_agv.cmd.servo_pitch      = data[0];
                g_agv.cmd.servo_yaw        = data[1];
                break;
            case CAN_ID_UNLOCK:
                if (data[0] == 0x01)
                    AGV_UnlockMotors();
                else
                    AGV_LockMotors();
                break;
            default:
                break;
        }
        G474_CAN_ClearLastRxID();
    }

    /* ---- 2. 心跳跟踪 ---- */
    if (G474_CAN_IsHeartbeatReceived()) {
        g_agv.heartbeat_rx_tick = HAL_GetTick();
        G474_CAN_ClearHeartbeatFlag();
    }

    /* ---- 3. 100Hz 状态上报 ---- */
    static uint32_t last_tx = 0;
    if (HAL_GetTick() - last_tx >= 100) {
        last_tx = HAL_GetTick();

        G474_StatusFrame_t status;
        status.dc_speed     = (int8_t)g_agv.fdb.dc_speed_pct;
        status.step_speed   = g_agv.fdb.step_speed;
        status.encoder_cnt  = g_agv.fdb.encoder_count;
        status.sensor_left  = g_agv.fdb.sensor_left;
        status.sensor_right = g_agv.fdb.sensor_right;
        status.sys_status   = g_agv.fdb.status_byte;
        status.tail         = 0xAA;

        G474_CAN_SendStatus(&status);
    }

#else
    /* ---- 传感器测试模式: printf 输出, 500ms 间隔 ---- */
    g_agv.heartbeat_rx_tick = HAL_GetTick();

    static uint32_t last_print = 0;
    if (HAL_GetTick() - last_print >= 500) {
        last_print = HAL_GetTick();
        printf("[SENSOR] L=%3u R=%3u  Enc=%6d  Stat=0x%02X\r\n",
               g_agv.fdb.sensor_left,
               g_agv.fdb.sensor_right,
               g_agv.fdb.encoder_count,
               g_agv.fdb.status_byte);
    }
#endif
}
