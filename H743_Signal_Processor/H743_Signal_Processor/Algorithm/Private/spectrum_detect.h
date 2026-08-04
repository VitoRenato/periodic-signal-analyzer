#ifndef SPECTRUM_DETECT_H
#define SPECTRUM_DETECT_H

#include "signal_analyzer.h"

#include <stddef.h>
#include <stdint.h>

#define SIGNAL_MAX_CANDIDATES 8U

typedef struct
{
    uint16_t bin;
    float coarse_frequency_Hz;
    float power;
    float snr_dB;
} SpectrumCandidate;

size_t SpectrumDetect_Find(
    const float samples[SIGNAL_FRAME_SAMPLES],
    uint32_t sample_rate_Hz,
    float minimum_frequency_Hz,
    float maximum_frequency_Hz,
    SpectrumCandidate candidates[SIGNAL_MAX_CANDIDATES]);

#endif