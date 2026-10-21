# F407 AD9226 采集端技术手册历史快照

> 发布状态：本文件**随仓库发布**，用于追溯早期 F407/H723 与分阶段 SPI1/SPI2
> 设计；它不是当前产品说明或接线依据。当前发布架构是 F407 + H743，采集接口是
> SPI1 8208 B 全双工帧（16 B 命令槽 + 8192 B 样本），以
> `../../COMMUNICATION_PROTOCOL.md` 为准。
>
> 本快照中的时钟、示波器、串口、烧录和“通过”数字均为日期不明的历史单点观察。
> 对应本地日志、仪器型号/校准记录与原始固件未随仓库发布，且未对当前二进制重新验证；
> 因而不得将其作为当前物理验收或性能结论。

> 主控 STM32F407ZGT6,采 AD9226 12-bit 并行 ADC,4096 点/帧。
> **采样率可被 H723 动态改**(通过 SPI2 从机接收 SET_FS 命令)。
> 工程：`../../../../AD9226_driver3/AD9226_driver`（相对于本文档目录）

---

## 1. 系统概述

F407 用 **TIM1 PWM** 给 AD9226 提供采样时钟(**默认 2.048 MHz,可被 H723 通过 SPI2 命令动态改到 5 kHz ~ 20 MHz**),AD9226 每个时钟周期把 12-bit 数据并行输出到 GPIOF[0..11]。F407 用 **DMA2_Stream5**(TIM1_UP 触发)每周期读一次 `GPIOF->IDR`,攒满 4096 点后通过 **SPI1** 把整帧发给 H723。

反向链路:**H723 → F407 SPI2**(PB13/PB15),F407 当 SPI2 从机,接收 H723 算完频率后发来的 `SET_FS` 命令,改 TIM1 ARR 调整采样率,下一帧采集自动用新 Fs。

```
         ┌─────────┐  PE9 (PWM 2.048MHz, 可变)   ┌────────┐
         │  F407   │─────────────────────────────▶│ AD9226 │
         │         │  PF0~PF11 (12 bit)           │        │
         │  TIM1   │◀────────────────────────────│  D0~11 │
         │   ↓trig │                              └────────┘
         │ DMA2_S5 │  读 GPIOF->IDR ×4096
         │   ↓     │
         │ s_adc_buf[4096] (uint16_t)
         │   ↓ SPI1 (主机)
         │ PA5/PA7 │─────────────────────▶  H723
         └─────────┘   5.25 MHz, 8192 B/帧
              ▲
              │ SPI2 (从机) PB13/PB15
              │ 收 SET_FS 命令
              │ ←─────────────────────  H723
```

**一帧时序**(Fs=2.048 MHz 时):
- 采集:4096 / 2.048 MHz = **2 ms**
- SPI 发送:8192 B × 8 bit / 5.25 Mbit/s = **12.5 ms**
- 帧间隔 ≈ 15 ms(67 fps)

---

## 2. 引脚定义

### 2.1 全部引脚表

| Pin | 信号 | 方向 | 用途 | 电平 |
|-----|------|------|------|------|
| PH0/PH1 | HSE | - | 8 MHz 晶振 | - |
| **PE9** | TIM1_CH1 | **输出** | PWM -> AD9226 CLKIN(采样时钟,可变) | 3.3V CMOS, 默认 2.048 MHz, 50% |
| **PF0~PF11** | GPIO_Input | **输入** | AD9226 D0~D11 并行数据 | 3.3V CMOS |
| PC12 | GPIO_Input | 输入 | AD9226 OTR(预留) | 3.3V |
| **PA5** | SPI1_SCK | **输出** | SPI 时钟 -> H723(发采集数据) | 3.3V, 5.25 MHz |
| PA6 | SPI1_MISO | 输入 | (未用,F407 不读) | - |
| **PA7** | SPI1_MOSI | **输出** | SPI 数据 -> H723(发采集数据) | 3.3V CMOS |
| **PA4** | GPIO_Input | **输入** | **H7_READY 握手**(内部上拉,等 H7 拉高才开始发 SPI1) | 3.3V CMOS,内部上拉 |
| **PB13** | SPI2_SCK | **输入** | SPI 时钟 <- H723(收 SET_FS 命令) | 3.3V |
| PB14 | SPI2_MISO | 输出 | (未用,从机模式下备用) | - |
| **PB15** | SPI2_MOSI | **输入** | SPI 数据 <- H723(收 SET_FS 命令) | 3.3V CMOS |
| PB12 | SPI2_NSS | 输入 | (未接,软件 NSS 模式从机常开) | - |
| **PA9** | USART1_TX | **输出** | printf 调试串口 | 3.3V, 115200 8N1 |
| PA10 | USART1_RX | 输入 | (未用) | - |
| PA13/PA14 | SWDIO/SWCLK | 双向 | SWD 调试 | - |

### 2.2 按功能分组

**输出到外设**:
- `PE9` -> AD9226 CLKIN(采样时钟,**可被 H723 改**)
- `PA5` -> H723 SPI1 SCK(发采集数据)
- `PA7` -> H723 SPI1 MOSI(发采集数据)
- `PA9` -> USB-TTL 调试串口

**输入从外设**:
- `PF0~PF11` ← AD9226 D0~D11
- `PA4` ← **H7 READY**(H7 拉高后才发 SPI1,**§3.1 握手**)
- `PB13` ← H723 SPI2 SCK(收命令)
- `PB15` ← H723 SPI2 MOSI(收命令)

**调试**:
- `PA13/PA14` ← ST-Link SWD(烧录 + OpenOCD 读寄存器)
- `PA9` ← tio 串口监控(`/dev/ttyUSB0`)

---

## 3. 信号规格

### 3.1 TIM1 PWM(PE9)

| 参数 | 值 |
|------|------|
| 频率 | **2.048780 MHz**(168 MHz / 82,默认) |
| **可变范围** | **5 000 ~ 20 000 000 Hz**(由 H723 SET_FS 命令动态改) |
| 占空比 | 50%(Pulse = (ARR+1)/2 - 1,跟随 ARR 同步) |
| 电平 | 3.3V CMOS,实测 3.64 Vpp(×10 探头) |
| 计数模式 | Up |
| 自动重装载预装载 | **Enable**(改 ARR 在下个 update event 生效,避免短脉冲) |
| ARR 公式 | `ARR = 168_000_000 / Fs - 1`(PSC=0) |

### 3.2 SPI1(PA5 SCK / PA7 MOSI)

| 参数 | 值 |
|------|------|
| 模式 | Master |
| 时钟 | **5.25 MHz**(APB2 84 MHz / 16) |
| CPOL/CPHA | 0/0(Mode 0) |
| 数据宽度 | **8 bit** |
| 位序 | MSB first |
| NSS | 软件(`SPI_NSS_SOFT`) |
| CRC | 关 |
| 帧大小 | 4096 × 2 = 8192 字节/帧 |

### 3.3 USART1(PA9)

| 参数 | 值 |
|------|------|
| 波特率 | 115200 |
| 格式 | 8N1 |
| 用途 | printf 调试输出 |
| 重定向函数 | `__io_putchar()`(走 syscalls.c 的 `_write` weak 链) |

### 3.4 DMA2_Stream5

| 参数 | 值 |
|------|------|
| Stream | DMA2_Stream5 |
| Channel | 6(TIM1_UP 专用) |
| 方向 | Peripheral -> Memory |
| 外设 | `GPIOF->IDR`(32-bit 寄存器,取低 16 位) |
| 内存 | `s_adc_buf[4096]` (uint16_t) |
| 外设数据宽度 | HalfWord(16-bit) |
| 内存数据宽度 | HalfWord(16-bit) |
| 外设地址自增 | Disable |
| 内存地址自增 | Enable |
| 模式 | Normal(单次,完成后硬件停 EN) |
| 优先级 | High |
| FIFO | Disable |
| 完成中断 | Enable(`DMA2_Stream5_IRQn`) |
| NVIC 优先级 | 0, 0(最高) |

### 3.5 SPI2 从机(收 H723 命令)

| 参数 | 值 |
|------|------|
| 模式 | **Slave** |
| 引脚 | **PB13**(SCK) / **PB15**(MOSI) / PB14(MISO,未用) / PB12(NSS,未接) |
| NSS | **软件 NSS**(`SSM=1, SSI=1`,从机常开,H723 不用控制 CS) |
| 时钟(SCK) | 由 H723 决定,Mode 0, ≤ 10 MHz |
| CPOL/CPHA | 0/0(Mode 0) |
| 数据宽度 | 8 bit |
| 位序 | MSB first |
| CRC | 关 |
| DMA | **DMA1_Stream3,Channel 0 = SPI2_RX**,Byte 宽度,Normal |
| 缓冲 | `s_rx_buf[10]`(固定 10 字节命令帧) |
| 完成回调 | `HAL_SPI_RxCpltCallback`(HAL 默认 SPI 路径) |
| NVIC 优先级 | 1, 0(比采集 DMA 低,不抢占) |

### 3.6 SPI_A_READY 握手(对应 G题文档 §4.1)🆕

> **单向握手 + 固定长度,无片选**。H7 拉高 READY = "可以发了",F407 不回 ack,直接发 4096 字节。

| 参数 | 值 |
|------|------|
| 信号 | **PA4 = H7_READY**(F407 端输入) |
| H7 端 | 推挽输出,空闲拉低,准备好拉高,接收完拉低 |
| 内部上拉 | **Enable**(~40 kΩ) |
| 默认电平 | **1**(H7 没接时,F407 读 1,直接发,兼容"无脑发") |
| F407 等到 | PA4 == 1 |
| 超时 | **100 ms**(等不到就丢本帧 + 错误计数 +1) |
| 片选 CS | **无**(软件 NSS,固定长度 4096 字节,H7 收满即停) |
| 对应 §4.1 时序 | 简化为 4 步(去掉 §4.1 第 5/7 步的 CS、第 10 步) |

**4 步握手时序**(对应 G题文档 §4.1 简化版):
```
1. H7 准备好后,把 READY 拉高                        ← "我准备好了"
2. F407 轮询 PA4 == 1,直接 HAL_SPI_Transmit(8192 B)  ← 长度固定,发完即停
3. H7 收满 4096 字节(DMA 完成中断),把 READY 拉低      ← "我在处理,别发"
4. H7 处理完数据,再次把 READY 拉高                    ← 回到 step 1
```

**单边握手(无 ack/req 回包)**:
- F407 不发任何 ack,不发 req;只**被动等待** PA4=1 然后发
- H7 只发 READY 单向信号;F407 通过 4096 字节发完的"自然完成"隐式通知
- 为什么够:4096 字节传输时间固定 ~13ms,H7 知道何时收完,不需要 ack 确认

**为什么不选 PA10**:
- 板上 PA10 = USART1_RX,被 USB-TTL (CH340) 的 TX 接到
- CH340 TX 输出低,PA10 实际被外部拉低到 0
- F407 内部上拉 ~40kΩ 拉不赢,即使配 input + pullup 也读 0
- **改用 PA4**(原 SPI NSS 候选,板上无外设,内部上拉稳 = 1)

**为什么 H7 没接时 F407 还能发**:
- 内部上拉保证 PA4 默认 = 1
- F407 轮询 PA4 立即读到 1,直接发
- 等效"无脑发"行为,L5 自环和 L4 冒烟不被破坏
- H7 接好之后,H7 拉低/拉高 READY → 握手自然生效

---

## 4. 数据格式

### 4.1 AD9226 原始数据

- AD9226 输出 12-bit 并行数据 D0~D11
- 接 F407 GPIOF[0..11],**位序一一对应**(D0->PF0, D1->PF1, ..., D11->PF11)
- `GPIOF->IDR` 低 16 位中,**bit 0~11 = ADC 数据,bit 12~15 不参与 ADC 数据**

### 4.2 内存存储

```c
static uint16_t s_adc_buf[4096];
```

每个 `uint16_t` 元素:
- bit 0~11:ADC 数据(0~4095)
- bit 12~15:0(padding)

F407 是 **little-endian**,`s_adc_buf[0] = 0x0FEF` 在内存中:
```
地址 +0: 0xEF  (低字节 = ADC bit 7~0)
地址 +1: 0x0F  (高字节 = 0 + ADC bit 11~8)
```

### 4.3 SPI 发送字节序

`HAL_SPI_Transmit((u8*)buf, 8192)` 按字节顺序发,字节内 MSB first:

| 字节 | 内容 | 含义 |
|------|------|------|
| 0 | 0xEF | sample[0] 低字节(ADC bit 7~0) |
| 1 | 0x0F | sample[0] 高字节(ADC bit 11~8) |
| 2 | ... | sample[1] 低字节 |
| 3 | ... | sample[1] 高字节 |
| ... | ... | ... |
| 8191 | ... | sample[4095] 高字节 |

**H723 端重组**:
```c
for (int i = 0; i < 4096; i++) {
    adc[i] = ((uint16_t)rx[2*i+1] << 8) | rx[2*i];  // 高字节左移 8
    adc[i] &= 0x0FFF;                                 // 取低 12 bit
}
```

---


## 5. 软件接口

### 5.1 AD9226 驱动(`ad9226.h` / `ad9226.c`)

| 函数 | 签名 | 作用 |
|------|------|------|
| `AD9226_Init` | `void AD9226_Init(void)` | 启 TIM1 PWM(出默认 2.048 MHz) |
| `AD9226_Start` | `void AD9226_Start(void)` | 启 DMA 采 4096 点 + 使能 TIM1_UP DMA 请求 |
| `AD9226_IsDone` | `bool AD9226_IsDone(void)` | 查询本帧是否采完(非阻塞) |
| `AD9226_GetBuf` | `const uint16_t *AD9226_GetBuf(void)` | 取缓冲区指针(12-bit 在低 12 位) |
| `AD9226_SetSampleRate` | `HAL_StatusTypeDef AD9226_SetSampleRate(uint32_t fs_hz)` | **改 TIM1 ARR 调采样率**(5k~20MHz),下个 update event 生效 |
| `AD9226_GetSampleRate` | `uint32_t AD9226_GetSampleRate(void)` | 取当前采样率(Hz) |

### 5.2 SPI 发送(`spi.h` / `spi.c`)

```c
HAL_StatusTypeDef AcqSpi_SendSamples(const uint16_t *buf,
                                      uint32_t n,
                                      uint32_t fs_hz);

uint32_t AcqSpi_GetReadyTimeoutCount(void);   /* 累计握手超时次数 */
```

- `buf`:样本缓冲(来自 `AD9226_GetBuf()`)
- `n`:样本数(4096)
- `fs_hz`:采样率,用于 logging(**实际从 `AD9226_GetSampleRate()` 取**)
- 返回 `HAL_OK` = 发送成功,`HAL_TIMEOUT` = **握手超时**(等 PA4=H7_READY 超 100ms)
- **§3.1 握手**:发送前先轮询 `PA4` 等于 1,最多 100ms(详见 §3.6)
- 阻塞发送 8192 字节,完成后 printf 一行统计
- 错误计数 `AcqSpi_GetReadyTimeoutCount()`:tio 看到这计数一直涨 = H7 链路故障

### 5.3 SPI2 命令从机(`spicmd.h` / `spicmd.c`)🆕

```c
void     SpiCmd_Init(void);          /* 初始化 SPI2 + DMA1_Stream3 + 启动首帧接收 */
void     SpiCmd_Task(void);          /* 主循环任务:解析收到的命令,改 AD9226 Fs */
uint32_t SpiCmd_GetOkCount(void);    /* 已成功应用的命令数 */
uint32_t SpiCmd_GetErrCount(void);   /* 帧错误数(magic/CRC 错) */
```

**命令帧格式**(固定 10 字节,小端):

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-1  | magic    | u16 LE | 固定 0xA55A(`5A A5`) |
| 2    | type     | u8     | 0x01 = SET_FS |
| 3    | reserved | u8     | 0x00 |
| 4-7  | fs_hz    | u32 LE | 目标采样率(Hz),合法 5 000 ~ 20 000 000 |
| 8-9  | crc16    | u16 LE | CRC16/CCITT-FALSE(poly 0x1021, init 0xFFFF),覆盖 [0..7] |

### 5.4 main loop 工作流

```c
AD9226_Init();                 // 出默认 2.048 MHz
// PA4 = Input + PullUp (H7_READY 握手,见 §3.6)  <-- USER CODE BEGIN 2 加
SpiCmd_Init();                 // 启动 SPI2 从机接收
AD9226_Start();                // 启第一帧采集
while (1) {
    SpiCmd_Task();             // 处理收到的 SET_FS(可能改 Fs)
    if (AD9226_IsDone()) {
        buf = AD9226_GetBuf();
        printf("mean = %lu Fs=%lu\n",
               compute_mean(buf),
               AD9226_GetSampleRate());       // 冒烟测试,显示当前 Fs
        AcqSpi_SendSamples(buf, 4096,
                           AD9226_GetSampleRate());  // 内部等 PA4=1 再发 SPI1
        AD9226_Start();        // 启下一帧(用当前 Fs)
    }
}
```

---

## 6. 编译与烧录

```powershell
# 从仓库根目录执行
Set-Location .\AD9226_driver3\AD9226_driver

make -j4                                                    # 编译
st-flash --connect-under-reset write build/AD9226_driver.bin 0x08000000   # 烧录
sudo tio -b 115200 /dev/ttyUSB0                             # 串口监控 (ctrl-t q 退)
```

---

## 7. 测试与验证

### 7.1 各级验证方法

| Level | 测试点 | 工具 | 预期信号 | 状态 |
|-------|--------|------|----------|------|
| L1 时钟 | (无) | tio 串口能解码 = 时钟对 | 115200 不乱码 | ✅ |
| L2 PWM | **PE9** | 示波器 ×10 探头 | 2.049 MHz, 50%, 3.64 Vpp | ✅ |
| L3 printf | **PA9** | USB-TTL + tio | `[1] UART OK` 等字符串 | ✅ |
| L4 DMA | **PA9**(看 mean) | tio | `AD9226 4096-pt mean = 4079` | ✅ |
| **L5 SPI 主机** | **PA5 / PA7 / PA4** | **杜邦线 PA7↔PA6 自环**(PA4 内部上拉=1) | `[LB] mism=0 PERFECT`,`[SPI] st=0` | **✅ 自环 + 握手通过** |
| ~~L6 H723 从机~~ | — | (H723 端任务) | — | **H723 另人做** |
| ~~L7 联调~~ | — | (等 L6) | — | **H723 另人做** |
| **L8 动态采样率** | **PB13 / PB15 / PA4** | tio 看 `[CMD] SET_FS:` + 看 PA4(若 H7 拉低) | tio 打印 `SET_FS: 2048780 -> xxxx Hz`,频率计 **PE9** 跟着变 | **⏳ 待 H723 接入** |

### 7.2 L5 SPI 自环测试(已通过 ✅)

**步骤**:
1. 断电,用杜邦线短接 **PA7 (MOSI) ↔ PA6 (MISO)**
2. 上电,`sudo tio -b 115200 /dev/ttyUSB0`
3. 按 RESET

**预期输出**:
```
[1] UART OK (H7_READY=PA4, pullup, idle=1)
[2] AD9226_Init OK, Fs=2048780 Hz
[2b] SpiCmd_Init OK
[3] AD9226_Start OK, entering loop
AD9226 4096-pt mean = 4095 (0xFFF) Fs=2048780
[LB] 8192 B in 17 ms, mism=0 PERFECT
[SPI] TX 8192 B (n=4096, Fs=2048780 Hz) in 13 ms, st=0
...
```

**关键判断**:
- `mism=0 PERFECT` = SPI Mode 0 / 5.25 MHz / MSB 配置全对 ✅
- `st=0` = HAL_OK,发送成功 ✅
- 13 ms 传输时间 = 8192×8/5.25M ≈ 12.5 ms,符合理论

> 联调前需要把 `AcqSpi_LoopbackTest` 调用注释掉(已注释),省 17 ms/帧。
>
> **自环时 PA4 内部上拉**:H7 没接,PA4 内部上拉稳 = 1,握手立即通过,F407 直接发,行为同无握手。

### 7.3 L8 动态采样率测试(待 H723 接入)

**需要**:H723 端(或任意 SPI 主机,如 Arduino)按命令帧格式发 SET_FS。

**最简验证(无需 H723)**:
用 Arduino/ESP32 写 5 行程序,固定 1 秒发一次:
```c
// 示例:1s 发一次,轮发 1MHz / 5MHz / 10MHz / 2.048MHz
uint8_t frame[10];
build_set_fs_cmd(frame, 1000000);   // 用 §10 的 build_set_fs_cmd
HAL_SPI_Transmit(&hspi, frame, 10, 100);
HAL_Delay(1000);
```

**预期 tio 输出**:
```
[CMD] SET_FS: 2048780 -> 1000000 Hz
[CMD] SET_FS: 1000000 -> 5000000 Hz
[CMD] SET_FS: 5000000 -> 10000000 Hz
...
[loop] alive, IsDone=0 Fs=10000000
```

**PE9 验证**:用频率计量 PE9,会看到 1.000 / 5.000 / 10.000 MHz 实时切换。

### 7.4 调试接口

| 想看什么 | 用什么 | 怎么做 |
|----------|--------|--------|
| 串口输出 | tio | `sudo tio -b 115200 /dev/ttyUSB0` |
| 寄存器值 | OpenOCD + ST-Link | `openocd ... -c "init; halt; mdw 0x40026488 2"` |
| .elf 符号 | arm-none-eabi-nm | `nm build/AD9226_driver.elf \| grep XXX` |
| 反汇编 | arm-none-eabi-objdump | `objdump -d build/AD9226_driver.elf` |

---

## 8. 当前进度与待办

| Level | 状态 |
|-------|------|
| L1 系统时钟 | ✅ 通过 |
| L2 TIM1 PWM | ✅ 通过(**PE9** 实测 2.049 MHz) |
| L3 USART1 printf | ✅ 通过 |
| L4 AD9226 DMA | ✅ 通过(mean=4079 多次打印) |
| L5 SPI 主机发送 | ✅ 通过(**自环 mism=0 PERFECT** + **§3.6 握手通过**) |
| L6 H723 SPI1 从机接收 | 🔄 **H723 端任务(另人做)** |
| L7 F407↔H723 联调 | 🔄 **H723 端任务(另人做)** |
| **L8 H723→F407 动态改采样率** | ✅ **F407 端代码就绪**(待 H723 接入验证) |

---

## 9. 关键文件

| 文件 | 作用 |
|------|------|
| `AD9226_driver.ioc` | CubeMX 配置 |
| `Core/Src/main.c` | MX_*_Init(GPIO/DMA/TIM1/SPI1/SPI2/USART1)+ main loop + `__io_putchar` + 手动补的 DMA Init/RegisterCallback |
| `Core/Inc/ad9226.h` / `Core/Src/ad9226.c` | AD9226 驱动(Init/Start/IsDone/GetBuf/SetSampleRate/GetSampleRate + DMA 回调) |
| `Core/Inc/spi.h` / `Core/Src/spi.c` | `AcqSpi_SendSamples` SPI1 主机发送 + **§3.6 PA4=H7_READY 握手(100ms 超时 + 错误计数)** |
| `Core/Inc/spicmd.h` / `Core/Src/spicmd.c` 🆕 | SPI2 从机,接 H723 的 SET_FS 命令,改采样率 |
| `Makefile` | arm-none-eabi-gcc,显式列源 |

---

## 10. H723 接口约定(给 H723 那个人)🆕

### 10.0 F407 端 H7_READY 信号(给 H7 端)🆕

**新增一根线**(对应 G题文档 §4.1 / §3.6 简化版握手):

```
H723 任意 GPIO(推挽输出)  ───→  采集F407  PA4
```

- **H7 端**:空闲拉低,准备好拉高,接收完成拉低
- **F407 端**:`PA4` 配置为 Input + 内部上拉,默认 1
- **F407 行为**:每帧发 SPI1 前轮询 PA4 == 1,最多等 100 ms,超时丢帧 + 错误计数 + 1
- **片选 CS**:**不需要**。F407 用软件 NSS(SSM=1,SSI=1),不操作任何片选引脚;H7 用软件 NSS 即可常听
- **双边 ack/req**:**不需要**。F407 是被动等 + 发,H7 单向给 READY 即可

**4 步简化握手时序**(对应 G题文档 §4.1,**比 G 题文档简化的部分见底部说明**):
```
1. H7 准备好后,把 READY 拉高                        ← "我准备好了"
2. F407 轮询 PA4 == 1,直接 HAL_SPI_Transmit(8192 B)  ← 长度固定,发完即停
3. H7 收满 4096 字节(DMA 完成中断),把 READY 拉低      ← "我在处理,别发"
4. H7 处理完数据,再次把 READY 拉高                    ← 回到 step 1
```

> **为什么要 READY**:F407 现在每 15 ms 发 8192 字节。H7 处理慢的话会丢数据。READY 是 H7 的"流控",告诉 F407"我准备好了,可以发"。
>
> **F407 端错误处理**:100 ms 等不到 READY(链路断/H7 卡死),F407 不阻塞,丢本帧,错误计数 +1,主循环继续。
>
> **与 G题文档 §4.1 的差异**(简化理由):
>
> | G题 §4.1 步骤 | F407 实现 | 理由 |
> |---|---|---|
> | 第 5 步:F407 拉低 CS | **去掉** | 固定长度 4096 字节,H7 收满即停,CS 无意义;软件 NSS 等效常选 |
> | 第 7 步:F407 拉高 CS | **去掉** | 同上 |
> | 第 10 步:H7 重新启动 DMA | **合并到第 4 步** | H7 处理完拉高 READY 同时启动下一帧 DMA,等价 |
> | 第 8 步:H7 拉低 READY + 置位帧完成 | **保留** | 必须,F407 需要知道 H7 何时处理完 |
> | 整体 10 步 → 4 步 | **简化** | 单边握手,无 ack/req,F407 被动 |

### 10.1 物理接线

```
H723 SPIx_SCK   ───→  采集F407  PB13   (SPI2_SCK)
H723 SPIx_MOSI  ───→  采集F407  PB15   (SPI2_MOSI)
GND             ───↔  GND
```

> **NSS 不接**:我们配的"软件 NSS"(SSM=1, SSI=1),从机常开。H723 不用控制 CS 引脚,直接 SCK+MOSI 发 10 字节即可。

### 10.2 SPI 配置要求(任何 SPI 主机都要满足)

- **Mode 0**(CPOL=0, CPHA=0)
- **8-bit, MSB First**
- **SCK ≤ 10 MHz**(F407 APB1 最高 21 MHz,留余量)

### 10.3 命令帧(10 字节,小端)

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-1  | magic    | u16 LE | 固定 `0xA55A` → 字节序 `5A A5` |
| 2    | type     | u8     | `0x01` = SET_FS |
| 3    | reserved | u8     | `0x00` |
| 4-7  | fs_hz    | u32 LE | 目标采样率(Hz),合法 5 000 ~ 20 000 000 |
| 8-9  | crc16    | u16 LE | CRC16/CCITT-FALSE(见下),覆盖 [0..7] |

**CRC16/CCITT-FALSE**:
- 多项式 `0x1021`,初值 `0xFFFF`,reflect in/out = false,xorout = `0x0000`
- 只覆盖前 8 字节,后 2 字节是校验值本身(小端)

### 10.4 H723 端 C 参考代码

```c
uint16_t crc16_ccitt_false(const uint8_t *d, uint32_t n) {
    uint16_t c = 0xFFFF;
    for (uint32_t i = 0; i < n; i++) {
        c ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++)
            c = (c & 0x8000) ? (c << 1) ^ 0x1021 : (c << 1);
    }
    return c;
}

void build_set_fs_cmd(uint8_t *frame, uint32_t fs_hz) {
    frame[0] = 0x5A;  frame[1] = 0xA5;            // magic
    frame[2] = 0x01;                               // type SET_FS
    frame[3] = 0x00;                               // reserved
    frame[4] = (fs_hz      ) & 0xFF;              // fs_hz 小端
    frame[5] = (fs_hz >>  8) & 0xFF;
    frame[6] = (fs_hz >> 16) & 0xFF;
    frame[7] = (fs_hz >> 24) & 0xFF;
    uint16_t crc = crc16_ccitt_false(frame, 8);
    frame[8] = (crc     ) & 0xFF;                 // crc 小端
    frame[9] = (crc >> 8) & 0xFF;
}

// 调用:
uint8_t frame[10];
build_set_fs_cmd(frame, new_fs);
HAL_SPI_Transmit(&hspi_x, frame, 10, 100);  // 任意 SPI 句柄,timeout 100ms
```

### 10.5 H723 决策示例(把 f0 映射到 Fs)

```c
// H723 算出基频 f0 后,决定新的 Fs
// 这里示例"每帧采 64 个周期": Fs = 64 × f0
float f0 = ...;  // FFT/CZT 算出的基频 Hz
uint32_t new_fs;

if (f0 < 1000.0f)        new_fs = 2000000;   // 低频兜底
else if (f0 > 312500.0f) new_fs = 20000000;  // 接近上限兜底
else                      new_fs = (uint32_t)(f0 * 64.0f);

build_set_fs_cmd(frame, new_fs);
HAL_SPI_Transmit(&hspi_x, frame, 10, 100);
```

> 映射规则(64 × f0)和兜底值由 H723 算法决定,F407 不参与,只管按 Fs 执行。
