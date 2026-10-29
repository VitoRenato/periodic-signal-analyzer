#ifndef H743_MEASUREMENT_PAGES_H
#define H743_MEASUREMENT_PAGES_H

#include "h743_set_fs_protocol.h"
#include "signal_analyzer.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define H743_MEASUREMENT_PAGE_COUNT 5U
#define H743_MEASUREMENT_FLAG_VALID 0x01U
#define H743_MEASUREMENT_FLAG_RECOVERED 0x02U
#define H743_MEASUREMENT_PAGE_SHIFT 2U
#define H743_MEASUREMENT_PAGE_MASK 0x07U
#define H743_MEASUREMENT_MODEL_SHIFT 5U
#define H743_MEASUREMENT_MODEL_MASK 0x03U
#define H743_MEASUREMENT_FLAG_SESSION_START H743_SET_FS_FLAG_SESSION_START

typedef struct
{
    SignalResult active_result;
    SignalResult pending_result;
    uint32_t active_sample_rate_Hz;
    uint32_t pending_sample_rate_Hz;
    uint32_t active_analysis_rate_Hz;
    uint32_t pending_analysis_rate_Hz;
    uint16_t active_request_generation;
    uint16_t pending_request_generation;
    uint8_t page;
    uint8_t model_id;
    uint8_t active_valid;
    uint8_t pending_valid;
} H743MeasurementPager;

void H743MeasurementPager_Init(H743MeasurementPager *pager);

/**
 * @brief 提交测量快照；若当前五页尚未发完，则在下一轮页0再切换。
 * @param analysis_sample_rate_Hz 产生该结果的样本帧实际采样率（来自帧头）。
 */
bool H743MeasurementPager_Load(
    H743MeasurementPager *pager,
    const SignalResult *result,
    uint32_t requested_sample_rate_Hz,
    uint32_t analysis_sample_rate_Hz,
    uint16_t request_generation);

/**
 * @brief 编码下一页。
 * @param session_start true 时页0携带“本轮会话已接受”标记，供 F407 绑定/重试确认。
 */
bool H743MeasurementPager_EncodeNext(
    H743MeasurementPager *pager,
    bool session_start,
    uint8_t frame[H743_SET_FS_FRAME_SIZE]);

/** @brief 传输中断后回到第0页重发，避免F407因丢页而拼不齐整组模型。 */
void H743MeasurementPager_Rewind(H743MeasurementPager *pager);

#ifdef __cplusplus
}
#endif

#endif
