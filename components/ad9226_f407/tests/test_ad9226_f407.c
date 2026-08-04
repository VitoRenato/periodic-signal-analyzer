#include "ad9226_f407.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static HAL_StatusTypeDef g_pwm_result;
static HAL_StatusTypeDef g_pwm_stop_result;
static HAL_StatusTypeDef g_dma_result;
static HAL_StatusTypeDef g_dma_abort_result;
static unsigned int g_pwm_start_calls;
static unsigned int g_pwm_stop_calls;
static unsigned int g_dma_start_calls;
static unsigned int g_dma_abort_calls;
static uint32_t g_last_pwm_channel;
static uint32_t g_dma_source;
static uint32_t g_dma_destination;
static uint32_t g_dma_length;
static Ad9226F407 *g_enable_dma_interleave_device;
static DMA_HandleTypeDef *g_enable_dma_interleave_dma;
static bool g_enable_dma_interleave_handled;

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    (void)timer;
    g_pwm_start_calls++;
    g_last_pwm_channel = channel;
    return g_pwm_result;
}

HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer, uint32_t channel)
{
    (void)timer;
    g_pwm_stop_calls++;
    g_last_pwm_channel = channel;
    return g_pwm_stop_result;
}

HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef *dma,
                                   uint32_t source,
                                   uint32_t destination,
                                   uint32_t length)
{
    (void)dma;
    g_dma_start_calls++;
    g_dma_source = source;
    g_dma_destination = destination;
    g_dma_length = length;
    return g_dma_result;
}

HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *dma)
{
    (void)dma;
    g_dma_abort_calls++;
    return g_dma_abort_result;
}

void FakeTim_EnableDma(TIM_HandleTypeDef *timer, uint32_t request)
{
    timer->Instance->enabled_dma_request = request;
    if ((g_enable_dma_interleave_device != NULL) &&
        (g_enable_dma_interleave_dma != NULL))
    {
        g_enable_dma_interleave_handled = Ad9226F407_OnDmaComplete(
            g_enable_dma_interleave_device,
            g_enable_dma_interleave_dma);
    }
}

void FakeTim_DisableDma(TIM_HandleTypeDef *timer, uint32_t request)
{
    timer->Instance->disabled_dma_request = request;
}

void FakeTim_SetAutoreload(TIM_HandleTypeDef *timer, uint32_t value)
{
    timer->Instance->ARR = value;
}

void FakeTim_SetCompare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t value)
{
    (void)channel;
    timer->Instance->CCR = value;
}

typedef struct
{
    TIM_TypeDef timer_instance;
    TIM_HandleTypeDef timer;
    DMA_HandleTypeDef dma;
    GPIO_TypeDef port;
    uint16_t samples[8];
    Ad9226F407 device;
    Ad9226F407_Config config;
} Fixture;

static void reset_fixture(Fixture *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->timer.Instance = &fixture->timer_instance;
    fixture->port.IDR = 0x4321U;
    fixture->config.timer = &fixture->timer;
    fixture->config.dma = &fixture->dma;
    fixture->config.data_port = &fixture->port;
    fixture->config.timer_channel = 2U;
    fixture->config.timer_dma_request = 0x80U;
    fixture->config.timer_clock_hz = 1000000U;
    fixture->config.min_sample_rate_hz = 10U;
    fixture->config.max_sample_rate_hz = 500000U;
    fixture->config.sample_buffer = fixture->samples;
    fixture->config.sample_count = 8U;
    g_pwm_result = HAL_OK;
    g_pwm_stop_result = HAL_OK;
    g_dma_result = HAL_OK;
    g_dma_abort_result = HAL_OK;
    g_pwm_start_calls = 0U;
    g_pwm_stop_calls = 0U;
    g_dma_start_calls = 0U;
    g_dma_abort_calls = 0U;
    g_last_pwm_channel = 0U;
    g_dma_source = 0U;
    g_dma_destination = 0U;
    g_dma_length = 0U;
    g_enable_dma_interleave_device = NULL;
    g_enable_dma_interleave_dma = NULL;
    g_enable_dma_interleave_handled = false;
}

static void initialize(Fixture *fixture)
{
    assert(Ad9226F407_Init(&fixture->device, &fixture->config) == AD9226_F407_OK);
}

static void test_init_rejects_null_and_incomplete_config(void)
{
    Fixture fixture;
    reset_fixture(&fixture);
    assert(Ad9226F407_Init(NULL, &fixture.config) == AD9226_F407_ERROR_ARGUMENT);
    assert(Ad9226F407_Init(&fixture.device, NULL) == AD9226_F407_ERROR_ARGUMENT);
    fixture.config.timer = NULL;
    assert(Ad9226F407_Init(&fixture.device, &fixture.config) == AD9226_F407_ERROR_ARGUMENT);
    fixture.config.timer = &fixture.timer;
    fixture.config.min_sample_rate_hz = 500000U;
    fixture.config.max_sample_rate_hz = 10U;
    assert(Ad9226F407_Init(&fixture.device, &fixture.config) == AD9226_F407_ERROR_RANGE);
}

static void test_init_starts_pwm_once(void)
{
    Fixture fixture;
    reset_fixture(&fixture);
    initialize(&fixture);
    assert(g_pwm_start_calls == 1U);
    assert(g_last_pwm_channel == 2U);
    assert(fixture.device.initialized);
    assert(!fixture.device.busy);
    assert(!fixture.device.done);
}

static void test_init_pwm_failure_leaves_uninitialized_device_without_dma_request(void)
{
    Fixture fixture;
    reset_fixture(&fixture);
    g_pwm_result = HAL_ERROR;
    assert(Ad9226F407_Init(&fixture.device, &fixture.config) == AD9226_F407_ERROR_HAL);
    assert(!fixture.device.initialized);
    assert(!fixture.device.busy);
    assert(!fixture.device.done);
    assert(fixture.device.actual_sample_rate_hz == 0U);
    assert(g_dma_start_calls == 0U);
    assert(fixture.timer_instance.enabled_dma_request == 0U);
    assert(fixture.timer_instance.disabled_dma_request == 0U);
}

static void test_init_rejects_reinitialization_while_frame_is_active(void)
{
    Fixture fixture;
    reset_fixture(&fixture);
    initialize(&fixture);
    assert(Ad9226F407_Start(&fixture.device) == AD9226_F407_OK);

    assert(Ad9226F407_Init(&fixture.device, &fixture.config) == AD9226_F407_ERROR_STATE);
    assert(fixture.device.initialized);
    assert(fixture.device.busy);
    assert(!fixture.device.done);
    assert(fixture.timer_instance.enabled_dma_request == 0x80U);
    assert(fixture.timer_instance.disabled_dma_request == 0U);
}

static void test_start_arms_dma_and_rejects_busy_restart(void)
{
    Fixture fixture;
    reset_fixture(&fixture);
    initialize(&fixture);
    assert(Ad9226F407_Start(&fixture.device) == AD9226_F407_OK);
    assert(g_dma_start_calls == 1U);
    assert(g_dma_source == (uint32_t)(uintptr_t)&fixture.port.IDR);
    assert(g_dma_destination == (uint32_t)(uintptr_t)fixture.samples);
    assert(g_dma_length == 8U);
    assert(fixture.timer_instance.enabled_dma_request == 0x80U);
    assert(fixture.device.busy);
    assert(!fixture.device.done);
    assert(Ad9226F407_Start(&fixture.device) == AD9226_F407_ERROR_STATE);
    assert(g_dma_start_calls == 1U);
}

static void test_start_dma_failure_leaves_frame_inactive(void)
{
    Fixture fixture;
    reset_fixture(&fixture);
    initialize(&fixture);
    fixture.device.done = true;
    g_dma_result = HAL_ERROR;
    assert(Ad9226F407_Start(&fixture.device) == AD9226_F407_ERROR_HAL);
    assert(!fixture.device.busy);
    assert(!fixture.device.done);
    assert(fixture.timer_instance.enabled_dma_request == 0U);
}

static void test_start_accepts_completion_during_dma_request_enable(void)
{
    Fixture fixture;
    reset_fixture(&fixture);
    initialize(&fixture);
    g_enable_dma_interleave_device = &fixture.device;
    g_enable_dma_interleave_dma = &fixture.dma;
    assert(Ad9226F407_Start(&fixture.device) == AD9226_F407_OK);
    assert(g_enable_dma_interleave_handled);
    assert(!fixture.device.busy);
    assert(fixture.device.done);
    assert(fixture.timer_instance.enabled_dma_request == 0x80U);
    assert(fixture.timer_instance.disabled_dma_request == 0x80U);
}

static void test_unrelated_dma_callback_is_ignored(void)
{
    Fixture fixture;
    DMA_HandleTypeDef unrelated_dma;
    reset_fixture(&fixture);
    memset(&unrelated_dma, 0, sizeof(unrelated_dma));
    initialize(&fixture);
    assert(Ad9226F407_Start(&fixture.device) == AD9226_F407_OK);
    assert(!Ad9226F407_OnDmaComplete(&fixture.device, &unrelated_dma));
    assert(fixture.device.busy);
    assert(!fixture.device.done);
    assert(fixture.timer_instance.disabled_dma_request == 0U);
}

static void test_matching_dma_callback_finishes_frame(void)
{
    Fixture fixture;
    reset_fixture(&fixture);
    initialize(&fixture);
    assert(Ad9226F407_Start(&fixture.device) == AD9226_F407_OK);
    assert(Ad9226F407_OnDmaComplete(&fixture.device, &fixture.dma));
    assert(!fixture.device.busy);
    assert(fixture.device.done);
    assert(fixture.timer_instance.disabled_dma_request == 0x80U);
    assert(Ad9226F407_IsDone(&fixture.device));
}

static void test_dma_error_quiesces_request_until_task_context_abort(void)
{
    Fixture fixture;
    reset_fixture(&fixture);
    initialize(&fixture);
    assert(Ad9226F407_Start(&fixture.device) == AD9226_F407_OK);

    assert(Ad9226F407_OnDmaError(&fixture.device, &fixture.dma));
    assert(!fixture.device.busy);
    assert(!fixture.device.done);
    assert(fixture.device.recovery_pending);
    assert(fixture.timer_instance.disabled_dma_request == 0x80U);
    assert(g_dma_abort_calls == 0U);
    assert(Ad9226F407_Start(&fixture.device) == AD9226_F407_ERROR_STATE);

    assert(Ad9226F407_AbortFrame(&fixture.device) == AD9226_F407_OK);
    assert(g_dma_abort_calls == 1U);
    assert(!fixture.device.recovery_pending);
}

static void test_abort_failure_blocks_restart_until_abort_succeeds(void)
{
    Fixture fixture;
    reset_fixture(&fixture);
    initialize(&fixture);
    assert(Ad9226F407_Start(&fixture.device) == AD9226_F407_OK);

    g_dma_abort_result = HAL_ERROR;
    assert(Ad9226F407_AbortFrame(&fixture.device) == AD9226_F407_ERROR_HAL);
    assert(!fixture.device.busy);
    assert(!fixture.device.done);
    assert(fixture.device.recovery_pending);
    assert(fixture.timer_instance.disabled_dma_request == 0x80U);
    assert(Ad9226F407_Start(&fixture.device) == AD9226_F407_ERROR_STATE);

    g_dma_abort_result = HAL_OK;
    assert(Ad9226F407_AbortFrame(&fixture.device) == AD9226_F407_OK);
    assert(g_dma_abort_calls == 2U);
    assert(!fixture.device.recovery_pending);
    assert(Ad9226F407_Start(&fixture.device) == AD9226_F407_OK);
}

static void test_stop_stops_pwm_and_allows_clean_reinitialization(void)
{
    Fixture fixture;
    reset_fixture(&fixture);
    initialize(&fixture);

    assert(Ad9226F407_Stop(&fixture.device) == AD9226_F407_OK);
    assert(g_pwm_stop_calls == 1U);
    assert(g_last_pwm_channel == 2U);
    assert(!fixture.device.initialized);
    assert(!fixture.device.busy);
    assert(!fixture.device.done);
    assert(!fixture.device.recovery_pending);
    assert(fixture.timer_instance.disabled_dma_request == 0x80U);

    initialize(&fixture);
    assert(g_pwm_start_calls == 2U);
}

static void test_samples_are_hidden_until_frame_is_done(void)
{
    Fixture fixture;
    size_t sample_count = 123U;
    reset_fixture(&fixture);
    initialize(&fixture);
    assert(Ad9226F407_GetSamples(&fixture.device, &sample_count) == NULL);
    assert(sample_count == 0U);
    assert(Ad9226F407_Start(&fixture.device) == AD9226_F407_OK);
    assert(Ad9226F407_OnDmaComplete(&fixture.device, &fixture.dma));
    assert(Ad9226F407_GetSamples(&fixture.device, &sample_count) == fixture.samples);
    assert(sample_count == 8U);
}

static void test_sample_rate_rejects_out_of_range_values(void)
{
    Fixture fixture;
    uint32_t actual_hz = 0U;
    reset_fixture(&fixture);
    initialize(&fixture);
    assert(Ad9226F407_SetSampleRate(&fixture.device, 9U, &actual_hz) == AD9226_F407_ERROR_RANGE);
    assert(Ad9226F407_SetSampleRate(&fixture.device, 500001U, &actual_hz) == AD9226_F407_ERROR_RANGE);
    assert(Ad9226F407_SetSampleRate(&fixture.device, 10U, &actual_hz) == AD9226_F407_ERROR_RANGE);
}

static void test_sample_rate_programs_arr_compare_and_returns_actual_rate(void)
{
    Fixture fixture;
    uint32_t actual_hz = 0U;
    reset_fixture(&fixture);
    initialize(&fixture);
    assert(Ad9226F407_SetSampleRate(&fixture.device, 250000U, &actual_hz) == AD9226_F407_OK);
    assert(fixture.timer_instance.ARR == 3U);
    assert(fixture.timer_instance.CCR == 1U);
    assert(actual_hz == 250000U);
    assert(fixture.device.actual_sample_rate_hz == 250000U);
}

static void test_sample_rate_rejects_change_while_busy(void)
{
    Fixture fixture;
    uint32_t actual_hz = 0U;
    reset_fixture(&fixture);
    initialize(&fixture);
    assert(Ad9226F407_Start(&fixture.device) == AD9226_F407_OK);
    assert(Ad9226F407_SetSampleRate(&fixture.device, 250000U, &actual_hz) ==
           AD9226_F407_ERROR_STATE);
    assert(fixture.timer_instance.ARR == 0U);
    assert(fixture.timer_instance.CCR == 0U);
}

int main(void)
{
    test_init_rejects_null_and_incomplete_config();
    test_init_starts_pwm_once();
    test_start_accepts_completion_during_dma_request_enable();
    test_init_pwm_failure_leaves_uninitialized_device_without_dma_request();
    test_init_rejects_reinitialization_while_frame_is_active();
    test_start_arms_dma_and_rejects_busy_restart();
    test_start_dma_failure_leaves_frame_inactive();
    test_unrelated_dma_callback_is_ignored();
    test_matching_dma_callback_finishes_frame();
    test_dma_error_quiesces_request_until_task_context_abort();
    test_abort_failure_blocks_restart_until_abort_succeeds();
    test_stop_stops_pwm_and_allows_clean_reinitialization();
    test_samples_are_hidden_until_frame_is_done();
    test_sample_rate_rejects_out_of_range_values();
    test_sample_rate_programs_arr_compare_and_returns_actual_rate();
    test_sample_rate_rejects_change_while_busy();
    puts("ad9226_f407_tests: PASS");
    return 0;
}
