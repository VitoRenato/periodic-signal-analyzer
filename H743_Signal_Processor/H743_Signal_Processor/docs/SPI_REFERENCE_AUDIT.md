# SPI 参考审计

外部等效采样工程仅用来对照 STM32H743 的 SPI DMA 挂载、READY 握手及 DMA 缓冲区的缓存一致性。工程不依赖该参考项目。

当前实现集中在 `Firmware_H743/Core/Src/spi_links.c` 与 `processor_app.c`。H743 在非缓存 SRAM 准备收发缓冲，挂载 SPI1 双向 DMA 后拉高 READY，完成或出错时拉低。F407 作为唯一 SPI 主机在同一事务内发送样本并读取控制槽。接口字段见 [SPI1 通信协议](COMMUNICATION_PROTOCOL.md)，板级核对步骤见[实机验证](HARDWARE_VERIFICATION.md)。
