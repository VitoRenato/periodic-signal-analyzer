/**
 * @file screen_key.h
 * @brief PA15 按键：短按切换 1/3 周期，长按 1.5 s 重新测量。
 */
#ifndef SCREEN_KEY_H
#define SCREEN_KEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void ScreenKey_Init(void);
void ScreenKey_Task(uint32_t now_ms);
uint8_t ScreenKey_TakeSinglePress(void);
uint8_t ScreenKey_TakeLongPress(void);

/* 由 EXTI15_10_IRQHandler 调用，标记按键按下 */
void ScreenKey_NotifyIrq(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_KEY_H */
