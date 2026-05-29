#include "app_h753.h"
#include "h753_can.h"
#include "h753_debug.h"
#include <stdio.h>

/* 子系统 — 真实模式下启用 */
#if (APP_MODE == 1)
#include "nfc.h"
#include "microphone.h"
#endif

/*================ 私有原型 ================*/
#if (APP_MODE == 0)
static void Simulate_SendCommands(void);
#endif

/*================ App_Init ================*/
void App_Init(void)
{
    H753_Debug_Init();
    H753_CAN_Init();

#if (APP_MODE == 0)
    printf("\r\n========== H753 Master Simulator ==========\r\n");
#else
    printf("\r\n========== H753 Master Running ==========\r\n");
#endif
    printf("CAN bus (1Mbps), filter accepts all standard IDs\r\n");
}

/*================ App_Run ================*/
void App_Run(void)
{
    static uint32_t lastHeartbeat = 0;
    static uint32_t lastCmd = 0;
    H753_StatusFrame_t status;

#if (APP_MODE == 0)
    /*-------------------- 模拟模式 --------------------*/

    /* 每100ms 发送心跳 */
    if (HAL_GetTick() - lastHeartbeat >= 100) {
        lastHeartbeat = HAL_GetTick();
        if (H753_CAN_SendHeartbeat() == 0)
            printf("[H753] Send heartbeat ID=0x2FF\r\n");
        else
            printf("[H753] Heartbeat send failed!\r\n");
    }

    /* 每2s 模拟发送指令 (AGV/Conveyor/Servo) */
    if (HAL_GetTick() - lastCmd >= 2000) {
        lastCmd = HAL_GetTick();
        Simulate_SendCommands();
    }

    /* 接收状态帧并打印 */
    if (H753_CAN_IsStatusFrameValid()) {
        H753_CAN_GetStatus(&status);
        H753_CAN_ClearStatusFlag();
        printf("\r\n[Status] encoder=%d | dc_speed=%d | step_speed=%d | "
               "sensor_left=%d | sensor_right=%d | sys_status=0x%02X\r\n",
               status.encoder_cnt, status.dc_speed, status.step_speed,
               status.sensor_left, status.sensor_right, status.sys_status);
    }

#else
    /*-------------------- 真实模式 --------------------*/

    /* 每100ms 发送心跳 */
    if (HAL_GetTick() - lastHeartbeat >= 100) {
        lastHeartbeat = HAL_GetTick();
        H753_CAN_SendHeartbeat();
    }

    /* NFC 轮询 */
    NFC_Task();

    /* 麦克风 DMA 缓冲区处理 */
    mic_process();

    /* 串口屏任务（待实现） */
    Screen_Task();

    /* 接收状态帧并更新屏幕 */
    if (H753_CAN_IsStatusFrameValid()) {
        H753_CAN_GetStatus(&status);
        H753_CAN_ClearStatusFlag();
        /* TODO: 推送到串口屏 */
    }

#endif
}

/*================ 模拟函数 ================*/
#if (APP_MODE == 0)
static void Simulate_SendCommands(void)
{
    static uint8_t cmdIdx = 0;
    uint8_t ret;

    switch (cmdIdx % 3) {
        case 0:
            printf("\r\n[Sim decision] Send AGV cmd: speed=120, dir=forward\r\n");
            ret = H753_CAN_SendAgvCmd(120, 0);
            break;
        case 1:
            printf("[Sim decision] Send conveyor cmd: start, speed=80, dir=forward\r\n");
            ret = H753_CAN_SendConveyorCmd(1, 80, 0);
            break;
        case 2:
            printf("[Sim decision] Send servo cmd: pitch=90 deg, yaw=45 deg\r\n");
            {
                uint8_t data[8] = {90, 45, 0, 0, 0, 0, 0, 0};
                ret = H753_CAN_SendRaw(0x203, data, 8);
            }
            break;
    }
    if (ret != 0)
        printf("[Error] CAN send failed!\r\n");

    cmdIdx++;
}
#endif

/*================ 占位函数 ================*/
#if (APP_MODE == 1)
__weak void Screen_Task(void)
{
    /* TODO: 串口屏 UART 轮询 + 页面刷新 */
}
#else
__weak void Screen_Task(void) {}
#endif
