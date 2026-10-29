#include "signal_analyzer.h"
#include "ad9226_calibration.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define PI 3.14159265358979323846
#define FS_HZ 2048780U
/* Fixed pre-trim conversion keeps these fixtures independent of the new coefficient. */
#define LEGACY_BNC_V_PER_CODE 0.0024605

static uint16_t samples[SIGNAL_FRAME_SAMPLES];
static unsigned failures;

static void near_value(const char *field, double expected, double actual, double tolerance)
{
    if (!isfinite(actual) || fabs(actual - expected) > tolerance)
    {
        fprintf(stderr, "FAIL %s expected=%.9f actual=%.9f tolerance=%.9f\n",
                field, expected, actual, tolerance);
        ++failures;
    }
}

static void bench_derived_sines(void)
{
    /* Reported mVpp pairs, not newly captured ADC frames. */
    static const double nominal[] = {100, 125, 200, 250, 300, 400, 500};
    static const double old_display[] = {101, 127, 204, 255, 307, 409, 509};
    for (size_t k = 0; k < sizeof(nominal) / sizeof(nominal[0]); ++k)
    {
        SignalResult result;
        for (size_t i = 0; i < SIGNAL_FRAME_SAMPLES; ++i)
        {
            const double voltage = old_display[k] * 0.0005
                * sin(2.0 * PI * 41000.0 * (double)i / FS_HZ + 0.31);
            samples[i] = (uint16_t)(2055L + lround(voltage / LEGACY_BNC_V_PER_CODE));
        }
        const SignalStatus status = SignalAnalyzer_Process(samples, FS_HZ, &result);
        if (status != SIGNAL_STATUS_VALID || result.component_count != 1U)
        {
            fprintf(stderr, "FAIL bench nominal=%.0f status=%u components=%u\n",
                    nominal[k], (unsigned)status, (unsigned)result.component_count);
            ++failures;
            continue;
        }
        const double target_V = nominal[k] * 0.001;
        near_value("bench Vpp", target_V, result.upp_V, 0.002);
        near_value("bench Vrms", target_V / (2.0 * sqrt(2.0)), result.urms_V, 0.001);
        near_value("bench peak", target_V / 2.0, result.component[0].amplitude_peak_V, 0.001);
        near_value("bench frequency", 41000.0, result.fundamental_Hz, 100.0);
        printf("bench-derived nominal=%.0f old=%.0f result_mVpp=%.3f rms_mV=%.3f peak_mV=%.3f f_Hz=%.3f\n",
               nominal[k], old_display[k], result.upp_V * 1000.0,
               result.urms_V * 1000.0, result.component[0].amplitude_peak_V * 1000.0,
               (double)result.fundamental_Hz);
    }
}

static void calibrated_range_and_harmonics(void)
{
    /* Explicit assumed post-trim input sensitivity, not a measured frequency response. */
    const double reference_V_per_code = 0.00241129;
    static const double frequency[] = {10000, 41000, 200000, 500000};
    static const double vpp[] = {0.050, 0.100, 0.250};
    for (size_t f = 0; f < sizeof(frequency) / sizeof(frequency[0]); ++f)
    {
        for (size_t a = 0; a < sizeof(vpp) / sizeof(vpp[0]); ++a)
        {
            SignalResult result;
            for (size_t i = 0; i < SIGNAL_FRAME_SAMPLES; ++i)
            {
                const double v = vpp[a] * 0.5 * sin(2.0 * PI * frequency[f] * (double)i / FS_HZ + 0.31);
                samples[i] = (uint16_t)(2055L + lround(v / reference_V_per_code));
            }
            if (SignalAnalyzer_Process(samples, FS_HZ, &result) != SIGNAL_STATUS_VALID)
            {
                fprintf(stderr, "FAIL range f=%.0f vpp=%.3f status=%u\n", frequency[f], vpp[a], (unsigned)result.status);
                ++failures;
                continue;
            }
            near_value("range Vpp", vpp[a], result.upp_V, 0.002);
            near_value("range Vrms", vpp[a] / (2.0 * sqrt(2.0)), result.urms_V, 0.001);
            near_value("range peak", vpp[a] / 2.0, result.component[0].amplitude_peak_V, 0.001);
            near_value("range frequency", frequency[f], result.fundamental_Hz, 100.0);
        }
    }
    SignalResult result;
    const double peak[] = {0.040, 0.020, 0.010};
    const double freq[] = {10500, 31500, 42000};
    for (size_t i = 0; i < SIGNAL_FRAME_SAMPLES; ++i)
    {
        double v = 0.0;
        for (size_t k = 0; k < 3; ++k)
            v += peak[k] * sin(2.0 * PI * freq[k] * (double)i / FS_HZ + 0.2 * (double)k);
        samples[i] = (uint16_t)(2055L + lround(v / reference_V_per_code));
    }
    if (SignalAnalyzer_Process(samples, FS_HZ, &result) != SIGNAL_STATUS_VALID || result.component_count != 3U)
    {
        fprintf(stderr, "FAIL harmonic status=%u components=%u\n", (unsigned)result.status, (unsigned)result.component_count);
        ++failures;
        return;
    }
    near_value("harmonic true RMS", sqrt((0.040*0.040 + 0.020*0.020 + 0.010*0.010)/2.0), result.urms_V, 0.0005);
    for (size_t k = 0; k < 3; ++k)
    {
        near_value("harmonic peak", peak[k], result.component[k].amplitude_peak_V, 0.0005);
        near_value("harmonic frequency", freq[k], result.component[k].frequency_Hz, 100.0);
    }
}

int main(void)
{
    near_value("zero unchanged", 0.0, Ad9226Calibration_CodeToBncV(2055U), 1e-8);
    near_value("positive scale", 0.241129, Ad9226Calibration_CodeToBncV(2155U), 1e-6);
    near_value("negative scale", -0.241129, Ad9226Calibration_CodeToBncV(1955U), 1e-6);
    bench_derived_sines();
    calibrated_range_and_harmonics();
    printf("voltage_calibration_tests failures=%u\n", failures);
    return failures ? 1 : 0;
}
