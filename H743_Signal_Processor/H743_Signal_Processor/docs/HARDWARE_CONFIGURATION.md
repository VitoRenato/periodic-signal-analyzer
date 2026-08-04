# STM32H743 固件硬件配置

更新日期：2026-07-31

## 1. 适用范围

本文记录 `Firmware_H743` 的板级配置和固件边界。目标器件为
`STM32H743ZIT6`。板卡原理图属于本地硬件参考资料，未随本仓库发布。

原理图确认板载HSE为PH0/PH1上的25 MHz晶体，不是外部有源时钟旁路。
PA6同时连接摄像头接口的DCMI像素时钟；本工程使用PA6作为SPI1_MISO，
因此不得同时启用DCMI，联调时也不要插接摄像头模块。

## 2. 时钟和电源

| 项目 | 配置 |
|---|---|
| 电源 | LDO |
| 电压等级 | VOS0 |
| HSE | 25 MHz Crystal/Ceramic Resonator |
| PLL1M/N/P/Q | 5 / 192 / 2 / 8 |
| CPU/SYSCLK | 480 MHz |
| HPRE | /4 |
| HCLK/AXI/AHB | 120 MHz |
| APB1/2/3/4 | 60 MHz |
| SPI123内核 | PLL1Q，120 MHz |

CubeMX生成代码必须同时出现`RCC_HSE_ON`、PH0和PH1；只有PH0的
`External Clock Source`表示错误的旁路配置。

## 3. SPI和READY

### 3.1 采集F407 ↔ H743

| 项目 | 值 |
|---|---|
| H743外设 | SPI1从机，全双工 |
| 引脚 | PA5 SCK、PA6 MISO、PA7 MOSI |
| 格式 | Mode 0、8 bit、MSB first、Software NSS |
| 输入 | 8208字节帧后8192字节，4096个小端`uint16_t`采样 |
| 返回 | 同帧MISO前16字节SET_FS与五页模型 |
| DMA | SPI1_RX DMA1 Stream0、SPI1_TX DMA1 Stream1 |
| READY | PD0 `ACQ_READY`，推挽输出，初始低 |

H743先挂载8208字节`HAL_SPI_TransmitReceive_DMA()`再把PD0拉高。F407在同一
事务中上传样本并读取MISO命令槽；DMA完成或错误回调立即拉低PD0，算法只在
主循环运行。

### 3.2 H743 → 主控F407

| 项目 | 值 |
|---|---|
| H743外设 | SPI2主机，TX only |
| 引脚 | PB13 SCK、PB15 MOSI |
| 格式 | Mode 0、8 bit、MSB first、Software NSS |
| 初始速率 | 120 MHz / 32 = 3.75 Mbit/s |
| 输出 | 固定88字节RESULT |
| DMA | SPI2_TX DMA1 Stream2 |
| READY | PD1 `MAIN_READY`，输入下拉 |

主控F407先挂载88字节RX DMA并拉高PD1。H743最多等待100 ms；超时丢弃本次
发送机会、记录计数并处理下一帧，不自动重发旧结果。

## 4. Cache、MPU和DMA内存

I-Cache和D-Cache均开启。MPU Region0配置如下：

| 项目 | 值 |
|---|---|
| Base | `0x30000000` |
| Size | 16 KB |
| SubRegionDisable | `0x00` |
| TEX | Level 1 |
| Permission | Full Access |
| Execute | Disable |
| Shareable | Enable |
| Cacheable | Disable |
| Bufferable | Disable |

CubeMX 6.18在未显式填写子区掩码时生成了`0x87`；该值会禁用部分2 KB子区，
不能覆盖从`0x30000000`开始的完整DMA区，现已在`.ioc`和`MPU_Config()`中
固定为`0x00`。

链接器把以下对象32字节对齐放入`.dma_buffer`：

| 符号 | 分配大小 | 用途 |
|---|---:|---|
| `g_dma_frame` | 8208 B | 16字节占位+4096点裸采样 |
| `g_dma_set_fs_frame` | 32 B分配对齐 | 16字节SET_FS分页命令槽 |
| `s_spi1_tx_frame` | 8208 B | 16字节命令+8192字节零占位 |

GNU链接脚本为`Firmware_H743/STM32H743ZITX_FLASH.ld`，Keil scatter文件为
`Firmware_H743/MDK-ARM/H743_Signal_Processor.sct`。普通`.data/.bss`放在
`0x24000000`的512 KB AXI SRAM，DMA区单独位于D2 SRAM；不得把DMA对象放入
`0x20000000`的DTCM。

## 5. 串行状态机

固件保持单采集缓冲区，严格串行：

```text
挂载SPI1 8208字节TxRx DMA → ACQ_READY高
→ 全双工收发完成/READY低
→ SignalAnalyzer_Process
→ pager生成下一帧16字节SET_FS与测量页
→ 更新下一帧实际Fs
→ 重新挂载下一帧SPI1 TxRx DMA
```

中断只更新完成/错误事件和READY，不执行FFT、CZT、协议编码或阻塞等待。
调试器可观察`ProcessorApp_GetDiagnostics()`返回的诊断结构。

## 6. 采样率整数分频

SET_FS请求范围仍为5 kHz到20 MHz。H743使用与采集F407一致的整数公式：

```text
divider = floor(168000000 / requested_fs)
actual  = floor(168000000 / divider)
```

默认请求`2048780`得到divider 82，协议中的整数实际值仍为`2048780`。
请求`10000000`实际为`10500000`。请求上限`20000000`会产生`21000000`，
所以算法输入上限扩展到21 MHz；RESULT中的`sample_rate_Hz`始终填写当前帧
使用的整数实际值，而不是未经量化的请求值。

## 7. CubeMX再生成规则

- 从`Firmware_H743/H743_Signal_Processor.ioc`打开工程。
- 保留User Code选项必须开启。
- 再生成后首先检查`MPU_InitStruct.SubRegionDisable == 0x00`。
- 检查`processor_app.h`、主循环初始化和运行调用仍位于USER CODE区。
- CubeMX不会维护自定义GNU CMake、GNU linker script或Keil新增算法分组；
  再生成后必须重新审查Keil工程文件，不能直接假定仍包含算法和协议源。
