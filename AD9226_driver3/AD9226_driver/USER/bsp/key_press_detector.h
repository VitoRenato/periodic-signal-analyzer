/**
 * @file key_press_detector.h
 * @brief 低电平有效按键的短按/长按判定（纯逻辑，不依赖 HAL）。
 */
#ifndef KEY_PRESS_DETECTOR_H
#define KEY_PRESS_DETECTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define KEY_PRESS_DEBOUNCE_MS      30u
#define KEY_PRESS_LONG_MS        1500u
/* 松开后这段时间内的 EXTI 下降沿视为触点抖动，不当作新的按键。 */
#define KEY_PRESS_EDGE_GUARD_MS   200u

typedef enum
{
  KEY_EVENT_NONE = 0,
  KEY_EVENT_SHORT,
  KEY_EVENT_LONG
} KeyEvent;

typedef struct
{
  bool stable_pressed;
  bool last_raw_pressed;
  bool long_fired;
  bool edge_pending;
  uint32_t last_change_ms;
  uint32_t press_start_ms;
  uint32_t last_release_ms;
  uint32_t last_pressed_seen_ms;
  uint32_t pending_edge_ms;
} KeyPressDetector;

void KeyPressDetector_Init(KeyPressDetector *detector, bool raw_pressed, uint32_t now_ms);

/**
 * @param raw_pressed 当前引脚电平是否为按下；调用方须先取走 EXTI 标志再读引脚
 * @param edge_seen   自上次调用以来 EXTI 是否捕获过下降沿
 * @param edge_ms     最近一次下降沿的时间戳（edge_seen 为 false 时忽略）
 * 只有轮询判定一条主路径；下降沿只用于补回主循环阻塞期间完整发生的短按。
 * 长按在按住满 KEY_PRESS_LONG_MS 时立即上报一次，松开时不再上报短按。
 */
KeyEvent KeyPressDetector_Update(KeyPressDetector *detector,
                                 uint32_t now_ms,
                                 bool raw_pressed,
                                 bool edge_seen,
                                 uint32_t edge_ms);

#ifdef __cplusplus
}
#endif

#endif /* KEY_PRESS_DETECTOR_H */
