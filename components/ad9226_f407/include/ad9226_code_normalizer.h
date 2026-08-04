/*
 * 可选兼容模块：用于项目中曾观察到的低8位连续码恢复。
 * 获取完整12位并行数据时不应默认启用。
 */
#ifndef AD9226_CODE_NORMALIZER_H
#define AD9226_CODE_NORMALIZER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief AD9226低8位连续解包的诊断统计。
 */
typedef struct
{
    uint16_t raw_min;
    uint16_t raw_max;
    uint16_t normalized_min;
    uint16_t normalized_max;
    int16_t frame_shift;
    uint16_t max_step;
    uint8_t ambiguous;
} Ad9226CodeNormalizeStats;

/**
 * @brief 按时间连续性恢复低8位跨界的ADC码，并把帧中心移到参考码附近。
 * @return true表示整帧可唯一解包；false表示参数非法、越界或出现128码歧义。
 */
bool Ad9226CodeNormalizer_Run(
    uint16_t *samples,
    size_t sample_count,
    uint16_t anchor_code,
    Ad9226CodeNormalizeStats *stats);

#endif
