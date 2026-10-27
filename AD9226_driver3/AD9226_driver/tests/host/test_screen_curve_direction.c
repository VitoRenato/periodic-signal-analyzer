#include "hmi_tjc.h"
#include "screen_config.h"
#include "screen_signal_model.h"
#include "screen_view.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

UART_HandleTypeDef huart2;
static uint32_t s_tick;
static char s_command[160];
static size_t s_command_len;
static char s_active_curve[32];
static uint16_t s_pending_len;
static uint8_t s_wave_canvas[SCREEN_WAVE_WIDTH];
static uint8_t s_fft_canvas[SCREEN_FFT_WIDTH];
static uint8_t s_wave_wire[SCREEN_WAVE_WIDTH];
static uint16_t s_wave_len;
static uint16_t s_fft_len;
static unsigned s_raw_transfers;

static void screen_reply(uint8_t code)
{
  HMI_RxByte(code);
  HMI_RxByte(0xffu);
  HMI_RxByte(0xffu);
  HMI_RxByte(0xffu);
}

/* The shipped HMI uses dir=0: old points move right, new points enter at x=0. */
static void push_dir0(uint8_t *canvas, size_t width, const uint8_t *data, uint16_t len)
{
  for (uint16_t i = 0u; i < len; ++i)
  {
    memmove(canvas + 1, canvas, width - 1u);
    canvas[0] = data[i];
  }
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
                                    uint16_t len, uint32_t timeout)
{
  (void)uart;
  (void)timeout;
  if (s_pending_len != 0u)
  {
    assert(len == s_pending_len);
    if (strcmp(s_active_curve, "s_wave.id") == 0)
    {
      assert(len <= sizeof(s_wave_canvas));
      memcpy(s_wave_wire, data, len);
      push_dir0(s_wave_canvas, sizeof(s_wave_canvas), data, len);
      s_wave_len = len;
    }
    else
    {
      assert(strcmp(s_active_curve, "s_fft.id") == 0);
      assert(len <= sizeof(s_fft_canvas));
      push_dir0(s_fft_canvas, sizeof(s_fft_canvas), data, len);
      s_fft_len = len;
    }
    ++s_raw_transfers;
    s_pending_len = 0u;
    screen_reply(0xfdu);
    return HAL_OK;
  }
  if (len == 3u && data[0] == 0xffu && data[1] == 0xffu && data[2] == 0xffu)
  {
    s_command[s_command_len] = '\0';
    if (strncmp(s_command, "addt ", 5u) == 0)
    {
      unsigned channel, count;
      assert(sscanf(s_command, "addt %31[^,],%u,%u", s_active_curve, &channel, &count) == 3);
      assert(channel == 0u && count > 0u && count <= SCREEN_WAVE_WIDTH);
      s_pending_len = (uint16_t)count;
      screen_reply(0xfeu);
    }
    else if (strncmp(s_command, "cle ", 4u) == 0)
    {
      char curve[32];
      unsigned mask;
      assert(sscanf(s_command, "cle %31[^,],%u", curve, &mask) == 2);
      assert(mask == 255u);
      if (strcmp(curve, "s_wave.id") == 0) memset(s_wave_canvas, 0, sizeof(s_wave_canvas));
      else
      {
        assert(strcmp(curve, "s_fft.id") == 0);
        memset(s_fft_canvas, 0, sizeof(s_fft_canvas));
      }
    }
    s_command_len = 0u;
    return HAL_OK;
  }
  assert(s_command_len + len < sizeof(s_command));
  memcpy(s_command + s_command_len, data, len);
  s_command_len += len;
  return HAL_OK;
}

void HAL_Delay(uint32_t delay_ms) { s_tick += delay_ms; }
uint32_t HAL_GetTick(void) { return s_tick++; }

static uint8_t expected_wave_pixel(uint16_t code)
{
  return (uint8_t)(80 + (((int32_t)code - 2048) * 72) / 2047);
}

static void test_time_increases_from_left_to_right(void)
{
  ScreenDataFrame frame = {0};
  ScreenDataFrame original;
  frame.wave_count = 761u;
  for (uint16_t i = 0u; i < frame.wave_count; ++i)
    frame.wave[i] = (uint16_t)(1u + ((uint32_t)i * 4094u) / (frame.wave_count - 1u));
  original = frame;
  ScreenView_RenderWavePeriods(&frame, SCREEN_WAVE_PERIOD_3);
  fprintf(stderr, "UART dir=0 canvas left=%u right=%u; expected left=8 right=152\n",
          s_wave_canvas[0], s_wave_canvas[760]);
  assert(s_wave_canvas[0] == 8u);
  assert(s_wave_canvas[760] == 152u);
  assert(s_wave_len == 761u);
  for (uint16_t i = 0u; i < s_wave_len; ++i)
    assert(s_wave_canvas[i] == expected_wave_pixel(frame.wave[i]));
  assert(s_wave_wire[0] == 152u && s_wave_wire[760] == 8u);
  assert(memcmp(&frame, &original, sizeof(frame)) == 0);
}

static void test_frequency_increases_from_left_to_right(void)
{
  ScreenDataFrame frame = {0};
  frame.fft_count = 256u;
  frame.fft[51] = 50u;
  frame.fft[204] = 20u;
  ScreenView_RenderFrame(&frame, SCREEN_MODE_FFT, SCREEN_WAVE_PERIOD_3);
  assert(s_fft_len == 441u);
  assert(s_fft_canvas[88] == 112u && s_fft_canvas[89] == 112u);
  assert(s_fft_canvas[352] == 44u && s_fft_canvas[353] == 44u);
  assert(s_fft_canvas[0] == 0u && s_fft_canvas[440] == 0u);
  puts("UART spectrum: 100kHz at x=88..89; 400kHz at x=352..353");
}

static void test_asymmetric_model_survives_period_switches(void)
{
  H743MeasurementModel model = {0};
  ScreenDataFrame frame = {0};
  ScreenDataFrame original;
  const ScreenWavePeriodMode modes[] = {SCREEN_WAVE_PERIOD_3, SCREEN_WAVE_PERIOD_1, SCREEN_WAVE_PERIOD_3};
  model.fundamental_Hz = 100000u;
  model.component_count = 3u;
  model.component[0] = (H743MeasurementComponent){1u, 50000u, 8192, 1u};
  model.component[1] = (H743MeasurementComponent){3u, 20000u, -10000, 1u};
  model.component[2] = (H743MeasurementComponent){4u, 10000u, 5000, 1u};
  frame.wave_count = 768u;
  assert(ScreenSignalModel_GenerateWaveCodes(&model, 3u, frame.wave, SCREEN_WAVE_MAX_POINTS));
  original = frame;
  for (size_t mode = 0u; mode < sizeof(modes) / sizeof(modes[0]); ++mode)
  {
    const uint16_t visible = (modes[mode] == SCREEN_WAVE_PERIOD_1) ? 256u : 768u;
    const uint16_t points = (visible < 761u) ? visible : 761u;
    ScreenView_RenderWavePeriods(&frame, modes[mode]);
    assert(s_wave_len == points);
    for (uint16_t x = 0u; x < points; ++x)
    {
      const uint16_t src = (uint16_t)(((uint32_t)x * (visible - 1u)) / (points - 1u));
      assert(s_wave_canvas[x] == expected_wave_pixel(original.wave[src]));
    }
    if (modes[mode] == SCREEN_WAVE_PERIOD_1)
      for (uint16_t x = points; x < 761u; ++x) assert(s_wave_canvas[x] == 0u);
    assert(memcmp(&frame, &original, sizeof(frame)) == 0);
  }
  puts("UART asymmetric model: periods 3 -> 1 -> 3 preserve time order and input frame");
}

int main(void)
{
  HMI_Init();
  test_time_increases_from_left_to_right();
  test_frequency_increases_from_left_to_right();
  test_asymmetric_model_survives_period_switches();
  assert(s_raw_transfers == 5u);
  puts("screen_curve_direction_tests: PASS (production model/view/HMI, documented dir=0 push model)");
  return 0;
}
