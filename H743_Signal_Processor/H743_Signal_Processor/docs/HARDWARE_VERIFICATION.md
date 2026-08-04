# H743 Signal Processor 硬件验证记录

更新日期：2026-08-01

## 1. 证据状态

| 类别 | 当前状态 | 可证明内容 |
|---|---|---|
| Windows主机算法/协议 | H743 9/9、F407 2/2 已通过 | C11算法和纯协议逻辑 |
| ARM构建与map | H743/F407 ARM GNU 已通过 | 可编译、内存布局、固件大小 |
| SPI/READY/DMA实板 | 未验证 | 必须连接两块F407和逻辑分析仪 |
| 测量精度实板 | 未验证 | 必须使用校准信号源和实测采样率 |

不得用ARM构建成功替代SPI波形或测量精度证据。

## 2. 采集输入验证

待执行：

- F407在8208字节全双工帧的样本区依次发送`0x000、0x001、0x807、0xFFE、0xFFF`和递增计数图样；
- H743调试器逐点比较全部4096个`g_dma_acquisition_samples`；
- 确认bit12~bit15始终为0；
- 逻辑分析仪同时捕获SCK、MOSI、MISO和PD0 ACQ_READY；
- 验证PD0只在8208字节TxRx DMA已经挂载后升高，完成/错误后立即降低。

## 3. SET_FS验证

待执行：

| 请求值/Hz | F407分频值 | 预期整数实际值/Hz | PE9实测值 | CRC/帧 |
|---:|---:|---:|---:|---|
| 2048780 | 82 | 2048780 | 待测 | 待测 |
| 5000000 | 33 | 5090909 | 待测 | 待测 |
| 10000000 | 16 | 10500000 | 待测 | 待测 |
| 20000000 | 8 | 21000000 | 待测 | 待测 |

F407必须在PD0升高后交换恰好8208字节，并从MISO前16字节读取SET_FS分页
命令槽。连续五帧页号应为0、1、2、3、4，模型代号一致；第五页后F407才
原子发布新模型。

## 4. RESULT验证

待执行：

- 主控F407先挂载恰好88字节RX DMA，再拉高PD1 MAIN_READY；
- 对比H743调试值与接收帧中的序号、Fs、状态、频率、幅值、相位和flags；
- 独立重算CRC-32/ISO-HDLC；
- 捕获PB13 SCK、PB15 MOSI和PD1；
- 在接收端测试路径翻转一位，必须整帧拒绝；
- 主控不拉高READY时，确认H743在约100 ms后累计超时并继续下一帧。

## 5. 测量精度验证

待执行。每个点记录：

```text
信号源设定、信号源实测、PE9采样率、H743输出、
频率误差、幅值误差、Upp误差、Urms误差、状态、残差、SNR
```

固定输入偏置1.50 V，BNC输入250 mVpp经前端2倍放大后ADC交流量约500 mVpp。
覆盖10~500 kHz、1/2/3分量、默认采样率和高采样率，并加入题目要求的
200 mVpp高频干扰。每组保存原始码min/max/mean、2048平台比例、有效样本
比例、拟合残差、削顶违反量和最终BNC指标。只有实测满足幅值绝对误差
≤5 mV、频率绝对误差≤1 kHz，才能宣称赛题精度完成；主机仿真和ARM构建
不能替代这项证据。

建议在`ProcessorApp_RunOnce()`完成算法并载入pager后观察：

```text
result.status
result.quality_flags
result.fundamental_Hz
result.upp_V
result.urms_V
result.component_count
result.component[0]
result.component[1]
result.component[2]
g_diagnostics.telemetry_page
g_diagnostics.telemetry_model_id
g_diagnostics.recovered_frame_count
g_diagnostics.unrecoverable_clip_count
```
