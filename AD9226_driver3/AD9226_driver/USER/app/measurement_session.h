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
  /* 已收到可显示的高采样率结果，等最终采样率确定；超时则锁定该结果。 */
  bool lock_candidate;
  /* 标定导出后保持当前显示，禁止自动连测重新启动测量。 */
  bool manual_hold;
  /* 本轮测量请求代号；同一轮重传不变，新轮次递增。 */
  uint16_t request_generation;
} MeasSession;

/* 上电：进入采集，下一帧带 BOOT|REARM 标志。 */
void MeasSession_Init(MeasSession *session, uint32_t now_ms);

/* 重新测量：进入采集，清零交换计数和模型序号，下一帧带 REARM 标志。 */
void MeasSession_Rearm(MeasSession *session, uint32_t now_ms);

uint8_t MeasSession_FrameFlags(const MeasSession *session);

/* 本轮请求代号，写入样本帧头并用于匹配 H743 回传。 */
uint16_t MeasSession_RequestGeneration(const MeasSession *session);

/*
 * 回传页只有在 F407 至少送出两帧后才可信：同一交换中收到的页是请求前预装的，
 * 可能属于上一轮。该规则是因果顺序，不是额外延时。
 */
bool MeasSession_ResponseTrusted(const MeasSession *session);

/* 一次 SPI 帧交换成功后调用；帧头标志保留到 H743 确认本轮会话。 */
void MeasSession_OnExchangeDone(MeasSession *session);

/* H743 已接受本轮会话：后续帧不再重复送 BOOT/REARM。 */
void MeasSession_OnSessionAccepted(MeasSession *session);

/**
 * @param rate_settled 结果来自最终采样率；高采样率观察阶段的结果只作候选。
 */
MeasModelDecision MeasSession_OnModel(MeasSession *session,
                                      uint32_t model_sequence,
                                      bool displayable,
                                      bool rate_settled,
                                      uint32_t now_ms);

/* 采集状态超时则转入 ERROR；若已有高采样率候选则锁定候选并转入 FROZEN。 */
bool MeasSession_CheckTimeout(MeasSession *session, uint32_t now_ms);

/* 采集链路本身失败（例如 ADC 帧不完整）时直接转入 ERROR。 */
void MeasSession_Fail(MeasSession *session, uint32_t now_ms);

/* 停止采集但不算错误（例如导出标定原始码后保持当前帧）。 */
void MeasSession_Hold(MeasSession *session, uint32_t now_ms);

/* 标定导出：保持当前显示并抑制自动连测，直到 m/长按重新测量。 */
void MeasSession_HoldForCalibration(MeasSession *session, uint32_t now_ms);

uint32_t MeasSession_ElapsedMs(const MeasSession *session, uint32_t now_ms);

void MeasSession_SetAutoRepeat(MeasSession *session, bool enabled);

/* 自动连续测量打开时，锁定或报错满 MEAS_SESSION_AUTO_REPEAT_MS 后返回 true。 */
bool MeasSession_AutoRepeatDue(const MeasSession *session, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* MEASUREMENT_SESSION_H */
