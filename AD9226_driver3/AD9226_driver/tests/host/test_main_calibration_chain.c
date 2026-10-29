#define main f407_production_main
#include "../../Core/Src/main.c"
#undef main

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned int g_failures;
static uint32_t g_tick;
static bool g_adc_done;
static uint16_t g_adc_buf[AD9226_SAMPLE_COUNT];
static unsigned int g_adc_starts;
static unsigned int g_spi_sends;
static unsigned int g_screen_measuring;
static unsigned int g_screen_errors;
static unsigned int g_screen_frames;
static unsigned int g_remeasure_requests;
static bool g_supply_model;
static H743MeasurementModel g_supplied_model;
TIM_TypeDef test_tim1;

uint32_t HAL_GetTick(void) { return g_tick; }
void HMI_RxByte(uint8_t byte) { (void)byte; }

HAL_StatusTypeDef HAL_Init(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *s) { (void)s; return HAL_OK; }
HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *s, uint32_t f) { (void)s; (void)f; return HAL_OK; }
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *s) { (void)s; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *s) { (void)s; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_ConfigClockSource(TIM_HandleTypeDef *s, TIM_ClockConfigTypeDef *c) { (void)s; (void)c; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *s) { (void)s; return HAL_OK; }
HAL_StatusTypeDef HAL_TIMEx_MasterConfigSynchronization(TIM_HandleTypeDef *s, TIM_MasterConfigTypeDef *c) { (void)s; (void)c; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *s, TIM_OC_InitTypeDef *c, uint32_t ch) { (void)s; (void)c; (void)ch; return HAL_OK; }
HAL_StatusTypeDef HAL_TIMEx_ConfigBreakDeadTime(TIM_HandleTypeDef *s, TIM_BreakDeadTimeConfigTypeDef *c) { (void)s; (void)c; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *s) { (void)s; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *s, uint8_t *p, uint16_t n) { (void)s; (void)p; (void)n; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *s, const uint8_t *p, uint16_t n, uint32_t t) { (void)s; (void)p; (void)n; (void)t; return HAL_OK; }
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *s) { (void)s; return HAL_OK; }
HAL_StatusTypeDef HAL_DMA_RegisterCallback(DMA_HandleTypeDef *s, uint32_t id, void (*cb)(DMA_HandleTypeDef *)) { (void)s; (void)id; (void)cb; return HAL_OK; }
void HAL_GPIO_Init(GPIO_TypeDef *p, GPIO_InitTypeDef *g) { (void)p; (void)g; }
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, uint32_t state) { (void)p; (void)pin; (void)state; }
void HAL_NVIC_SetPriority(uint32_t irq, uint32_t p, uint32_t s) { (void)irq; (void)p; (void)s; }
void HAL_NVIC_EnableIRQ(uint32_t irq) { (void)irq; }
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *h) { (void)h; }
int printf(const char *fmt, ...) { (void)fmt; return 0; }

void AD9226_Init(void) { }
void AD9226_Start(void) { ++g_adc_starts; g_adc_done = false; }
bool AD9226_IsDone(void) { return g_adc_done; }
void AD9226_Abort(void) { g_adc_done = false; }
uint16_t AD9226_GetFrameDivider(void) { return 82U; }
bool AD9226_FinalizeFrame(void) { return true; }
const uint16_t *AD9226_GetBuf(void) { return g_adc_buf; }
HAL_StatusTypeDef AD9226_SetSampleRate(uint32_t fs) { (void)fs; return HAL_OK; }
uint32_t AD9226_GetSampleRate(void) { return AD9226_DEFAULT_FS_HZ; }
HAL_StatusTypeDef AD9226_SetEqFundamental(uint32_t f) { (void)f; return HAL_ERROR; }
void AD9226_ClearEqMode(void) { }
bool AD9226_IsEqMode(void) { return false; }
uint32_t AD9226_GetEqFundamental(void) { return 0U; }
uint16_t AD9226_GetEqDivider(void) { return 0U; }

HAL_StatusTypeDef AcqSpi_SendSamples(const uint16_t *b, uint32_t n, const AcqFrameTxInfo *i)
{
    (void)b; (void)n; (void)i; ++g_spi_sends; return HAL_OK;
}
HAL_StatusTypeDef AcqSpi_RecvCmd(uint8_t *o, uint32_t n)
{
    (void)memset(o, 0, n); return HAL_OK;
}
uint32_t AcqSpi_GetReadyTimeoutCount(void) { return 0U; }

void SpiCmd_Init(void) { }
void SpiCmd_ResetMeasurement(void) { }
void SpiCmd_ProcessRx(const uint8_t *b, uint32_t n) { (void)b; (void)n; }
bool SpiCmd_TakeSessionStart(void) { return false; }
void SpiCmd_SetRequestGeneration(uint16_t g) { (void)g; }
bool SpiCmd_GetMeasurementModel(H743MeasurementModel *m)
{
    if (!g_supply_model) return false;
    *m=g_supplied_model;
    return true;
}
bool SpiCmd_GetMeasurement(uint32_t *f, uint16_t *v) { (void)f; (void)v; return false; }

void ScreenApp_Init(void) { }
void ScreenApp_SetWavePeriods(ScreenWavePeriodMode p) { (void)p; }
void ScreenApp_ShowMeasuring(void) { ++g_screen_measuring; }
void ScreenApp_ShowMeasurementError(void) { ++g_screen_errors; }
void ScreenApp_SetFrame(const ScreenDataFrame *f) { (void)f; ++g_screen_frames; }
uint8_t ScreenApp_TakeRemeasureRequest(void)
{
    const uint8_t r = (g_remeasure_requests != 0U) ? 1U : 0U;
    g_remeasure_requests = 0U;
    return r;
}
void ScreenApp_Task(void) { }

bool ScreenSignalModel_GenerateWaveCodes(const H743MeasurementModel *m, uint8_t p, uint16_t *w, size_t n)
{ (void)m; (void)p; (void)w; (void)n; return false; }
bool ScreenSignalModel_FillSpectrum(const H743MeasurementModel *m, ScreenSignalSpectrum *s)
{ (void)m; (void)s; return false; }
bool ScreenSignalModel_RasterizeSpectrum(const ScreenSignalSpectrum *s, uint32_t f, uint16_t *d, size_t n)
{ (void)s; (void)f; (void)d; (void)n; return false; }

bool EsReconstructPeriod(const int16_t in[ES_EQ_SAMPLE_COUNT], uint32_t f0, uint16_t div, int16_t out[ES_PERIOD_POINTS])
{ (void)in; (void)f0; (void)div; (void)out; return false; }
void EsWaveform_Publish(const int16_t p[ES_PERIOD_POINTS], uint32_t seq, uint32_t f0, uint16_t div)
{ (void)p; (void)seq; (void)f0; (void)div; }
bool ComputePeriodSpectrum(const int16_t p[ES_PERIOD_POINTS], uint32_t raw[ES_PERIOD_POINTS], uint16_t disp[ES_PERIOD_POINTS])
{ (void)p; (void)raw; (void)disp; return false; }
bool NormalReconstructPeriod(const int16_t s[ES_EQ_SAMPLE_COUNT], uint32_t n, uint32_t f, uint32_t fs, int16_t p[ES_PERIOD_POINTS])
{ (void)s; (void)n; (void)f; (void)fs; (void)p; return false; }
uint16_t EsWaveform_Copy(int16_t *d, uint16_t cap, uint16_t cyc, EsWaveformInfo *i)
{ (void)d; (void)cap; (void)cyc; (void)i; return 0U; }

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

static void reset_observations(void)
{
    g_tick = 100U;
    g_adc_done = false;
    g_adc_starts = 0U;
    g_spi_sends = 0U;
    g_screen_measuring = 0U;
    g_screen_errors = 0U;
    g_screen_frames = 0U;
    g_remeasure_requests = 0U;
    g_supply_model = false;
    g_uart_rx_flag = false;
    g_remeasure_req = false;
    g_calibration_dump_req = false;
    htim1.Instance = &test_tim1;
    App_MeasurementInit();
}

static void press_c(void)
{
    g_uart_rx_byte = 'c';
    HAL_UART_RxCpltCallback(&huart1);
}

static void test_acquiring_export_freezes_without_spi_or_screen_change(void)
{
    reset_observations();
    CHECK(g_session.state == MEAS_SESSION_ACQUIRING);
    g_adc_done = true;
    press_c();
    const unsigned int starts_before = g_adc_starts;

    App_MeasurementLoopOnce();

    CHECK(g_session.state == MEAS_SESSION_FROZEN);
    CHECK(g_session.manual_hold);
    CHECK(!g_calibration_dump_req);
    CHECK(g_calibration_export.capturing == 0U);
    CHECK(g_adc_starts == starts_before);
    CHECK(g_spi_sends == 0U);
    CHECK(g_screen_measuring == 0U);
    CHECK(g_screen_frames == 0U);
}

static void test_frozen_export_starts_one_capture_then_holds(void)
{
    reset_observations();
    MeasSession_OnSessionAccepted(&g_session);
    CHECK(MeasSession_OnModel(&g_session, 1U, true, true, g_tick)
          == MEAS_MODEL_FREEZE);
    const unsigned int starts_before = g_adc_starts;

    press_c();
    g_adc_done = true;
    App_MeasurementLoopOnce();
    CHECK(g_adc_starts == starts_before + 1U);
    CHECK(g_session.state == MEAS_SESSION_FROZEN);

    g_adc_done = false;
    App_MeasurementLoopOnce();
    CHECK(g_adc_starts == starts_before + 1U);
    CHECK(g_spi_sends == 0U);
    CHECK(g_screen_measuring == 0U);
    CHECK(g_screen_frames == 0U);
}

static void test_error_export_keeps_error_and_stops(void)
{
    reset_observations();
    MeasSession_Fail(&g_session, g_tick);
    CHECK(g_session.state == MEAS_SESSION_ERROR);
    const unsigned int errors_before = g_screen_errors;
    const unsigned int starts_before = g_adc_starts;

    press_c();
    g_adc_done = true;
    App_MeasurementLoopOnce();
    CHECK(g_adc_starts == starts_before + 1U);
    CHECK(g_session.state == MEAS_SESSION_ERROR);
    CHECK(g_screen_errors == errors_before);
    CHECK(g_spi_sends == 0U);
}

static void test_auto_repeat_does_not_restart_after_calibration(void)
{
    reset_observations();
    MeasSession_SetAutoRepeat(&g_session, true);
    g_adc_done = true;
    press_c();
    App_MeasurementLoopOnce();
    CHECK(g_session.manual_hold);

    const unsigned int starts = g_adc_starts;
    const unsigned int measuring = g_screen_measuring;
    g_tick += 5000U;
    App_MeasurementLoopOnce();
    CHECK(!MeasSession_AutoRepeatDue(&g_session, g_tick));
    CHECK(g_adc_starts == starts);
    CHECK(g_screen_measuring == measuring);
}

static void test_remeasure_restarts_and_shows_measuring(void)
{
    reset_observations();
    MeasSession_SetAutoRepeat(&g_session, true);
    g_adc_done = true;
    press_c();
    App_MeasurementLoopOnce();
    CHECK(g_session.state == MEAS_SESSION_FROZEN);

    g_remeasure_req = true;
    const unsigned int measuring = g_screen_measuring;
    App_MeasurementLoopOnce();
    CHECK(g_session.state == MEAS_SESSION_ACQUIRING);
    CHECK(!g_session.manual_hold);
    CHECK(g_screen_measuring == measuring + 1U);
}

static void test_long_press_restarts_and_shows_measuring(void)
{
    reset_observations();
    g_adc_done = true;
    press_c();
    App_MeasurementLoopOnce();
    CHECK(g_session.state == MEAS_SESSION_FROZEN);

    g_remeasure_requests = 1U;
    const unsigned int measuring = g_screen_measuring;
    App_MeasurementLoopOnce();
    CHECK(g_session.state == MEAS_SESSION_ACQUIRING);
    CHECK(g_screen_measuring == measuring + 1U);
}

static H743MeasurementModel normal_model(uint32_t seq)
{
    H743MeasurementModel m={0};
    m.published_sequence=seq;
    m.fundamental_Hz=400000U;
    m.upp_uV=200000U;
    m.urms_uV=71000U;
    m.component_count=1U;
    m.analysis_sample_rate_Hz=2048780U;
    m.request_generation=MeasSession_RequestGeneration(&g_session);
    m.component[0].order=1U;
    m.component[0].amplitude_peak_uV=100000U;
    m.component[0].valid=1U;
    return m;
}

static void feed_model(H743MeasurementModel m)
{
    g_supplied_model=m;
    g_supply_model=true;
    g_adc_done=true;
    g_tick+=50U;
    App_MeasurementLoopOnce();
}

static void test_real_main_requires_two_independent_models(void)
{
    reset_observations();
    H743MeasurementModel m=normal_model(1U);
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_ACQUIRING);
    CHECK(g_screen_frames==0U);
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_ACQUIRING);
    m.published_sequence=2U;
    m.fundamental_Hz+=12U;
    m.component[0].phase_q15=12000;
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_FROZEN);
    CHECK(g_screen_frames==1U);
}

static void test_real_main_invalid_and_changed_models_break_run(void)
{
    reset_observations();
    H743MeasurementModel m=normal_model(1U);
    feed_model(m);
    m.published_sequence=2U;
    m.status=4U;
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_ACQUIRING);
    m=normal_model(3U);
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_ACQUIRING);
    m.published_sequence=4U;
    m.upp_uV=100000U;
    m.urms_uV=35000U;
    m.component[0].amplitude_peak_uV=50000U;
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_ACQUIRING);
    m.published_sequence=5U;
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_FROZEN);
}

static void test_real_main_rate_change_and_rearm_reset_stability(void)
{
    reset_observations();
    H743MeasurementModel m=normal_model(1U);
    m.analysis_sample_rate_Hz=10500000U;
    feed_model(m);
    m=normal_model(2U);
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_ACQUIRING);
    m.published_sequence=3U;
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_FROZEN);
    measurement_rearm("test");
    m=normal_model(1U);
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_ACQUIRING);
    m.published_sequence=2U;
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_FROZEN);
}

static void test_real_main_single_high_model_cannot_freeze_at_timeout(void)
{
    reset_observations();
    H743MeasurementModel m=normal_model(1U);
    m.analysis_sample_rate_Hz=10500000U;
    feed_model(m);
    g_adc_done=false;
    g_tick=2100U;
    App_MeasurementLoopOnce();
    CHECK(g_session.state==MEAS_SESSION_ERROR);

    reset_observations();
    m=normal_model(1U);
    m.analysis_sample_rate_Hz=10500000U;
    feed_model(m);
    m.published_sequence=2U;
    feed_model(m);
    g_adc_done=false;
    g_tick=2100U;
    App_MeasurementLoopOnce();
    CHECK(g_session.state==MEAS_SESSION_FROZEN);
}

static void test_real_main_rejects_first_native_bad_model_as_final_result(void)
{
    reset_observations();
    H743MeasurementModel m=normal_model(1U);
    m.fundamental_Hz=10206U;
    m.upp_uV=42000U;
    m.urms_uV=11000U;
    m.component_count=2U;
    m.component[0].amplitude_peak_uV=14000U;
    m.component[1].order=41U;
    m.component[1].amplitude_peak_uV=7000U;
    m.component[1].valid=1U;
    CHECK(H743MeasurementModel_IsDisplayable(&m));
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_ACQUIRING);
    CHECK(g_screen_frames==0U);
    m=normal_model(2U);
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_ACQUIRING);
    m.published_sequence=3U;
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_FROZEN);
    CHECK(g_screen_frames==1U);
}

static void test_real_main_invalidates_a_stable_high_candidate(void)
{
    reset_observations();
    H743MeasurementModel m=normal_model(1U);
    m.analysis_sample_rate_Hz=10500000U;
    feed_model(m);
    m.published_sequence=2U;
    feed_model(m);
    CHECK(g_pending_model_valid);
    m.published_sequence=3U;
    m.status=4U;
    feed_model(m);
    CHECK(!g_pending_model_valid);
    g_adc_done=false;
    g_tick=2100U;
    App_MeasurementLoopOnce();
    CHECK(g_session.state==MEAS_SESSION_ERROR);
}

static void test_real_main_deadline_with_a_new_model(void)
{
    reset_observations();
    H743MeasurementModel m=normal_model(1U);
    m.analysis_sample_rate_Hz=10500000U;
    feed_model(m);
    m.published_sequence=2U;
    feed_model(m);
    CHECK(g_pending_model_valid);
    m.published_sequence=3U;
    g_tick=2050U;
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_FROZEN);

    reset_observations();
    m=normal_model(1U);
    feed_model(m);
    m.published_sequence=2U;
    g_tick=2051U;
    feed_model(m);
    CHECK(g_session.state==MEAS_SESSION_ERROR);
}

int main(void)
{
    test_real_main_deadline_with_a_new_model();
    test_real_main_rejects_first_native_bad_model_as_final_result();
    test_real_main_invalidates_a_stable_high_candidate();
    test_real_main_requires_two_independent_models();
    test_real_main_invalid_and_changed_models_break_run();
    test_real_main_rate_change_and_rearm_reset_stability();
    test_real_main_single_high_model_cannot_freeze_at_timeout();
    test_acquiring_export_freezes_without_spi_or_screen_change();
    test_frozen_export_starts_one_capture_then_holds();
    test_error_export_keeps_error_and_stops();
    test_auto_repeat_does_not_restart_after_calibration();
    test_remeasure_restarts_and_shows_measuring();
    test_long_press_restarts_and_shows_measuring();

    if (g_failures != 0U)
    {
        (void)fprintf(stderr, "%u main-chain calibration test(s) failed\n",
                      g_failures);
        return 1;
    }
    (void)fprintf(stdout, "main_calibration_chain_tests: PASS\n");
    return 0;
}
