#ifndef SIGNAL_METRICS_H
#define SIGNAL_METRICS_H

#include "harmonic_model.h"
#include "orthogonal_fit.h"
#include "signal_analyzer.h"

#include <stdbool.h>

bool SignalMetrics_Populate(
    const HarmonicModel *model,
    const OrthogonalFitResult *fit,
    SignalResult *result);

bool SignalMetrics_PopulateCalibrated(
    const HarmonicModel *model,
    const OrthogonalFitResult *fit,
    SignalResult *result);

#endif