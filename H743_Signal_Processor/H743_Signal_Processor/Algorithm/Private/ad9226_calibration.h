#ifndef AD9226_CALIBRATION_H
#define AD9226_CALIBRATION_H

#include <stdint.h>

/* BNC voltage uses the fixed 2x frontend gain.
 * The source settings are the contest's nominal references. Bench gain trim:
 * 0.004921 V/code * 0.98; keep DC zero and sampling timing unchanged.
 */
#define AD9226_ZERO_CODE          2055
#define AD9226_ADC_V_PER_CODE     0.00482258f
#define AD9226_FRONTEND_AC_GAIN   2.0f
#define AD9226_BNC_V_PER_CODE \
    (AD9226_ADC_V_PER_CODE / AD9226_FRONTEND_AC_GAIN)

static inline float Ad9226Calibration_CodeToBncV(uint16_t code)
{
    return (float)((int32_t)code - AD9226_ZERO_CODE)
        * AD9226_BNC_V_PER_CODE;
}

#endif
