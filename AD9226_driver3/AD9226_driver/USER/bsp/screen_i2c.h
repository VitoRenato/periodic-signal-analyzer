/**
 * @file screen_i2c.h
 * @brief Nonblocking I2C receiver for raw parameters from the first F407.
 */
#ifndef SCREEN_I2C_H
#define SCREEN_I2C_H

#ifdef __cplusplus
extern "C" {
#endif

#include "screen_i2c_protocol.h"
#include <stdbool.h>

void ScreenI2c_Init(void);
void ScreenI2c_Task(void);
bool ScreenI2c_GetLatest(ScreenI2cRawParamsFrame *out);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_I2C_H */
