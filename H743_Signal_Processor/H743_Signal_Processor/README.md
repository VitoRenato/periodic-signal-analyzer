# H743 Signal Processor

本目录用于 STM32H743ZIT6 信号参数计算工程。当前发布架构是**采集端
STM32F407ZGT6 + 信号处理端 STM32H743ZIT6**；旧 H723 计划只保留为历史
背景，不是本发布版本的接线、编译或通信依据。

当前状态：**完整主机算法流程和独立88字节RESULT协议均已通过Debug/Release自动测试。**
## 最新中文文档

- [H743 信号处理端 软件技术手册](H743_信号处理端_软件技术手册.md) — 当前固件数据流、算法、协议、编译调试
- [H743 信号处理端 接线指南](H743_信号处理端_接线指南.md) — 电源、晶振、SPI1/SPI2、调试串口、SWD

> ⚠️ 当前 F407↔H743 采集链路唯一的发布契约是 **SPI1 8208 字节全双工帧**：
> MOSI 为 16 B 占位 + 8192 B 样本，MISO 同时返回 16 B 命令槽。详见
> [`docs/COMMUNICATION_PROTOCOL.md`](docs/COMMUNICATION_PROTOCOL.md)。旧 H723、
> “先传 8192 B 再读 10 B”及“SPI2 已启用”的文字均为历史设计记录，不能用于
> 当前接线或固件配置。



下次对话从这里开始：

- [docs/START_HERE.md](docs/START_HERE.md)

主要技术文档：

- [docs/TECHNICAL_ROUTE.md](docs/TECHNICAL_ROUTE.md)
- [docs/SPI_REFERENCE_AUDIT.md](docs/SPI_REFERENCE_AUDIT.md)
- [docs/COMMUNICATION_PROTOCOL.md](docs/COMMUNICATION_PROTOCOL.md)
- [docs/references/REFERENCE_MANIFEST.md](docs/references/REFERENCE_MANIFEST.md)

## 当前职责边界

- 本目录负责：接收 4096 点 AD9226 原始数据，估计基频、Upp、真有效值及
  最多 3 个有效频谱分量。
- H743 使用 25 MHz 外部晶振。
- 采集由另一块 STM32F407ZGT6 控制 AD9226 完成。
- 512 点波形和 256 点频谱是显示绘图数据，不由本 H743 算法生成或传输。
- 88 字节 `RESULT` 的 CRC32 编解码和主机测试已实现；其物理发送接口仍须以
  母板为准，不能据此声称当前二板采集链路已启用 SPI2。
- 采集 F407 始终作为 SPI1 主机：一次 8208 B `TransmitReceive` 同时上传样本
  并从 H743 的 MISO 取得 16 B 命令槽；不会进行独立的第二阶段 10 B 读取。

## 已实现的完整主机算法流程与结果协议

- `Algorithm/Inc/signal_analyzer.h`：公共状态、分量、结果结构和
  `SignalAnalyzer_Process()` 接口。
- `Algorithm/Src/signal_analyzer.c` 与 `signal_preprocess.c`：参数校验、
  12 位样本掩码、当前 AD9226 标定、去静态直流、端点码和高位异常检查。
- `tests/test_signal_analyzer.c`：主机端参数、无信号、削顶和链路异常测试。
- `harmonic_model.c`：不假定最大峰为基波，保留最多3个期望谐波分量和
  1个不低于1 MHz的非期望干扰候选。
- `orthogonal_fit.c`：double法方程、部分主元消元、枢轴比检查、联合幅相、
  残差和SNR计算。
- `tests/test_orthogonal_fit.c`：普通I/Q串扰反例、谐波结构、干扰列、相位
  归一化、病态矩阵和无效输入测试。
- `signal_metrics.c`：只对期望谐波分量进行8192相位点重构求Upp，并按
  `sqrt(sum(A_i^2/2))`计算真有效值。
- `SignalAnalyzer_Process()`：完整串联预处理、RFFT候选、CZT、谐波选模、
  联合拟合、指标和采样率建议。
- `tests/test_signal_end_to_end.c`：覆盖10/100/500 kHz、1/2/3分量、固定
  伪随机相位、DC偏置、1.1 MHz干扰、带外模型和采样率风险状态。
- `Protocol`：CRC-32/ISO-HDLC与显式小端88字节RESULT编解码；不直接发送
  C结构体，失败时不污染调用方输出。
- `tests/test_result_protocol.c`：CRC已知向量、独立字面量帧、往返、饱和、
  空闲槽清零、合法非VALID状态和破坏/语义拒绝测试。

本阶段权威主机验证：`build/final-debug5-gcc`与
`build/final-release5-gcc`完整CTest均为`7/7 PASS`，编译无警告。

主机验证命令：

```powershell
cmake -S . -B build\host -G Ninja
cmake --build build\host
ctest --test-dir build\host --output-on-failure
```

当前主机测试已验证无噪、未削顶AD9226量化夹具的端到端测量门限和RESULT
协议字节行为，但不代表ARM编译与性能、模拟抗混叠、SPI物理链路或上板测量
已经完成。采样率风险状态只依据数字侧可观测条件。

## 下次对话建议

让新对话先完整阅读`docs/START_HERE.md`。下一步是板级工程：先提供并核对
实际母板原理图，确认当前 SPI1、READY 和任何可选结果接口后，再在临时副本中
配置 CubeMX。不要重新实现已通过测试的算法和协议，也不要修改旧工程。
