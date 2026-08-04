#ifndef AD9226_CALIBRATION_H
#define AD9226_CALIBRATION_H

#include <stdint.h>

/*
 * 当前系数描述 ADC 前端电压，而赛题要求显示 BNC 输入端电压。
 * 板上交流通道增益已经确认为 2 倍，因此所有算法输出统一除以该增益。
 * 0.005236 V/code 仍需通过 BNC 端两点法做最终实板精校。
 */
#define AD9226_ZERO_CODE          2055
#define AD9226_ADC_V_PER_CODE     0.004921f
#define AD9226_FRONTEND_AC_GAIN   2.0f
#define AD9226_BNC_V_PER_CODE \
    (AD9226_ADC_V_PER_CODE / AD9226_FRONTEND_AC_GAIN)

static inline float Ad9226Calibration_CodeToBncV(uint16_t code)
{
    return (float)((int32_t)code - AD9226_ZERO_CODE)
        * AD9226_BNC_V_PER_CODE;
}

#endif
