# 周期信号测量装置

基于 STM32F407、AD9226 和 STM32H743 的周期信号测量项目。F407 采集并行 ADC 数据，H743 分析频率与幅值，再由 F407 把测量结果、重建波形和频谱送到淘晶驰串口屏。

## 当前状态

采集端已恢复为真实十二位原始码，只清除 GPIO 读数的高四位，不再用低八位展开或固定零点修补数据。ADC 数据线使用 PF0～PF11，时钟输出为 PE9，OTR 保留在 PC12。

已用零输入及 100 kHz、两档幅度的正弦采样检查原始码和幅度比例。正常测量链路能够锁定并显示结果；屏幕波特率与曲线坐标问题也已在实机上确认修复。屏幕保留原有 40×40 像素网格，固件按 `s_wave` 的 761×161 和 `s_fft` 的 441×121 尺寸绘图，不需要扩大控件。

电压系数尚未做最终标定。信号源标称 250 mVpp 时，屏幕约显示 255～256 mVpp、90 mVrms 和 128 mV 分量峰值；当时示波器读数约为 260 mVpp。这是单频联调记录，不代表全频段精度验收。采样时序、协议异常处理和抗干扰能力仍需继续验证。

## 工程入口

| 目录 | 内容 |
|---|---|
| `AD9226_driver3/AD9226_driver/` | F407 采集、板间通信和串口屏固件 |
| `H743_Signal_Processor/H743_Signal_Processor/` | H743 分析算法、协议与 `Firmware_H743/` 固件 |
| [TJC_HMI](TJC_HMI/README.md) | 屏幕源工程 `TJCscr.HMI`、控件尺寸和连接说明 |
| [components/ad9226_f407](components/ad9226_f407/README.md) | 独立驱动组件与历史算法测试，不是当前生产采集入口 |

使用 VS Code 时可打开 [periodic-signal-analyzer.code-workspace](periodic-signal-analyzer.code-workspace)。部分技术手册保留了旧架构与旧显示流程，当前行为以本页、[原始码标定说明](AD9226_driver3/AD9226_driver/docs/ADC12_CALIBRATION.md)和实际源码为准。

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

新增回归直接编译生产 ADC 驱动，检查十二位码保留；显示回归检查实际控件的坐标、宽度及频点覆盖。主机测试不替代 ADC 引脚时序、电压精度和屏幕实机验收。

## 导出原始码

F407 调试口为 USART1，PA9 是 TX、PA10 是 RX，参数为 921600 baud、8N1。PA15 按键短按切换 1/3 周期，长按 1.5 s 重新测量；串口发送 `m` 同样触发重新测量，发送 `a` 切换自动连续测量。发送 `c` 或 `C` 可获取一帧 4096 点原始码，完整保存 `[CAL12 BEGIN]` 到 `[CAL12 END]` 的内容。

导出后采集冻结，这一帧不送 H743，屏幕保持原状；再次发送 `c` 可重新取帧。返回正常测量时复位 F407，不发送 `c`。正式标定使用原始码，不使用旧 `d` 命令的电压换算列。`fs_hz` 按定时器寄存器推导，不是示波器实测频率。接线和数据格式见[标定说明](AD9226_driver3/AD9226_driver/docs/ADC12_CALIBRATION.md)。

## 许可证

贡献者原创且没有单独许可声明的文件使用 [MIT License](LICENSE)。STM32 HAL、CMSIS 和其他第三方材料保留原许可，边界见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
