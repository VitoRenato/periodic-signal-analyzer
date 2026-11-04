#include "measurement_session.h"
#include "measurement_stability.h"
#include "measurement_lock_policy.h"
#include "acq_frame_tx.h"
#include "h743_measurement_rx.h"
#include "h743_measurement_model.h"
#include "spicmd.h"
#include "processor_app.h"
#include "processor_telemetry_schedule.h"
#include "sample_rate_policy.h"
#include "acq_frame_header.h"
#include "h743_crc32.h"
#include "h743_measurement_pages.h"
#include "h743_set_fs_protocol.h"
#include "spi_links.h"
#include "ad9226.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_BYTES     8214U
#define CMD_BYTES       22U
#define SAMPLE_BYTES    8192U
#define F_HIGH          5000000U

static uint32_t g_tick;
static uint8_t g_h743_tx[CMD_BYTES];
static uint8_t *g_h743_rx;
static bool g_armed;
static SpiLinkEvent g_event;
static uint32_t g_actual_fs;

static uint32_t g_analyses;
static uint32_t g_high_analyses;
static uint32_t g_default_after_revert;
static bool g_stepup;
static bool g_interference_forever;
static bool g_invalidate_high;
static bool g_high_seen;
static float g_wanted_f0;

typedef struct
{
    H743MeasurementModel locked;
    H743MeasurementModel pending;
    bool locked_valid;
    bool pending_valid;
    uint32_t seq;
    uint32_t header_errors;
} FlowState;

static FlowState g_flow;
static MeasSession g_flow_session;
static MeasurementStability g_flow_stability;
static SPI_HandleTypeDef g_spi;
static uint8_t g_rx_frame[FRAME_BYTES];

uint32_t HAL_GetTick(void) { return g_tick; }
void Error_Handler(void) { abort(); }

void SPI_Links_Init(SPI_HandleTypeDef *spi)
{
    (void)spi;
    g_armed = false;
    g_event = SPI_LINK_EVENT_NONE;
}

HAL_StatusTypeDef SPI_Links_StartFrameExchange(
    uint8_t *rx, const uint8_t *tx, uint16_t n)
{
    if ((n != FRAME_BYTES) || g_armed)
    {
        return HAL_ERROR;
    }
    memcpy(g_h743_tx, tx, CMD_BYTES);
    g_h743_rx = rx;
    g_armed = true;
    return HAL_OK;
}

SpiLinkEvent SPI_Links_TakeFrameEvent(void)
{
    const SpiLinkEvent e = g_event;
    g_event = SPI_LINK_EVENT_NONE;
    return e;
}

uint32_t SPI_Links_GetLastHalError(void) { return 0U; }
void SPI_Links_AbortAll(void) { g_armed = false; g_event = SPI_LINK_EVENT_NONE; }
bool SPI_Links_HasPartialTransferTimedOut(uint32_t now, uint32_t timeout)
{
    (void)now;
    (void)timeout;
    return false;
}

HAL_StatusTypeDef AD9226_SetSampleRate(uint32_t fs_hz)
{
    const uint32_t actual = H743SetFsProtocol_ActualRate(fs_hz);
    if (actual == 0U)
    {
        return HAL_ERROR;
    }
    g_actual_fs = actual;
    return HAL_OK;
}

uint32_t AD9226_GetSampleRate(void) { return g_actual_fs; }
void AD9226_ClearEqMode(void) { }
HAL_StatusTypeDef AD9226_SetEqFundamental(uint32_t f0_hz)
{
    (void)f0_hz;
    return HAL_ERROR;
}
uint16_t AD9226_GetEqDivider(void) { return 0U; }

static void set_result(SignalResult *r, uint8_t status, uint8_t quality,
                       uint32_t recommended)
{
    memset(r, 0, sizeof(*r));
    r->status = (SignalStatus)status;
    r->sample_rate_Hz = g_actual_fs;
    r->quality_flags = quality;
    r->recommended_sample_rate_Hz = recommended;
    r->fundamental_Hz = g_wanted_f0;
    r->upp_V = 1.0f;
    r->urms_V = 0.353553f;
    r->component_count = 1U;
    r->component[0].frequency_Hz = g_wanted_f0;
    r->component[0].amplitude_peak_V = 0.5f;
}

SignalStatus SignalAnalyzer_ProcessCalibrated(
    const uint16_t s[SIGNAL_FRAME_SAMPLES], uint32_t fs, SignalResult *r)
{
    (void)s;
    ++g_analyses;
    if (fs >= F_HIGH)
    {
        g_high_seen = true;
        ++g_high_analyses;
        if (g_invalidate_high && (g_high_analyses >= 2U))
        {
            set_result(r, SIGNAL_STATUS_NO_SIGNAL, 0U, 0U);
        }
        else if (g_interference_forever)
        {
            set_result(r, SIGNAL_STATUS_VALID,
                       SIGNAL_QUALITY_INTERFERENCE_REMOVED, 0U);
        }
        else
        {
            set_result(r, SIGNAL_STATUS_VALID, 0U, 0U);
        }
    }
    else if (g_stepup && !g_high_seen)
    {
        set_result(r, SIGNAL_STATUS_SAMPLE_RATE_UNSAFE,
                   SIGNAL_QUALITY_INTERFERENCE_REMOVED,
                   SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ);
    }
    else
    {
        if (g_high_seen)
        {
            ++g_default_after_revert;
        }
        set_result(r, SIGNAL_STATUS_VALID, 0U, 0U);
    }
    return r->status;
}

static void flow_reset(bool stepup, bool interference)
{
    g_tick = 100U;
    g_actual_fs = AD9226_DEFAULT_FS_HZ;
    g_analyses = 0U;
    g_high_analyses = 0U;
    g_default_after_revert = 0U;
    g_stepup = stepup;
    g_interference_forever = interference;
    g_invalidate_high = false;
    g_high_seen = false;
    g_wanted_f0 = 100000.0f;
    g_flow = (FlowState){0};
    memset(g_h743_tx, 0, sizeof(g_h743_tx));
    memset(g_rx_frame, 0, sizeof(g_rx_frame));
    g_h743_rx = NULL;
    g_armed = false;
    g_event = SPI_LINK_EVENT_NONE;
    MeasSession_Init(&g_flow_session, g_tick);
    MeasurementStability_Reset(&g_flow_stability);
    SpiCmd_Init();
    SpiCmd_SetRequestGeneration(
        MeasSession_RequestGeneration(&g_flow_session));
    ProcessorApp_Init(&g_spi);
}

static bool flow_arm(void)
{
    for (unsigned int i = 0U; (i < 12U) && !g_armed; ++i)
    {
        g_tick += 5U;
        ProcessorApp_RunOnce();
    }
    return g_armed;
}

static void flow_apply_model(void)
{
    H743MeasurementModel model;

    if (SpiCmd_TakeSessionStart())
    {
        MeasSession_OnSessionAccepted(&g_flow_session);
    }
    if (!SpiCmd_GetMeasurementModel(&model))
    {
        return;
    }
    {
        const bool displayable = MeasurementStability_Observe(
            &g_flow_stability, &model, H743MeasurementModel_IsDisplayable(&model));
        const bool settled = model.analysis_sample_rate_Hz < F_HIGH;
        const MeasModelDecision decision = MeasSession_OnModel(
            &g_flow_session, model.published_sequence, displayable,
            settled, g_tick);

        if (decision == MEAS_MODEL_FREEZE)
        {
            g_flow.locked = model;
            g_flow.locked_valid = true;
        }
        else if ((decision == MEAS_MODEL_WAIT) && displayable && !settled)
        {
            g_flow.pending = model;
            g_flow.pending_valid = true;
        }
        else if ((decision == MEAS_MODEL_WAIT) && !displayable)
        {
            g_flow.pending_valid = false;
        }
    }
}

static void flow_exchange(bool corrupt_header, bool corrupt_sample,
                          bool transport_error)
{
    AcqFrameTxInfo info;
    const uint16_t divider = (uint16_t)(168000000U / g_actual_fs);

    if (!flow_arm())
    {
        (void)fprintf(stderr, "FAIL: H743 did not arm\n");
        abort();
    }

    memset(g_h743_rx, 0, FRAME_BYTES);
    info.flags = MeasSession_FrameFlags(&g_flow_session);
    info.timer_divider = divider;
    info.sequence = ++g_flow.seq;
    info.request_generation =
        MeasSession_RequestGeneration(&g_flow_session);
    if (!AcqFrameTx_Encode(&info, &g_h743_rx[ACQ_FRAME_HEADER_BYTES],
                          SAMPLE_BYTES, g_h743_rx))
    {
        abort();
    }
    if (corrupt_header)
    {
        g_h743_rx[14] ^= 0x01U;
    }
    if (corrupt_sample)
    {
        g_h743_rx[ACQ_FRAME_HEADER_BYTES + 64U] ^= 0x01U;
    }

    g_armed = false;
    g_event = transport_error ? SPI_LINK_EVENT_ERROR : SPI_LINK_EVENT_COMPLETE;
    g_tick += 15U;
    MeasSession_OnExchangeDone(&g_flow_session);

    if (!transport_error && MeasSession_ResponseTrusted(&g_flow_session))
    {
        SpiCmd_ProcessRx(g_h743_tx, sizeof(g_h743_tx));
        flow_apply_model();
    }

    ProcessorApp_RunOnce();
    g_flow.header_errors = ProcessorApp_GetDiagnostics()->header_error_count;
}

static void flow_poll_deadline(void)
{
    if (MeasSession_CheckTimeout(&g_flow_session, g_tick))
    {
        if ((g_flow_session.state == MEAS_SESSION_FROZEN)
            && g_flow.pending_valid)
        {
            g_flow.locked = g_flow.pending;
            g_flow.locked_valid = true;
        }
    }
}

static bool flow_run(int exchanges, bool header_corrupt_once,
                     bool sample_corrupt_once, bool transport_error_once)
{
    bool header_done = false;
    bool sample_done = false;
    bool transport_done = false;

    for (int i = 0; i < exchanges; ++i)
    {
        const bool new_session = (MeasSession_FrameFlags(&g_flow_session)
            & ACQ_FRAME_FLAG_REARM) != 0U;
        const bool corrupt_header = header_corrupt_once && !header_done
            && new_session;
        const bool corrupt_sample = sample_corrupt_once && !sample_done
            && new_session;
        const bool transport_error = transport_error_once && !transport_done
            && !new_session;

        flow_exchange(corrupt_header, corrupt_sample, transport_error);
        header_done = header_done || corrupt_header;
        sample_done = sample_done || corrupt_sample;
        transport_done = transport_done || transport_error;
        flow_poll_deadline();
        if (g_flow.locked_valid)
        {
            return true;
        }
    }
    return false;
}

static unsigned int g_failures;

#define CHECK(expr)                                                          \
    do                                                                       \
    {                                                                        \
        if (!(expr))                                                         \
        {                                                                    \
            (void)fprintf(stderr, "FAIL %s:%d: %s\n",                        \
                          __FILE__, __LINE__, #expr);                        \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_U32(expected, actual)                                          \
    do                                                                       \
    {                                                                        \
        const uint32_t e_ = (uint32_t)(expected);                            \
        const uint32_t a_ = (uint32_t)(actual);                              \
        if (e_ != a_)                                                        \
        {                                                                    \
            (void)fprintf(stderr, "FAIL %s:%d: %u != %u\n",                  \
                          __FILE__, __LINE__, (unsigned)e_, (unsigned)a_);   \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

static void start_locked_100k(void)
{
    flow_reset(false, false);
    CHECK(flow_run(60, false, false, false));
    CHECK(g_flow.locked_valid);
    CHECK_U32(100000U, g_flow.locked.fundamental_Hz);
    CHECK_U32(AD9226_DEFAULT_FS_HZ, g_flow.locked.analysis_sample_rate_Hz);
}

static void rearm_for(uint32_t f0)
{
    MeasSession_Rearm(&g_flow_session, g_tick);
    MeasurementStability_Reset(&g_flow_stability);
    SpiCmd_ResetMeasurement();
    SpiCmd_SetRequestGeneration(
        MeasSession_RequestGeneration(&g_flow_session));
    g_flow.locked_valid = false;
    g_flow.pending_valid = false;
    g_wanted_f0 = (float)f0;
}

static void test_header_crc_error_on_first_rearm_frame(void)
{
    uint32_t before;

    start_locked_100k();
    before = g_analyses;
    rearm_for(200000U);
    CHECK(flow_run(80, true, false, false));
    CHECK(g_flow.locked_valid);
    CHECK_U32(200000U, g_flow.locked.fundamental_Hz);
    CHECK_U32(AD9226_DEFAULT_FS_HZ, g_flow.locked.analysis_sample_rate_Hz);
    CHECK(g_analyses > before);
    CHECK(g_flow.header_errors >= 1U);
}

static void test_sample_crc_error_on_first_rearm_frame(void)
{
    uint32_t before;

    start_locked_100k();
    before = g_analyses;
    rearm_for(200000U);
    CHECK(flow_run(80, false, true, false));
    CHECK(g_flow.locked_valid);
    CHECK_U32(200000U, g_flow.locked.fundamental_Hz);
    CHECK(g_analyses > before);
}

static void test_old_pager_pages_cannot_lock_after_rearm(void)
{
    start_locked_100k();
    rearm_for(200000U);

    /* 两次坏首帧期间 H743 只会重发旧分页，F407 未绑定本轮不得锁定。 */
    for (unsigned int i = 0U; i < 2U; ++i)
    {
        flow_exchange(true, false, false);
        flow_poll_deadline();
        CHECK(!g_flow.locked_valid);
    }
    CHECK(flow_run(80, false, false, false));
    CHECK_U32(200000U, g_flow.locked.fundamental_Hz);
}

static void test_consecutive_errors_then_recovery(void)
{
    start_locked_100k();
    rearm_for(200000U);
    for (unsigned int i = 0U; i < 3U; ++i)
    {
        flow_exchange(true, false, false);
        flow_poll_deadline();
        CHECK(!g_flow.locked_valid);
    }
    CHECK(flow_run(80, false, false, false));
    CHECK_U32(200000U, g_flow.locked.fundamental_Hz);
}

static void test_f407_alone_reset_gets_new_session(void)
{
    start_locked_100k();

    flow_arm();
    MeasSession_Init(&g_flow_session, g_tick);
    MeasurementStability_Reset(&g_flow_stability);
    SpiCmd_Init();
    SpiCmd_SetRequestGeneration(
        MeasSession_RequestGeneration(&g_flow_session));
    g_flow.seq = 0U;
    g_flow.locked_valid = false;
    g_flow.pending_valid = false;
    g_wanted_f0 = 300000.0f;

    CHECK(flow_run(100, false, false, false));
    CHECK_U32(300000U, g_flow.locked.fundamental_Hz);
}

static void test_rearm_before_previous_ack(bool corrupt_header)
{
    uint32_t before;

    flow_reset(false, false);
    flow_exchange(false, false, false);
    CHECK(flow_arm());
    CHECK((g_h743_tx[3] & H743_MEASUREMENT_FLAG_SESSION_START) != 0U);

    before = g_analyses;
    rearm_for(200000U);
    flow_exchange(corrupt_header, false, false);
    CHECK((MeasSession_FrameFlags(&g_flow_session)
           & ACQ_FRAME_FLAG_REARM) != 0U);
    CHECK(flow_run(100, false, false, false));
    CHECK_U32(200000U, g_flow.locked.fundamental_Hz);
    CHECK(g_analyses > before);
}

static void test_consecutive_rearms_before_ack(void)
{
    start_locked_100k();
    rearm_for(200000U);
    flow_exchange(false, false, false);
    CHECK((MeasSession_FrameFlags(&g_flow_session)
           & ACQ_FRAME_FLAG_REARM) != 0U);

    rearm_for(300000U);
    CHECK(flow_run(100, false, false, false));
    CHECK_U32(300000U, g_flow.locked.fundamental_Hz);
}

static void test_invalidated_high_candidate_is_not_locked_at_deadline(void)
{
    flow_reset(true, false);
    g_invalidate_high = true;
    for (unsigned int i = 0U; (i < 160U) && (g_flow_session.state == MEAS_SESSION_ACQUIRING); ++i)
    {
        flow_exchange(false, false, false);
        flow_poll_deadline();
    }
    CHECK(!g_flow.locked_valid);
    CHECK(g_flow_session.state == MEAS_SESSION_ERROR);
}

static void test_transport_error_during_paging_recovers(void)
{
    start_locked_100k();
    rearm_for(400000U);
    CHECK(flow_run(100, false, false, true));
    CHECK_U32(400000U, g_flow.locked.fundamental_Hz);
}

static void test_high_rate_three_clean_then_revert_and_lock(void)
{
    flow_reset(true, false);
    CHECK(flow_run(240, false, false, false));
    CHECK(g_flow.locked_valid);
    CHECK_U32(3U, g_high_analyses);
    CHECK_U32(2U, g_default_after_revert);
    CHECK_U32(AD9226_DEFAULT_FS_HZ, g_flow.locked.analysis_sample_rate_Hz);
    CHECK_U32(100000U, g_flow.locked.fundamental_Hz);
}

static void test_persistent_interference_locks_high_rate_at_deadline(void)
{
    flow_reset(true, true);
    for (unsigned int i = 0U; (i < 120U) && !g_flow.pending_valid; ++i)
    {
        flow_exchange(false, false, false);
        flow_poll_deadline();
    }
    CHECK(g_flow.pending_valid);
    CHECK(!g_flow.locked_valid);
    g_tick = 100U + MEAS_SESSION_DEADLINE_MS;
    flow_poll_deadline();
    CHECK(g_flow.locked_valid);
    CHECK(g_flow.locked.analysis_sample_rate_Hz >= F_HIGH);
    CHECK_U32(100000U, g_flow.locked.fundamental_Hz);
}

static void test_paging_does_not_count_as_two_models(void)
{
    flow_reset(false, false);
    for (unsigned i=0;i<6U;++i) flow_exchange(false,false,false);
    CHECK(!g_flow.locked_valid);
    CHECK(flow_run(80,false,false,false));
    CHECK(g_flow.locked_valid);
    CHECK(g_analyses>=2U);
    CHECK(g_tick-g_flow_session.started_ms<=MEAS_SESSION_DEADLINE_MS);
}

int main(void)
{
    test_paging_does_not_count_as_two_models();
    test_header_crc_error_on_first_rearm_frame();
    test_sample_crc_error_on_first_rearm_frame();
    test_old_pager_pages_cannot_lock_after_rearm();
    test_consecutive_errors_then_recovery();
    test_f407_alone_reset_gets_new_session();
    test_rearm_before_previous_ack(false);
    test_rearm_before_previous_ack(true);
    test_consecutive_rearms_before_ack();
    test_invalidated_high_candidate_is_not_locked_at_deadline();
    test_transport_error_during_paging_recovers();
    test_high_rate_three_clean_then_revert_and_lock();
    test_persistent_interference_locks_high_rate_at_deadline();

    if (g_failures != 0U)
    {
        (void)fprintf(stderr, "%u flow test(s) failed\n", g_failures);
        return 1;
    }
    (void)puts("measurement_flow_tests: PASS");
    return 0;
}
