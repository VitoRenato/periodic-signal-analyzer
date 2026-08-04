# 第三方材料与许可边界

根目录 MIT License 只适用于项目贡献者原创且没有另行声明许可证的文件。它不改变、替代或重新授权本仓库中的任何第三方代码、生成模板或文档；第三方文件的原始文件头、目录许可证和随附条款优先。

## STMicroelectronics

STM32Cube HAL、STM32 设备支持文件以及 CubeMX 生成模板保留 STMicroelectronics 的原始文件头和许可条件。主要随附许可文件包括：

- `AD9226_driver3/AD9226_driver/Drivers/STM32F4xx_HAL_Driver/LICENSE.txt`
- `AD9226_driver3/AD9226_driver/Drivers/CMSIS/Device/ST/STM32F4xx/LICENSE.txt`
- `H743_Signal_Processor/H743_Signal_Processor/Firmware_H743/Drivers/STM32H7xx_HAL_Driver/LICENSE.txt`
- `H743_Signal_Processor/H743_Signal_Processor/Firmware_H743/Drivers/CMSIS/Device/ST/STM32H7xx/LICENSE.txt`

使用或再分发这些文件时，应同时核对文件头和相应目录中的实际许可文本，而不是只依据根目录 MIT License。

## Arm CMSIS

CMSIS 文件保留其原版权和许可证。本仓库所带 CMSIS 许可入口包括：

- `AD9226_driver3/AD9226_driver/Drivers/CMSIS/LICENSE.txt`
- `H743_Signal_Processor/H743_Signal_Processor/Firmware_H743/Drivers/CMSIS/LICENSE.txt`

CMSIS 子目录中还可能包含具有独立许可的组件；使用者应遵守最接近该组件的许可证与文件头。

## 康威科技 AD9226 V1.4 模块兼容性说明

本项目的采集适配曾使用康威科技 AD9226 V1.4 模块。`components/ad9226_f407` 是项目贡献者编写的社区驱动，不是康威科技官方软件；厂商名称和产品名称仅用于说明硬件兼容性，不表示厂商授权、认可、赞助或从属关系。

公开仓库不包含康威科技或其他厂商的原理图、PCB 设计文件、PDF 手册，也不包含从这些资料直接复制的图片。模块接线、电平、供电和安全限制应由使用者依据合法取得的对应硬件资料自行核对。

## 其他第三方内容

其他第三方组件按其所在目录的许可证、NOTICE 和源码文件头使用。如果某个文件已单独声明许可，根目录 MIT License 不适用于该文件。
