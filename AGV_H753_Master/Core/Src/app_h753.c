#include "app_h753.h"
#include "h753_can.h"
#include "h753_debug.h"
#include <stdio.h>

#if (APP_MODE == 1)
#include "nfc.h"
#include "microphone.h"
#include "screen.h"
#include "camera.h"
#include "wm8960.h"
#include "kws_pipeline.h"
extern UART_HandleTypeDef huart1;
#endif

/*================ 调试开关 ================*/
#define CAN_ENABLE  0  /* 1=启用CAN, 0=关闭CAN */

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
    printf("\r\n========== H753 KWS ==========\r\n");
    // Screen_Init(&huart1);
    // Camera_Init(); Camera_Start();
    // WM8960_Init();

    KWS_Init(); mic_start_monitor(KWS_Feed);
    printf("[App] KWS running\r\n");
#endif
}

/*================ App_Run ================*/
void App_Run(void)
{
#if (APP_MODE == 0)
    /* 模拟模式 */
    H753_StatusFrame_t status;
#if CAN_ENABLE
    static uint32_t lastHeartbeat = 0;
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
#endif /* CAN_ENABLE */

#else
    /* 真实模式 */
    H753_StatusFrame_t status;
    static uint32_t lastHeartbeat = 0;
    static uint32_t lastTimeoutCheck = 0;
    static uint8_t  g474_online = 0;
    static uint32_t g474_last_seen = 0;

#if CAN_ENABLE
    if (HAL_GetTick() - lastHeartbeat >= 100) {
        lastHeartbeat = HAL_GetTick();
        H753_CAN_SendHeartbeat();
    }

    if (H753_CAN_IsStatusFrameValid()) {
        H753_CAN_GetStatus(&status);
        H753_CAN_ClearStatusFlag();
        g474_online     = 1;
        g474_last_seen  = HAL_GetTick();
    }

    if (HAL_GetTick() - lastTimeoutCheck >= 500) {
        lastTimeoutCheck = HAL_GetTick();
        if (g474_online && (HAL_GetTick() - g474_last_seen > 500))
            g474_online = 0;
    }
#endif /* CAN_ENABLE */

    // NFC_Task();
    mic_process();
    if (KWS_IsResultReady()) {
        KWS_Result_t r = KWS_GetResult();
        printf("[KWS] %s (%d%%)\r\n", r.label, r.confidence);
    }
    // Camera frame handling

#endif /* APP_MODE */
}
