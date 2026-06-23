#include "camera.h"
#include "dcmi.h"
#include "i2c.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

/*================ 帧缓冲 ================*/
/* JPEG 模式, QVGA 320x240, 最大帧 ~50KB */
#define CAMERA_FB_WIDTH   320
#define CAMERA_FB_HEIGHT  240
#define CAMERA_FB_SIZE    (CAMERA_FB_WIDTH * CAMERA_FB_HEIGHT)

static uint32_t camera_fb[CAMERA_FB_SIZE / 2] __attribute__((aligned(32)));  /* 32-bit word buffer for DCMI */
static uint8_t  camera_tx_fb[CAMERA_FB_SIZE * 2] __attribute__((aligned(32))); /* TX byte buffer */
volatile uint8_t camera_frame_ready = 0;
static volatile uint8_t  camera_tx_busy = 0;
static volatile uint32_t camera_jpeg_size = 0;  /* 当前 JPEG 帧字节数 */

extern DCMI_HandleTypeDef hdcmi;

/*================ 内部状态 ================*/
static Camera_Resolution_t current_res = CAMERA_RES_QVGA;
static Camera_Format_t     current_fmt = CAMERA_FMT_JPEG;
static uint8_t             camera_started = 0;

/*================ 寄存器序列表 ================*/

/* SVGA 800x600 初始化 (30fps) */
const uint8_t ov2640_init_svga_cfg[][2] = {
    {0xFF, 0x00}, {0x2C, 0xFF}, {0x2E, 0xDF}, {0xFF, 0x01}, {0x3C, 0x32},
    {0x11, 0x00}, {0x09, 0x02}, {0x04, 0xA8}, {0x13, 0xE5}, {0x14, 0x48},
    {0x2C, 0x0C}, {0x33, 0x78}, {0x3A, 0x33}, {0x3B, 0xFB}, {0x3E, 0x00},
    {0x43, 0x11}, {0x16, 0x10}, {0x39, 0x92}, {0x35, 0xDA}, {0x22, 0x1A},
    {0x37, 0xC3}, {0x23, 0x00}, {0x34, 0xC0}, {0x36, 0x1A}, {0x06, 0x88},
    {0x07, 0xC0}, {0x0D, 0x87}, {0x0E, 0x41}, {0x4C, 0x00}, {0x48, 0x00},
    {0x5B, 0x00}, {0x42, 0x03}, {0x4A, 0x81}, {0x21, 0x99}, {0x24, 0x40},
    {0x25, 0x38}, {0x26, 0x82}, {0x5C, 0x00}, {0x63, 0x00}, {0x46, 0x22},
    {0x0C, 0x3C}, {0x61, 0x70}, {0x62, 0x80}, {0x7C, 0x05}, {0x20, 0x80},
    {0x28, 0x30}, {0x6C, 0x00}, {0x6D, 0x80}, {0x6E, 0x00}, {0x70, 0x02},
    {0x71, 0x94}, {0x73, 0xC1}, {0x3D, 0x34}, {0x5A, 0x57}, {0x12, 0x40},
    {0x17, 0x11}, {0x18, 0x43}, {0x19, 0x00}, {0x1A, 0x4B}, {0x32, 0x09},
    {0x37, 0xC0}, {0x4F, 0xCA}, {0x50, 0xA8}, {0x5A, 0x23}, {0x6D, 0x00},
    {0x3D, 0x38}, {0xFF, 0x00}, {0xE5, 0x7F}, {0xF9, 0xC0}, {0x41, 0x24},
    {0xE0, 0x14}, {0x76, 0xFF}, {0x33, 0xA0}, {0x42, 0x20}, {0x43, 0x18},
    {0x4C, 0x00}, {0x87, 0xD5}, {0x88, 0x3F}, {0xD7, 0x03}, {0xD9, 0x10},
    {0xD3, 0x82}, {0xC8, 0x08}, {0xC9, 0x80}, {0x7C, 0x00}, {0x7D, 0x00},
    {0x7C, 0x03}, {0x7D, 0x48}, {0x7D, 0x48}, {0x7C, 0x08}, {0x7D, 0x20},
    {0x7D, 0x10}, {0x7D, 0x0E}, {0x90, 0x00}, {0x91, 0x0E}, {0x91, 0x1A},
    {0x91, 0x31}, {0x91, 0x5A}, {0x91, 0x69}, {0x91, 0x75}, {0x91, 0x7E},
    {0x91, 0x88}, {0x91, 0x8F}, {0x91, 0x96}, {0x91, 0xA3}, {0x91, 0xAF},
    {0x91, 0xC4}, {0x91, 0xD7}, {0x91, 0xE8}, {0x91, 0x20}, {0x92, 0x00},
    {0x93, 0x06}, {0x93, 0xE3}, {0x93, 0x05}, {0x93, 0x05}, {0x93, 0x00},
    {0x93, 0x04}, {0x93, 0x00}, {0x93, 0x00}, {0x93, 0x00}, {0x93, 0x00},
    {0x93, 0x00}, {0x93, 0x00}, {0x93, 0x00}, {0x96, 0x00}, {0x97, 0x08},
    {0x97, 0x19}, {0x97, 0x02}, {0x97, 0x0C}, {0x97, 0x24}, {0x97, 0x30},
    {0x97, 0x28}, {0x97, 0x26}, {0x97, 0x02}, {0x97, 0x98}, {0x97, 0x80},
    {0x97, 0x00}, {0x97, 0x00}, {0xC3, 0xED}, {0xA4, 0x00}, {0xA8, 0x00},
    {0xC5, 0x11}, {0xC6, 0x51}, {0xBF, 0x80}, {0xC7, 0x10}, {0xB6, 0x66},
    {0xB8, 0xA5}, {0xB7, 0x64}, {0xB9, 0x7C}, {0xB3, 0xAF}, {0xB4, 0x97},
    {0xB5, 0xFF}, {0xB0, 0xC5}, {0xB1, 0x94}, {0xB2, 0x0F}, {0xC4, 0x5C},
    {0xC0, 0x64}, {0xC1, 0x4B}, {0x8C, 0x00}, {0x86, 0x3D}, {0x50, 0x00},
    {0x51, 0xC8}, {0x52, 0x96}, {0x53, 0x00}, {0x54, 0x00}, {0x55, 0x00},
    {0x5A, 0xC8}, {0x5B, 0x96}, {0x5C, 0x00}, {0xD3, 0x02}, {0xC3, 0xED},
    {0x7F, 0x00}, {0xDA, 0x09}, {0xE5, 0x1F}, {0xE1, 0x67}, {0xE0, 0x00},
    {0xDD, 0x7F}, {0x05, 0x00},
};

/* UXGA 1600x1200 初始化 (15fps) */
const uint8_t ov2640_init_uxga_cfg[][2] = {
    {0xFF, 0x00}, {0x2C, 0xFF}, {0x2E, 0xDF}, {0xFF, 0x01}, {0x3C, 0x32},
    {0x11, 0x00}, {0x09, 0x02}, {0x04, 0xA8}, {0x13, 0xE5}, {0x14, 0x48},
    {0x2C, 0x0C}, {0x33, 0x78}, {0x3A, 0x33}, {0x3B, 0xFB}, {0x3E, 0x00},
    {0x43, 0x11}, {0x16, 0x10}, {0x39, 0x92}, {0x35, 0xDA}, {0x22, 0x1A},
    {0x37, 0xC3}, {0x23, 0x00}, {0x34, 0xC0}, {0x36, 0x1A}, {0x06, 0x88},
    {0x07, 0xC0}, {0x0D, 0x87}, {0x0E, 0x41}, {0x4C, 0x00}, {0x48, 0x00},
    {0x5B, 0x00}, {0x42, 0x03}, {0x4A, 0x81}, {0x21, 0x99}, {0x24, 0x40},
    {0x25, 0x38}, {0x26, 0x82}, {0x5C, 0x00}, {0x63, 0x00}, {0x46, 0x00},
    {0x0C, 0x3C}, {0x61, 0x70}, {0x62, 0x80}, {0x7C, 0x05}, {0x20, 0x80},
    {0x28, 0x30}, {0x6C, 0x00}, {0x6D, 0x80}, {0x6E, 0x00}, {0x70, 0x02},
    {0x71, 0x94}, {0x73, 0xC1}, {0x3D, 0x34}, {0x5A, 0x57}, {0x12, 0x00},
    {0x17, 0x11}, {0x18, 0x75}, {0x19, 0x01}, {0x1A, 0x97}, {0x32, 0x36},
    {0x03, 0x0F}, {0x37, 0x40}, {0x4F, 0xCA}, {0x50, 0xA8}, {0x5A, 0x23},
    {0x6D, 0x00}, {0x6D, 0x38}, {0xFF, 0x00}, {0xE5, 0x7F}, {0xF9, 0xC0},
    {0x41, 0x24}, {0xE0, 0x14}, {0x76, 0xFF}, {0x33, 0xA0}, {0x42, 0x20},
    {0x43, 0x18}, {0x4C, 0x00}, {0x87, 0xD5}, {0x88, 0x3F}, {0xD7, 0x03},
    {0xD9, 0x10}, {0xD3, 0x82}, {0xC8, 0x08}, {0xC9, 0x80}, {0x7C, 0x00},
    {0x7D, 0x00}, {0x7C, 0x03}, {0x7D, 0x48}, {0x7D, 0x48}, {0x7C, 0x08},
    {0x7D, 0x20}, {0x7D, 0x10}, {0x7D, 0x0E}, {0x90, 0x00}, {0x91, 0x0E},
    {0x91, 0x1A}, {0x91, 0x31}, {0x91, 0x5A}, {0x91, 0x69}, {0x91, 0x75},
    {0x91, 0x7E}, {0x91, 0x88}, {0x91, 0x8F}, {0x91, 0x96}, {0x91, 0xA3},
    {0x91, 0xAF}, {0x91, 0xC4}, {0x91, 0xD7}, {0x91, 0xE8}, {0x91, 0x20},
    {0x92, 0x00}, {0x93, 0x06}, {0x93, 0xE3}, {0x93, 0x05}, {0x93, 0x05},
    {0x93, 0x00}, {0x93, 0x04}, {0x93, 0x00}, {0x93, 0x00}, {0x93, 0x00},
    {0x93, 0x00}, {0x93, 0x00}, {0x93, 0x00}, {0x93, 0x00}, {0x96, 0x00},
    {0x97, 0x08}, {0x97, 0x19}, {0x97, 0x02}, {0x97, 0x0C}, {0x97, 0x24},
    {0x97, 0x30}, {0x97, 0x28}, {0x97, 0x26}, {0x97, 0x02}, {0x97, 0x98},
    {0x97, 0x80}, {0x97, 0x00}, {0x97, 0x00}, {0xC3, 0xEF}, {0xA4, 0x00},
    {0xA8, 0x00}, {0xC5, 0x11}, {0xC6, 0x51}, {0xBF, 0x80}, {0xC7, 0x10},
    {0xB6, 0x66}, {0xB8, 0xA5}, {0xB7, 0x64}, {0xB9, 0x7C}, {0xB3, 0xAF},
    {0xB4, 0x97}, {0xB5, 0xFF}, {0xB0, 0xC5}, {0xB1, 0x94}, {0xB2, 0x0F},
    {0xC4, 0x5C}, {0xC0, 0xC8}, {0xC1, 0x96}, {0x8C, 0x00}, {0x86, 0x3D},
    {0x50, 0x00}, {0x51, 0x90}, {0x52, 0x2C}, {0x53, 0x00}, {0x54, 0x00},
    {0x55, 0x88}, {0x5A, 0x90}, {0x5B, 0x2C}, {0x5C, 0x05}, {0xD3, 0x02},
    {0xC3, 0xED}, {0x7F, 0x00}, {0xDA, 0x09}, {0xE5, 0x1F}, {0xE1, 0x67},
    {0xE0, 0x00}, {0xDD, 0x7F}, {0x05, 0x00},
};

/* 切换到 RGB565 输出的寄存器序列 */
const uint8_t ov2640_set_rgb565_cfg[][2] = {
    {0xFF, 0x00}, {0xDA, 0x09}, {0xD7, 0x03}, {0xDF, 0x02}, {0x33, 0xA0},
    {0x3C, 0x00}, {0xE1, 0x67}, {0xFF, 0x01}, {0xE0, 0x00}, {0xE1, 0x00},
    {0xE5, 0x00}, {0xD7, 0x00}, {0xDA, 0x00}, {0xE0, 0x00},
};

/* 切换到 YUV422 输出的寄存器序列 (JPEG 的前置步骤) */
const uint8_t ov2640_set_yuv422_cfg[][2] = {
    {0xFF, 0x00}, {0xDA, 0x10}, {0xD7, 0x03}, {0xDF, 0x00}, {0x33, 0x80},
    {0x3C, 0x40}, {0xE1, 0x77}, {0x00, 0x00},
};

/* 切换到 JPEG 输出的寄存器序列 */
const uint8_t ov2640_set_jpeg_cfg[][2] = {
    {0xFF, 0x01}, {0xE0, 0x14}, {0xE1, 0x77}, {0xE5, 0x1F}, {0xD7, 0x03},
    {0xDA, 0x10}, {0xE0, 0x00},
};

/*================ OV2640 寄存器读写 (硬件 I2C1, 与 WM8960 共享总线) ================*/

extern I2C_HandleTypeDef hi2c1;

static void ov2640_write_reg(uint8_t reg, uint8_t dat)
{
    HAL_I2C_Mem_Write(&hi2c1, OV2640_ADDR << 1, reg,
                      I2C_MEMADD_SIZE_8BIT, &dat, 1, 10);
}

static uint8_t ov2640_read_reg(uint8_t reg)
{
    uint8_t dat = 0;
    HAL_I2C_Mem_Read(&hi2c1, OV2640_ADDR << 1, reg,
                     I2C_MEMADD_SIZE_8BIT, &dat, 1, 10);
    return dat;
}

/* 选择寄存器组 (0=DSP, 1=Sensor) */
static void ov2640_reg_bank_select(uint8_t bank)
{
    ov2640_write_reg(OV2640_REG_BANK_SEL, bank ? 0x01 : 0x00);
}

/* 写寄存器序列表 */
static void ov2640_write_table(const uint8_t table[][2], uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        ov2640_write_reg(table[i][0], table[i][1]);
    }
}

/* 获取当前分辨率对应的宽高 */
static void get_resolution(Camera_Resolution_t res, uint16_t *w, uint16_t *h)
{
    switch (res) {
        case CAMERA_RES_QQVGA: *w = 160;  *h = 120;  break;
        case CAMERA_RES_QVGA:  *w = 320;  *h = 240;  break;
        case CAMERA_RES_VGA:   *w = 640;  *h = 480;  break;
        case CAMERA_RES_SVGA:  *w = 800;  *h = 600;  break;
        default:               *w = 320;  *h = 240;  break;
    }
}

/* 设置 OV2640 输出窗口 (Sensor 裁剪 + DSP 缩放) */
static void ov2640_set_image_window(uint16_t off_x, uint16_t off_y,
                                     uint16_t width, uint16_t height)
{
    uint16_t hsize, vsize;
    uint8_t vhyx;

    hsize = width >> 2;
    vsize = height >> 2;
    vhyx = (uint8_t)(((vsize >> 1) & 0x80) | ((off_y >> 4) & 0x70)
                   | ((hsize >> 5) & 0x08) | ((off_x >> 8) & 0x07));

    ov2640_reg_bank_select(0);
    ov2640_write_reg(OV2640_REG_DSP_RESET, 0x04);
    ov2640_write_reg(OV2640_REG_DSP_HSIZE, (uint8_t)(hsize & 0xFF));
    ov2640_write_reg(OV2640_REG_DSP_VSIZE, (uint8_t)(vsize & 0xFF));
    ov2640_write_reg(OV2640_REG_DSP_XOFFL, (uint8_t)(off_x & 0xFF));
    ov2640_write_reg(OV2640_REG_DSP_YOFFL, (uint8_t)(off_y & 0xFF));
    ov2640_write_reg(OV2640_REG_DSP_VHYX, vhyx);
    ov2640_write_reg(OV2640_REG_DSP_TEST, (uint8_t)((hsize >> 2) & 0x80));
    ov2640_write_reg(OV2640_REG_DSP_RESET, 0x00);
}

/* 设置 OV2640 输出尺寸 (缩放后) */
static void ov2640_set_output_size(uint16_t width, uint16_t height)
{
    uint16_t ow = width >> 2;
    uint16_t oh = height >> 2;

    ov2640_reg_bank_select(0);
    ov2640_write_reg(OV2640_REG_DSP_RESET, 0x04);
    ov2640_write_reg(OV2640_REG_DSP_ZMOW, (uint8_t)(ow & 0xFF));
    ov2640_write_reg(OV2640_REG_DSP_ZMOH, (uint8_t)(oh & 0xFF));
    ov2640_write_reg(OV2640_REG_DSP_ZMHH,
                     ((uint8_t)(ow >> 8) & 0x03) | ((uint8_t)(oh >> 6) & 0x04));
    ov2640_write_reg(OV2640_REG_DSP_RESET, 0x00);

    printf("[Camera] Output size: %dx%d\r\n", width, height);
}

/* OV2640 硬复位 */
static void ov2640_hw_reset(void)
{
    CAMERA_RST(0);
    HAL_Delay(10);
    CAMERA_RST(1);
    HAL_Delay(10);
}

/* OV2640 软复位 */
static void ov2640_sw_reset(void)
{
    ov2640_reg_bank_select(1);
    ov2640_write_reg(OV2640_REG_SENSOR_COM7, 0x80);
    HAL_Delay(50);
}

/* 读 MID/PID 验证芯片身份 */
static int ov2640_verify_id(void)
{
    uint16_t mid, pid;

    ov2640_reg_bank_select(1);
    mid  = ov2640_read_reg(OV2640_REG_SENSOR_MIDH) << 8;
    mid |= ov2640_read_reg(OV2640_REG_SENSOR_MIDL);

    pid  = ov2640_read_reg(OV2640_REG_SENSOR_PIDH) << 8;
    pid |= ov2640_read_reg(OV2640_REG_SENSOR_PIDL);

    printf("[Camera] MID=0x%04X PID=0x%04X\r\n", mid, pid);

    if (mid != 0x7FA2 || pid != 0x2642) {
        printf("[Camera] OV2640 not detected!\r\n");
        return CAMERA_ERROR;
    }
    return CAMERA_OK;
}

/*================ DCMI 回调 (覆盖 HAL __weak) ================*/

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi_cb)
{
    if (hdcmi_cb->Instance == DCMI) {
        static uint32_t fcnt = 0;
        uint32_t words, size;

        fcnt++;

        /* 从 DMA NDTR 计算实际传输的 32-bit word 数 */
        words = (CAMERA_FB_SIZE / 2) - ((DMA_Stream_TypeDef *)hdcmi.DMA_Handle->Instance)->NDTR;
        size  = words * 4;

        if (size == 0 || size > sizeof(camera_tx_fb)) {
            HAL_DCMI_Stop(&hdcmi);
            HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_SNAPSHOT,
                               (uint32_t)camera_fb, CAMERA_FB_SIZE / 2);
            return;
        }

        /* 上帧发完才复制新帧, 否则丢弃 */
        if (!camera_tx_busy && camera_started) {
            memcpy(camera_tx_fb, camera_fb, size);
            camera_jpeg_size = size;
            camera_tx_busy = 1;
            camera_frame_ready = 1;
        }

        if (fcnt % 30 == 0) {
            printf("[Camera] Frame %lu, %lu bytes\r\n", fcnt, size);
        }
    }
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *hdcmi_cb)
{
    if (hdcmi_cb->Instance == DCMI) {
        printf("[Camera] DCMI Error: %lu\r\n", hdcmi_cb->ErrorCode);
    }
}

/*================ API 实现 ================*/

/**
 * @brief  初始化 OV2640 摄像头模块
 * @retval CAMERA_OK / CAMERA_ERROR
 */
int Camera_Init(void)
{
    uint16_t w, h;

    ov2640_hw_reset();
    /* I2C1 已在 main.c 中由 MX_I2C1_Init 初始化 */
    ov2640_sw_reset();

    if (ov2640_verify_id() != CAMERA_OK) {
        return CAMERA_ERROR;
    }

    ov2640_write_table(ov2640_init_svga_cfg,
                       sizeof(ov2640_init_svga_cfg) / sizeof(ov2640_init_svga_cfg[0]));
    ov2640_write_table(ov2640_set_yuv422_cfg,
                       sizeof(ov2640_set_yuv422_cfg) / sizeof(ov2640_set_yuv422_cfg[0]));
    ov2640_write_table(ov2640_set_jpeg_cfg,
                       sizeof(ov2640_set_jpeg_cfg) / sizeof(ov2640_set_jpeg_cfg[0]));

    get_resolution(current_res, &w, &h);
    ov2640_set_output_size(w, h);

    current_fmt = CAMERA_FMT_JPEG;
    printf("[Camera] Init OK (%dx%d JPEG)\r\n", w, h);
    return CAMERA_OK;
}

/**
 * @brief  启动 DCMI 连续帧捕获
 * @retval CAMERA_OK / CAMERA_ERROR
 */
int Camera_Start(void)
{
    uint16_t w, h;
    uint32_t fb_size;

    if (camera_started) return CAMERA_OK;

    get_resolution(current_res, &w, &h);
    fb_size = (uint32_t)w * h / 2;  /* 缓冲: 32-bit word 数, 150KB 足够 JPEG */

    if (current_fmt == CAMERA_FMT_JPEG) {
        hdcmi.Init.JPEGMode = DCMI_JPEG_ENABLE;
    } else {
        hdcmi.Init.JPEGMode = DCMI_JPEG_DISABLE;
    }

    /* 重配 DCMI 极性 */
    hdcmi.Init.SynchroMode      = DCMI_SYNCHRO_HARDWARE;
    hdcmi.Init.PCKPolarity      = DCMI_PCKPOLARITY_RISING;
    hdcmi.Init.VSPolarity       = DCMI_VSPOLARITY_LOW;
    hdcmi.Init.HSPolarity       = DCMI_HSPOLARITY_LOW;
    hdcmi.Init.CaptureRate      = DCMI_CR_ALL_FRAME;
    hdcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
    if (HAL_DCMI_Init(&hdcmi) != HAL_OK) {
        printf("[Camera] DCMI re-init failed\r\n");
        return CAMERA_ERROR;
    }

    camera_frame_ready = 0;
    __HAL_DCMI_ENABLE_IT(&hdcmi, DCMI_IT_FRAME);

    if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_SNAPSHOT,
                           (uint32_t)camera_fb, fb_size) != HAL_OK) {
        printf("[Camera] DCMI Start_DMA failed\r\n");
        return CAMERA_ERROR;
    }

    camera_started = 1;
    printf("[Camera] DCMI started (%dx%d JPEG snapshot)\r\n", w, h);
    return CAMERA_OK;
}

/**
 * @brief  停止 DCMI 捕获
 * @retval CAMERA_OK
 */
int Camera_Stop(void)
{
    if (!camera_started) return CAMERA_OK;
    __HAL_DCMI_DISABLE_IT(&hdcmi, DCMI_IT_FRAME);
    HAL_DCMI_Stop(&hdcmi);
    camera_started = 0;
    camera_frame_ready = 0;
    printf("[Camera] DCMI stopped\r\n");
    return CAMERA_OK;
}

/**
 * @brief  切换输出格式 (RGB565 / JPEG)
 * @param  fmt: 目标格式
 * @retval CAMERA_OK / CAMERA_ERROR
 */
int Camera_SetFormat(Camera_Format_t fmt)
{
    if (fmt == current_fmt) return CAMERA_OK;

    switch (fmt) {
        case CAMERA_FMT_RGB565:
            ov2640_write_table(ov2640_set_rgb565_cfg,
                               sizeof(ov2640_set_rgb565_cfg) / sizeof(ov2640_set_rgb565_cfg[0]));
            hdcmi.Init.JPEGMode = DCMI_JPEG_DISABLE;
            break;
        case CAMERA_FMT_JPEG:
            ov2640_write_table(ov2640_set_yuv422_cfg,
                               sizeof(ov2640_set_yuv422_cfg) / sizeof(ov2640_set_yuv422_cfg[0]));
            ov2640_write_table(ov2640_set_jpeg_cfg,
                               sizeof(ov2640_set_jpeg_cfg) / sizeof(ov2640_set_jpeg_cfg[0]));
            hdcmi.Init.JPEGMode = DCMI_JPEG_ENABLE;
            break;
        default:
            return CAMERA_ERROR;
    }

    HAL_DCMI_Init(&hdcmi);
    current_fmt = fmt;
    printf("[Camera] Format -> %s\r\n", fmt == CAMERA_FMT_RGB565 ? "RGB565" : "JPEG");
    return CAMERA_OK;
}

/**
 * @brief  切换输出分辨率
 * @note   高分辨率需更大的帧缓冲，当前缓冲按 CAMERA_FB_WIDTH x CAMERA_FB_HEIGHT 分配
 * @param  res: 目标分辨率
 * @retval CAMERA_OK / CAMERA_ERROR
 */
int Camera_SetResolution(Camera_Resolution_t res)
{
    uint16_t w, h;
    get_resolution(res, &w, &h);

    if ((uint32_t)w * h > CAMERA_FB_SIZE) {
        printf("[Camera] Resolution %dx%d exceeds FB size\r\n", w, h);
        return CAMERA_ERROR;
    }

    ov2640_set_image_window(0, 0, w, h);
    ov2640_set_output_size(w, h);
    current_res = res;
    return CAMERA_OK;
}

/**
 * @brief  发送当前帧到电脑 (USART3, 原始 RGB565 格式)
 * @retval CAMERA_OK / CAMERA_ERROR
 */
int Camera_DumpFrame(void)
{
    uint8_t header[10];
    uint32_t data_len = camera_jpeg_size;

    header[0] = 0xAA;
    header[1] = 0x55;
    header[2] = 1;  /* type: 1=JPEG, 0=RGB565 */
    header[3] = 0;
    header[4] = (data_len >> 0) & 0xFF;
    header[5] = (data_len >> 8) & 0xFF;
    header[6] = (data_len >> 16) & 0xFF;
    header[7] = (data_len >> 24) & 0xFF;
    header[8] = 0x00;  /* reserved */
    header[9] = 0x00;

    if (HAL_UART_Transmit(&huart3, header, 10, 100) != HAL_OK) {
        camera_tx_busy = 0;
        return CAMERA_ERROR;
    }

    /* 分块发送 JPEG 数据 */
    uint8_t *p = camera_tx_fb;
    uint32_t remaining = data_len;
    while (remaining > 0) {
        uint32_t chunk = remaining > 512 ? 512 : remaining;
        if (HAL_UART_Transmit(&huart3, p, chunk, 500) != HAL_OK) {
            camera_tx_busy = 0;
            return CAMERA_ERROR;
        }
        p += chunk;
        remaining -= chunk;
    }

    camera_tx_busy = 0;
    return CAMERA_OK;
}

/**
 * @brief  重启 DCMI 快照捕获 (主循环调用)
 */
void Camera_RestartSnapshot(void)
{
    HAL_DCMI_Stop(&hdcmi);
    HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_SNAPSHOT,
                       (uint32_t)camera_fb, CAMERA_FB_SIZE / 2);
}
