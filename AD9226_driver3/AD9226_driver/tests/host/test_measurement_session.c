#include "measurement_session.h"
#include "acq_frame_tx.h"

#include <assert.h>
#include <stdio.h>

static void test_boot_flags_and_fresh_exchange_gate(void)
{
  MeasSession session;
  uint32_t i;

  MeasSession_Init(&session, 100u);
  assert(session.state == MEAS_SESSION_ACQUIRING);
  assert(MeasSession_FrameFlags(&session) == (ACQ_FRAME_FLAG_BOOT | ACQ_FRAME_FLAG_REARM));

  /* 第一帧送出后，后续帧不再带 BOOT/REARM。 */
  MeasSession_OnExchangeDone(&session);
  assert(MeasSession_FrameFlags(&session) == 0u);

  /* 不足 6 次交换就收齐的模型视为旧帧残留，不能锁定。 */
  for (i = 2u; i <= MEAS_SESSION_MIN_FRESH_EXCHANGES; ++i)
  {
    assert(MeasSession_OnModel(&session, i, true, 150u) == MEAS_MODEL_STALE);
    MeasSession_OnExchangeDone(&session);
  }

  /* 第 6 次交换后收到的有效模型立即锁定。 */
  assert(session.exchanges == MEAS_SESSION_MIN_FRESH_EXCHANGES);
  assert(MeasSession_OnModel(&session, 10u, true, 260u) == MEAS_MODEL_FREEZE);
  assert(session.state == MEAS_SESSION_FROZEN);
  assert(MeasSession_ElapsedMs(&session, 900u) == 160u);
}

static void test_rearm_from_frozen_and_error(void)
{
  MeasSession session;
  uint32_t i;

  MeasSession_Init(&session, 0u);
  MeasSession_OnExchangeDone(&session);
  assert(MeasSession_CheckTimeout(&session, 1999u) == false);
  assert(MeasSession_CheckTimeout(&session, 2000u) == true);
  assert(session.state == MEAS_SESSION_ERROR);

  /* 从 ERROR 重新测量：只带 REARM，不带 BOOT。 */
  MeasSession_Rearm(&session, 3000u);
  assert(session.state == MEAS_SESSION_ACQUIRING);
  assert(MeasSession_FrameFlags(&session) == ACQ_FRAME_FLAG_REARM);

  for (i = 0u; i < MEAS_SESSION_MIN_FRESH_EXCHANGES; ++i)
  {
    MeasSession_OnExchangeDone(&session);
  }
  assert(MeasSession_OnModel(&session, 1u, false, 3200u) == MEAS_MODEL_WAIT);
  assert(MeasSession_OnModel(&session, 2u, true, 3350u) == MEAS_MODEL_FREEZE);
  assert(session.state == MEAS_SESSION_FROZEN);
}

static void test_auto_repeat_schedule(void)
{
  MeasSession session;
  uint32_t i;

  MeasSession_Init(&session, 0u);
  MeasSession_SetAutoRepeat(&session, true);
  for (i = 0u; i < MEAS_SESSION_MIN_FRESH_EXCHANGES; ++i)
  {
    MeasSession_OnExchangeDone(&session);
  }
  assert(MeasSession_OnModel(&session, 1u, true, 300u) == MEAS_MODEL_FREEZE);
  assert(!MeasSession_AutoRepeatDue(&session, 1299u));
  assert(MeasSession_AutoRepeatDue(&session, 1300u));
}

int main(void)
{
  test_boot_flags_and_fresh_exchange_gate();
  test_rearm_from_frozen_and_error();
  test_auto_repeat_schedule();
  puts("measurement_session_tests: PASS");
  return 0;
}
