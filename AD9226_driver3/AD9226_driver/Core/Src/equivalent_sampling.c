#include "equivalent_sampling.h"

#include <limits.h>
#include <math.h>
#include <string.h>

/* 线性主循环中复用工作区，避免占用较大的函数栈。 */
static int64_t g_bin_sums[ES_PERIOD_POINTS];
static uint16_t g_bin_counts[ES_PERIOD_POINTS];
static int16_t g_align_buffer[ES_PERIOD_POINTS];
static int16_t g_published_period[ES_PERIOD_POINTS];
static EsWaveformInfo g_waveform_info;

static uint32_t EsPhaseIncrement(
    uint32_t fundamental_hz,
    uint16_t timer_divider)
{
    /* 用32位无符号数表示一整个周期，溢出后自然回到下一周期起点。 */
    const uint64_t numerator =
        ((uint64_t)fundamental_hz * timer_divider) << 32;

    return (uint32_t)(numerator / ES_TIMER_CLOCK_HZ);
}

static int16_t EsRoundedAverage(int64_t sum, uint16_t count)
{
    if (sum >= 0) {
        return (int16_t)((sum + count / 2U) / count);
    }

    return (int16_t)((sum - count / 2U) / count);
}

static void EsAlignFundamental(int16_t period[ES_PERIOD_POINTS])
{
    float sine_component = 0.0f;
    float cosine_component = 0.0f;

    /* 只估计基频相位，不删除重建波形中的谐波分量。 */
    for (uint16_t i = 0U; i < ES_PERIOD_POINTS; ++i) {
        const float angle =
            2.0f * ES_PI * (float)i / (float)ES_PERIOD_POINTS;
        sine_component += (float)period[i] * sinf(angle);
        cosine_component += (float)period[i] * cosf(angle);
    }

    const float phase = atan2f(cosine_component, sine_component);
    const int32_t shift =
        (int32_t)lroundf(
            -phase * (float)ES_PERIOD_POINTS / (2.0f * ES_PI));

    for (uint16_t i = 0U; i < ES_PERIOD_POINTS; ++i) {
        int32_t source = ((int32_t)i + shift) % (int32_t)ES_PERIOD_POINTS;
        if (source < 0) {
            source += ES_PERIOD_POINTS;
        }
        g_align_buffer[i] = period[source];
    }

    memcpy(period, g_align_buffer, sizeof(g_align_buffer));
}

uint16_t EsChooseTimerDivider(uint32_t fundamental_hz)
{
    uint16_t best_divider = ES_TIMER_DIVIDER_MIN;
    uint16_t best_coverage = 0U;
    uint64_t best_error = UINT64_MAX;

    if (fundamental_hz == 0UL) {
        return best_divider;
    }

    for (uint16_t divider = ES_TIMER_DIVIDER_MIN;
         divider <= ES_TIMER_DIVIDER_MAX;
         ++divider) {
        memset(g_bin_counts, 0, sizeof(g_bin_counts));

        /* 先模拟4096个采样点在256个相位格中的分布。 */
        const uint32_t increment =
            EsPhaseIncrement(fundamental_hz, divider);
        uint32_t phase = 0UL;

        for (uint16_t i = 0U; i < ES_EQ_SAMPLE_COUNT; ++i) {
            ++g_bin_counts[phase >> 24];
            phase += increment;
        }

        uint16_t coverage = 0U;
        uint64_t error = 0UL;
        for (uint16_t bin = 0U; bin < ES_PERIOD_POINTS; ++bin) {
            if (g_bin_counts[bin] != 0U) {
                ++coverage;
            }

            const int32_t difference =
                (int32_t)g_bin_counts[bin] - 16;
            error += (uint64_t)(difference * difference);
        }

        /* 优先覆盖更多相位格，覆盖相同时选择每格点数更均匀的分频值。 */
        if ((coverage > best_coverage) ||
            ((coverage == best_coverage) && (error < best_error))) {
            best_divider = divider;
            best_coverage = coverage;
            best_error = error;
        }
    }

    return best_divider;
}

bool EsReconstructPeriod(
    const int16_t samples[ES_EQ_SAMPLE_COUNT],
    uint32_t fundamental_hz,
    uint16_t timer_divider,
    int16_t period[ES_PERIOD_POINTS])
{
    if ((fundamental_hz == 0UL) ||
        (timer_divider < ES_TIMER_DIVIDER_MIN) ||
        (timer_divider > ES_TIMER_DIVIDER_MAX)) {
        return false;
    }

    memset(g_bin_sums, 0, sizeof(g_bin_sums));
    memset(g_bin_counts, 0, sizeof(g_bin_counts));

    const uint32_t increment =
        EsPhaseIncrement(fundamental_hz, timer_divider);
    uint32_t phase = 0UL;

    /* 将跨越多个真实周期的采样点折叠到同一个基频周期。 */
    for (uint16_t i = 0U; i < ES_EQ_SAMPLE_COUNT; ++i) {
        const uint16_t bin = (uint16_t)(phase >> 24);
        g_bin_sums[bin] += samples[i];
        ++g_bin_counts[bin];
        phase += increment;
    }

    uint16_t coverage = 0U;
    for (uint16_t bin = 0U; bin < ES_PERIOD_POINTS; ++bin) {
        if (g_bin_counts[bin] != 0U) {
            /* 同一相位格中的多次采样取平均，可减小随机噪声。 */
            period[bin] =
                EsRoundedAverage(g_bin_sums[bin], g_bin_counts[bin]);
            ++coverage;
        }
    }

    if (coverage < 2U) {
        return false;
    }

    /* 空相位格使用首尾相连的线性插值补齐。 */
    for (uint16_t bin = 0U; bin < ES_PERIOD_POINTS; ++bin) {
        if (g_bin_counts[bin] != 0U) {
            continue;
        }

        uint16_t left_distance = 1U;
        uint16_t right_distance = 1U;

        while (g_bin_counts[
                   (uint16_t)((bin + ES_PERIOD_POINTS - left_distance) &
                              0x00FFU)] == 0U) {
            ++left_distance;
        }
        while (g_bin_counts[
                   (uint16_t)((bin + right_distance) & 0x00FFU)] == 0U) {
            ++right_distance;
        }

        const uint16_t left =
            (uint16_t)((bin + ES_PERIOD_POINTS - left_distance) & 0x00FFU);
        const uint16_t right =
            (uint16_t)((bin + right_distance) & 0x00FFU);
        const int32_t weighted =
            (int32_t)period[left] * right_distance +
            (int32_t)period[right] * left_distance;

        period[bin] =
            (int16_t)(weighted / (left_distance + right_distance));
    }

    EsAlignFundamental(period);
    return true;
}

void EsWaveform_Publish(
    const int16_t period[ES_PERIOD_POINTS],
    uint32_t sequence,
    uint32_t fundamental_hz,
    uint16_t timer_divider)
{
    /* 先复制波形，再更新与这份波形对应的批次和采样参数。 */
    memcpy(g_published_period, period, sizeof(g_published_period));

    g_waveform_info.sequence = sequence;
    g_waveform_info.fundamental_hz = fundamental_hz;
    g_waveform_info.sample_rate_hz =
        ES_TIMER_CLOCK_HZ / timer_divider;
    g_waveform_info.point_count = ES_PERIOD_POINTS;
    g_waveform_info.valid = 1U;
}

uint16_t EsWaveform_Copy(
    int16_t *destination,
    uint16_t capacity,
    uint16_t cycles,
    EsWaveformInfo *info)
{
    if ((g_waveform_info.valid == 0U) ||
        ((cycles != 1U) && (cycles != 3U))) {
        return 0U;
    }

    const uint16_t point_count = (uint16_t)(cycles * ES_PERIOD_POINTS);
    if (capacity < point_count) {
        return 0U;
    }

    for (uint16_t cycle = 0U; cycle < cycles; ++cycle) {
        /* 三周期显示直接重复同一份已对齐的基频周期。 */
        memcpy(
            &destination[cycle * ES_PERIOD_POINTS],
            g_published_period,
            sizeof(g_published_period));
    }

    *info = g_waveform_info;
    info->point_count = point_count;
    return point_count;
}

/* 普通采样模式下，用基频从 raw ADC 截取一个周期并重采样到 ES_PERIOD_POINTS 点。
 * 不依赖等效采样分频范围，只要 raw buffer 里有一个完整周期即可。
 */
bool NormalReconstructPeriod(
    const int16_t samples[ES_EQ_SAMPLE_COUNT],
    uint32_t sample_count,
    uint32_t freq_hz,
    uint32_t fs_hz,
    int16_t period[ES_PERIOD_POINTS])
{
    if ((samples == NULL) || (period == NULL) ||
        (freq_hz == 0U) || (fs_hz == 0U) || (sample_count < 4U)) {
        return false;
    }

    const float samples_per_period = (float)fs_hz / (float)freq_hz;
    if ((samples_per_period < 2.0f) || (samples_per_period > (float)sample_count)) {
        return false;
    }

    /* 找最近的上升过零点，锁定相位，使波形静止。 */
    uint32_t start = 0U;
    uint32_t search_limit = (uint32_t)(samples_per_period * 2.0f + 0.5f);
    if (search_limit > (sample_count - 1U)) {
        search_limit = sample_count - 1U;
    }
    for (uint32_t i = 0U; (i + 1U) < sample_count && i < search_limit; ++i) {
        if ((samples[i] <= 0) && (samples[i + 1U] > 0)) {
            start = i;
            break;
        }
    }

    /* 用精确的小数周期长度重建，避免把 N 个样本硬凑成一个周期造成的
     * 首尾不连续，从而显著减小 FFT 频谱泄漏。 */
    for (uint16_t i = 0U; i < ES_PERIOD_POINTS; ++i) {
        const float phase = (float)i / (float)ES_PERIOD_POINTS;
        const float fidx = (float)start + phase * samples_per_period;
        const uint32_t idx0 = (uint32_t)fidx;
        const float frac = fidx - (float)idx0;
        const uint32_t s0 = idx0 % sample_count;
        const uint32_t s1 = (idx0 + 1U) % sample_count;

        const float v0 = (float)samples[s0];
        const float v1 = (float)samples[s1];
        int32_t v = (int32_t)(v0 + frac * (v1 - v0) + 0.5f);
        if (v < INT16_MIN) {
            v = INT16_MIN;
        } else if (v > INT16_MAX) {
            v = INT16_MAX;
        }
        period[i] = (int16_t)v;
    }

    return true;
}

/* 256 点基 2 FFT（复数输入），输出各 bin 的幅度。 */
static void Fft256Radix2(float real[ES_PERIOD_POINTS], float imag[ES_PERIOD_POINTS])
{
    uint16_t i, j, k;
    uint16_t len;
    float t_real, t_imag;
    float u_real, u_imag;

    /* 位反转置换 */
    j = 0U;
    for (i = 0U; i < ES_PERIOD_POINTS; ++i)
    {
        if (i < j)
        {
            t_real = real[i];
            t_imag = imag[i];
            real[i] = real[j];
            imag[i] = imag[j];
            real[j] = t_real;
            imag[j] = t_imag;
        }
        for (k = ES_PERIOD_POINTS >> 1U; (k & j) != 0U; k >>= 1U)
        {
            j ^= k;
        }
        j ^= k;
    }

    /* 蝶形运算 */
    for (len = 2U; len <= ES_PERIOD_POINTS; len <<= 1U)
    {
        const float ang = -2.0f * ES_PI / (float)len;
        const float wlen_real = cosf(ang);
        const float wlen_imag = sinf(ang);

        for (i = 0U; i < ES_PERIOD_POINTS; i += len)
        {
            float w_real = 1.0f;
            float w_imag = 0.0f;

            for (k = 0U; k < (len >> 1U); ++k)
            {
                const uint16_t u_idx = i + k;
                const uint16_t v_idx = u_idx + (len >> 1U);

                u_real = real[u_idx];
                u_imag = imag[u_idx];
                t_real = real[v_idx] * w_real - imag[v_idx] * w_imag;
                t_imag = real[v_idx] * w_imag + imag[v_idx] * w_real;

                real[u_idx] = u_real + t_real;
                imag[u_idx] = u_imag + t_imag;
                real[v_idx] = u_real - t_real;
                imag[v_idx] = u_imag - t_imag;

                const float next_w_real = w_real * wlen_real - w_imag * wlen_imag;
                const float next_w_imag = w_real * wlen_imag + w_imag * wlen_real;
                w_real = next_w_real;
                w_imag = next_w_imag;
            }
        }
    }
}

bool ComputePeriodSpectrum(
    const int16_t period[ES_PERIOD_POINTS],
    uint32_t raw_spectrum[ES_PERIOD_POINTS],
    uint16_t display_spectrum[ES_PERIOD_POINTS])
{
    static float real[ES_PERIOD_POINTS];
    static float imag[ES_PERIOD_POINTS];
    float mean = 0.0f;
    uint16_t i;

    if ((period == NULL) || (raw_spectrum == NULL) || (display_spectrum == NULL))
    {
        return false;
    }

    /* 去掉直流分量，让基频/谐波峰值在频域更明显 */
    for (i = 0U; i < ES_PERIOD_POINTS; ++i)
    {
        mean += (float)period[i];
    }
    mean /= (float)ES_PERIOD_POINTS;

    for (i = 0U; i < ES_PERIOD_POINTS; ++i)
    {
        real[i] = (float)period[i] - mean;
        imag[i] = 0.0f;
    }

    /* 加 Hanning 窗抑制非相干采样导致的频谱泄漏，
     * 让正弦输入的 FFT 更干净，谐波检测更可靠。 */
    if (ES_PERIOD_POINTS > 1U)
    {
        const float denom = (float)(ES_PERIOD_POINTS - 1U);
        for (i = 0U; i < ES_PERIOD_POINTS; ++i)
        {
            const float w = 0.5f - 0.5f * cosf((2.0f * ES_PI * (float)i) / denom);
            real[i] *= w;
        }
    }

    Fft256Radix2(real, imag);

    /* 同时输出两组谱线：
     *
     * 1) raw_spectrum：原始 FFT 模值（uint32）。谐波提取依赖
     *    peak_mag / fundamental_mag 的比例，使用原始模值可以避免
     *    缩放到 uint16 后因数值变小、整数舍入导致阈值判断不稳定。
     *
     * 2) display_spectrum：幅度谱（ADC LSB 为单位）。原始 FFT 模值对满幅
     *    正弦约为 幅度 × (N/2)；加 Hanning 窗后相干增益约 0.5，所以峰值
     *    ≈ 幅度 × (N/2) × 0.5 = 幅度 × (N/4)。N=256 时系数为 64。
     *    除以 64 后满幅输入不会溢出，频域波形上的尖峰高度就能反映幅度差异。
     */
    const float fft_scale = (float)(ES_PERIOD_POINTS / 4U);

    for (i = 0U; i < ES_PERIOD_POINTS; ++i)
    {
        const float mag = sqrtf((real[i] * real[i]) + (imag[i] * imag[i]));
        float scaled;

        raw_spectrum[i] = (mag > (float)UINT32_MAX) ? UINT32_MAX : (uint32_t)(mag + 0.5f);

        scaled = mag / fft_scale;
        if (scaled > 65535.0f)
        {
            scaled = 65535.0f;
        }
        display_spectrum[i] = (uint16_t)(scaled + 0.5f);
    }

    return true;
}
