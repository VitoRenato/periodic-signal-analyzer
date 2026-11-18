#include "screen_view.h"
#include "hmi_tjc.h"
#include "screen_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

UART_HandleTypeDef huart2;
static unsigned s_wave_addt_count;
static unsigned s_fft_addt_count;
static unsigned s_wave_clear_count;
static unsigned s_fft_clear_count;
static uint8_t s_wave_canvas[SCREEN_WAVE_VISIBLE_POINTS];
static uint16_t s_last_wave_len;
static uint8_t s_fft_canvas[SCREEN_FFT_VISIBLE_POINTS];
static uint16_t s_last_fft_len;

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *data,
                                    uint16_t len, uint32_t timeout)
{
  (void)huart;
  (void)data;
  (void)len;
  (void)timeout;
  return HAL_OK;
}
void HAL_Delay(uint32_t delay_ms) { (void)delay_ms; }
uint32_t HAL_GetTick(void) { return 0u; }
void HMI_SetText(const char *obj, const char *text) { (void)obj; (void)text; }

bool HMI_Addt_Send(const char *curve_id, uint8_t ch, const uint8_t *data, uint16_t len)
{
  (void)ch;
  assert(len > 0u);
  if (strcmp(curve_id, "s_wave") == 0)
  {
    assert(len <= sizeof(s_wave_canvas));
    for (uint16_t x = 0u; x < len; ++x) s_wave_canvas[x] = data[len - 1u - x];
    s_last_wave_len = len;
    ++s_wave_addt_count;
  }
  else if (strcmp(curve_id, "s_fft") == 0)
  {
    assert(len <= sizeof(s_fft_canvas));
    for (uint16_t x = 0u; x < len; ++x) s_fft_canvas[x] = data[len - 1u - x];
    s_last_fft_len = len;
    ++s_fft_addt_count;
  }
  else { assert(0); }
  return true;
}

void HMI_ClearWave(const char *obj, uint8_t channel_mask)
{
  assert(channel_mask == 255u);
  if (strcmp(obj, "s_wave") == 0) ++s_wave_clear_count;
  else if (strcmp(obj, "s_fft") == 0) ++s_fft_clear_count;
  else { assert(0); }
}
void HMI_GotoPage(const char *page_name) { (void)page_name; }

static void test_wave_peaks_fit_161_pixel_control(void)
{
  ScreenDataFrame frame = {0};
  frame.wave_count = 3u;
  frame.wave[0] = 1u;
  frame.wave[1] = 2048u;
  frame.wave[2] = 4095u;
  ScreenView_RenderWavePeriods(&frame, SCREEN_WAVE_PERIOD_3);
  fprintf(stderr, "dir=0 canvas actual=[%u,%u,%u], expected=[8,80,152]\n",
          s_wave_canvas[0], s_wave_canvas[1], s_wave_canvas[2]);
  assert(s_last_wave_len == 3u);
  assert(s_wave_canvas[0] == 8u);
  assert(s_wave_canvas[1] == 80u);
  assert(s_wave_canvas[2] == 152u);
  assert((80u - s_wave_canvas[0]) == (s_wave_canvas[2] - 80u));
}

static void test_period_switch_does_not_duplicate_spectrum(void)
{
  ScreenDataFrame frame = {0};
  const unsigned wave_before = s_wave_addt_count;
  const unsigned fft_before = s_fft_addt_count;
  const unsigned fft_clear_before = s_fft_clear_count;
  frame.wave_count = 768u;
  frame.fft_count = 256u;
  for (uint16_t i = 0u; i < frame.wave_count; ++i) frame.wave[i] = 2048u;
  frame.fft[51] = 63u;
  frame.fft[102] = 322u;
  ScreenView_RenderFrame(&frame, SCREEN_MODE_WAVE, SCREEN_WAVE_PERIOD_3);
  assert(s_wave_addt_count == wave_before + 1u);
  assert(s_fft_addt_count == fft_before + 1u);
  assert(s_fft_clear_count == fft_clear_before + 1u);
  ScreenView_RenderWavePeriods(&frame, SCREEN_WAVE_PERIOD_1);
  assert(s_wave_addt_count == wave_before + 2u);
  assert(s_fft_addt_count == fft_before + 1u);
  assert(s_fft_clear_count == fft_clear_before + 1u);
}

static void test_three_periods_fit_width_and_preserve_last_sample(void)
{
  ScreenDataFrame frame = {0};
  ScreenDataFrame original;
  frame.wave_count = 768u;
  for (uint16_t i = 0; i < frame.wave_count; ++i) frame.wave[i] = 2048u;
  frame.wave[767] = 4095u;
  original = frame;
  ScreenView_RenderWavePeriods(&frame, SCREEN_WAVE_PERIOD_3);
  assert(s_last_wave_len == 761u);
  assert(s_wave_canvas[0] == 80u);
  assert(s_wave_canvas[760] == 152u);
  assert(memcmp(&frame, &original, sizeof(frame)) == 0);
}

static void test_one_period_does_not_read_second_period(void)
{
  ScreenDataFrame frame = {0};
  ScreenDataFrame original;
  frame.wave_count = 768u;
  for (uint16_t i = 0; i < frame.wave_count; ++i) frame.wave[i] = 2048u;
  frame.wave[255] = 1u;
  frame.wave[256] = 4095u;
  original = frame;
  ScreenView_RenderWavePeriods(&frame, SCREEN_WAVE_PERIOD_1);
  assert(s_last_wave_len == SCREEN_WAVE_VISIBLE_POINTS);
  assert(s_wave_canvas[0] == 80u);
  assert(s_wave_canvas[SCREEN_WAVE_VISIBLE_POINTS - 1u] == 8u);
  for (uint16_t i = 0; i < s_last_wave_len; ++i) assert(s_wave_canvas[i] <= 80u);
  assert(memcmp(&frame, &original, sizeof(frame)) == 0);

  frame.wave_count = 256u;
  original = frame;
  ScreenView_RenderWavePeriods(&frame, SCREEN_WAVE_PERIOD_1);
  assert(s_last_wave_len == SCREEN_WAVE_VISIBLE_POINTS);
  assert(s_wave_canvas[SCREEN_WAVE_VISIBLE_POINTS - 1u] == 8u);
  for (uint16_t i = 0; i < s_last_wave_len; ++i) assert(s_wave_canvas[i] <= 80u);
  assert(memcmp(&frame, &original, sizeof(frame)) == 0);
}

static void test_all_adc_codes_stay_inside_wave_height(void)
{
  ScreenDataFrame frame = {0};
  frame.wave_count = 64u;
  for (uint16_t base = 0; base < 4096u; base += 64u)
  {
    for (uint16_t i = 0; i < frame.wave_count; ++i) frame.wave[i] = base + i;
    ScreenView_RenderWavePeriods(&frame, SCREEN_WAVE_PERIOD_3);
    assert(s_last_wave_len == 64u);
    for (uint16_t i = 0; i < s_last_wave_len; ++i)
    {
      assert(s_wave_canvas[i] >= 8u);
      assert(s_wave_canvas[i] <= 152u);
      if (i > 0u) assert(s_wave_canvas[i] >= s_wave_canvas[i - 1u]);
    }
  }
}

static void test_zero_spectrum_uses_bottom_baseline(void)
{
  ScreenDataFrame frame = {0};
  frame.fft_count = 256u;
  ScreenView_RenderFrame(&frame, SCREEN_MODE_FFT, SCREEN_WAVE_PERIOD_3);
  assert(s_last_fft_len == 441u);
  for (uint16_t i = 0; i < s_last_fft_len; ++i) assert(s_fft_canvas[i] == 0u);
}

static void test_last_fft_bin_reaches_right_edge(void)
{
  ScreenDataFrame frame = {0};
  frame.fft_count = 256u;
  frame.fft[255] = 125u;
  ScreenView_RenderFrame(&frame, SCREEN_MODE_FFT, SCREEN_WAVE_PERIOD_3);
  assert(s_last_fft_len == 441u);
  assert(s_fft_canvas[0] == 0u);
  assert(s_fft_canvas[440] == 112u);
}

static void test_each_spectrum_bin_survives_width_mapping(void)
{
  ScreenDataFrame frame = {0};
  ScreenDataFrame original;
  frame.fft_count = 256u;
  for (uint16_t bin = 0; bin < 256u; ++bin)
  {
    memset(frame.fft, 0, sizeof(frame.fft));
    frame.fft[bin] = 100u;
    original = frame;
    ScreenView_RenderFrame(&frame, SCREEN_MODE_FFT, SCREEN_WAVE_PERIOD_3);
    unsigned peaks = 0u;
    assert(s_last_fft_len == 441u);
    for (uint16_t i = 0; i < s_last_fft_len; ++i)
    {
      assert(s_fft_canvas[i] <= 112u);
      if (s_fft_canvas[i] == 112u) ++peaks;
    }
    assert(peaks > 0u);
    assert(memcmp(&frame, &original, sizeof(frame)) == 0);
  }
}

static void test_low_frequency_stays_left_of_high_frequency(void)
{
  ScreenDataFrame frame = {0};
  frame.fft_count = 256u;
  frame.fft[51] = 50u;
  frame.fft[204] = 20u;
  ScreenView_RenderFrame(&frame, SCREEN_MODE_FFT, SCREEN_WAVE_PERIOD_3);
  assert(s_fft_canvas[88] == 112u);
  assert(s_fft_canvas[352] == 44u);
}

static void test_empty_and_single_point_wave(void)
{
  ScreenDataFrame frame = {0};
  const unsigned before = s_wave_addt_count;
  ScreenView_RenderWavePeriods(&frame, SCREEN_WAVE_PERIOD_3);
  assert(s_wave_addt_count == before);
  ScreenView_RenderWavePeriods(&frame, SCREEN_WAVE_PERIOD_1);
  assert(s_wave_addt_count == before);
  frame.wave_count = 1u;
  frame.wave[0] = 3072u;
  ScreenView_RenderWavePeriods(&frame, SCREEN_WAVE_PERIOD_3);
  assert(s_wave_addt_count == before + 1u);
  assert(s_last_wave_len == 1u);
  assert(s_wave_canvas[0] == 116u);
  ScreenView_RenderWavePeriods(&frame, SCREEN_WAVE_PERIOD_1);
  assert(s_wave_addt_count == before + 2u);
  assert(s_last_wave_len == 1u);
  assert(s_wave_canvas[0] == 116u);
}

int main(void)
{
  test_wave_peaks_fit_161_pixel_control();
  test_period_switch_does_not_duplicate_spectrum();
  test_three_periods_fit_width_and_preserve_last_sample();
  test_one_period_does_not_read_second_period();
  test_all_adc_codes_stay_inside_wave_height();
  test_zero_spectrum_uses_bottom_baseline();
  test_last_fft_bin_reaches_right_edge();
  test_each_spectrum_bin_survives_width_mapping();
  test_low_frequency_stays_left_of_high_frequency();
  test_empty_and_single_point_wave();
  puts("screen_view_tests: PASS (dir=0 canvas, 761x161 wave, 441x121 spectrum)");
  return 0;
}
