# 周期信号测量装置

基于 STM32F407、AD9226 和 STM32H743 的周期信号测量项目。F407 采集并行 ADC 数据，H743 分析频率与幅值，再由 F407 把测量结果、重建波形和频谱送到淘晶驰串口屏。

## 测量链路

1. **F407 采集。** PE9 的 TIM1_CH1 向 AD9226 提供采样时钟，默认约 2.04878 MSPS。TIM1 更新事件触发 DMA2 Stream5，以 16 位半字读取 GPIOF。每帧 4096 点，AD9226 的 D0～D11 对应 PF0～PF11，样本取 GPIOF 的低 12 位。
2. **H743 分析。** F407 通过 SPI1 发送采样率分频值、帧序号、测量请求代号及原始采样。H743 估计基频和至多三个正弦分量（含基波），在回传结果中提供幅度、相位、模型峰峰值和交流有效值。回传模型可携带采样率请求；生成测量值时按分量频率应用输入前端的幅度补偿。
3. **F407 显示。** F407 锁定结果后，通过 USART2 更新淘晶驰屏幕上的基频、Vpp、Vrms、重建波形和频谱。波形由返回的分量叠加生成，Vpp 和 Vrms 是该模型的测量值。非正弦信号含有模型未收录的分量时，屏上波形和峰峰值可能与原始输入不同。

## 使用仪器

开机自动测量一轮。锁定后保持显示，以新结果触发重画。PA15 短按切换显示 1 或 3 个周期，长按约 1.5 秒或向 F407 调试口发送 `m` 重新测量。发送 `a` 切换自动连测，默认关闭；开启后，每轮结束满 1 秒发起下一轮。

## 工程入口

| 入口 | 内容 |
|---|---|
| [F407 技术手册](AD9226_driver3/AD9226_driver/F407_AD9226_技术手册.md) | AD9226 采集、接线、按键和调试入口 |
| [H743 工程](H743_Signal_Processor/H743_Signal_Processor/README.md) | 信号分析、板间通信和构建 |
| [TJC_HMI](TJC_HMI/README.md) | 串口屏工程、接线和控件 |
| [AD9226 独立驱动组件](components/ad9226_f407/README.md) | 独立 HAL 采集组件 |

使用 VS Code 时可打开 [periodic-signal-analyzer.code-workspace](periodic-signal-analyzer.code-workspace)。烧录时为双板使用配套固件；板间帧格式见 [SPI1 通信协议](H743_Signal_Processor/H743_Signal_Processor/docs/COMMUNICATION_PROTOCOL.md)。

## 构建固件

Linux 需要 GNU Arm Embedded Toolchain、Make、CMake 和 Ninja。以下命令在仓库根目录执行。

```sh
make -C AD9226_driver3/AD9226_driver -j4

cmake -S H743_Signal_Processor/H743_Signal_Processor/Firmware_H743 \
  -B work/h743-firmware -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/H743_Signal_Processor/H743_Signal_Processor/Firmware_H743/cmake/arm-none-eabi-toolchain.cmake"
cmake --build work/h743-firmware --parallel 4
```

F407 输出 `AD9226_driver3/AD9226_driver/build/AD9226_driver.elf`，H743 输出 `work/h743-firmware/H743_Signal_Processor_Firmware.elf`。F407 Makefile 尚未自动追踪头文件依赖，修改配置头文件后给构建命令加 `-B`，避免沿用旧目标文件。

烧录前核对目标板和探针。F407 可用 `make -C AD9226_driver3/AD9226_driver flash`；多个 ST-Link 同时连接时，显式传入 `STLINK_SERIAL_F4`。H743 的烧录入口在 VS Code 工作区中，不要把两块板的固件混用。

## 运行主机测试

主机测试需要本机 C 编译器、CMake 和 Ninja，不连接开发板。

```sh
cmake -S AD9226_driver3/AD9226_driver/tests/host -B work/f407-host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build work/f407-host
ctest --test-dir work/f407-host --output-on-failure

cmake -S H743_Signal_Processor/H743_Signal_Processor -B work/h743-host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build work/h743-host
ctest --test-dir work/h743-host --output-on-failure

cmake -S components/ad9226_f407/tests -B work/component-host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build work/component-host
ctest --test-dir work/component-host --output-on-failure
```

主机测试覆盖原始采集、测量会话、板间协议、算法及屏幕渲染。ADC 引脚时序、电压精度和显示效果在实机上验证。

## 导出原始码

F407 调试口使用 USART1（PA9 TX、PA10 RX），921600 baud、8N1。发送 `c` 或 `C` 获取一帧 4096 点原始码，保存 `[CAL12 BEGIN]` 到 `[CAL12 END]`。该帧用于标定，不送 H743，屏幕保持原状。再次发送 `c` 可取下一帧；发送 `m` 或长按 PA15 恢复测量。详见[原始码导出与标定](AD9226_driver3/AD9226_driver/docs/ADC12_CALIBRATION.md)。

## 许可证

贡献者原创且没有单独许可声明的文件使用 [MIT License](LICENSE)。STM32 HAL、CMSIS 和其他第三方材料保留原许可，边界见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
