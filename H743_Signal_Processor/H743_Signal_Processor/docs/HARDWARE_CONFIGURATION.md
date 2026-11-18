# STM32H743 固件硬件配置

目标器件为 STM32H743ZIT6。固件配置见 `Firmware_H743/Core/Src/main.c`、`stm32h7xx_hal_msp.c` 和链接脚本。

| 项目 | 配置 |
|---|---|
| 时钟 | HSE 25 MHz 晶体，PLL1 M/N/P/Q = 5/192/2/8，CPU 480 MHz，HCLK 120 MHz，APB 60 MHz |
| SPI1 | 全双工从机，PA5/PA6/PA7，Mode 0、8 位、MSB first、软件 NSS |
| SPI1 DMA | DMA1 Stream0 接收、Stream1 发送 |
| 采集握手 | PD0 推挽输出；挂载双向 DMA 后置高，完成或错误时置低 |
| 调试串口 | USART1，PA9 TX、PA10 RX，115200 baud、8N1 |

I-Cache 和 D-Cache 均开启。MPU 区域 0 覆盖从 `0x30000000` 开始的 32 KB，子区全部启用，配置为可共享、不可缓存、不可缓冲、不可执行。`.dma_buffer` 中的对象按 32 字节对齐，包含 8214 字节接收帧、22 字节控制槽与 8214 字节发送缓冲。SPI1 交换见[通信协议](COMMUNICATION_PROTOCOL.md)。
