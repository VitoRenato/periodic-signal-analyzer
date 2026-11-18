# AD9226 串口数据工具

生产固件的原始码采集使用普通串口终端：按 [原始码导出与标定](../docs/ADC12_CALIBRATION.md)配置 921600 baud、8N1，发送 `c` 后保存整帧 CSV。数据列为 `index,code`，可用常见 CSV 工具绘图和计算。

本目录的 `ad9226_viewer.py` 是独立调试工具，通过 `python3 tools/ad9226_viewer.py /dev/ttyUSB0` 启动（依赖 `pyserial` 和 `numpy`，图形输出可用 `matplotlib`）。它固定使用 115200 baud，采集请求与生产固件的标定导出接口不同；采集当前固件的数据使用上面的终端流程。
