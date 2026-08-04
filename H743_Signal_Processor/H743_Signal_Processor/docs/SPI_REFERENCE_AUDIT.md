# 等效采样工程 SPI 参考审计

审计日期：2026-07-31  
只读参考：本地等效采样工程（未随仓库发布）

## 1. 参考目的

该工程只用于借鉴STM32H743的SPI、DMA、READY和缓存处理方式。本项目不复制
其ESPI报文协议、AD9238采样格式或等效采样业务流程。

## 2. 已检查的参考文件

- `Processor_H743\Processor_H743.ioc`
- `Processor_H743\Core\Src\main.c`
- `Processor_H743\Core\Src\stm32h7xx_hal_msp.c`
- `Common\Protocol\es_protocol.h`
- `Common\Protocol\es_protocol.c`
- `docs\superpowers\specs\2026-07-31-equivalent-sampling-spi-system-design.md`
- `docs\hardware-bringup-checklist.md`

## 3. 可以借鉴的内容

### 3.1 H743时钟基线

参考工程确认了同型号STM32H743ZIT6和25 MHz HSE的可生成配置：

```text
PLL1M = 5
PLL1N = 192
PLL1P = 2
CPU   = 480 MHz
HPRE  = /4
HCLK  = 120 MHz
SPI123 kernel = 120 MHz
```

这可以作为新CubeMX工程的起点，但仍需依据实际H743板卡、电源等级和芯片
修订确认，不能仅凭参考工程宣称硬件稳定。

### 3.2 DMA与运行顺序

可复用的设计原则：

- SPI DMA使用Normal模式；
- 一个输入帧只使用一个DMA缓冲区；
- 先挂载从机RX DMA，再拉高READY；
- DMA回调只拉低READY并设置`volatile`完成/错误标志；
- CRC、解码和算法均在主循环执行；
- 当前帧处理和输出完成后才重新挂载输入DMA；
- HAL启动失败或DMA错误不在中断中自动无限重试。

### 3.3 H743缓存和内存

可复用：

- DMA1/DMA2缓冲区不能放普通DTCM；
- `.dma_buffer`放RAM_D2；
- 32字节对齐；
- 用MPU把专用DMA区域配置为不可缓存；
- 保持I-Cache和D-Cache整体开启；
- FFT/CZT等CPU工作区放AXI SRAM或合适的CPU可访问RAM。

### 3.4 主机发送侧

参考工程的SPI2主机配置可以作为“主机TX DMA”的写法参考：

| 功能 | 候选配置 |
|---|---|
| 参考SPI2 SCK | PB13 |
| 参考SPI2 MISO | PB14，可不接 |
| 参考SPI2 MOSI | PB15 |
| 主控READY输入 | PD1，下拉 |
| H743角色 | SPI主机 |
| 初始SCK | 3.75 Mbit/s |
| DMA | DMA1 Normal模式TX |

本项目直接借鉴其SPI2主机TX DMA形式向主控F407发送结果：建议PB13=SCK、
PB15=MOSI，PD1=主控READY。SPI2采用固定88字节帧和READY边界，保持无CS。
SET_FS不占用SPI2，而是由采集F407在原SPI1上提供时钟，H743通过MISO分时
返回。若最终母板强制接有CS，应只修改平台适配层，不能改变88字节报文内容。

## 4. 明确不能照搬的内容

| 参考工程内容 | 本项目为什么不用 |
|---|---|
| SPI1 16位数据宽度 | 采集F407已确定8位SPI |
| PA4硬件NSS和PG15软件CS | 当前采集接口明确无CS |
| 8220字节ESPI采样报文 | 当前输入是8192字节裸AD9226数字码 |
| 报文内采样率、序号和CRC32 | 当前采集帧不提供这些字段 |
| H743经SPI1返回24字节基频 | 本项目仍用SPI1返回，但改为16字节SET_FS分页命令槽 |
| 48字节三分量结果报文 | 本项目使用自己的88字节完整结果帧 |
| AD9238有符号12位格式 | 本项目是AD9226低12位无符号码 |
| 21/42 Mbit/s长帧设置 | 当前采集SPI时钟约5.25 MHz |
| 256点等效采样重建 | 绘图数据不属于H743算法职责 |

## 5. 本项目采用后的SPI结构

```text
采集F407 --SPI1 MOSI, 8192B raw--> H743
采集F407 <--SPI1 MISO, 10B SET_FS-- H743
              两阶段时钟均由采集F407产生
              READY由H743输出

H743 --SPI2, 88B RESULT-----> 主控F407
                  READY由主控F407输出
```

两个方向的SPI协议相互独立：

- 采集输入和SET_FS共用SPI1，采集F407始终为主机；
- 88字节RESULT是本H743项目自己的协议；
- 等效采样工程的ESPI协议不参与任何链路。
- H743的SPI2只连接主控F407。

## 6. 证据边界

参考工程的软件构建和静态配置只能证明这些模式有代码实例，不能证明本项目
的板级引脚、无CS时序、5.25/3.75 MHz长帧、RAM_D2放置或硬件通信已经通过。
正式结论必须来自本项目自己的CubeMX、map文件、逻辑分析仪和实物联调。
