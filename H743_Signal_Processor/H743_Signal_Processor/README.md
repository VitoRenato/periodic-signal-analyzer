# H743 信号处理端

STM32H743ZIT6 通过 SPI1 从 F407 接收 AD9226 原始采样。H743 解析帧头给出的实际采样率和测量代号，估计基频与谐波分量。结果分五页回传，与采样率请求共用 SPI1 控制槽。F407 负责锁定测量结果并驱动串口屏。

- [`Firmware_H743/`](Firmware_H743/)：板级固件。采集交换和分析调度入口为 `Core/Src/processor_app.c`。
- [`Algorithm/`](Algorithm/)：预处理、基频估计、谐波模型拟合、电压标定和测量值计算。
- [`Protocol/`](Protocol/)：采集帧解码、采样率控制与模型分页。
- [`tests/`](tests/)：算法和协议主机测试；构建、测试和烧录入口见[仓库说明](../../README.md)。

使用时可查 [接线指南](H743_信号处理端_接线指南.md)、[软件手册](H743_信号处理端_软件技术手册.md)、[SPI1 通信协议](docs/COMMUNICATION_PROTOCOL.md)及[硬件配置](docs/HARDWARE_CONFIGURATION.md)。
