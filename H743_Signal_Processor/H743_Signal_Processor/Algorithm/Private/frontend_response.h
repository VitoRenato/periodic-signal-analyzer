#ifndef FRONTEND_RESPONSE_H
#define FRONTEND_RESPONSE_H

#include <stddef.h>

/* Relative to the existing 0.98 ADC gain trim. Both source fixtures and their
 * prior-firmware gain metadata are checked by tools/fit_frontend_response.py.
 * Range endpoints retain the user's reported intervals, not extra trials.
 * Only amplitude is calibrated; phase and raw fit diagnostics are unchanged.
 */
static inline float FrontendResponse_Gain(float frequency_Hz)
{
    static const struct
    {
        float frequency_Hz;
        float gain;
    } nodes[] = {
        {10000.0f, 1.05499742f},
        {50000.0f, 1.05457345f},
        {100000.0f, 1.05712180f},
        {101000.0f, 1.028172878f},
        {110000.0f, 1.030782211f},
        {124000.0f, 1.028593595f},
        {125000.0f, 1.032470353f},
        {140000.0f, 1.030116212f},
        {145000.0f, 1.033383984f},
        {200000.0f, 1.04111364f},
        {300000.0f, 1.05566149f},
        {400000.0f, 1.08138656f},
        {500000.0f, 1.12075896f}
    };
    const size_t count = sizeof(nodes) / sizeof(nodes[0]);
    if (frequency_Hz <= nodes[0].frequency_Hz)
    {
        return nodes[0].gain;
    }
    for (size_t index = 1U; index < count; ++index)
    {
        if (frequency_Hz <= nodes[index].frequency_Hz)
        {
            const float fraction = (frequency_Hz - nodes[index - 1U].frequency_Hz)
                / (nodes[index].frequency_Hz - nodes[index - 1U].frequency_Hz);
            return nodes[index - 1U].gain
                + fraction * (nodes[index].gain - nodes[index - 1U].gain);
        }
    }
    return nodes[count - 1U].gain;
}

#endif
