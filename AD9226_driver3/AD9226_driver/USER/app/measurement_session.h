/**
 * @file measurement_session.h
 * @brief 一轮测量的状态机：采集 -> 锁定/报错 -> 重新测量（纯逻辑，不依赖 HAL）。
 */
#ifndef MEASUREMENT_SESSION_H
#define MEASUREMENT_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define MEAS_SESSION_DEADLINE_MS          2000u
/*
 * 重新测量后，第 1 次交换送出新帧，H743 处理后再用 5 次交换发完五页结果，
 * 所以第 6 次交换之前收齐的结果一定来自旧帧。
 */
#define MEAS_SESSION_MIN_FRESH_EXCHANGES  6u
#define MEAS_SESSION_AUTO_REPEAT_MS       1000u

typedef enum
{
  MEAS_SESSION_ACQUIRING = 0,
  MEAS_SESSION_FROZEN,
  MEAS_SESSION_ERROR
} MeasSessionState;

typedef enum
{
  MEAS_MODEL_IGNORED = 0,   /* 非采集状态或重复序号 */
  MEAS_MODEL_STALE,         /* 重新测量前的帧算出的结果 */
  MEAS_MODEL_WAIT,
  MEAS_MODEL_FREEZE,
  MEAS_MODEL_TIMEOUT
} MeasModelDecision;

typedef struct
{
  MeasSessionState state;
  uint32_t started_ms;
  uint32_t ended_ms;
  uint32_t exchanges;
  uint32_t last_model_sequence;
  uint8_t next_frame_flags;
  bool auto_repeat;
} MeasSession;

/* 上电：进入采集，下一帧带 BOOT|REARM 标志。 */
void MeasSession_Init(MeasSession *session, uint32_t now_ms);

/* 重新测量：进入采集，清零交换计数和模型序号，下一帧带 REARM 标志。 */
void MeasSession_Rearm(MeasSession *session, uint32_t now_ms);

uint8_t MeasSession_FrameFlags(const MeasSession *session);

/* 一次 SPI 帧交换成功后调用；帧头标志只随第一帧发送。 */
void MeasSession_OnExchangeDone(MeasSession *session);

MeasModelDecision MeasSession_OnModel(MeasSession *session,
                                      uint32_t model_sequence,
                                      bool displayable,
                                      uint32_t now_ms);

/* 采集状态超时则转入 ERROR 并返回 true。 */
bool MeasSession_CheckTimeout(MeasSession *session, uint32_t now_ms);

/* 采集链路本身失败（例如 ADC 帧不完整）时直接转入 ERROR。 */
void MeasSession_Fail(MeasSession *session, uint32_t now_ms);

/* 停止采集但不算错误（例如导出标定原始码后保持当前帧）。 */
void MeasSession_Hold(MeasSession *session, uint32_t now_ms);

uint32_t MeasSession_ElapsedMs(const MeasSession *session, uint32_t now_ms);

void MeasSession_SetAutoRepeat(MeasSession *session, bool enabled);

/* 自动连续测量打开时，锁定或报错满 MEAS_SESSION_AUTO_REPEAT_MS 后返回 true。 */
bool MeasSession_AutoRepeatDue(const MeasSession *session, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* MEASUREMENT_SESSION_H */
