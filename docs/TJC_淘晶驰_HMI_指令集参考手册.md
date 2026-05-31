# 淘晶驰 TJC HMI 串口屏指令集参考手册

适用: T1 / K0 / X3 / X5 系列, 上位机 USART HMI  
协议核心: ASCII 指令 + `0xFF 0xFF 0xFF` 结束符

---

## 一、基本规则

| 规则 | 说明 |
|------|------|
| 结束符 | 每条指令必须以 **3个 0xFF** 结尾 (HEX 数据, 非字符串) |
| 指令名 | 全部使用 **小写字母** |
| 数据格式 | 指令名和参数使用 **ASCII 字符串** |
| 波特率 | 出厂默认 **9600**, 支持 2400 ~ 921600 |

---

## 二、对象及系统操作指令汇总

| 序号 | 指令 | 功能 | 格式 |
|------|------|------|------|
| 1 | `page` | 刷新页面 | `page pageid` |
| 2 | `ref` | 重绘控件 | `ref obj` |
| 3 | `click` | 激活控件按下/弹起 | `click obj,event` (0=弹起, 1=按下) |
| 4 | `get` | 带格式获取变量值 | `get att` |
| 5 | `prints` | 串口打印变量/常量 | `prints att,lenth` |
| 6 | `printh` | 串口打印 Hex | `printh hex` |
| 7 | `vis` | 隐藏/显示控件 | `vis obj,state` (0=隐藏, 1=显示) |
| 8 | `tsw` | 控件触摸使能 | `tsw obj,state` |
| 9 | `randset` | 随机数范围设置 | `randset minval,maxval` |
| 10 | `add` | 往曲线控件添加数据 | `add objid,ch,val` |
| 11 | `cle` | 清除曲线数据 | `cle objid,ch` |
| 12 | `addt` | 曲线数据透传 | `addt objid,ch,qyt` |
| 13 | `doevents` | 让出 CPU 给屏幕刷新 | 无参数 |
| 14 | `sendme` | 发送当前页面 ID | 无参数 |
| 15 | `covx` | 变量类型转换 | `covx att1,att2,lenth,format` |
| 16 | `strlen` | 字符串长度测试 | `strlen att0,att1` |
| 17 | `btlen` | 字符串字节长度测试 | `btlen att0,att1` |
| 18 | `substr` | 字符串截取 | `substr att0,att1,star,lenth` |
| 19 | `touch_j` | 触摸校准 | 无参数 |
| 20 | `ref_stop` | 暂停屏幕刷新 | 无参数 |
| 21 | `ref_star` | 恢复屏幕刷新 | 无参数 |
| 22 | `com_stop` | 暂停串口指令执行 | 无参数 |
| 23 | `com_star` | 恢复串口指令执行 | 无参数 |
| 24 | `code_c` | 清空串口缓冲区 | 无参数 |
| 25 | `rest` | 复位 | 无参数 |
| 26 | `wepo` | 写变量到用户存储区 | `wepo att,add` |
| 27 | `repo` | 从用户存储区读数据 | `repo att,add` |
| 28 | `wept` | 透传数据写入存储区 | `wept add,lenth` |
| 29 | `rept` | 透传数据读存储区 | `rept add,lenth` |
| 30 | `cfgpio` | 扩展 IO 模式配置 | `cfgpio id,state,obj` |

### CRC 校验指令

| 序号 | 指令 | 功能 |
|------|------|------|
| 31 | `crcrest` | 复位 CRC 初始值: `crcrest crctype,initval` |
| 32 | `crcputs` | CRC 校验变量/常量: `crcputs att,length` |
| 33 | `crcputh` | CRC 校验 Hex: `crcputh Hex` |
| 34 | `crcputu` | CRC 校验缓冲区数据: `crcputu star,length` |

### 高级指令 (仅 X3/X5 系列)

| 序号 | 指令 | 功能 |
|------|------|------|
| 35 | `setlayer` | 运行中改变图层顺序 |
| 36 | `move` | 控件移动动画 |
| 37 | `play` | 音频播放 |
| 38 | `twfile` | 串口透传文件 |
| 39 | `delfile` | 删除文件 |
| 40 | `refile` | 重命名文件 |
| 41 | `findfile` | 查找文件 |
| 42 | `rdfile` | 透传读文件 |
| 43 | `newfile` | 创建文件 |
| 44 | `newdir` | 创建文件夹 |
| 45 | `deldir` | 删除文件夹 |
| 46 | `redir` | 重命名文件夹 |
| 47 | `finddir` | 查找文件夹 |

---

## 三、核心指令详解

### `page` — 刷新页面

```
page pageid
```
- `pageid`: 页面 ID 或页面名称
- 设备上电自动刷新第 0 页
- 也可用系统变量 `dp=0` 跳转
- 是跳转指令, 其后的代码会忽略

**示例:**
```
page 0
page main
```

### `click` — 激活控件事件

```
click obj,event
```
- `obj`: 控件 ID 或名称
- `event`: 0 = 弹起, 1 = 按下

**示例:**
```
click b0,1
click 2,0
```

### `get` — 获取变量/常量值

```
get att
```
- 返回格式: 字符串 `0x70 + ASCII + 0xFF 0xFF 0xFF`
- 返回格式: 数值 `0x71 + 4字节(int LE) + 0xFF 0xFF 0xFF`

**示例:**
```
get t0.txt
get n0.val
get 123
```

### `prints` — 串口打印(无前缀无结束符)

```
prints att,lenth
```
- `lenth`: 发送长度 (0=自动)

**示例:**
```
prints t0.txt,0
prints n0.val,0
```

### `printh` — 串口打印 Hex

```
printh 55 01 02 00 ff ff ff
```

### `vis` — 隐藏/显示控件

```
vis obj,state
```
- `state`: 0=隐藏, 1=显示

### `tsw` — 控件触摸使能

```
tsw obj,state
```
- `state`: 0=禁止触摸, 1=允许触摸

---

## 四、GUI 绘图指令

主要用于上位软件无法实现的特殊显示需求, 使用时需注意 `ref` 重绘遮挡。

| 序号 | 指令 | 功能 | 格式 |
|------|------|------|------|
| 1 | `cls` | 清屏 | `cls color` |
| 2 | `pic` | 刷图 | `pic x,y,picid` |
| 3 | `picq` | 切图 | `picq x,y,w,h,picid` |
| 4 | `xpic` | 高级切图 | `xpic x,y,w,h,x0,y0,picid` |
| 5 | `xstr` | 写字 | `xstr x,y,w,h,fontid,pc,bc,xc,yc,sta,str` |
| 6 | `fill` | 区域填充 | `fill x,y,w,h,color` |
| 7 | `line` | 画线 | `line x,y,x2,y2,color` |
| 8 | `draw` | 画矩形 | `draw x,y,x2,y2,color` |
| 9 | `cir` | 空心圆 | `cir x,y,r,color` |
| 10 | `cirs` | 实心圆 | `cirs x,y,r,color` |

**示例:**
```
cls RED
fill 10,10,200,100,BLUE
xstr 10,10,200,100,0,65535,BLACK,0,0,0,"ABC"
```

---

## 五、书写语法

### 赋值操作

**字符串属性:**
```
t0.txt="Hello World"
t0.txt=t1.txt            (拷贝控件属性)
```
错误写法: `t0.txt=123` (字符串属性不能赋数值常量, 需加引号)

**数值属性:**
```
n0.val=123
n0.val=h0.val            (拷贝控件属性)
baud=115200              (改波特率)
```
错误写法: `n0.val="123"` (数值属性不能赋字符串)

> 仅 `txt` 属性为字符串类型, 其他属性均为数值类型。

### 运算操作

支持: `+ - * / % & | ^ << >>`, **一律从左到右执行, 不支持优先级和括号。**

```
n0.val=n0.val+n1.val+2
n0.val++
n0.val+=2
n0.val=h0.val*10
```

### 跨页面操作

```
page0.n0.val=100         (设置第0页n0的值为100)
page1.t0.txt="ABC"       (设置第1页t0的文本)
```

---

## 六、系统变量

### 页面与显示

| 变量 | 含义 | 说明 |
|------|------|------|
| `dp` | 当前页面ID | `dp=1` 等同 `page 1` |
| `dim` | 当前背光亮度 (0-100) | `dim=50` |
| `dims` | 上电默认背光亮度 | 掉电保存 |

### 通信

| 变量 | 含义 | 说明 |
|------|------|------|
| `baud` | 当前波特率 | 本次修改, 掉电丢失 |
| `bauds` | 上电默认波特率 | 掉电保存 |
| `bkcmd` | 指令结果返回等级 | 0=不返回, 1=仅成功, 2=仅失败(默认), 3=全部 |
| `addr` | 设备地址 | 256-2815, 0=无地址 |
| `recmod` | 解析模式 | 0=被动(默认), 1=主动(自定义协议) |

### 睡眠

| 变量 | 含义 |
|------|------|
| `sleep` | `sleep=1` 进入睡眠, `sleep=0` 退出 |
| `ussp` | 无串口数据自动睡眠时间 (秒) |
| `thsp` | 无触摸自动睡眠时间 (秒) |
| `thup` | 触摸唤醒开关 (0/1) |
| `usup` | 串口数据唤醒开关 (0/1) |
| `wup` | 唤醒后刷新页面设置 |

### 触摸

| 变量 | 含义 |
|------|------|
| `sendxy` | 实时发送触摸坐标开关 |
| `tch0-tch3` | 实时/上次触摸坐标 XY (只读) |
| `thc` | 触摸绘图画笔色 |
| `thdra` | 触摸绘图功能开关 |

### 音频 (带音频硬件)

| 变量 | 含义 |
|------|------|
| `volume` | 系统音量 (0-100, 掉电保存) |
| `audio0/audio1` | 通道控制: 0=停止, 1=播放, 2=暂停 |
| `eql/eqm/eqh` | 低/中/高音衰减 (0-15) |
| `eq0-eq9` | 独立频点衰减 (31Hz-16KHz) |

### 扩展 IO (带扩展 IO 硬件)

| 变量 | 含义 |
|------|------|
| `pio0-pio7` | 扩展 IO 电平 (需先 cfgpio 配置) |
| `pwm4-pwm7` | 扩展 IO PWM 占空比 (0-100) |
| `pwmf` | PWM 频率 (1-65535 Hz, 默认 1000) |

### 其他

| 变量 | 含义 |
|------|------|
| `delay` | 设备停顿: `delay=100` (100ms) |
| `rand` | 随机数 (需先用 randset 设范围) |
| `rtc0-rtc6` | RTC 时钟 (年/月/日/时/分/秒/星期) |
| `spax/spay` | 字符显示横向/纵向间距 |
| `crcval` | CRC 校验结果 (只读) |

---

## 七、数据返回格式

设备返回数据结束符为 **`0xFF 0xFF 0xFF`** (3字节)。

### 指令执行结果 (受 bkcmd 影响)

| 首字节 | 含义 |
|--------|------|
| `0x00` | 无效指令 |
| `0x01` | 指令执行成功 |
| `0x02` | 控件 ID 无效 |
| `0x03` | 页面 ID 无效 |
| `0x04` | 图片 ID 无效 |
| `0x05` | 字库 ID 无效 |
| `0x09` | CRC 校验失败 |
| `0x1A` | 变量名称无效 |
| `0x1B` | 变量运算无效 |
| `0x1C` | 赋值操作失败 |
| `0x1E` | 参数数量无效 |
| `0x24` | 串口缓冲区溢出 |

### 主动返回 (不受 bkcmd 影响)

| 帧头 | 含义 | 格式 |
|------|------|------|
| `0x65` | **触摸热区事件** | `0x65 + PageID + ButtonID + Event + 结束符` |
| `0x66` | 当前页面 ID | `0x66 + PageID + 结束符` |
| `0x67` | 触摸坐标 | `0x67 + X高 + X低 + Y高 + Y低 + 事件 + 结束符` |
| `0x68` | 睡眠触摸坐标 | 同 0x67 |
| `0x70` | 字符串数据返回 | `0x70 + ASCII + 结束符` |
| `0x71` | 数值数据返回 | `0x71 + int32(LE) + 结束符` |
| `0x88` | **系统启动成功** | `0x88 + 结束符` |
| `0x89` | SD 卡升级 | `0x89 + 结束符` |
| `0xFD` | 透传完成 | `0xFD + 结束符` |
| `0xFE` | 透传就绪 | `0xFE + 结束符` |

### 触摸事件 `0x65` 详解

```
0x65 + PageID + ButtonID + Event + 0xFF 0xFF 0xFF
```
- Event: `0x01` = 按下, `0x00` = 弹起
- 前提: 控件在 TJC 上位机中勾选了"发送键值"选框

**示例:** `0x65 0x00 0x02 0x01 0xFF 0xFF 0xFF`
→ 页面 0、按钮 2、按下

---

## 八、颜色代号表

所有代号书写为 **大写**。

| 代号 | 10进制值 | 颜色 |
|------|----------|------|
| `RED` | 63488 | 红色 |
| `BLUE` | 31 | 蓝色 |
| `GRAY` | 33840 | 灰色 |
| `BLACK` | 0 | 黑色 |
| `WHITE` | 65535 | 白色 |
| `GREEN` | 2016 | 绿色 |
| `BROWN` | 48192 | 橙色 |
| `YELLOW` | 65504 | 黄色 |

---

## 九、CRC 校验指令

### 格式区别
- 普通指令结束符: `0xFF 0xFF 0xFF`
- CRC 指令结束符: `0xFE 0xFE 0xFE`
- CRC16 校验码(2字节 LE) + 0x01 插入在指令与结束符之间

### 示例
不带 CRC:
```
n0.val=100\xFF\xFF\xFF
```
带 CRC (CRC16=0x1234):
```
n0.val=100\x34\x12\x01\xFE\xFE\xFE
```

---

## 十、主动解析模式 (自定义协议)

设置 `recmod=1` 后, 屏幕不再解析标准串口指令, 所有数据存入缓冲区等待读取。

**关键变量/指令:**
- `usize` — 缓冲区已缓存数据大小 (只读)
- `u[index]` — 缓冲区指定位置单字节数据
- `ucopy att,srcstar,lenth,decstar` — 缓冲区拷贝到变量
- `udelete size` — 删除已处理的缓冲字节

**退出主动解析:**

发送 24 字节密码 + 结束符:
```
DRAKJHSUYDGBNCJHGJKSHBDN\xFF\xFF\xFF
```

---

## 十一、常用快速参考

### 页面切换
```
page 0
```

### 文本更新
```
t0.txt="你好"\xFF\xFF\xFF
```

### 数值更新
```
n0.val=255\xFF\xFF\xFF
```

### 隐藏/显示
```
vis b0,0\xFF\xFF\xFF     (隐藏)
vis b0,1\xFF\xFF\xFF     (显示)
```

### 改波特率 (永久)
```
bauds=115200\xFF\xFF\xFF
```

### 复位
```
rest\xFF\xFF\xFF
```

### 坐标查询
```
sendxy=1\xFF\xFF\xFF     (开启坐标上报)
```
