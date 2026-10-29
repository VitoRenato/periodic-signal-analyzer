#include "measurement_stability.h"
#include <stddef.h>

void MeasurementStability_Reset(MeasurementStability *state)
{
    if (state != NULL) *state = (MeasurementStability){0};
}

static bool close_value(uint32_t a, uint32_t b, uint32_t floor)
{
    const uint32_t difference = a > b ? a-b : b-a;
    uint32_t tolerance = (a > b ? a : b) / 50U;
    if (tolerance < floor) tolerance = floor;
    return difference <= tolerance;
}

static bool same_signal(const H743MeasurementModel *a, const H743MeasurementModel *b)
{
    const uint32_t difference = a->fundamental_Hz > b->fundamental_Hz
        ? a->fundamental_Hz-b->fundamental_Hz : b->fundamental_Hz-a->fundamental_Hz;
    if (a->analysis_sample_rate_Hz != b->analysis_sample_rate_Hz
        || a->request_generation != b->request_generation
        || a->component_count != b->component_count
        || a->status != b->status || a->quality_flags != b->quality_flags
        || difference > 500U
        || !close_value(a->upp_uV,b->upp_uV,3000U)
        || !close_value(a->urms_uV,b->urms_uV,2000U)) return false;
    for (uint8_t k=0U;k<a->component_count;++k)
    {
        if (a->component[k].order != b->component[k].order
            || !close_value(a->component[k].amplitude_peak_uV,
                            b->component[k].amplitude_peak_uV,2000U)) return false;
    }
    /* Absolute phase changes between untriggered frames and is not a stability test. */
    return true;
}

bool MeasurementStability_Observe(MeasurementStability *state,
                                  const H743MeasurementModel *model,
                                  bool displayable)
{
    if (state == NULL || model == NULL) return false;
    if (state->have_sequence && state->last_sequence == model->published_sequence
        && state->last_generation == model->request_generation) return false;
    state->last_sequence = model->published_sequence;
    state->last_generation = model->request_generation;
    state->have_sequence = true;
    if (!displayable)
    {
        state->consecutive = 0U;
        return false;
    }
    const bool matches = state->consecutive != 0U && same_signal(&state->previous,model);
    state->previous = *model;
    state->consecutive = matches ? 2U : 1U;
    return state->consecutive >= 2U;
}
