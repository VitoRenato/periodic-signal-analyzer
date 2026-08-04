#ifndef CZT_ZOOM_H
#define CZT_ZOOM_H

#include "signal_analyzer.h"

#include <stdint.h>

typedef struct
{
    float frequency_Hz;
    float peak_power;
    uint8_t valid;
} CztRefinedPeak;

CztRefinedPeak CztZoom_Refine(
    const float samples[SIGNAL_FRAME_SAMPLES],
    uint32_t sample_rate_Hz,
    float coarse_frequency_Hz);

#endif