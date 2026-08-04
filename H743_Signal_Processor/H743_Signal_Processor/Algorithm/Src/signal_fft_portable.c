#include "signal_fft.h"

#include <math.h>
#include <stddef.h>

#define SIGNAL_FFT_PI 3.14159265358979323846

static void SignalFft_BitReverse(
    SignalFftComplex data[SIGNAL_FRAME_SAMPLES])
{
    size_t reversed = 0U;
    size_t index;

    for (index = 1U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        size_t bit = SIGNAL_FRAME_SAMPLES >> 1U;

        while ((reversed & bit) != 0U)
        {
            reversed ^= bit;
            bit >>= 1U;
        }
        reversed ^= bit;

        if (index < reversed)
        {
            const SignalFftComplex temporary = data[index];

            data[index] = data[reversed];
            data[reversed] = temporary;
        }
    }
}

void SignalFft_Forward4096(
    SignalFftComplex data[SIGNAL_FRAME_SAMPLES])
{
    size_t transform_length;

    SignalFft_BitReverse(data);

    for (transform_length = 2U;
         transform_length <= SIGNAL_FRAME_SAMPLES;
         transform_length <<= 1U)
    {
        const size_t half_length = transform_length >> 1U;
        const double angle = -2.0 * SIGNAL_FFT_PI
            / (double)transform_length;
        const double twiddle_step_real = cos(angle);
        const double twiddle_step_imag = sin(angle);
        size_t block_start;

        for (block_start = 0U;
             block_start < SIGNAL_FRAME_SAMPLES;
             block_start += transform_length)
        {
            double twiddle_real = 1.0;
            double twiddle_imag = 0.0;
            size_t offset;

            for (offset = 0U; offset < half_length; ++offset)
            {
                const size_t even_index = block_start + offset;
                const size_t odd_index = even_index + half_length;
                const double odd_real =
                    ((double)data[odd_index].real * twiddle_real)
                    - ((double)data[odd_index].imag * twiddle_imag);
                const double odd_imag =
                    ((double)data[odd_index].real * twiddle_imag)
                    + ((double)data[odd_index].imag * twiddle_real);
                const double even_real = (double)data[even_index].real;
                const double even_imag = (double)data[even_index].imag;
                const double next_twiddle_real =
                    (twiddle_real * twiddle_step_real)
                    - (twiddle_imag * twiddle_step_imag);
                const double next_twiddle_imag =
                    (twiddle_real * twiddle_step_imag)
                    + (twiddle_imag * twiddle_step_real);

                data[even_index].real = (float)(even_real + odd_real);
                data[even_index].imag = (float)(even_imag + odd_imag);
                data[odd_index].real = (float)(even_real - odd_real);
                data[odd_index].imag = (float)(even_imag - odd_imag);

                twiddle_real = next_twiddle_real;
                twiddle_imag = next_twiddle_imag;
            }
        }
    }
}