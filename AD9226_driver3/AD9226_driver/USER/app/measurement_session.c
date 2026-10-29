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
  session->lock_candidate = false;
  session->manual_hold = false;
  session->request_generation = (uint16_t)(session->request_generation + 1u);
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
  session->request_generation = 0u;
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

uint16_t MeasSession_RequestGeneration(const MeasSession *session)
{
  return (session != NULL) ? session->request_generation : 0u;
}

bool MeasSession_ResponseTrusted(const MeasSession *session)
{
  return (session != NULL) && (session->exchanges >= 2u);
}

void MeasSession_OnExchangeDone(MeasSession *session)
{
  if (session != NULL)
  {
    ++session->exchanges;
  }
}

void MeasSession_OnSessionAccepted(MeasSession *session)
{
  if (session != NULL)
  {
    session->next_frame_flags = 0u;
  }
}

MeasModelDecision MeasSession_OnModel(MeasSession *session,
                                      uint32_t model_sequence,
                                      bool displayable,
                                      bool rate_settled,
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
  if (!displayable)
  {
    /* 更新的无效模型使先前的显示候选失效。 */
    session->lock_candidate = false;
  }

  decision = MeasurementLockPolicy_Decide(true, displayable, rate_settled,
                                          MeasSession_ElapsedMs(session, now_ms),
                                          MEAS_SESSION_DEADLINE_MS);
  if (decision == MEASUREMENT_LOCK_FREEZE)
  {
    session->lock_candidate = false;
    MeasSession_End(session, MEAS_SESSION_FROZEN, now_ms);
    return MEAS_MODEL_FREEZE;
  }
  if (decision == MEASUREMENT_LOCK_TIMEOUT)
  {
    /* A new high-rate page set must not bypass the existing deadline candidate. */
    if (session->lock_candidate && displayable && !rate_settled)
    {
      return MEAS_MODEL_WAIT;
    }
    MeasSession_End(session, MEAS_SESSION_ERROR, now_ms);
    return MEAS_MODEL_TIMEOUT;
  }
  if (displayable && !rate_settled)
  {
    session->lock_candidate = true;
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
  MeasSession_End(session,
                  session->lock_candidate ? MEAS_SESSION_FROZEN : MEAS_SESSION_ERROR,
                  now_ms);
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

void MeasSession_HoldForCalibration(MeasSession *session, uint32_t now_ms)
{
  if (session == NULL)
  {
    return;
  }
  session->manual_hold = true;
  session->lock_candidate = false;
  if (session->state == MEAS_SESSION_ACQUIRING)
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
      && !session->manual_hold
      && (session->state != MEAS_SESSION_ACQUIRING)
      && ((now_ms - session->ended_ms) >= MEAS_SESSION_AUTO_REPEAT_MS);
}
