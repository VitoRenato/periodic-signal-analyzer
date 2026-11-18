# H743 开发入口

先按[接线指南](../H743_信号处理端_接线指南.md)和[整机说明](../../../README.md)完成双板连接与构建。板间交换使用 [SPI1 通信协议](COMMUNICATION_PROTOCOL.md) 的帧格式。

固件从 `Firmware_H743/Core/Src/main.c` 启动。`processor_app.c` 调度 `spi_links.c`、采样率策略和算法，`Algorithm/` 负责信号分析，`Protocol/` 负责编解码。阅读 [软件手册](../H743_信号处理端_软件技术手册.md) 可沿这条路径定位问题。

需要检查幅度时，在同一输入设置下同时记录参考电压、F407 导出的 4096 点原始码和完整屏显结果。接线及时钟参数见[硬件配置](HARDWARE_CONFIGURATION.md)。
