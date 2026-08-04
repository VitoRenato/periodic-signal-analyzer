#include "signal_fft.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#define TEST_PI 3.14159265358979323846

static SignalFftComplex g_fft_data[SIGNAL_FRAME_SAMPLES];
static unsigned int g_failures;

#define CHECK_TRUE(condition)                                                   \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            (void)fprintf(                                                      \
                stderr,                                                         \
                "FAIL %s:%d: %s\n",                                           \
                __FILE__,                                                       \
                __LINE__,                                                       \
                #condition);                                                    \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

#define CHECK_NEAR(expected, actual, tolerance)                                 \
    do                                                                          \
    {                                                                           \
        const double expected_value = (double)(expected);                       \
        const double actual_value = (double)(actual);                           \
        if (fabs(actual_value - expected_value) > (double)(tolerance))          \
        {                                                                       \
            (void)fprintf(                                                      \
                stderr,                                                         \
                "FAIL %s:%d: expected %.12g, got %.12g\n",                   \
                __FILE__,                                                       \
                __LINE__,                                                       \
                expected_value,                                                 \
                actual_value);                                                  \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

static void clear_fft_data(void)
{
    size_t index;

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        g_fft_data[index].real = 0.0f;
        g_fft_data[index].imag = 0.0f;
    }
}

static void test_impulse_has_unit_value_in_every_bin(void)
{
    size_t index;

    clear_fft_data();
    g_fft_data[0].real = 1.0f;

    SignalFft_Forward4096(g_fft_data);

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        CHECK_NEAR(1.0, g_fft_data[index].real, 1.0e-6);
        CHECK_NEAR(0.0, g_fft_data[index].imag, 1.0e-6);
    }
}

static void test_bin_centered_complex_tone_lands_in_one_bin(void)
{
    const size_t tone_bin = 37U;
    double leakage_energy = 0.0;
    size_t index;

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        const double phase = 2.0 * TEST_PI * (double)tone_bin
            * (double)index / (double)SIGNAL_FRAME_SAMPLES;

        g_fft_data[index].real = (float)cos(phase);
        g_fft_data[index].imag = (float)sin(phase);
    }

    SignalFft_Forward4096(g_fft_data);

    CHECK_NEAR(
        (double)SIGNAL_FRAME_SAMPLES,
        g_fft_data[tone_bin].real,
        0.01);
    CHECK_NEAR(0.0, g_fft_data[tone_bin].imag, 0.01);

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        if (index != tone_bin)
        {
            const double real = (double)g_fft_data[index].real;
            const double imag = (double)g_fft_data[index].imag;
            leakage_energy += (real * real) + (imag * imag);
        }
    }

    CHECK_TRUE(leakage_energy < 0.01);
}

static void test_parseval_energy_is_preserved(void)
{
    double time_energy = 0.0;
    double frequency_energy = 0.0;
    size_t index;

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        const double phase_17 = 2.0 * TEST_PI * 17.0 * (double)index
            / (double)SIGNAL_FRAME_SAMPLES;
        const double phase_91 = 2.0 * TEST_PI * 91.0 * (double)index
            / (double)SIGNAL_FRAME_SAMPLES;
        const double sample = 0.25
            + (0.50 * cos(phase_17))
            + (0.20 * sin(phase_91));

        g_fft_data[index].real = (float)sample;
        g_fft_data[index].imag = 0.0f;
        time_energy += sample * sample;
    }

    SignalFft_Forward4096(g_fft_data);

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        const double real = (double)g_fft_data[index].real;
        const double imag = (double)g_fft_data[index].imag;
        frequency_energy += (real * real) + (imag * imag);
    }

    CHECK_NEAR(
        time_energy,
        frequency_energy / (double)SIGNAL_FRAME_SAMPLES,
        time_energy * 1.0e-5);
}

static void test_real_input_has_conjugate_symmetric_spectrum(void)
{
    size_t index;

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        const double phase_13 = 2.0 * TEST_PI * 13.0 * (double)index
            / (double)SIGNAL_FRAME_SAMPLES;
        const double phase_211 = 2.0 * TEST_PI * 211.0 * (double)index
            / (double)SIGNAL_FRAME_SAMPLES;

        g_fft_data[index].real =
            (float)((0.75 * cos(phase_13)) + (0.125 * sin(phase_211)));
        g_fft_data[index].imag = 0.0f;
    }

    SignalFft_Forward4096(g_fft_data);

    CHECK_NEAR(0.0, g_fft_data[0U].imag, 1.0e-5);
    CHECK_NEAR(
        0.0,
        g_fft_data[SIGNAL_FRAME_SAMPLES / 2U].imag,
        1.0e-5);

    for (index = 1U; index < (SIGNAL_FRAME_SAMPLES / 2U); ++index)
    {
        const size_t mirror = SIGNAL_FRAME_SAMPLES - index;

        CHECK_NEAR(
            g_fft_data[index].real,
            g_fft_data[mirror].real,
            1.0e-4);
        CHECK_NEAR(
            g_fft_data[index].imag,
            -g_fft_data[mirror].imag,
            1.0e-4);
    }
}

int main(void)
{
    test_impulse_has_unit_value_in_every_bin();
    test_bin_centered_complex_tone_lands_in_one_bin();
    test_parseval_energy_is_preserved();
    test_real_input_has_conjugate_symmetric_spectrum();

    if (g_failures != 0U)
    {
        (void)fprintf(stderr, "%u FFT test(s) failed\n", g_failures);
        return 1;
    }

    (void)puts("signal_fft_tests: PASS");
    return 0;
}