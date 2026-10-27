#include "measurement_session.h"

#include "acq_frame_tx.h"
#include "measurement_lock_policy.h"

#include <stddef.h>

static void MeasSession_Start(MeasSession *session, uint32_t now_ms, uint8_t flags)
{
  session->state = MEAS_SESSION_ACQUIRING;
  session->started_ms = now_ms;
  session->ended_ms = now_ms;
  session->exchanges = 0u;
  session->last_model_sequence = 0u;
  session->next_frame_flags = flags;
}

static void MeasSession_End(MeasSession *session, MeasSessionState state, uint32_t now_ms)
{
  session->state = state;
  session->ended_ms = now_ms;
}

void MeasSession_Init(MeasSession *session, uint32_t now_ms)
{
  if (session == NULL)
  {
    return;
  }
  session->auto_repeat = false;
  MeasSession_Start(session, now_ms, (uint8_t)(ACQ_FRAME_FLAG_BOOT | ACQ_FRAME_FLAG_REARM));
}

void MeasSession_Rearm(MeasSession *session, uint32_t now_ms)
{
  if (session == NULL)
  {
    return;
  }
  MeasSession_Start(session, now_ms,
                    (uint8_t)(session->next_frame_flags | ACQ_FRAME_FLAG_REARM));
}

uint8_t MeasSession_FrameFlags(const MeasSession *session)
{
  return (session != NULL) ? session->next_frame_flags : 0u;
}

void MeasSession_OnExchangeDone(MeasSession *session)
{
  if (session == NULL)
  {
    return;
  }
  ++session->exchanges;
  session->next_frame_flags = 0u;
}

MeasModelDecision MeasSession_OnModel(MeasSession *session,
                                      uint32_t model_sequence,
                                      bool displayable,
                                      uint32_t now_ms)
{
  MeasurementLockDecision decision;

  if ((session == NULL)
      || (session->state != MEAS_SESSION_ACQUIRING)
      || (model_sequence == session->last_model_sequence))
  {
    return MEAS_MODEL_IGNORED;
  }
  session->last_model_sequence = model_sequence;

  if (session->exchanges < MEAS_SESSION_MIN_FRESH_EXCHANGES)
  {
    return MEAS_MODEL_STALE;
  }

  decision = MeasurementLockPolicy_Decide(true, displayable,
                                          MeasSession_ElapsedMs(session, now_ms),
                                          MEAS_SESSION_DEADLINE_MS);
  if (decision == MEASUREMENT_LOCK_FREEZE)
  {
    MeasSession_End(session, MEAS_SESSION_FROZEN, now_ms);
    return MEAS_MODEL_FREEZE;
  }
  if (decision == MEASUREMENT_LOCK_TIMEOUT)
  {
    MeasSession_End(session, MEAS_SESSION_ERROR, now_ms);
    return MEAS_MODEL_TIMEOUT;
  }
  return MEAS_MODEL_WAIT;
}

bool MeasSession_CheckTimeout(MeasSession *session, uint32_t now_ms)
{
  if ((session == NULL)
      || (session->state != MEAS_SESSION_ACQUIRING)
      || (MeasSession_ElapsedMs(session, now_ms) < MEAS_SESSION_DEADLINE_MS))
  {
    return false;
  }
  MeasSession_End(session, MEAS_SESSION_ERROR, now_ms);
  return true;
}

void MeasSession_Fail(MeasSession *session, uint32_t now_ms)
{
  if ((session != NULL) && (session->state == MEAS_SESSION_ACQUIRING))
  {
    MeasSession_End(session, MEAS_SESSION_ERROR, now_ms);
  }
}

void MeasSession_Hold(MeasSession *session, uint32_t now_ms)
{
  if ((session != NULL) && (session->state == MEAS_SESSION_ACQUIRING))
  {
    MeasSession_End(session, MEAS_SESSION_FROZEN, now_ms);
  }
}

uint32_t MeasSession_ElapsedMs(const MeasSession *session, uint32_t now_ms)
{
  if (session == NULL)
  {
    return 0u;
  }
  if (session->state == MEAS_SESSION_ACQUIRING)
  {
    return now_ms - session->started_ms;
  }
  return session->ended_ms - session->started_ms;
}

void MeasSession_SetAutoRepeat(MeasSession *session, bool enabled)
{
  if (session != NULL)
  {
    session->auto_repeat = enabled;
  }
}

bool MeasSession_AutoRepeatDue(const MeasSession *session, uint32_t now_ms)
{
  return (session != NULL)
      && session->auto_repeat
      && (session->state != MEAS_SESSION_ACQUIRING)
      && ((now_ms - session->ended_ms) >= MEAS_SESSION_AUTO_REPEAT_MS);
}
