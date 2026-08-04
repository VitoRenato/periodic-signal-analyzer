# EDC2026_Screen 协议发送端说明

## 修改点

`Core/Src/disp_uart.c` 已改为发送 `ScreenDataFrame`(与 EDC2026_Screen 二进制兼容)。

调用方式:

```c
#include "disp_uart.h"

int16_t samples[256];   /* 单周期 256 点,signed -2048..2047 */
...
DispUart_SendWaveform(samples, 256, seq, f0_hz);
```

## 协议字段

- `magic` = 0x32434445 ("EDC2")
- `byte_len` = 1084
- `frequency_mhz` = f0_hz * 1000
- `vpp_uv` / `vrms_uv`: 由 signed 波形自动换算(假设 3.3V 12bit ADC,1 LSB = 806uV)
- `wave[]`: signed + 2048 → uint16 0..4095
- `fft_count` = 0,fft[] = 0
- `wave_periods` = 3(默认),mode = 0(wave)
- `checksum` = sum32(前 1080 字节)

## 注意

- 一帧 1084 字节,921600 波特率下约 9.4ms,建议刷新率 <= 30~50Hz。
- 若上一次 DMA 未完成则返回 HAL_BUSY,不阻塞。
