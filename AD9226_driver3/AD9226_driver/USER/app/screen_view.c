#include "screen_view.h"

#include "hmi_tjc.h"
#include "screen_config.h"
#include <stdio.h>
#include "screen_app.h"

static void ScreenView_FormatUv(uint32_t uv, char *value, size_t value_size, char *unit, size_t unit_size)
{
  if (uv >= 1000000u)
  {
    (void)snprintf(value, value_size, "%lu.%03lu", (unsigned long)(uv / 1000000u), (unsigned long)((uv % 1000000u) / 1000u));
    (void)snprintf(unit, unit_size, "V");
  }
  else if (uv >= 1000u)
  {
    (void)snprintf(value, value_size, "%lu.%03lu", (unsigned long)(uv / 1000u), (unsigned long)(uv % 1000u));
    (void)snprintf(unit, unit_size, "mV");
  }
  else
  {
    (void)snprintf(value, value_size, "%lu", (unsigned long)uv);
    (void)snprintf(unit, unit_size, "uV");
  }
}

static void ScreenView_FormatMhz(uint32_t mhz, char *value, size_t value_size, char *unit, size_t unit_size)
{
  if (mhz >= 1000000000u)
  {
    (void)snprintf(value, value_size, "%lu.%03lu", (unsigned long)(mhz / 1000000000u), (unsigned long)((mhz % 1000000000u) / 1000000u));
    (void)snprintf(unit, unit_size, "MHz");
  }
  else if (mhz >= 1000000u)
  {
    (void)snprintf(value, value_size, "%lu.%03lu", (unsigned long)(mhz / 1000000u), (unsigned long)((mhz % 1000000u) / 1000u));
    (void)snprintf(unit, unit_size, "kHz");
  }
  else
  {
    (void)snprintf(value, value_size, "%lu.%03lu", (unsigned long)(mhz / 1000u), (unsigned long)(mhz % 1000u));
    (void)snprintf(unit, unit_size, "Hz");
  }
}

static uint8_t ScreenView_MapWaveCode(uint16_t code)
{
  const int32_t zero = ((int32_t)SCREEN_WAVE_HEIGHT - 1) / 2;
  const int32_t radius = zero - (int32_t)SCREEN_CURVE_SAMPLE_MARGIN;
  int32_t delta = (int32_t)code - (int32_t)SCREEN_WAVE_CODE_ZERO;
  int32_t y;

  if (delta < -(int32_t)SCREEN_WAVE_CODE_RADIUS)
  {
    delta = -(int32_t)SCREEN_WAVE_CODE_RADIUS;
  }
  if (delta > (int32_t)SCREEN_WAVE_CODE_RADIUS)
  {
    delta = (int32_t)SCREEN_WAVE_CODE_RADIUS;
  }
  y = zero + (delta * radius) / (int32_t)SCREEN_WAVE_CODE_RADIUS;
  return (uint8_t)y;
}

static uint8_t ScreenView_MapSpectrum(uint16_t sample, uint16_t max_sample)
{
  const uint32_t span = SCREEN_FFT_HEIGHT - 1u - SCREEN_CURVE_SAMPLE_MARGIN;
  uint32_t y = 0u;

  if (max_sample != 0u)
  {
    y = ((uint32_t)sample * span) / max_sample;
  }
  if (y > span)
  {
    y = span;
  }
  return (uint8_t)y;
}

static uint16_t ScreenView_MaxU16(const uint16_t *data, uint16_t count)
{
  uint16_t max_value = 1u;
  uint16_t i;

  for (i = 0u; i < count; i++)
  {
    if (data[i] > max_value)
    {
      max_value = data[i];
    }
  }

  return max_value;
}

static void ScreenView_RenderMeasurements(const ScreenDataFrame *frame)
{
  char value[24];
  char unit[8];
  uint8_t i;

  ScreenView_FormatUv(frame->vpp_uv, value, sizeof(value), unit, sizeof(unit));
  HMI_SetText("vpp_uint", unit);
  HMI_SetText("t_vpp", value);

  ScreenView_FormatUv(frame->vrms_uv, value, sizeof(value), unit, sizeof(unit));
  HMI_SetText("vrms_unit", unit);
  HMI_SetText("t_rms", value);

  ScreenView_FormatMhz(frame->frequency_mhz, value, sizeof(value), unit, sizeof(unit));
  HMI_SetText("f1_unit", unit);
  HMI_SetText("t_f1", value);

  for (i = 0u; i < 3u; i++)
  {
    char obj[12];
    char text_obj[12];
    char unit_obj[16];

    (void)snprintf(obj, sizeof(obj), "f0%u", (unsigned int)(i + 1u));
    (void)snprintf(text_obj, sizeof(text_obj), "v0%u", (unsigned int)(i + 1u));
    (void)snprintf(unit_obj, sizeof(unit_obj), "f0%u_unit", (unsigned int)(i + 1u));
    ScreenView_FormatMhz(frame->harmonic_freq_mhz[i], value, sizeof(value), unit, sizeof(unit));
    HMI_SetText(obj, value);
    HMI_SetText(unit_obj, unit);

    (void)snprintf(unit_obj, sizeof(unit_obj), "v0%u_unit", (unsigned int)(i + 1u));
    /* 赛题频谱幅值按正弦峰值显示，不使用 RMS 冒充谱线高度。 */
    ScreenView_FormatUv(frame->harmonic_peak_uv[i], value, sizeof(value), unit, sizeof(unit));
    HMI_SetText(text_obj, value);
    HMI_SetText(unit_obj, unit);
  }
}

static void ScreenView_RenderWave(const ScreenDataFrame *frame, ScreenWavePeriodMode periods)
{
  uint16_t visible_count = frame->wave_count;
  uint16_t points;
  uint16_t i;
  static ScreenWavePeriodMode s_last_periods = 0xFFu;
  static uint8_t s_pixel_buf[SCREEN_WAVE_VISIBLE_POINTS];

  /* 冻结帧始终保存三周期；PA15 只决定显示前一周期还是全部三周期。 */
  if ((periods == SCREEN_WAVE_PERIOD_1) && (visible_count > 256u))
  {
    visible_count = 256u;
  }

  points = (visible_count < SCREEN_WAVE_VISIBLE_POINTS) ? visible_count : SCREEN_WAVE_VISIBLE_POINTS;
  if (points == 0u)
  {
    return;
  }

  for (i = 0u; i < points; i++)
  {
    uint16_t src = 0u;
    if (points > 1u)
    {
      src = (uint16_t)(((uint32_t)i * (visible_count - 1u)) / (points - 1u));
    }
    s_pixel_buf[i] = ScreenView_MapWaveCode(frame->wave[src]);
  }

  /* 切换显示周期数时清屏，避免残留上一模式的波形 */
  if (periods != s_last_periods)
  {
    s_last_periods = periods;
    HMI_ClearWave(SCREEN_WAVE_CTRL, 255u);
  }
  HMI_Addt_Send(SCREEN_WAVE_CTRL, 0u, s_pixel_buf, points);
}


static void ScreenView_RenderFft(const ScreenDataFrame *frame)
{
  uint16_t points = (frame->fft_count != 0u) ? SCREEN_FFT_VISIBLE_POINTS : 0u;
  uint16_t max_value;
  uint16_t i;
  static uint8_t s_pixel_buf[SCREEN_FFT_VISIBLE_POINTS];

#if SCREEN_FFT_FIXED_MAX > 0
  max_value = (uint16_t)SCREEN_FFT_FIXED_MAX;
#else
  max_value = ScreenView_MaxU16(frame->fft, frame->fft_count);
#endif

  if (points == 0u)
  {
    return;
  }

  for (i = 0u; i < points; i++)
  {
    /* 源频点覆盖整个控件宽度，保留最高频率端点。 */
    uint16_t src = 0u;
    if (points > 1u)
    {
      src = (uint16_t)(((uint32_t)i * (frame->fft_count - 1u))
                       / (points - 1u));
    }
    s_pixel_buf[i] = ScreenView_MapSpectrum(frame->fft[src], max_value);
  }
  /* addt 会向曲线缓存继续追加；重画前先清空，避免出现整份重复频谱。 */
  HMI_ClearWave(SCREEN_FFT_CTRL, 255u);
  HMI_Addt_Send(SCREEN_FFT_CTRL, 0u, s_pixel_buf, points);
}

void ScreenView_Init(void)
{
  HAL_Delay(SCREEN_HMI_BOOT_DELAY_MS);
  HMI_GotoPage(SCREEN_PAGE_NAME);
  HMI_ClearWave(SCREEN_WAVE_CTRL, 255u);
  HMI_ClearWave(SCREEN_FFT_CTRL, 255u);
  ScreenView_SetMode(SCREEN_MODE_WAVE);
  ScreenView_SetWavePeriods((ScreenWavePeriodMode)SCREEN_DEFAULT_WAVE_PERIODS);
}

void ScreenView_SetMode(ScreenDisplayMode mode)
{
  (void)mode;
}

void ScreenView_SetWavePeriods(ScreenWavePeriodMode periods)
{
  HMI_SetText("T_num", (periods == SCREEN_WAVE_PERIOD_1) ? "1" : "3");
}

void ScreenView_RenderWavePeriods(const ScreenDataFrame *frame, ScreenWavePeriodMode periods)
{
  if (frame == NULL)
  {
    return;
  }

  ScreenView_SetWavePeriods(periods);
  ScreenView_RenderWave(frame, periods);
}

void ScreenView_SetLinkState(uint8_t linked, uint32_t sequence)
{
  (void)linked;
  (void)sequence;
}

void ScreenView_RenderFrame(const ScreenDataFrame *frame, ScreenDisplayMode mode, ScreenWavePeriodMode periods)
{
  (void)mode;

  if (frame == NULL)
  {
    return;
  }

  ScreenView_RenderMeasurements(frame);

  /* 时域波形始终刷新；频域波形有数据时也同时刷新 */
  ScreenView_RenderWavePeriods(frame, periods);
  if (frame->fft_count > 0u)
  {
    ScreenView_RenderFft(frame);
  }
}

void ScreenView_ShowMeasurementError(void)
{
  uint8_t i;

  HMI_ClearWave(SCREEN_WAVE_CTRL, 255u);
  HMI_ClearWave(SCREEN_FFT_CTRL, 255u);
  HMI_SetText("t_f1", "ERR");
  HMI_SetText("f1_unit", "");
  HMI_SetText("t_vpp", "---");
  HMI_SetText("vpp_uint", "");
  HMI_SetText("t_rms", "---");
  HMI_SetText("vrms_unit", "");

  for (i = 0u; i < 3u; ++i)
  {
    char freq_obj[12];
    char peak_obj[12];
    char freq_unit_obj[16];
    char peak_unit_obj[16];

    (void)snprintf(freq_obj, sizeof(freq_obj), "f0%u", (unsigned)(i + 1u));
    (void)snprintf(peak_obj, sizeof(peak_obj), "v0%u", (unsigned)(i + 1u));
    (void)snprintf(freq_unit_obj, sizeof(freq_unit_obj), "f0%u_unit", (unsigned)(i + 1u));
    (void)snprintf(peak_unit_obj, sizeof(peak_unit_obj), "v0%u_unit", (unsigned)(i + 1u));
    HMI_SetText(freq_obj, "---");
    HMI_SetText(peak_obj, "---");
    HMI_SetText(freq_unit_obj, "");
    HMI_SetText(peak_unit_obj, "");
  }
}
