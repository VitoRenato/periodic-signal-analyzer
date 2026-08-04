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
#define H743_MEASUREMENT_MODEL_SHIFT 5U

typedef struct
{
    SignalResult active_result;
    SignalResult pending_result;
    uint32_t active_sample_rate_Hz;
    uint32_t pending_sample_rate_Hz;
    uint8_t page;
    uint8_t model_id;
    uint8_t active_valid;
    uint8_t pending_valid;
} H743MeasurementPager;

void H743MeasurementPager_Init(H743MeasurementPager *pager);

/**
 * @brief 提交测量快照；若当前五页尚未发完，则在下一轮页0再切换。
 */
bool H743MeasurementPager_Load(
    H743MeasurementPager *pager,
    const SignalResult *result,
    uint32_t requested_sample_rate_Hz);

bool H743MeasurementPager_EncodeNext(
    H743MeasurementPager *pager,
    uint8_t frame[H743_SET_FS_FRAME_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
