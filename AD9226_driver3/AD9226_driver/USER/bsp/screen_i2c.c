/**
 * @file screen_i2c.c
 * @brief Stub for F4 acquisition board: no I2C raw-parameter receiver is used here.
 *
 * The original screen_i2c.c was written for the dedicated screen MCU (F407)
 * which received raw measurement parameters from the acquisition F4 over I2C2.
 * On the acquisition F4, raw parameters come from the H7 command slot instead,
 * so I2C screen reception is intentionally disabled.
 */
#include "screen_i2c.h"
#include <stddef.h>
#include <string.h>

static ScreenI2cRawParamsFrame s_latest;

void ScreenI2c_Init(void)
{
    memset(&s_latest, 0, sizeof(s_latest));
}

void ScreenI2c_Task(void)
{
    /* No-op: raw parameters are supplied by the application if available. */
}

bool ScreenI2c_GetLatest(ScreenI2cRawParamsFrame *out)
{
    if (out == NULL)
    {
        return false;
    }
    memcpy(out, &s_latest, sizeof(*out));
    return false;
}
