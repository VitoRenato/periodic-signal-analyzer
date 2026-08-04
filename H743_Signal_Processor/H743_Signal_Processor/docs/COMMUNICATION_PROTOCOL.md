# H743 Signal Processor 通信协议

版本：2  
冻结日期：2026-08-01  
字节序：所有多字节整数均为小端

## 1. 协议边界

本系统区分三个不同接口，不允许混用。其中第 1、2 项构成当前发布的
F407↔H743 采集链路；第 3 项是已测试的结果帧编解码契约，物理传输尚未随当前
二板链路启用或板级验证：

1. 采集F407 → H743：8208字节全双工帧中的8192字节裸采样区。
2. H743 → 采集F407：同一帧MISO开头16字节`SET_FS + 分页测量模型`命令槽。
3. H743 → 主控F407：本项目定义的88字节`RESULT`结果帧（编码/解码已测试，
   物理 SPI2 发送为待母板确认的候选接口）。

等效采样参考工程的8220、24和48字节ESPI报文全部不采用。

## 2. 采集数据输入

### 2.1 物理层

| 项目 | 值 |
|---|---|
| 采集F407角色 | SPI主机 |
| H743角色 | SPI从机 |
| 模式 | Mode 0 |
| 数据宽度 | 8 bit |
| 位序 | MSB first |
| SCK | 约5.25 MHz |
| CS/NSS | 软件NSS，无物理CS |
| READY | H743输出，F407输入 |

### 2.2 帧

固定8208字节；前16字节是命令槽占位，随后8192字节无帧头、序号和CRC：

```text
byte 0..15       = F407发送的命令槽占位
byte 16 + 2i     = sample[i]低8位
byte 16 + 2i + 1 = sample[i]高8位
i = 0..4095
```

H743必须在挂载恰好8208字节全双工DMA后才拉高READY。收满后立即拉低READY。
重组出的`uint16_t`必须检查bit12~bit15为0，再取低12位数字码。

该接口无法自行检测错位但长度相同的损坏帧；当前只能依靠READY、固定长度、
DMA错误标志和数据合理性检查。若采集组未来允许升级，应优先增加序号和CRC，
但版本1不得由H743单方面改变。

## 3. SET_FS命令

### 3.1 物理层

| 项目 | 值 |
|---|---|
| 采集F407角色 | SPI1主机，产生读取时钟 |
| H743角色 | SPI1从机，通过MISO返回 |
| 模式 | Mode 0 |
| 数据宽度 | 8 bit |
| 位序 | MSB first |
| SCK | 约5.25 MHz，与采样上传阶段相同 |
| CS/NSS | 软件NSS，无物理CS |
| READY | H743输出，F407输入 |

SPI1在整个运行期间不切换主从。H743预先把16字节命令槽放入8208字节TX
缓冲开头；F407检测到READY后一次性产生8208字节时钟，同时上传采样并从
MISO读取命令槽。H743不需要也不能主动产生SPI1时钟。

### 3.2 分时握手

```text
H743挂载8208字节SPI1 TxRx DMA并拉高READY
→ F407通过MOSI发送16字节占位和8192字节采样
→ F407同时从MISO读取16字节命令槽和8192字节零占位
→ H743收发完成后拉低READY并运行算法
→ H743把最新分页结果编码到下一次TxRx帧，再挂载DMA
```

READY高电平只有一个含义：“H743已经挂载本次8208字节TxRx DMA”。算法计算
期间READY保持低电平。

### 3.3 16字节命令槽与五页测量模型

| 偏移 | 长度 | 字段 | 值/单位 |
|---:|---:|---|---|
| 0 | 2 | magic | `0xA55A` |
| 2 | 1 | type | `0x01` |
| 3 | 1 | flags | 有效/恢复/页号/模型代号，见下表 |
| 4 | 4 | fs_hz | 5000~20000000 Hz |
| 8 | 2 | crc16 | CRC16/CCITT-FALSE |
| 10 | 6 | page_payload | 当前测量页，显式小端 |

CRC参数：poly=`0x1021`，init=`0xFFFF`，refin=false，refout=false，
xorout=`0`，覆盖偏移0~7。后6字节依靠五页语义校验和同代号原子组装；当前
CRC不覆盖后6字节，这是兼容既有SET_FS头部的边界。

`flags`：bit0=本页有效，bit1=削顶模型恢复，bit2~4=页号0~4，
bit5~7=模型代号模8。五页负载为：

| 页 | payload[0..5] |
|---:|---|
| 0 | `fundamental_Hz:u32 + upp_mV:u16` |
| 1 | `urms_mV:u16 + component_count:u8 + status:u8 + quality:u8 + 0:u8` |
| 2~4 | 对应分量：`order:u8 + amplitude_peak_mV:u16 + phase_q15:i16 + valid:u8` |

`phase_q15`映射`[-π,π)`；谱线幅值是BNC输入端正弦峰值，不是RMS。F407只有
收到同一模型代号、顺序完整且语义合法的0~4页后才发布新模型。

F407在8208字节TxRx结束后校验命令，校验通过才应用新采样率。由于输入裸帧
不回传确认或实际Fs，版本2使用双方相同的
TIM1整数分频公式计算实际值：

```text
divider  = floor(168000000 / requested_fs)
actualFs = 168000000 / divider
```

这里的`floor`对应采集手册给出的整数公式
`ARR = 168000000 / requested_fs - 1`。算法处理下一帧时使用`actualFs`，
而不是未经量化的请求值；上板时还要与采集F407实际源码和PE9测量值核对。

## 4. RESULT结果协议

### 4.1 候选物理层（未作为当前二板采集链路启用）

| 项目 | 值 |
|---|---|
| H743角色 | SPI2主机（候选配置） |
| 主控F407角色 | SPI从机（候选配置） |
| 模式 | Mode 0 |
| 数据宽度 | 8 bit |
| 位序 | MSB first |
| 初始SCK | 3.75 MHz |
| CS/NSS | 软件NSS，无物理CS |
| READY | 主控F407输出，H743输入，下拉 |
| 固定长度 | 88字节 |

以下时序是对后续主控 F407 接入的候选约定，不是当前发布树已烧录或实测的
SPI2 通路：主控 F407 先挂载 88 字节 RX DMA，再拉高 READY；H743 检测到
READY 高后启动 TX DMA。主控收满后拉低 READY。H743 等待 READY 的初始超时为
100 ms；超时不自动重复发送，记录错误并保留最近一次算法结果。

候选引脚参考：

| H743引脚 | 功能 |
|---|---|
| PB13 | SPI2_SCK |
| PB15 | SPI2_MOSI |
| PB14 | SPI2_MISO，业务不用，可不接 |
| PD1 | MAIN_READY输入 |

引脚在建立CubeMX工程前仍需与实际母板复核。

### 4.2 88字节布局

| 偏移 | 长度 | 类型 | 字段 | 说明 |
|---:|---:|---|---|---|
| 0 | 2 | `uint16_t` | magic | `0x4853`，字节为`53 48` |
| 2 | 1 | `uint8_t` | version | `1` |
| 3 | 1 | `uint8_t` | message_type | `0x21` |
| 4 | 2 | `uint16_t` | total_length | `88` |
| 6 | 2 | `uint16_t` | header_flags | 版本1发送0 |
| 8 | 4 | `uint32_t` | sequence | H743本地帧序号 |
| 12 | 4 | `uint32_t` | sample_rate_Hz | 本帧实际Fs |
| 16 | 2 | `uint16_t` | status | 见4.3 |
| 18 | 1 | `uint8_t` | component_count | 0~3 |
| 19 | 1 | `uint8_t` | reserved0 | 发送0 |
| 20 | 4 | `uint32_t` | fundamental_mHz | 基频，0.001 Hz/LSB |
| 24 | 4 | `uint32_t` | upp_uV | 峰峰值 |
| 28 | 4 | `uint32_t` | urms_uV | 去ADC零偏后的真有效值 |
| 32 | 4 | `uint32_t` | residual_rms_uV | 拟合残差RMS |
| 36 | 4 | `int32_t` | snr_milli_dB | 0.001 dB/LSB |
| 40 | 4 | `uint32_t` | recommended_fs_Hz | 建议下一采样率 |
| 44 | 12 | slot0 | component[0] | 见4.4 |
| 56 | 12 | slot1 | component[1] | 见4.4 |
| 68 | 12 | slot2 | component[2] | 见4.4 |
| 80 | 4 | `uint32_t` | result_flags | 见4.5 |
| 84 | 4 | `uint32_t` | crc32 | 覆盖偏移0~83 |

不直接发送C结构体。必须使用显式小端编解码函数，避免填充、对齐和编译器
ABI差异。

### 4.3 status

协议值与`SignalStatus`保持一致：

| 值 | 名称 |
|---:|---|
| 0 | VALID |
| 1 | BAD_ARGUMENT |
| 2 | CLIPPED |
| 3 | NO_SIGNAL |
| 4 | MODEL_INVALID |
| 5 | ILL_CONDITIONED |
| 6 | SAMPLE_RATE_UNSAFE |

状态不是VALID时，接收端必须先检查有效标志，不能使用残留数值。

### 4.4 分量槽

每槽12字节：

| 槽内偏移 | 长度 | 类型 | 字段 |
|---:|---:|---|---|
| 0 | 4 | `uint32_t` | frequency_mHz |
| 4 | 4 | `uint32_t` | amplitude_peak_uV |
| 8 | 4 | `int32_t` | phase_mdeg |

相位范围规范化为`[-180000, 180000)`毫度。幅值为正弦峰值，不是RMS或
峰峰值。无效槽的12字节全部发送0。

### 4.5 result_flags

| bit | 含义 |
|---:|---|
| 0 | 基频有效 |
| 1 | Upp和Urms有效 |
| 2 | 检测到高频干扰 |
| 3 | 存在混叠风险 |
| 4 | 使用的是当前单点标定参数 |
| 5 | 建议采样率有效 |
| 6~31 | 保留，发送0 |

### 4.6 CRC32

使用CRC-32/ISO-HDLC：

```text
poly   = 0x04C11DB7
init   = 0xFFFFFFFF
refin  = true
refout = true
xorout = 0xFFFFFFFF
check("123456789") = 0xCBF43926
```

CRC字段本身按小端存放。接收端必须先检查固定长度、magic、version、type、
component_count和保留位，再检查CRC和状态。

## 5. 数值转换

编码时采用饱和和四舍五入：

```text
frequency_mHz   = round(frequency_Hz × 1000)
amplitude_uV    = round(amplitude_peak_V × 1e6)
phase_mdeg      = round(phase_rad × 180000 / π)
upp_uV          = round(upp_V × 1e6)
urms_uV         = round(urms_V × 1e6)
residual_rms_uV = round(residual_rms_V × 1e6)
snr_milli_dB    = round(snr_dB × 1000)
```

NaN、Inf、负幅值、超范围频率或`component_count > 3`必须使编码失败，不能
静默生成看似有效的帧。

## 6. 版本兼容

- 版本1固定88字节。
- 新字段只能通过新版本或当前保留位表达。
- 版本1接收端遇到未知version、type、非零保留字段或错误长度时整帧作废。
- 不允许通过改变C结构体布局隐式升级协议。
