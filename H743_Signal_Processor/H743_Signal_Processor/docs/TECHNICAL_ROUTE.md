# H743 测量算法

算法输入是 4096 个 `uint16_t` 采样码及该帧的实际采样率。正常测量把码值换算到输入端电压，识别基频并拟合基波与选中的谐波。`signal_analyzer.c` 组织分析，正常路径根据拟合残差和模型复杂度选择候选。削顶输入按独立路径尝试恢复，并通过结果状态与质量标志说明恢复情况。

正式显示模型最多含三个正弦分量，基波包含在内。H743 先以未经频响补偿的残差检查拟合质量，再对各分量峰值施加频率相关的幅度修正。设分量为 $A_k\cos(k\theta+\phi_k)$，则峰峰值为同一周期内合成曲线的最大值减最小值，交流有效值为 $\sqrt{\sum_k A_k^2/2}$。模型外的分量不计入这两个结果。

算法入口为 `Algorithm/Src/signal_analyzer.c`，测量值计算见 `Algorithm/Src/signal_metrics.c`，标定系数见 `Algorithm/Private/ad9226_calibration.h` 和 `frontend_response.h`。输出由 [SPI1 回传协议](COMMUNICATION_PROTOCOL.md) 送回 F407。
