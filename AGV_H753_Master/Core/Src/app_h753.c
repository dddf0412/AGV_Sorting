#include "app_h753.h"
#include "h753_can.h"
#include "h753_debug.h"
#include <stdio.h>

#if (APP_MODE == 1)
#include "nfc.h"
#include "microphone.h"
#include "screen.h"
extern UART_HandleTypeDef huart1;
#endif

/*================ 调试开关 ================*/
#define CAN_ENABLE  1  /* 1=启用CAN, 0=关闭CAN (仅查串口屏时关) */

/*================ App_Init ================*/
void App_Init(void)
{
    H753_Debug_Init();

#if CAN_ENABLE
    H753_CAN_Init();
    printf("CAN bus (1Mbps)\r\n");
#endif

#if (APP_MODE == 0)
    printf("\r\n========== H753 Master Simulator ==========\r\n");
#else
    printf("\r\n========== H753 Master Running ==========\r\n");
    Screen_Init(&huart1);

    /* 上屏: 切到首页, 显示标题 */
    Screen_SendRaw("page main");
    Screen_SetText("t1", "系统就绪");
#endif
}

/*================ App_Run ================*/
void App_Run(void)
{
    static uint32_t lastHeartbeat = 0;
    static uint32_t lastDisplay   = 0;
    static uint32_t lastTimeoutCheck = 0;
    static uint8_t  g474_online = 0;
    static uint32_t g474_last_seen = 0;
    H753_StatusFrame_t status;

#if (APP_MODE == 0)
    /*==================== 模拟模式 ====================*/
#if CAN_ENABLE
    if (HAL_GetTick() - lastHeartbeat >= 100) {
        lastHeartbeat = HAL_GetTick();
        H753_CAN_SendHeartbeat();
    }

    static uint32_t lastCmd = 0;
    if (HAL_GetTick() - lastCmd >= 2000) {
        lastCmd = HAL_GetTick();
        static uint8_t ci = 0;
        switch (ci++ % 3) {
            case 0: H753_CAN_SendAgvCmd(120, 0); break;
            case 1: H753_CAN_SendConveyorCmd(1, 80, 0); break;
            case 2: { uint8_t d[8]={90,45}; H753_CAN_SendRaw(0x203,d,8); } break;
        }
    }

    if (H753_CAN_IsStatusFrameValid()) {
        H753_CAN_GetStatus(&status);
        H753_CAN_ClearStatusFlag();
        printf("[Status] dc=%d step=%d enc=%d L=%d R=%d sys=0x%02X\r\n",
               status.dc_speed, status.step_speed, status.encoder_cnt,
               status.sensor_left, status.sensor_right, status.sys_status);
    }
#endif

#else
    /*==================== 真实模式 ====================*/

    /*--- 串口屏触摸处理 ---*/
    Screen_Task();

    /*--- 每 1s 刷新屏幕显示 ---*/
    if (HAL_GetTick() - lastDisplay >= 1000) {
        lastDisplay = HAL_GetTick();
        Screen_SetVal("n2", g474_online ? (int32_t)status.encoder_cnt : -1);
    }

#if CAN_ENABLE
    /*--- 每 100ms 心跳 ---*/
    if (HAL_GetTick() - lastHeartbeat >= 100) {
        lastHeartbeat = HAL_GetTick();
        H753_CAN_SendHeartbeat();
    }

    /*--- 接收 G474 状态帧 ---*/
    if (H753_CAN_IsStatusFrameValid()) {
        H753_CAN_GetStatus(&status);
        H753_CAN_ClearStatusFlag();

        g474_online   = 1;
        g474_last_seen = HAL_GetTick();

        /* 推送数据到屏幕 */
        Screen_SetVal("n0", status.dc_speed);
        Screen_SetVal("n1", status.step_speed);
        Screen_SetVal("n2", status.encoder_cnt);
        Screen_SetVal("n3", status.sensor_left);
        Screen_SetVal("n4", status.sensor_right);

        /* 系统状态文本 */
        if (status.sys_status & 0x01) {
            Screen_SetText("t1", "脱轨! 已停车");
        } else if (status.sys_status & 0x02) {
            Screen_SetText("t1", "通信超时");
        } else {
            Screen_SetText("t1", "运行中");
        }

        /* CAN 连接状态显示 */
        Screen_SetText("t2", "G474 已连接");
    }

    /*--- 500ms G474 断线检测 ---*/
    if (HAL_GetTick() - lastTimeoutCheck >= 500) {
        lastTimeoutCheck = HAL_GetTick();
        if (g474_online && (HAL_GetTick() - g474_last_seen > 500)) {
            g474_online = 0;
            Screen_SetText("t2", "G474 断开");
            Screen_SetText("t1", "通信中断");
        }
    }

    /*--- NFC + 麦克风 ---*/
    NFC_Task();
    mic_process();

#endif /* CAN_ENABLE */
#endif /* APP_MODE */
}
