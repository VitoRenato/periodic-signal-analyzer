/**
 * @file screen_spi.h
 * @brief Nonblocking SPI receiver for H723 result frames.
 */
#ifndef SCREEN_SPI_H
#define SCREEN_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "screen_protocol.h"
#include <stdbool.h>

void ScreenSpi_Init(void);
void ScreenSpi_Task(void);
bool ScreenSpi_GetLatest(ScreenDataFrame *out);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_SPI_H */
