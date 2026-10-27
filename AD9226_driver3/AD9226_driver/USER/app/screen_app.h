/**
 * @file screen_app.h
 * @brief Application coordinator for the screen MCU.
 */
#ifndef SCREEN_APP_H
#define SCREEN_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "screen_protocol.h"

void ScreenApp_Init(void);
void ScreenApp_Task(void);
void ScreenApp_SetFrame(const ScreenDataFrame *frame);
void ScreenApp_SetDisplayMode(ScreenDisplayMode mode);
void ScreenApp_SetWavePeriods(ScreenWavePeriodMode periods);
ScreenWavePeriodMode ScreenApp_GetWavePeriods(void);
void ScreenApp_ShowMeasurementError(void);
void ScreenApp_ShowMeasuring(void);
/* PA15 长按 1.5 s 后置位；取走即清除。 */
uint8_t ScreenApp_TakeRemeasureRequest(void);

extern uint8_t toggled_indeed;

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_APP_H */
