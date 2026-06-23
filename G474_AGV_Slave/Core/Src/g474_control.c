#include "g474_control.h"
#include "g474_sensor.h"
#include "g474_encoder.h"
#include "g474_dc_motor.h"
#include "g474_stepper.h"
#include "g474_servo.h"
#include "tim.h"

/* 传感器测试阶段: 电机执行暂屏蔽 */
#define MOTOR_ENABLE 0

extern TIM_HandleTypeDef htim6;

volatile AGV_State_t g_agv = {0};

void Control_Init(void)
{
    g_agv.motor_locked = false;
    g_agv.heartbeat_rx_tick = HAL_GetTick();
    g_agv.cmd.servo_pitch = 90;
    g_agv.cmd.servo_yaw = 90;

    HAL_TIM_Base_Start_IT(&htim6);
}

void AGV_LockMotors(void)
{
    g_agv.motor_locked = true;
}

void AGV_UnlockMotors(void)
{
    g_agv.motor_locked = false;
}

/*================ 1kHz 控制循环 (TIM6 ISR) ================*/

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM6) return;

    /* 1. 读传感器 (DMA 缓存, 无阻塞) */
    uint16_t raw_left  = Sensor_GetRawLeft();
    uint16_t raw_right = Sensor_GetRawRight();
    g_agv.fdb.sensor_left  = (uint8_t)(raw_left  >> 4);
    g_agv.fdb.sensor_right = (uint8_t)(raw_right >> 4);

    /* 2. 读编码器 */
    g_agv.fdb.encoder_count = Encoder_GetCount();

    /* 3. 脱轨检测 */
    if (g_agv.fdb.sensor_left  < LINE_SENSOR_THRESHOLD &&
        g_agv.fdb.sensor_right < LINE_SENSOR_THRESHOLD) {
        g_agv.fdb.status_byte |= 0x01;
    } else {
        g_agv.fdb.status_byte &= ~0x01;
    }

    /* 4. 通信超时检测 */
    if ((HAL_GetTick() - g_agv.heartbeat_rx_tick) > HEARTBEAT_TIMEOUT_MS) {
        g_agv.fdb.status_byte |= 0x02;
    } else {
        g_agv.fdb.status_byte &= ~0x02;
    }

#if MOTOR_ENABLE
    /* 5. 安全判断与电机执行 */
    bool derailed     = (g_agv.fdb.status_byte & 0x01) != 0;
    bool comm_lost    = (g_agv.fdb.status_byte & 0x02) != 0;
    bool safe_to_run  = !g_agv.motor_locked && !derailed && !comm_lost;

    if (safe_to_run) {
        /* DC 电机 */
        uint8_t pct = (uint8_t)(((uint16_t)g_agv.cmd.dc_speed_raw * 100) / 255);
        DC_Motor_Set(pct, g_agv.cmd.dc_direction);
        g_agv.fdb.dc_speed_pct = pct;

        /* 步进电机 */
        if (g_agv.cmd.conveyor_enable) {
            Stepper_SetDirection(g_agv.cmd.conveyor_dir);
            Stepper_SetSpeed(g_agv.cmd.conveyor_speed);
            g_agv.fdb.step_speed = g_agv.cmd.conveyor_speed;
        } else {
            Stepper_Stop();
            g_agv.fdb.step_speed = 0;
        }

        /* 舵机 */
        Servo_SetAngle(0, g_agv.cmd.servo_pitch);
        Servo_SetAngle(1, g_agv.cmd.servo_yaw);

    } else {
        DC_Motor_Stop();
        Stepper_Stop();
        g_agv.fdb.dc_speed_pct = 0;
        g_agv.fdb.step_speed = 0;
    }
#endif
}
