#include "harmonic_model.h"

#include <math.h>
#include <stddef.h>

#define HARMONIC_MIN_DESIRED_HZ 10000.0
#define HARMONIC_MAX_DESIRED_HZ 500000.0
#define HARMONIC_MIN_INTERFERENCE_HZ 1000000.0
#define HARMONIC_MAX_FREQUENCY_ERROR_HZ 1000.0
#define HARMONIC_SCORE_EPSILON 1.0e-12
#define HARMONIC_MAX_DESIRED_TONES 3U

typedef struct
{
    int valid;
    uint8_t count;
    size_t peak_index[HARMONIC_MAX_DESIRED_TONES];
    double error_Hz;
    double power;
    double base_frequency_Hz;
} HarmonicChoice;

static int HarmonicModel_IsDesiredPeak(const CztRefinedPeak *peak)
{
    return (peak->valid != 0U)
        && isfinite(peak->frequency_Hz)
        && isfinite(peak->peak_power)
        && ((double)peak->frequency_Hz >= HARMONIC_MIN_DESIRED_HZ)
        && ((double)peak->frequency_Hz <= HARMONIC_MAX_DESIRED_HZ)
        && (peak->peak_power > 0.0f);
}

static int HarmonicModel_IsInterferencePeak(const CztRefinedPeak *peak)
{
    return (peak->valid != 0U)
        && isfinite(peak->frequency_Hz)
        && isfinite(peak->peak_power)
        && ((double)peak->frequency_Hz >= HARMONIC_MIN_INTERFERENCE_HZ)
        && (peak->peak_power > 0.0f);
}

static int HarmonicModel_MatchOrder(
    double base_frequency_Hz,
    double frequency_Hz,
    unsigned int *order,
    double *error_Hz)
{
    const double ratio = frequency_Hz / base_frequency_Hz;
    const double rounded_order = floor(ratio + 0.5);
    double predicted_frequency_Hz;

    if (!isfinite(ratio)
        || (rounded_order < 2.0)
        || (rounded_order > 50.0))
    {
        return 0;
    }

    predicted_frequency_Hz = rounded_order * base_frequency_Hz;
    *error_Hz = fabs(frequency_Hz - predicted_frequency_Hz);
    if (*error_Hz > HARMONIC_MAX_FREQUENCY_ERROR_HZ)
    {
        return 0;
    }

    *order = (unsigned int)rounded_order;
    return 1;
}

static int HarmonicModel_IsBetter(
    const HarmonicChoice *candidate,
    const HarmonicChoice *best)
{
    if (!best->valid)
    {
        return 1;
    }
    if (candidate->count != best->count)
    {
        return candidate->count > best->count;
    }
    if (candidate->error_Hz
        < (best->error_Hz - HARMONIC_SCORE_EPSILON))
    {
        return 1;
    }
    if (candidate->error_Hz
        > (best->error_Hz + HARMONIC_SCORE_EPSILON))
    {
        return 0;
    }
    if (candidate->power > (best->power + HARMONIC_SCORE_EPSILON))
    {
        return 1;
    }
    if (candidate->power < (best->power - HARMONIC_SCORE_EPSILON))
    {
        return 0;
    }
    return candidate->base_frequency_Hz < best->base_frequency_Hz;
}

static void HarmonicModel_Consider(
    const CztRefinedPeak peaks[SIGNAL_MAX_CANDIDATES],
    const size_t peak_index[HARMONIC_MAX_DESIRED_TONES],
    uint8_t count,
    double error_Hz,
    HarmonicChoice *best)
{
    HarmonicChoice candidate = {0};
    size_t index;

    candidate.valid = 1;
    candidate.count = count;
    candidate.error_Hz = error_Hz;
    candidate.base_frequency_Hz =
        (double)peaks[peak_index[0]].frequency_Hz;

    for (index = 0U; index < (size_t)count; ++index)
    {
        candidate.peak_index[index] = peak_index[index];
        candidate.power += (double)peaks[peak_index[index]].peak_power;
    }

    if (HarmonicModel_IsBetter(&candidate, best))
    {
        *best = candidate;
    }
}

static void HarmonicModel_SortDesired(HarmonicModel *model)
{
    size_t index;

    for (index = 1U; index < (size_t)model->tone_count; ++index)
    {
        const float frequency_Hz = model->frequency_Hz[index];
        const uint8_t desired = model->desired[index];
        size_t position = index;

        while ((position > 0U)
            && (model->frequency_Hz[position - 1U] > frequency_Hz))
        {
            model->frequency_Hz[position] =
                model->frequency_Hz[position - 1U];
            model->desired[position] = model->desired[position - 1U];
            --position;
        }
        model->frequency_Hz[position] = frequency_Hz;
        model->desired[position] = desired;
    }
}

bool HarmonicModel_Select(
    const CztRefinedPeak peaks[SIGNAL_MAX_CANDIDATES],
    size_t peak_count,
    HarmonicModel *model)
{
    HarmonicChoice best = {0};
    size_t base_index;
    size_t interference_index = 0U;
    int have_interference = 0;

    if (model == NULL)
    {
        return false;
    }
    *model = (HarmonicModel){0};

    if ((peaks == NULL)
        || (peak_count == 0U)
        || (peak_count > SIGNAL_MAX_CANDIDATES))
    {
        return false;
    }

    for (base_index = 0U; base_index < peak_count; ++base_index)
    {
        size_t selected[HARMONIC_MAX_DESIRED_TONES] = {base_index, 0U, 0U};
        size_t first_index;

        if (!HarmonicModel_IsDesiredPeak(&peaks[base_index]))
        {
            continue;
        }

        HarmonicModel_Consider(peaks, selected, 1U, 0.0, &best);
        for (first_index = 0U; first_index < peak_count; ++first_index)
        {
            unsigned int first_order;
            double first_error_Hz;
            size_t second_index;

            if ((first_index == base_index)
                || !HarmonicModel_IsDesiredPeak(&peaks[first_index])
                || !HarmonicModel_MatchOrder(
                    (double)peaks[base_index].frequency_Hz,
                    (double)peaks[first_index].frequency_Hz,
                    &first_order,
                    &first_error_Hz))
            {
                continue;
            }

            selected[1U] = first_index;
            HarmonicModel_Consider(
                peaks,
                selected,
                2U,
                first_error_Hz,
                &best);

            for (second_index = first_index + 1U;
                 second_index < peak_count;
                 ++second_index)
            {
                unsigned int second_order;
                double second_error_Hz;

                if ((second_index == base_index)
                    || !HarmonicModel_IsDesiredPeak(&peaks[second_index])
                    || !HarmonicModel_MatchOrder(
                        (double)peaks[base_index].frequency_Hz,
                        (double)peaks[second_index].frequency_Hz,
                        &second_order,
                        &second_error_Hz)
                    || (second_order == first_order))
                {
                    continue;
                }

                selected[2U] = second_index;
                HarmonicModel_Consider(
                    peaks,
                    selected,
                    3U,
                    first_error_Hz + second_error_Hz,
                    &best);
            }
        }
    }

    if (!best.valid)
    {
        return false;
    }

    model->tone_count = best.count;
    for (base_index = 0U; base_index < (size_t)best.count; ++base_index)
    {
        model->frequency_Hz[base_index] =
            peaks[best.peak_index[base_index]].frequency_Hz;
        model->desired[base_index] = 1U;
    }
    HarmonicModel_SortDesired(model);

    for (base_index = 0U; base_index < peak_count; ++base_index)
    {
        if (HarmonicModel_IsInterferencePeak(&peaks[base_index])
            && (!have_interference
                || (peaks[base_index].peak_power
                    > peaks[interference_index].peak_power)))
        {
            interference_index = base_index;
            have_interference = 1;
        }
    }

    if (have_interference
        && (model->tone_count < SIGNAL_MAX_FIT_TONES))
    {
        const size_t output_index = (size_t)model->tone_count;

        model->frequency_Hz[output_index] =
            peaks[interference_index].frequency_Hz;
        model->desired[output_index] = 0U;
        ++model->tone_count;
    }

    return true;
}