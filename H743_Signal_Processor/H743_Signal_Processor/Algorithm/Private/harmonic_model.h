#ifndef HARMONIC_MODEL_H
#define HARMONIC_MODEL_H

#include "czt_zoom.h"
#include "spectrum_detect.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SIGNAL_MAX_FIT_TONES 4U

typedef struct
{
    uint8_t tone_count;
    float frequency_Hz[SIGNAL_MAX_FIT_TONES];
    uint8_t desired[SIGNAL_MAX_FIT_TONES];
} HarmonicModel;

typedef double (*HarmonicModelScorer)(const HarmonicModel *model, void *context);

bool HarmonicModel_SelectScored(
    const CztRefinedPeak peaks[SIGNAL_MAX_CANDIDATES],
    size_t peak_count,
    HarmonicModelScorer scorer,
    void *context,
    HarmonicModel *model);

bool HarmonicModel_Select(
    const CztRefinedPeak peaks[SIGNAL_MAX_CANDIDATES],
    size_t peak_count,
    HarmonicModel *model);

#endif