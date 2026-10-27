#include "key_press_detector.h"

#include <stddef.h>

static bool KeyPressDetector_IsAtOrAfter(uint32_t time_ms, uint32_t reference_ms)
{
  return (int32_t)(time_ms - reference_ms) >= 0;
}

void KeyPressDetector_Init(KeyPressDetector *detector, bool raw_pressed, uint32_t now_ms)
{
  if (detector == NULL)
  {
    return;
  }
  detector->stable_pressed = raw_pressed;
  detector->last_raw_pressed = raw_pressed;
  /* 上电时已按下的键不产生长按，必须先松开一次。 */
  detector->long_fired = raw_pressed;
  detector->edge_pending = false;
  detector->last_change_ms = now_ms;
  detector->press_start_ms = now_ms;
  detector->last_release_ms = now_ms;
  detector->last_pressed_seen_ms = now_ms;
  detector->pending_edge_ms = now_ms;
}

static KeyEvent KeyPressDetector_UpdateDebounced(KeyPressDetector *detector,
                                                 uint32_t now_ms,
                                                 bool raw_pressed)
{
  if (raw_pressed != detector->last_raw_pressed)
  {
    detector->last_raw_pressed = raw_pressed;
    detector->last_change_ms = now_ms;
    return KEY_EVENT_NONE;
  }

  if ((raw_pressed != detector->stable_pressed)
      && ((now_ms - detector->last_change_ms) >= KEY_PRESS_DEBOUNCE_MS))
  {
    detector->stable_pressed = raw_pressed;
    if (raw_pressed)
    {
      detector->press_start_ms = detector->last_change_ms;
      detector->long_fired = false;
      return KEY_EVENT_NONE;
    }

    detector->last_release_ms = now_ms;
    /* 这次按下/松开产生的下降沿已经由轮询处理。 */
    detector->edge_pending = false;
    return detector->long_fired ? KEY_EVENT_NONE : KEY_EVENT_SHORT;
  }

  if (detector->stable_pressed && !detector->long_fired
      && ((now_ms - detector->press_start_ms) >= KEY_PRESS_LONG_MS))
  {
    detector->long_fired = true;
    return KEY_EVENT_LONG;
  }
  return KEY_EVENT_NONE;
}

static KeyEvent KeyPressDetector_UpdateMissedTap(KeyPressDetector *detector,
                                                 uint32_t now_ms,
                                                 bool raw_pressed)
{
  const bool polling_saw_press =
      KeyPressDetector_IsAtOrAfter(detector->last_pressed_seen_ms, detector->pending_edge_ms);
  const bool edge_is_release_bounce =
      (int32_t)(detector->pending_edge_ms - detector->last_release_ms)
      <= (int32_t)KEY_PRESS_EDGE_GUARD_MS;
  const bool released_long_enough = !raw_pressed
      && !detector->stable_pressed
      && ((now_ms - detector->pending_edge_ms) >= KEY_PRESS_DEBOUNCE_MS);

  if (!detector->edge_pending)
  {
    return KEY_EVENT_NONE;
  }
  if (polling_saw_press || edge_is_release_bounce)
  {
    detector->edge_pending = false;
    return KEY_EVENT_NONE;
  }
  if (released_long_enough)
  {
    /* 按下和松开都发生在主循环阻塞期间，轮询从未看到低电平。 */
    detector->edge_pending = false;
    detector->last_release_ms = now_ms;
    return KEY_EVENT_SHORT;
  }
  return KEY_EVENT_NONE;
}

KeyEvent KeyPressDetector_Update(KeyPressDetector *detector,
                                 uint32_t now_ms,
                                 bool raw_pressed,
                                 bool edge_seen,
                                 uint32_t edge_ms)
{
  KeyEvent event;

  if (detector == NULL)
  {
    return KEY_EVENT_NONE;
  }
  if (edge_seen)
  {
    detector->edge_pending = true;
    detector->pending_edge_ms = edge_ms;
  }
  if (raw_pressed)
  {
    detector->last_pressed_seen_ms = now_ms;
  }

  event = KeyPressDetector_UpdateDebounced(detector, now_ms, raw_pressed);
  if (event != KEY_EVENT_NONE)
  {
    return event;
  }
  return KeyPressDetector_UpdateMissedTap(detector, now_ms, raw_pressed);
}
