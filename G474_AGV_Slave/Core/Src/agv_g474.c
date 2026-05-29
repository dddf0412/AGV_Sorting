/* Core/Src/agv_g474.c */
#include "agv_g474.h"

AGV_State_t g_agv = {0};

void AGV_Init(void)
{
    /* 初始状态：电机锁定，等待NFC授权 */
    g_agv.motor_locked = true;

    /* 初始化指令默认值 */
    g_agv.cmd.agv_speed = 0;
    g_agv.cmd.conveyor_enable = 0;
    g_agv.cmd.servo_pitch = 90;
    g_agv.cmd.servo_yaw = 90;

    /* 初始化反馈值 */
    g_agv.feedback.status.byte = 0;

    /* 记录初始时间戳 */
    g_agv.heartbeat_rx_tick = HAL_GetTick();
    g_agv.heartbeat_tx_tick = HAL_GetTick();
}

void AGV_LockMotors(void)
{
    g_agv.motor_locked = true;
    /* 立即停止所有运动 */
    g_agv.cmd.agv_speed = 0;
    g_agv.cmd.conveyor_enable = 0;
}

void AGV_UnlockMotors(void)
{
    g_agv.motor_locked = false;
}
