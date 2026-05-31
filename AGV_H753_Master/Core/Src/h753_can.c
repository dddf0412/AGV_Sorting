#include "h753_can.h"
#include <stdio.h>
#include <string.h>

extern FDCAN_HandleTypeDef hfdcan1;

static uint8_t  s_status_flag = 0;
static H753_StatusFrame_t s_last_status = {0};

void H753_CAN_Init(void)
{
    FDCAN_FilterTypeDef sFilterConfig = {0};

    sFilterConfig.IdType = FDCAN_STANDARD_ID;
    sFilterConfig.FilterIndex = 0;
    sFilterConfig.FilterType = FDCAN_FILTER_MASK;
    sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    sFilterConfig.FilterID1 = 0x000;
    sFilterConfig.FilterID2 = 0x7FF;   // accept all standard IDs

    if (HAL_FDCAN_ConfigFilter(&hfdcan1, &sFilterConfig) != HAL_OK)
    {
        printf("[CAN] Filter config failed\r\n");
        Error_Handler();
    }

    if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK)
    {
        printf("[CAN] Start failed\r\n");
        Error_Handler();
    }

    if (HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK)
    {
        printf("[CAN] Activate notification failed\r\n");
        Error_Handler();
    }

    printf("[CAN] H753 CAN init OK (1Mbps)\r\n");
}

uint8_t H753_CAN_SendHeartbeat(void)
{
    uint8_t data[8] = {0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    return H753_CAN_SendRaw(CAN_ID_HEARTBEAT, data, 8);
}

uint8_t H753_CAN_SendAgvCmd(uint8_t speed, uint8_t dir)
{
    uint8_t data[8] = {speed, dir, 0, 0, 0, 0, 0, 0};
    return H753_CAN_SendRaw(CAN_ID_AGV_CMD, data, 8);
}

uint8_t H753_CAN_SendConveyorCmd(uint8_t run, uint8_t speed, uint8_t dir)
{
    uint8_t data[8] = {run, speed, dir, 0, 0, 0, 0, 0};
    return H753_CAN_SendRaw(CAN_ID_CONVEYOR_CMD, data, 8);
}

uint8_t H753_CAN_SendRaw(uint32_t id, uint8_t *data, uint8_t len)
{
    FDCAN_TxHeaderTypeDef TxHeader = {0};

    TxHeader.Identifier = id;
    TxHeader.IdType = FDCAN_STANDARD_ID;
    TxHeader.TxFrameType = FDCAN_DATA_FRAME;
    TxHeader.DataLength = FDCAN_DLC_BYTES_8;
    TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
    TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
    TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    TxHeader.MessageMarker = 0;

    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &TxHeader, data) != HAL_OK)
    {
        printf("[CAN] Send ID=0x%03lX failed\r\n", id);
        return 1;
    }
    printf("[CAN] Sent ID=0x%03lX, data: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
           id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
    return 0;
}

void H753_CAN_RxHandler(void)
{
    FDCAN_RxHeaderTypeDef RxHeader;
    uint8_t RxData[8];

    if (HAL_FDCAN_GetRxMessage(&hfdcan1, FDCAN_RX_FIFO0, &RxHeader, RxData) == HAL_OK)
    {
        printf("[CAN] Received ID=0x%03lX, data: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
               RxHeader.Identifier, RxData[0], RxData[1], RxData[2], RxData[3],
               RxData[4], RxData[5], RxData[6], RxData[7]);

        if (RxHeader.Identifier == CAN_ID_STATUS && RxData[7] == 0xAA)
        {
            s_status_flag = 1;
            s_last_status.dc_speed     = (int8_t)RxData[0];
            s_last_status.step_speed   = RxData[1];
            s_last_status.encoder_cnt  = (int16_t)(RxData[2] | (RxData[3] << 8));
            s_last_status.sensor_left  = RxData[4];
            s_last_status.sensor_right = RxData[5];
            s_last_status.sys_status   = RxData[6];
            s_last_status.tail         = RxData[7];
        }
    }
}

uint8_t H753_CAN_IsStatusFrameValid(void) { return s_status_flag; }
void H753_CAN_GetStatus(H753_StatusFrame_t *out)
{
    if (out) memcpy(out, &s_last_status, sizeof(H753_StatusFrame_t));
}
void H753_CAN_ClearStatusFlag(void) { s_status_flag = 0; }

/* HAL callback */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    (void)hfdcan;
    (void)RxFifo0ITs;
    H753_CAN_RxHandler();
}
