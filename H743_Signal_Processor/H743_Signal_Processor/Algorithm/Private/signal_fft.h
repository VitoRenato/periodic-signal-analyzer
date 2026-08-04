#ifndef SIGNAL_FFT_H
#define SIGNAL_FFT_H

#include "signal_analyzer.h"

typedef struct
{
    float real;
    float imag;
} SignalFftComplex;

/**
 * @brief 对固定4096点复数序列执行未归一化的原位前向FFT。
 */
void SignalFft_Forward4096(
    SignalFftComplex data[SIGNAL_FRAME_SAMPLES]);

#endif