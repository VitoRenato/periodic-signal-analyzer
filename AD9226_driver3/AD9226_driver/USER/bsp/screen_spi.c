/**
 * @file screen_spi.c
 * @brief Stub for F4 acquisition board: no SPI1 screen receiver is used here.
 *
 * The original screen_spi.c was written for the dedicated screen MCU (F407)
 * which received a full ScreenDataFrame from the H723 FFT board over SPI1.
 * On the acquisition F4, SPI1 is already used as the data/command link to H7,
 * so screen SPI reception is intentionally disabled.
 */
#include "screen_spi.h"
#include <stddef.h>
#include <string.h>

static ScreenDataFrame s_latest;

void ScreenSpi_Init(void)
{
    memset(&s_latest, 0, sizeof(s_latest));
}

void ScreenSpi_Task(void)
{
    /* No-op: frames are supplied by the application via ScreenApp_SetFrame(). */
}

bool ScreenSpi_GetLatest(ScreenDataFrame *out)
{
    if (out == NULL)
    {
        return false;
    }
    memcpy(out, &s_latest, sizeof(*out));
    return false;
}
