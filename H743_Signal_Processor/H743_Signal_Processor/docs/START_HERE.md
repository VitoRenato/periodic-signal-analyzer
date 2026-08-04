# H743 Signal Processor 接手入口

更新日期：2026-08-01

下次对话进入仓库内 `H743_Signal_Processor/H743_Signal_Processor` 后，先读本文件，不需要再查阅
旧 H723、F407 或本地消息目录恢复背景。

## 1. 当前结论

- 目标芯片：STM32H743ZIT6，外部晶振25 MHz。
- 输入：采集F407发送的4096个AD9226十二位数字码，共8192字节。
- 输入链路：SPI Mode 0、8 bit、MSB first、约5.25 MHz、无CS，READY流控。
- 输入帧没有帧头、采样率、序号或CRC；这是采集组已经确定的外部契约。
- 算法：预处理 → 4096点RFFT → 局部CZT → 联合正交相关器 →
  谐波模型 → Upp/真有效值。
- 返回采样率和测量模型：采集F407继续作为SPI1主机，在8208字节全双工帧
  的MISO前16字节读取SET_FS与五页测量模型。
- 输出：H743通过SPI2向主控F407发送自有88字节结果帧。
- 512点波形和256点频谱绘图数据不由H743计算或传输。

## 2. 文档阅读顺序

1. [TECHNICAL_ROUTE.md](TECHNICAL_ROUTE.md)：总体算法和系统路线。
2. [SPI_REFERENCE_AUDIT.md](SPI_REFERENCE_AUDIT.md)：从等效采样工程借鉴什么。
3. [COMMUNICATION_PROTOCOL.md](COMMUNICATION_PROTOCOL.md)：本项目的正式通信契约。
4. [references/REFERENCE_MANIFEST.md](references/REFERENCE_MANIFEST.md)：外部资料快照及校验值。

## 3. 已有代码状态

当前已经实现：

- 公共纯C11接口 `SignalAnalyzer_Process()`；
- AD9226十二位码掩码；
- `2055`零码和`0.005236 V/code`标定；
- 去直流、交流能量、端点码和高位异常检查；
- 固定4096点原位radix-2复数FFT便携后端；
- Hann窗RFFT候选检测：非峰中位数噪声底、12 dB门限、相邻去重、
  最多8个最强候选并按频率排序；
- FFT数学性质和候选行为的主机CMake/CTest测试；
- 129点直接局部CZT：Hann窗、递归复相量、double累加、log-power峰顶插值；
- CZT覆盖3种采样率、10~500 kHz、分数bin、无效输入和强谐波泄漏测试。
- 谐波结构选择：不把最大峰直接当基波，最多3个期望分量加1个高频干扰列；
- 联合正交拟合：double法方程、部分主元消元、`1e-10`枢轴比门限、
  统一峰值幅度/相位、残差与SNR；
- 联合拟合测试覆盖普通I/Q超过5 mV串扰、2 mV幅值和0.02 rad相位约束、
  基波非最强、1.1 MHz干扰、相位归一化和病态矩阵；
- 完整`SignalAnalyzer_Process()`编排：预处理、候选检测、CZT、选模、
  联合拟合、指标与状态映射；
- 只使用期望谐波的8192相位点Upp和解析真有效值；
- 固定1.50 V偏置下识别2048下限平台，只用未削顶样本联合拟合，并以平台
  不等式筛除错误模型；BNC结果已补偿前端2倍交流增益；
- 五页测量协议传输f0、Upp、Urms、状态、质量标志和最多3个整数次分量；
- F407仅在同一模型代号五页齐全后原子发布，并按峰值幅度/余弦相位重建波形；
- AD9226量化端到端测试覆盖10/100/500 kHz、1/2/3分量、固定伪随机相位、
  DC偏置、1.1 MHz干扰、采样率建议和带外模型；
- 独立`Protocol`静态库：CRC-32/ISO-HDLC和显式小端88字节RESULT编解码；
- 协议测试覆盖独立字面量帧、状态边界、数值饱和、空闲槽清零、逐区破坏、
  修复CRC后的语义拒绝以及失败不污染输出。

2026-08-01复核结果：

```text
H743 host Debug: 9/9 PASS
F407 host Debug: 2/2 PASS
H743 ARM GNU: build passed, .dma_buffer=0x30000000..0x3000405f
F407 ARM GNU: build passed
```

这证明算法、分页协议、F407模型绘图和两端固件可以通过主机/ARM构建。尚未证明：

- H743上的执行周期和实时余量；
- 未知上限高频干扰下的模拟抗混叠效果；
- SPI、READY、DMA和缓存配置；
- 实际硬件联调。

## 4. 工程边界

本次允许修改本目录以及以下F407采集/显示工程：

- `../../../AD9226_driver3/AD9226_driver`（相对于本 `docs` 目录）

以下旧工程仍为只读接口对端：

- 旧 F407 采集 SPI 主机工程（本地只读参考，未随仓库发布）
- 旧 F407 显示 SPI 从机工程（本地只读参考，未随仓库发布）
- 旧 H723 信号处理工程（本地只读参考，未随仓库发布）
- 等效采样工程（本地只读参考，未随仓库发布）

采集组的输入接口不能擅自改成等效采样工程的8220字节协议。主控F407若要
接收本项目的88字节结果帧，需要由主控组单独升级；本目录只实现H743侧和
无HAL依赖的协议编解码测试。

## 5. 下一步

下一步是完整实施计划Task 6。开始CubeMX前必须先取得并核对实际母板原理图，
确认SPI1的PA5/PA6/PA7、SPI2的PB13/PB15、PD0 ACQ_READY和PD1 MAIN_READY
是否可用；确认后在临时副本中生成工程、检查差异，再复制到`Firmware_H743`。
不要重新实现已通过测试的算法和协议，也不要修改旧H723/F407工程。

实现文件的SHA-256审查清单：

- [Task 1 RFFT](reviews/2026-07-31-rfft-task1.sha256)
- [Task 2 CZT](reviews/2026-07-31-czt-task2.sha256)
- [Task 3 joint fit](reviews/2026-07-31-joint-fit-task3.sha256)
- [Task 4 end-to-end](reviews/2026-07-31-end-to-end-task4.sha256)
- [Task 5 result protocol](reviews/2026-07-31-result-protocol-task5.sha256)
