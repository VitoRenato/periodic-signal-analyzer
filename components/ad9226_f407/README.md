# AD9226 F407 community driver

这是一个社区驱动，非康威科技官方软件。它面向康威科技 AD9226 V1.4 模块的 STM32F407/HAL 集成；将它用于其他板卡前，必须自行核对数字电平、供电要求和全部引脚连接。

本目录不包含厂商 PDF、原理图或 PCB 材料。驱动仅提供定时器触发的 GPIO 并行采样封装，应用仍应根据自己的板卡、电气条件和测量要求完成验证。

## CubeMX configuration checklist

- 配置 `GPIOC` 的 `PC0..PC11` 为输入，用作 AD9226 12 位并行数据；DMA 源地址为 `GPIOC->IDR`。
- 配置 `PE9` 为 `TIM1_CH1`，由 TIM1 产生采样时钟。
- 启用 TIM1 update DMA request，并把对应的 CubeMX DMA 句柄接到 `hdma_tim1_up`。
- DMA 使用 **Normal** 模式：源为 `GPIOC->IDR`，目标为 `uint16_t` 采样缓冲区。`App_Ad9226Init()` 会把完成回调显式注册到匹配的 `hdma_tim1_up`：支持注册 API 的 HAL 使用 `HAL_DMA_RegisterCallback(..., HAL_DMA_XFER_CPLT_CB_ID, ...)`；旧版 HAL 则绑定该句柄的 `XferCpltCallback`。只定义全局 `HAL_DMA_XferCpltCallback()` 不能保证直接 `HAL_DMA_Start_IT` 路径会调用应用代码。
- 示例按 168 MHz TIM1 时钟、5 kHz 至 20 MHz 请求采样率和 4096 点缓冲区配置。实际时钟树、DMA stream/通道和允许采样率必须与项目配置一致。

`examples/stm32cube_hal_example.c` 展示调用方拥有静态 `uint16_t[4096]` 缓冲区和 `Ad9226F407` 对象的最小接入。生产工程将该文件加入自己的应用目标，并保留 CubeMX 生成的 `htim1`、`hdma_tim1_up` 定义。

## Required call order

按下列顺序调用：

1. `Ad9226F407_Init`
2. 注册匹配的 DMA 完成回调
3. `Ad9226F407_SetSampleRate`
4. `Ad9226F407_Start`
5. 已注册的 DMA 回调中调用 `Ad9226F407_OnDmaComplete`，仅置完成标志
6. 主循环调用 `App_Ad9226Poll()`；它调用 `Ad9226F407_GetSamples`，并将一帧交给 `App_Ad9226SamplesReady`

示例在 `App_Ad9226Init()` 中完成第 1、2 步，在 `App_Ad9226Start()` 中完成第 3、4 步。已注册回调只对匹配的 `hdma_tim1_up` 完成帧并置待处理标志；`App_Ad9226Poll()` 会先领取该标志，因此每帧至多交付一次。不要跳过 DMA 回调注册，也不要在驱动仍忙碌或尚有待处理帧时再次启动。

## Lifetime and recovery contract

- `Ad9226F407` 必须在首次 `Init` 前为零初始化。已初始化对象（包括正在采集、已完成但尚未 `Stop` 的对象）不能再次 `Init`；先在**任务上下文**调用 `Ad9226F407_Stop`，待其成功关闭 PWM 后才可重新 `Init`。这样不会用新配置覆盖仍在运行的 DMA 帧。
- `Ad9226F407_AbortFrame` 只能在任务上下文调用。它先关闭 TIM DMA request，再调用 `HAL_DMA_Abort`；成功后对象保持已初始化、空闲状态，可再次 `Start`。如果 HAL abort 失败，对象保持 `recovery_pending`，`Start` 被拒绝；修复 HAL/外设条件后重试 `AbortFrame`。
- DMA 完成和错误回调都可调用相应的 `OnDma...` 函数。`OnDmaComplete` 只关闭 request 并发布完成状态。`OnDmaError` 只关闭 request、清除当前帧并发布恢复待办；两者都不在 ISR 内调用可能阻塞的 HAL abort/stop。错误回调后由主循环调用 `Ad9226F407_AbortFrame`（示例为 `App_Ad9226Recover`）完成恢复，再开始下一帧。
- `Ad9226F407_Stop` 是任务上下文 API：必要时先中止帧，再关闭 TIM PWM，并将对象恢复为可 `Init` 的未初始化状态。`App_Ad9226Init` 若 DMA 回调注册失败会调用它回滚已经启动的 PWM/驱动状态。

同一对象的 `Init`、`Start`、`AbortFrame`、`Stop` 和 `SetSampleRate` 应由一个任务串行调用；DMA 回调只调用 `OnDmaComplete` 或 `OnDmaError`。主机测试验证状态机和 HAL 调用顺序，但不替代实板 DMA/时钟验证。

## Optional code normalizer

`ad9226_code_normalizer` 默认不启用。它是针对某些历史低 8 位连续码观察现象的可选兼容模块；对于正确获取的完整 12 位并行数据，默认使用它会改变原始码值，因而不应作为正常采样路径的一部分。

## Host compile and tests

在 VS Code 中，使用 CMake Tools 将源目录设为 `components/ad9226_f407/tests`、构建目录设为 `work/ad9226_component/host`，执行 Configure 后运行 Build；CTest 可从 Testing 视图或下列终端命令运行。

```powershell
cmake -S .\components\ad9226_f407\tests -B .\work\ad9226_component\host -G Ninja
cmake --build .\work\ad9226_component\host --clean-first
ctest --test-dir .\work\ad9226_component\host --output-on-failure
```

主机检查会把示例作为 OBJECT target 编译，并使用 fake HAL 句柄替代 CubeMX 的 `extern` 句柄，因此能持续检查示例语法和公共接口。主机测试通过不等于实板采样正确：它不能证明 `PE9` 时钟波形、GPIO 数据时序、DMA 请求/数据宽度、电平与供电匹配，或采样码值与测量精度。

## License

本组件以 [MIT License](LICENSE) 发布。
