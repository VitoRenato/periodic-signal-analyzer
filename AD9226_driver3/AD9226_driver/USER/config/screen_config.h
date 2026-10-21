/**
 * @file screen_config.h
 * @brief Screen layout and refresh tuning knobs.
 */
#ifndef SCREEN_CONFIG_H
#define SCREEN_CONFIG_H

#include <stdint.h>

/* HMI UART handle: use USART2 (PA2 TX / PA3 RX) on the AD9226 F4 board.
 * USART1 is kept for debug console. */
#define HMI_UART_HANDLE huart2

#define SCREEN_PAGE_NAME "page0"

#define SCREEN_WAVE_CTRL "s_wave"
#define SCREEN_FFT_CTRL "s_fft"

/* addt 点值同时受 8 位范围和控件实际高度限制。尺寸保留 40 像素网格边界。 */
#define SCREEN_WAVE_WIDTH          761u
#define SCREEN_WAVE_HEIGHT         161u
#define SCREEN_FFT_WIDTH           441u
#define SCREEN_FFT_HEIGHT          121u
#define SCREEN_CURVE_SAMPLE_MAX    255u
#define SCREEN_CURVE_SAMPLE_MARGIN   8u
#define SCREEN_WAVE_CODE_ZERO     2048u
#define SCREEN_WAVE_CODE_RADIUS   2047u

#define SCREEN_WAVE_VISIBLE_POINTS SCREEN_WAVE_WIDTH
#define SCREEN_FFT_VISIBLE_POINTS  SCREEN_FFT_WIDTH

#if (SCREEN_WAVE_HEIGHT > (SCREEN_CURVE_SAMPLE_MAX + 1u)) \
    || (SCREEN_FFT_HEIGHT > (SCREEN_CURVE_SAMPLE_MAX + 1u)) \
    || (SCREEN_WAVE_HEIGHT <= (2u * SCREEN_CURVE_SAMPLE_MARGIN + 1u)) \
    || (SCREEN_FFT_HEIGHT <= (SCREEN_CURVE_SAMPLE_MARGIN + 1u))
#error "Curve heights must fit byte coordinates and leave room for display margins"
#endif

#define SCREEN_DEFAULT_WAVE_PERIODS SCREEN_WAVE_PERIOD_3

/* FFT 显示纵轴参考选择：
 *   0 : 每帧按当前最大值归一化（小信号自动充满屏幕，但大谐波会看起来接近基波）。
 *   >0: 固定参考值（ADC LSB 幅度），不同幅度尖峰高度成比例。
 *       AD9226 满幅 2Vpp 对应幅度约 2048 LSB。
 */
#define SCREEN_FFT_FIXED_MAX 0

#define SCREEN_REFRESH_INTERVAL_MS 300u
#define SCREEN_STATUS_INTERVAL_MS 500u
#define SCREEN_HMI_BOOT_DELAY_MS 500u

#define SCREEN_TEST_ENABLE 0u
#define SCREEN_TEST_UPDATE_INTERVAL_MS 200u

#define SCREEN_COLOR_WHITE 65535u
#define SCREEN_COLOR_GREEN 2016u
#define SCREEN_COLOR_YELLOW 65504u

#endif /* SCREEN_CONFIG_H */
