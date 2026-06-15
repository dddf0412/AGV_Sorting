# MFRC-522 NFC 身份认证模块技术规格书

**适用平台**: STM32H753ZI 主控
**版本**: V2.0
**日期**: 2026-05-15

---

## 1. 模块概述

本模块基于 **MFRC-522** RFID 读卡器芯片，为嵌赛ST平台提供 NFC 身份认证功能。采用"状态机 + 授权白名单"架构，支持三种工作模式。

### 模块设计目标

| 目标 | 说明 |
|:---|:---|
| 自包含 | 所有逻辑封装在 nfc.h / nfc.c，main.c 仅需调用两个函数 |
| 非阻塞 | 基于 HAL_GetTick() 间隔调度，不占用主循环 CPU |
| 易调试 | 通过 USART3 串口命令实时切换模式 |
| 防误操作 | ADD 模式一次只添加一张卡，成功后自动切回 VERIFY |

---

## 2. 硬件连接

### 2.1 引脚分配 (STM32H753ZI)

| 信号名 | 引脚 | 类型 | 说明 |
|:---|:---|:---|:---|
| SPI1_SCK | PA5 | SPI 时钟输出 | MFRC-522 SCK |
| SPI1_MISO | PA6 | SPI 数据输入 | MFRC-522 MISO |
| SPI1_MOSI | PB5 | SPI 数据输出 | MFRC-522 MOSI (V2 修订: 原 PA7→PB5) |
| SPI1_CS | PC0 | GPIO 片选输出 | MFRC-522 CS (V2 修订: 原 PA4→PC0) |
| USART3_TX | PB10 | UART 发送 | 调试/命令输入 115200 |
| USART3_RX | PB11 | UART 接收 | 调试/命令输入 115200 |
| 3.3V | — | 电源 | MFRC-522 逻辑供电 |
| GND | — | 地 | 与 H753 共地 |

### 2.2 硬件注意事项

- MFRC-522 工作电压为 **3.3V**，严禁直接接入 5V
- SPI 通信距离建议控制在 **20cm** 以内
- 天线区域下方避免大面积铺铜，防止涡流损耗导致读卡距离缩短
- 模块上电后自动执行软复位，约 1ms 后进入就绪状态

---

## 3. 软件架构

### 3.1 分层结构

| 层级 | 组成 | 职责 |
|:---|:---|:---|
| 硬件抽象层 | HAL_SPI / HAL_UART / HAL_GPIO | CubeMX 生成的外设驱动 |
| 驱动层 | NFC_ReadReg / NFC_WriteReg / NFC_ToCard | MFRC-522 寄存器读写、SPI 时序、CRC 计算 |
| 协议层 | NFC_Request / NFC_Anticoll / NFC_SelectTag | ISO14443A 寻卡、防冲撞、选卡 |
| 业务层 | NFC_Task / NFC_ProcessCmd | 状态机调度、串口命令解析、模式分支 |
| 应用层 | main.c | 仅调用 NFC_InitSystem() 和 NFC_Task() |

### 3.2 状态机

```
IDLE (默认上电状态)  ←→  VERIFY
VERIFY  ←→  ADD (串口命令切换，ADD 成功后自动回到 VERIFY)
任何模式均可通过 NFC:IDLE 进入空闲
```

---

## 4. 工作模式

### 4.1 IDLE (空闲模式)

- 模块不进行任何寻卡操作，MFRC-522 天线保持开启但不轮询
- 适用场景: 系统待机省电、NFC 功能临时关闭、调试其他外设
- 资源占用: 仅 UART 命令解析，SPI 总线完全释放

### 4.2 ADD_CARD (添加模式)

- 每 500ms 执行一次寻卡，检测到新卡后读取 4 字节 UID，加入白名单

关键约束:

| 约束 | 说明 |
|:---|:---|
| 一次一张 | 成功后自动切回 VERIFY，防止连续误刷 |
| 防重复 | UID 已存在则拒绝，提示"Card exists" |
| 容量上限 | 最大 10 张卡 (NFC_MAX_CARDS) |
| 冷却期 | 添加成功后 1.5s 冷却 |

### 4.3 VERIFY_CARD (识别模式)

- 每 500ms 执行一次寻卡，读取 UID 与白名单比对

输出结果:

| 结果 | 说明 |
|:---|:---|
| **GRANTED** | UID 在白名单中，触发外部解锁 |
| **DENIED** | UID 不在白名单中，保持锁定 |
| 冷却期 | 无论通过或拒绝，均进入 1.5s 冷却 |

---

## 5. API 接口

`main.c` 仅需调用两个函数：

| 函数原型 | 调用时机 | 返回值 | 说明 |
|:---|:---|:---|:---|
| `uint8_t NFC_InitSystem(void)` | main() 初始化 | MI_OK / MI_ERR | 初始化 MFRC-522，启动 UART 中断接收，默认进入 VERIFY |
| `void NFC_Task(void)` | while(1) 主循环 | 无 | 处理串口命令 + NFC 状态机 |

### 最小集成示例

```c
#include "nfc.h"

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_SPI1_Init();
    MX_USART3_UART_Init();

    if (NFC_InitSystem() != MI_OK) {
        printf("[MAIN] NFC init failed!");
        Error_Handler();
    }

    while (1) {
        NFC_Task();
        HAL_Delay(10);
    }
}
```

---

## 6. 串口命令协议

- **通信参数**: USART3, 115200bps, 8N1, 无硬件流控
- **命令格式**: 纯 ASCII 文本，以回车符 `\r` 或 `\n` 结束

### 命令列表

| 命令文本 | 功能 | 响应示例 |
|:---|:---|:---|
| `NFC:IDLE` | 进入空闲模式 | `[NFC] Mode switched to: IDLE` |
| `NFC:ADD` | 进入添加模式 | `[NFC] Mode switched to: ADD_CARD` |
| `NFC:VERIFY` | 进入识别模式 | `[NFC] Mode switched to: VERIFY_CARD` |
| `NFC:LIST` | 打印授权列表 | `[NFC] Card list (2/10):\n  [1] UID: A1B2C3D4` |
| `NFC:CLEAR` | 清空白名单 | `[NFC] All cards cleared.` |
| `HELP` | 显示帮助 | `----- UART Command List -----` |

### 交互示例

```
>>> NFC:ADD
<<< [NFC] Mode switched to: ADD_CARD
<<< [UART] Enter ADD mode. Swipe ONE card to add...
(用户刷卡)
<<< [NFC][ADD] >>> Card ADDED! UID=A1B2C3D4 (Total:1)
<<< [NFC][ADD] Auto-switching to VERIFY mode.

>>> NFC:LIST
<<< [NFC] Card list (1/10):
<<<   [1] UID: A1B2C3D4
(刷卡)
<<< [NFC][VERIFY] >>> Access GRANTED! UID=A1B2C3D4
```

---

## 7. 典型操作流程

### 7.1 首次上电

1. 上电 → NFC_InitSystem() → 默认 IDLE 模式
2. 串口助手下 `HELP` 查看命令
3. `NFC:LIST` 确认白名单为空
4. `NFC:ADD` → 刷卡 → 观察"Card ADDED"提示
5. `NFC:LIST` 确认 UID 已录入
6. 刷卡测试 → "Access GRANTED"

### 7.2 日常验证

1. 确保 VERIFY 模式 (`NFC:VERIFY`)
2. 用户刷卡
3. 已授权 → GRANTED → 触发解锁/播报
4. 未授权 → DENIED → 保持锁定/报警

### 7.3 管理

- **全部清空**: `NFC:CLEAR` → 白名单归零
- **单张删除**: 预留 NFC_RemoveCard 接口（通过扩展命令实现）

---

## 8. 日志输出规范

所有日志通过 USART3 输出 (115200-8N1)。

| 前缀 | 含义 | 示例 |
|:---|:---|:---|
| `[NFC]` | 模块级通用日志 | `[NFC] MFRC-522 OK! Version: 0x92` |
| `[NFC][IDLE]` | 处于空闲模式 | `[NFC][IDLE] Anticoll failed` |
| `[NFC][ADD]` | 处于添加模式 | `[NFC][ADD] >>> Card ADDED! UID=...` |
| `[NFC][VERIFY]` | 处于识别模式 | `[NFC][VERIFY] >>> Access GRANTED!` |
| `[UART]` | 串口命令解析日志 | `[UART] Enter ADD mode...` |

---

## 9. 文件结构与职责

| 文件 | 职责 | 修改约束 |
|:---|:---|:---|
| nfc.h | 宏定义、寄存器地址、函数原型、模式枚举 | 可随硬件变更调整 |
| nfc.c | SPI驱动、状态机、卡片库、UART命令解析 | 业务逻辑核心，修改需谨慎 |
| usart.c | huart3 句柄、__io_putchar | 禁止添加命令解析 |
| spi.c | hspi1 句柄、HAL SPI 初始化 | CubeMX 生成，通常不手动修改 |
| main.c | 调用 NFC_InitSystem / NFC_Task | 禁止直接操作 MFRC-522 寄存器 |

---

## 10. 与外部模块联动

NFC 模块内部预留了与项目其他子系统的联动点。

### 10.1 验证通过 (GRANTED)

| 动作 | 说明 |
|:---|:---|
| FDCAN | 发送解锁帧 (0x201/0x202/0x203) 给 G474 |
| Audio | WM8960 播报"验证通过，请操作" |
| Screen | 串口屏切换绿色"已授权"状态 |
| 标志位 | 设置全局 g_nfc_auth_ok 供其他任务查询 |

### 10.2 验证失败 (DENIED)

| 动作 | 说明 |
|:---|:---|
| FDCAN | 不发送解锁帧，G474 保持锁定 |
| Audio | 播报"未授权卡片，请联系管理员" |
| Screen | 串口屏显示红色 "Access Denied" |

### 10.3 集成建议

推荐"回调函数指针"方式，而非在 nfc.c 中直接调用 CAN/音频/屏幕函数：

```c
/* nfc.h */
typedef void (*NFC_Callback_t)(uint8_t granted, uint8_t* uid);
extern NFC_Callback_t g_nfc_callback;

/* main.c 注册回调 */
g_nfc_callback = My_App_Callback;

/* nfc.c 内部调用 */
if (g_nfc_callback) {
    g_nfc_callback(1, uid);  /* GRANTED */
}
```

---

## 11. 故障排查

| 现象 | 可能原因 | 排查方法 |
|:---|:---|:---|
| 上电后无 [NFC] 日志 | USART3 未初始化 | 检查 MX_USART3_UART_Init 和 __io_putchar |
| [NFC] MFRC-522 FAILED! | SPI 接线错误、模块未供电 | 示波器抓 SCK/CS 波形 |
| 版本号读取为 0x00/0xFF | MOSI/MISO 接反 | 检查 PB5(MOSI) 和 PA6(MISO) |
| NFC:ADD 无响应 | UART 中断未启动、未加回车 | 确认 NFC_InitSystem 中调用了 UART_RxStart |
| 刷卡无反应 | 天线未开启、距离过远 | 检查 TX_CONTROL=0x83，换卡测试 |
| 添加成功但 LIST 为空 | g_card_count 未自增 | 检查 NFC_AddCard 返回值 |
| 同一张卡连续触发 | 冷却期未生效 | 确认 SysTick 正常，检查 cooldown_tick |

### 调试技巧

- 串口助手务必勾选"发送新行"或手动加 `\r`
- 在 NFC_Init() 后增加寄存器轮询打印
- 若怀疑 SPI 时序，临时降低 SPI 波特率测试
- ADD 模式测试时准备两张不同 UID 的卡片
