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
static uint8_t s_last_wave_data[SCREEN_WAVE_VISIBLE_POINTS];
static uint16_t s_last_wave_len;
static uint8_t s_last_fft_data[SCREEN_FFT_VISIBLE_POINTS];
static uint16_t s_last_fft_len;

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart,
                                    uint8_t *data,
                                    uint16_t len,
                                    uint32_t timeout)
{
  (void)huart;
  (void)data;
  (void)len;
  (void)timeout;
  return HAL_OK;
}

void HAL_Delay(uint32_t delay_ms)
{
  (void)delay_ms;
}

uint32_t HAL_GetTick(void)
{
  return 0u;
}

void HMI_SetText(const char *obj, const char *text)
{
  (void)obj;
  (void)text;
}

bool HMI_Addt_Send(const char *curve_id, uint8_t ch, const uint8_t *data, uint16_t len)
{
  (void)ch;
  assert(len > 0u);
  if (strcmp(curve_id, "s_wave") == 0)
  {
    assert(len <= SCREEN_WAVE_VISIBLE_POINTS);
    memcpy(s_last_wave_data, data, len);
    s_last_wave_len = len;
    ++s_wave_addt_count;
  }
  else if (strcmp(curve_id, "s_fft") == 0)
  {
    assert(len <= SCREEN_FFT_VISIBLE_POINTS);
    memcpy(s_last_fft_data, data, len);
    s_last_fft_len = len;
    ++s_fft_addt_count;
  }
  else
  {
    assert(0);
  }
  return true;
}

void HMI_ClearWave(const char *obj, uint8_t channel_mask)
{
  assert(channel_mask == 255u);
  if (strcmp(obj, "s_wave") == 0)
  {
    ++s_wave_clear_count;
  }
  else if (strcmp(obj, "s_fft") == 0)
  {
    ++s_fft_clear_count;
  }
  else
  {
    assert(0);
  }
}

void HMI_GotoPage(const char *page_name)
{
  (void)page_name;
}

static void test_period_switch_does_not_duplicate_spectrum(void)
{
  ScreenDataFrame frame;
  uint16_t i;

  memset(&frame, 0, sizeof(frame));
  frame.wave_count = 768u;
  frame.fft_count = 256u;
  for (i = 0u; i < frame.wave_count; ++i)
  {
    frame.wave[i] = (uint16_t)(i & 0x0FFFu);
  }
  frame.fft[51] = 63u;
  frame.fft[102] = 322u;

  ScreenView_RenderFrame(&frame, SCREEN_MODE_WAVE, SCREEN_WAVE_PERIOD_3);
  assert(s_wave_addt_count == 1u);
  assert(s_fft_addt_count == 1u);
  assert(s_fft_clear_count == 1u);

  ScreenView_RenderWavePeriods(&frame, SCREEN_WAVE_PERIOD_1);
  assert(s_wave_addt_count == 2u);
  assert(s_wave_clear_count == 2u);
  assert(s_fft_addt_count == 1u);
  assert(s_fft_clear_count == 1u);
}

static void test_wave_zero_and_peaks_are_centered_on_128(void)
{
  ScreenDataFrame frame;

  memset(&frame, 0, sizeof(frame));
  frame.wave_count = 3u;
  frame.wave[0] = 1u;
  frame.wave[1] = 2048u;
  frame.wave[2] = 4095u;

  ScreenView_RenderWavePeriods(&frame, SCREEN_WAVE_PERIOD_3);
  assert(s_last_wave_len == 3u);
  assert(s_last_wave_data[0] < 128u);
  assert(s_last_wave_data[1] == 128u);
  assert(s_last_wave_data[2] > 128u);
  assert((128u - s_last_wave_data[0])
      == (s_last_wave_data[2] - 128u));
}

static void test_zero_spectrum_stays_on_positive_axis_baseline(void)
{
  ScreenDataFrame frame;
  uint16_t index;

  memset(&frame, 0, sizeof(frame));
  frame.fft_count = 256u;

  ScreenView_RenderFrame(&frame, SCREEN_MODE_FFT, SCREEN_WAVE_PERIOD_3);
  assert(s_last_fft_len == SCREEN_FFT_VISIBLE_POINTS);
  for (index = 0u; index < s_last_fft_len; ++index)
  {
    assert(s_last_fft_data[index] == 128u);
  }
}

static void test_last_fft_bin_reaches_last_visible_positive_point(void)
{
  ScreenDataFrame frame;

  memset(&frame, 0, sizeof(frame));
  frame.fft_count = 256u;
  frame.fft[255] = 125u;

  ScreenView_RenderFrame(&frame, SCREEN_MODE_FFT, SCREEN_WAVE_PERIOD_3);
  assert(s_last_fft_len == SCREEN_FFT_VISIBLE_POINTS);
  assert(s_last_fft_data[0] == 128u);
  assert(s_last_fft_data[SCREEN_FFT_VISIBLE_POINTS - 1u] > 128u);
}

int main(void)
{
  test_period_switch_does_not_duplicate_spectrum();
  test_wave_zero_and_peaks_are_centered_on_128();
  test_zero_spectrum_stays_on_positive_axis_baseline();
  test_last_fft_bin_reaches_last_visible_positive_point();
  puts("screen_view_tests: PASS");
  return 0;
}
