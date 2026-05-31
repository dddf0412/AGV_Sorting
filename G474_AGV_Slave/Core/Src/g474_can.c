#include "g474_can.h"
#include <stdio.h>
#include <string.h>

extern FDCAN_HandleTypeDef hfdcan1;

static uint32_t s_last_rx_id = 0;
static uint8_t  s_last_rx_data[8] = {0};
static uint8_t  s_heartbeat_flag = 0;

void G474_CAN_Init(void)
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

    printf("[CAN] G474 CAN init OK (1Mbps)\r\n");
}

uint8_t G474_CAN_SendStatus(const G474_StatusFrame_t *status)
{
    uint8_t TxData[8];
    TxData[0] = (uint8_t)status->dc_speed;
    TxData[1] = status->step_speed;
    TxData[2] = (uint8_t)(status->encoder_cnt & 0xFF);
    TxData[3] = (uint8_t)((status->encoder_cnt >> 8) & 0xFF);
    TxData[4] = status->sensor_left;
    TxData[5] = status->sensor_right;
    TxData[6] = status->sys_status;
    TxData[7] = status->tail;

    return G474_CAN_SendRaw(CAN_ID_STATUS, TxData, 8);
}

uint8_t G474_CAN_SendRaw(uint32_t id, uint8_t *data, uint8_t len)
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

void G474_CAN_RxHandler(void)
{
    FDCAN_RxHeaderTypeDef RxHeader;
    uint8_t RxData[8];

    if (HAL_FDCAN_GetRxMessage(&hfdcan1, FDCAN_RX_FIFO0, &RxHeader, RxData) == HAL_OK)
    {
        s_last_rx_id = RxHeader.Identifier;
        memcpy(s_last_rx_data, RxData, 8);
        printf("[CAN] Received ID=0x%03lX, data: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
               RxHeader.Identifier, RxData[0], RxData[1], RxData[2], RxData[3],
               RxData[4], RxData[5], RxData[6], RxData[7]);

        if (RxHeader.Identifier == CAN_ID_HEARTBEAT && RxData[0] == 0xAA)
        {
            s_heartbeat_flag = 1;
        }
    }
}

/* Query and clear interfaces */
uint32_t G474_CAN_GetLastRxID(void)       { return s_last_rx_id; }
uint8_t* G474_CAN_GetLastRxData(void)     { return s_last_rx_data; }
uint8_t  G474_CAN_IsHeartbeatReceived(void) { return s_heartbeat_flag; }
void     G474_CAN_ClearHeartbeatFlag(void)  { s_heartbeat_flag = 0; }
void     G474_CAN_ClearLastRxID(void)       { s_last_rx_id = 0; }

/* HAL callback */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    (void)hfdcan;
    (void)RxFifo0ITs;
    G474_CAN_RxHandler();
}
