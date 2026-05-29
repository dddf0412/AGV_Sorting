#include "app_g474.h"
#include "g474_can.h"
#include "g474_debug.h"
#include <stdio.h>
#include <string.h>

#if (APP_MODE == 1)
#include "agv_g474.h"
#include "motor_control.h"
#endif

/*================ 私有原型 ================*/
#if (APP_MODE == 0)
static void Simulate_UpdateStatus(G474_StatusFrame_t *status);
#endif

/*================ App_Init ================*/
void App_Init(void)
{
    G474_Debug_Init();
    G474_CAN_Init();

#if (APP_MODE == 0)
    printf("\r\n========== G474 Slave Simulator ==========\r\n");
#else
    AGV_Init();
    Motor_Init();
    printf("\r\n========== G474 Slave Running ==========\r\n");
#endif
    printf("CAN bus (1Mbps), filter accepts all standard IDs\r\n");
}

/*================ App_Run ================*/
void App_Run(void)
{
    static G474_StatusFrame_t status;
    static uint8_t status_init = 0;
    static uint32_t lastSend = 0;
    static uint32_t lastHeartbeatCheck = 0;

    if (!status_init) {
        memset(&status, 0, sizeof(status));
        status.tail = 0xAA;
        status_init = 1;
    }

#if (APP_MODE == 0)
    /*-------------------- 模拟模式 --------------------*/

    /* 每100ms 发送模拟状态 */
    if (HAL_GetTick() - lastSend >= 100) {
        lastSend = HAL_GetTick();
        Simulate_UpdateStatus(&status);
        if (G474_CAN_SendStatus(&status) == 0) {
            printf("[G474] Send status ID=0x301, encoder=%d, left/right sensors=%d/%d\r\n",
                   status.encoder_cnt, status.sensor_left, status.sensor_right);
        } else {
            printf("[G474] Status send failed!\r\n");
        }
    }

    /* 每500ms 心跳超时检查 */
    if (HAL_GetTick() - lastHeartbeatCheck >= 500) {
        lastHeartbeatCheck = HAL_GetTick();
        if (!G474_CAN_IsHeartbeatReceived()) {
            printf("[G474 Safety] No heartbeat from H753 for >500ms! Simulating stop all motors\r\n");
        } else {
            G474_CAN_ClearHeartbeatFlag();
        }
    }

    /* CAN 命令接收并打印 */
    uint32_t rxId = G474_CAN_GetLastRxID();
    if (rxId != 0) {
        uint8_t *data = G474_CAN_GetLastRxData();
        switch (rxId) {
            case CAN_ID_AGV_CMD:
                printf("[Sim exec] AGV cmd: speed=%d, dir=%s\r\n",
                       data[0], data[1] ? "reverse" : "forward");
                break;
            case CAN_ID_CONVEYOR_CMD:
                printf("[Sim exec] Conveyor cmd: run=%d, speed=%d, dir=%s\r\n",
                       data[0], data[1], data[2] ? "reverse" : "forward");
                break;
            case CAN_ID_SERVO_CMD:
                printf("[Sim exec] Servo cmd: pitch=%d deg, yaw=%d deg\r\n", data[0], data[1]);
                break;
            default:
                break;
        }
        G474_CAN_ClearLastRxID();
    }

#else
    /*-------------------- 真实模式 --------------------*/

    /* 每100ms 组装状态帧并发送 */
    if (HAL_GetTick() - lastSend >= 100) {
        lastSend = HAL_GetTick();
        status.dc_speed     = g_agv.feedback.dc_speed;
        status.step_speed   = g_agv.feedback.step_speed;
        status.encoder_cnt  = g_agv.feedback.encoder_count;
        status.sensor_left  = g_agv.feedback.sensor_left;
        status.sensor_right = g_agv.feedback.sensor_right;
        status.sys_status   = g_agv.feedback.status.byte;
        status.tail         = 0xAA;
        G474_CAN_SendStatus(&status);
    }

    /* 每500ms 心跳超时检查 */
    if (HAL_GetTick() - lastHeartbeatCheck >= 500) {
        lastHeartbeatCheck = HAL_GetTick();
        if (!G474_CAN_IsHeartbeatReceived()) {
            /* 通信超时 — 停止所有电机 */
            g_agv.feedback.status.bits.comm_timeout = 1;
            DC_Motor_Stop();
            Stepper_Stop();
        } else {
            G474_CAN_ClearHeartbeatFlag();
        }
    }

    /* CAN 命令接收并执行 */
    uint32_t rxId = G474_CAN_GetLastRxID();
    if (rxId != 0 && !g_agv.motor_locked) {
        uint8_t *data = G474_CAN_GetLastRxData();
        switch (rxId) {
            case CAN_ID_AGV_CMD:
                g_agv.cmd.agv_speed = (int8_t)data[0];
                g_agv.cmd.agv_direction = data[1];
                DC_Motor_SetSpeed(g_agv.cmd.agv_speed);
                break;
            case CAN_ID_CONVEYOR_CMD:
                g_agv.cmd.conveyor_enable = data[0];
                g_agv.cmd.conveyor_speed  = data[1];
                g_agv.cmd.conveyor_dir    = data[2] ? 1 : 0;
                if (g_agv.cmd.conveyor_enable) {
                    Stepper_SetDirection(g_agv.cmd.conveyor_dir);
                    Stepper_SetSpeed(g_agv.cmd.conveyor_speed);
                } else {
                    Stepper_Stop();
                }
                break;
            case CAN_ID_SERVO_CMD:
                g_agv.cmd.servo_pitch = data[0];
                g_agv.cmd.servo_yaw   = data[1];
                Servo_SetAngle(0, g_agv.cmd.servo_pitch);
                Servo_SetAngle(1, g_agv.cmd.servo_yaw);
                break;
            default:
                break;
        }
        G474_CAN_ClearLastRxID();
    }

#endif
}

/*================ 模拟函数 ================*/
#if (APP_MODE == 0)
static void Simulate_UpdateStatus(G474_StatusFrame_t *status)
{
    static int16_t encoder = 0;
    static uint8_t left = 50, right = 50;
    static int8_t  dc = 0;
    static uint8_t step = 0;

    encoder += 10;
    if (encoder > 3000) encoder = 0;

    step++;
    if (step < 128) {
        left  = 50 + step;
        right = 50 + 128 - step;
    } else {
        left  = 50 + (255 - step);
        right = 50 + (step - 128);
    }

    dc = (dc == 0) ? 60 : 0;

    status->dc_speed     = dc;
    status->step_speed   = 100;
    status->encoder_cnt  = encoder;
    status->sensor_left  = left;
    status->sensor_right = right;
    status->sys_status   = 0;
    status->tail         = 0xAA;
}
#endif
