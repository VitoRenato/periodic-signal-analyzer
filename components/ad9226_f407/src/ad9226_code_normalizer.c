/*
 * 可选兼容模块：用于项目中曾观察到的低8位连续码恢复。
 * 获取完整12位并行数据时不应默认启用。
 */
#include "ad9226_code_normalizer.h"

#include <limits.h>

#define AD9226_CODE_MAX 4095L
#define AD9226_MODULUS 256L
#define AD9226_HALF_MODULUS 128L

static int32_t Ad9226CodeNormalizer_NearestBranch(
    uint8_t low_byte,
    int32_t reference)
{
    int32_t candidate = (reference & ~255L) | (int32_t)low_byte;
    const int32_t delta = candidate - reference;

    if (delta > 127L)
    {
        candidate -= AD9226_MODULUS;
    }
    else if (delta < -128L)
    {
        candidate += AD9226_MODULUS;
    }
    return candidate;
}

static int32_t Ad9226CodeNormalizer_Abs(int32_t value)
{
    return (value < 0L) ? -value : value;
}

static int32_t Ad9226CodeNormalizer_RoundShiftSteps(
    int64_t centered_sum,
    int64_t sample_count)
{
    const int64_t denominator = (int64_t)AD9226_MODULUS * sample_count;

    if (centered_sum >= 0)
    {
        return (int32_t)((centered_sum + denominator / 2) / denominator);
    }
    return -(int32_t)((-centered_sum + denominator / 2) / denominator);
}

bool Ad9226CodeNormalizer_Run(
    uint16_t *samples,
    size_t sample_count,
    uint16_t anchor_code,
    Ad9226CodeNormalizeStats *stats)
{
    int64_t sum = 0;
    int32_t previous = 0;
    int32_t shift_steps;
    int32_t frame_shift;
    size_t index;

    if (stats != NULL)
    {
        *stats = (Ad9226CodeNormalizeStats){0};
    }
    if ((samples == NULL)
        || (stats == NULL)
        || (sample_count == 0U)
        || (sample_count > (size_t)INT32_MAX)
        || (anchor_code > (uint16_t)AD9226_CODE_MAX))
    {
        return false;
    }

    stats->raw_min = UINT16_MAX;
    stats->normalized_min = UINT16_MAX;
    for (index = 0U; index < sample_count; ++index)
    {
        const uint16_t raw = samples[index];
        const int32_t reference = (index == 0U)
            ? (int32_t)anchor_code
            : previous;
        const int32_t candidate = Ad9226CodeNormalizer_NearestBranch(
            (uint8_t)(raw & 0xffU),
            reference);
        const int32_t step = Ad9226CodeNormalizer_Abs(candidate - reference);

        if (raw < stats->raw_min) stats->raw_min = raw;
        if (raw > stats->raw_max) stats->raw_max = raw;
        if (step > (int32_t)stats->max_step)
        {
            stats->max_step = (uint16_t)step;
        }
        if ((step == AD9226_HALF_MODULUS)
            || (candidate < 0L)
            || (candidate > AD9226_CODE_MAX))
        {
            stats->ambiguous = 1U;
            return false;
        }

        samples[index] = (uint16_t)candidate;
        previous = candidate;
        sum += (int64_t)candidate;
    }

    shift_steps = Ad9226CodeNormalizer_RoundShiftSteps(
        (int64_t)anchor_code * (int64_t)sample_count - sum,
        (int64_t)sample_count);
    frame_shift = shift_steps * (int32_t)AD9226_MODULUS;
    if ((frame_shift < (int32_t)INT16_MIN)
        || (frame_shift > (int32_t)INT16_MAX))
    {
        stats->ambiguous = 1U;
        return false;
    }

    for (index = 0U; index < sample_count; ++index)
    {
        const int32_t normalized = (int32_t)samples[index] + frame_shift;

        if ((normalized < 0L) || (normalized > AD9226_CODE_MAX))
        {
            stats->ambiguous = 1U;
            return false;
        }
        samples[index] = (uint16_t)normalized;
        if (samples[index] < stats->normalized_min)
        {
            stats->normalized_min = samples[index];
        }
        if (samples[index] > stats->normalized_max)
        {
            stats->normalized_max = samples[index];
        }
    }
    stats->frame_shift = (int16_t)frame_shift;
    return true;
}
