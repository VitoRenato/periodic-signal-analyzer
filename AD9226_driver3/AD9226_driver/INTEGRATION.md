# F407 系统集成

F407 是 SPI1 主机。每次交换向 H743 上传帧头和 4096 点样本，同时在 MISO 读取一个控制槽。H743 拉高 READY 后，F407 发起交换；采样和模型报文见 [SPI1 通信协议](../../H743_Signal_Processor/H743_Signal_Processor/docs/COMMUNICATION_PROTOCOL.md)。

F407 锁定模型后构建内部 `ScreenDataFrame`，由 `ScreenApp_SetFrame()` 交给显示任务。`ScreenApp_Task()` 检查帧序号，再由显示层通过 USART2 向 TJC 发送控件命令及曲线数据。接线与控件见 [TJC 屏幕工程](../../TJC_HMI/README.md)。
