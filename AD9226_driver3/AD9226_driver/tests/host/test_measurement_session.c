#include "measurement_session.h"
#include "acq_frame_tx.h"

#include <assert.h>
#include <stdio.h>

static void test_flags_wait_for_session_confirmation(void)
{
  MeasSession session;

  MeasSession_Init(&session, 100u);
  assert(session.state == MEAS_SESSION_ACQUIRING);
  assert(MeasSession_RequestGeneration(&session) == 1u);
  assert(!MeasSession_ResponseTrusted(&session));
  assert(MeasSession_FrameFlags(&session)
         == (ACQ_FRAME_FLAG_BOOT | ACQ_FRAME_FLAG_REARM));

  /* 交换成功不代表 H743 已接受本轮；重发标志直到收到会话确认。 */
  MeasSession_OnExchangeDone(&session);
  MeasSession_OnExchangeDone(&session);
  assert(MeasSession_FrameFlags(&session)
         == (ACQ_FRAME_FLAG_BOOT | ACQ_FRAME_FLAG_REARM));
  assert(MeasSession_ResponseTrusted(&session));

  MeasSession_OnSessionAccepted(&session);
  assert(MeasSession_FrameFlags(&session) == 0u);

  MeasSession_Rearm(&session, 200u);
  assert(MeasSession_RequestGeneration(&session) == 2u);
  assert(!MeasSession_ResponseTrusted(&session));
}

static void test_settled_model_freezes_and_candidate_waits(void)
{
  MeasSession session;

  MeasSession_Init(&session, 0u);
  MeasSession_OnSessionAccepted(&session);
  assert(MeasSession_OnModel(&session, 1u, true, true, 300u)
         == MEAS_MODEL_FREEZE);
  assert(session.state == MEAS_SESSION_FROZEN);

  MeasSession_Rearm(&session, 500u);
  assert(session.state == MEAS_SESSION_ACQUIRING);
  assert(MeasSession_FrameFlags(&session) == ACQ_FRAME_FLAG_REARM);
  assert(MeasSession_OnModel(&session, 2u, true, false, 600u)
         == MEAS_MODEL_WAIT);
  assert(session.lock_candidate);
  assert(session.state == MEAS_SESSION_ACQUIRING);
  assert(MeasSession_OnModel(&session, 2u, true, true, 700u)
         == MEAS_MODEL_IGNORED);
  assert(MeasSession_OnModel(&session, 3u, true, true, 800u)
         == MEAS_MODEL_FREEZE);
}

static void test_deadline_locks_high_rate_candidate_or_errors(void)
{
  MeasSession session;
  MeasSession plain;
  MeasSession invalidated;

  MeasSession_Init(&session, 0u);
  assert(MeasSession_OnModel(&session, 1u, true, false, 100u)
         == MEAS_MODEL_WAIT);
  assert(!MeasSession_CheckTimeout(&session, 1999u));
  assert(MeasSession_CheckTimeout(&session, 2000u));
  assert(session.state == MEAS_SESSION_FROZEN);

  MeasSession_Init(&plain, 0u);
  assert(MeasSession_OnModel(&plain, 1u, false, false, 100u)
         == MEAS_MODEL_WAIT);
  assert(MeasSession_CheckTimeout(&plain, 2000u));
  assert(plain.state == MEAS_SESSION_ERROR);

  /* 有效高速候选后出现更新的无效模型：候选必须撤销，超时不能冻结。 */
  MeasSession_Init(&invalidated, 0u);
  assert(MeasSession_OnModel(&invalidated, 1u, true, false, 100u)
         == MEAS_MODEL_WAIT);
  assert(invalidated.lock_candidate);
  assert(MeasSession_OnModel(&invalidated, 2u, false, false, 150u)
         == MEAS_MODEL_WAIT);
  assert(!invalidated.lock_candidate);
  assert(MeasSession_CheckTimeout(&invalidated, 2000u));
  assert(invalidated.state == MEAS_SESSION_ERROR);
}

static void test_calibration_hold_blocks_auto_repeat_until_rearm(void)
{
  MeasSession session;

  MeasSession_Init(&session, 0u);
  MeasSession_SetAutoRepeat(&session, true);
  MeasSession_HoldForCalibration(&session, 300u);
  assert(session.state == MEAS_SESSION_FROZEN);
  assert(session.manual_hold);
  assert(!MeasSession_AutoRepeatDue(&session, 5000u));

  MeasSession_Rearm(&session, 6000u);
  assert(!session.manual_hold);
  assert(session.state == MEAS_SESSION_ACQUIRING);
}

static void test_auto_repeat_schedule(void)
{
  MeasSession session;

  MeasSession_Init(&session, 0u);
  MeasSession_SetAutoRepeat(&session, true);
  MeasSession_OnSessionAccepted(&session);
  assert(MeasSession_OnModel(&session, 1u, true, true, 300u)
         == MEAS_MODEL_FREEZE);
  assert(!MeasSession_AutoRepeatDue(&session, 1299u));
  assert(MeasSession_AutoRepeatDue(&session, 1300u));
}

int main(void)
{
  test_flags_wait_for_session_confirmation();
  test_settled_model_freezes_and_candidate_waits();
  test_deadline_locks_high_rate_candidate_or_errors();
  test_calibration_hold_blocks_auto_repeat_until_rearm();
  test_auto_repeat_schedule();
  puts("measurement_session_tests: PASS");
  return 0;
}
