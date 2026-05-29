/* Core/Src/motor_control.c */
#include "motor_control.h"
#include "adc.h"
#include "tim.h"

/* 内部PID结构 */
typedef struct {
    float kp, ki, kd;
    float err, err_last, integral;
    float output;
} PID_t;

//static PID_t pid_dc = {2.0f, 0.5f, 0.1f, 0, 0, 0, 0};

/*================ 直流电机 ================*/
void DC_Motor_SetSpeed(int8_t speed)
{
    if (g_agv.motor_locked && speed != 0) return;

    uint16_t pwm = 0;
    if (speed > 100) speed = 100;
    if (speed < -100) speed = -100;

    if (speed >= 0) {
        HAL_GPIO_WritePin(MOTOR_DIR1_GPIO_Port, MOTOR_DIR1_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(MOTOR_DIR2_GPIO_Port, MOTOR_DIR2_Pin, GPIO_PIN_RESET);
        pwm = (uint16_t)(speed * DC_MOTOR_PWM_MAX / 100);
    } else {
        HAL_GPIO_WritePin(MOTOR_DIR1_GPIO_Port, MOTOR_DIR1_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_DIR2_GPIO_Port, MOTOR_DIR2_Pin, GPIO_PIN_SET);
        pwm = (uint16_t)((-speed) * DC_MOTOR_PWM_MAX / 100);
    }

    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pwm);
    g_agv.feedback.dc_speed = speed;
}

void DC_Motor_Stop(void)
{
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
    HAL_GPIO_WritePin(MOTOR_DIR1_GPIO_Port, MOTOR_DIR1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_DIR2_GPIO_Port, MOTOR_DIR2_Pin, GPIO_PIN_RESET);
    g_agv.feedback.dc_speed = 0;
}

/*================ 步进电机 ================*/
void Stepper_SetSpeed(uint8_t speed)
{
    if (g_agv.motor_locked && speed != 0) return;

    if (speed == 0) {
        Stepper_Stop();
        return;
    }

    /* 速度映射到脉冲频率: 0~255 -> 100Hz~2kHz */
    uint16_t arr = (uint16_t)(1000000 / (100 + speed * 7.5f)) - 1;
    if (arr > 9999) arr = 9999;
    if (arr < 499) arr = 499;

    __HAL_TIM_SET_AUTORELOAD(&htim1, arr);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, arr / 2); /* 50%占空比 */

    if (__HAL_TIM_GET_COUNTER(&htim1) == 0 && !(htim1.Instance->CR1 & TIM_CR1_CEN)) {
        HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    }

    g_agv.feedback.step_speed = speed;
}

void Stepper_SetDirection(uint8_t dir)
{
    HAL_GPIO_WritePin(STEP_DIR_GPIO_Port, STEP_DIR_Pin,
                      dir ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Stepper_Stop(void)
{
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
    g_agv.feedback.step_speed = 0;
}

/*================ 舵机 ================*/
void Servo_SetAngle(uint8_t ch, uint8_t angle)
{
    if (angle > 180) angle = 180;

    /* 角度映射到脉宽: 0~180 -> 500~2500us */
    uint16_t pulse = SERVO_PWM_MIN + (angle * (SERVO_PWM_MAX - SERVO_PWM_MIN) / 180);

    if (ch == 0) {
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, pulse);
    } else {
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, pulse);
    }
}

/*================ 编码器 ================*/
int16_t Encoder_GetCount(void)
{
    return (int16_t)__HAL_TIM_GET_COUNTER(&htim3);
}

void Encoder_Reset(void)
{
    __HAL_TIM_SET_COUNTER(&htim3, 0);
}

/*================ 传感器读取 ================*/
void Sensor_Read(uint8_t* left, uint8_t* right)
{
    /* 启动ADC转换 */
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 1);
    uint16_t raw_left = HAL_ADC_GetValue(&hadc1);

    HAL_ADC_Start(&hadc2);
    HAL_ADC_PollForConversion(&hadc2, 1);
    uint16_t raw_right = HAL_ADC_GetValue(&hadc2);

    /* 12bit右移4位 -> 8bit (0~255) */
    *left = (uint8_t)(raw_left >> 4);
    *right = (uint8_t)(raw_right >> 4);
}

/*================ 1kHz控制循环 ================*/
void Control_Loop_1kHz(void)
{
    static uint16_t can_tx_div = 0;

    /* 1. 读取编码器 */
    g_agv.feedback.encoder_count = Encoder_GetCount();

    /* 2. 读取传感器 */
    Sensor_Read(&g_agv.feedback.sensor_left, &g_agv.feedback.sensor_right);

    /* 3. 脱轨检测: 双传感器均为白色(值<阈值) */
    uint8_t threshold = 50; /* 根据实际地面调试 */
    if (g_agv.feedback.sensor_left < threshold &&
        g_agv.feedback.sensor_right < threshold) {
        g_agv.feedback.status.bits.derail_flag = 1;
        DC_Motor_Stop(); /* 立即停车 */
    } else {
        g_agv.feedback.status.bits.derail_flag = 0;
    }

    /* 4. 通信超时检测 */
    if (HAL_GetTick() - g_agv.heartbeat_rx_tick > HEARTBEAT_TIMEOUT_MS) {
        g_agv.feedback.status.bits.comm_timeout = 1;
        DC_Motor_Stop();
        Stepper_Stop();
    } else {
        g_agv.feedback.status.bits.comm_timeout = 0;
    }

    /* 5. 执行CAN指令 (如果未锁定) */
    if (!g_agv.motor_locked) {
        DC_Motor_SetSpeed(g_agv.cmd.agv_speed);
        if (g_agv.cmd.conveyor_enable) {
            Stepper_SetDirection(g_agv.cmd.conveyor_dir);
            Stepper_SetSpeed(g_agv.cmd.conveyor_speed);
        } else {
            Stepper_Stop();
        }
        Servo_SetAngle(0, g_agv.cmd.servo_pitch);
        Servo_SetAngle(1, g_agv.cmd.servo_yaw);
    } else {
        DC_Motor_Stop();
        Stepper_Stop();
    }

    /* 6. 100Hz CAN状态上报 */
    can_tx_div++;
    if (can_tx_div >= (CONTROL_FREQ_HZ / CAN_TX_FREQ_HZ)) {
        can_tx_div = 0;
        /* CAN发送将在can_protocol.c中实现 */
        extern void CAN_SendStatus(void);
        CAN_SendStatus();
    }
}

/*================ 初始化 ================*/
void Motor_Init(void)
{
    /* 启动PWM (初始占空比0) */
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);

    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);

    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);

    /* 舵机归中 */
    Servo_SetAngle(0, 90);
    Servo_SetAngle(1, 90);

    /* 启动编码器 */
    HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);

    /* 启动TIM6中断 (1kHz控制循环) */
    HAL_TIM_Base_Start_IT(&htim6);
}
