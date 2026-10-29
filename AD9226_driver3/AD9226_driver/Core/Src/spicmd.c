/**
 * @file    spicmd.c
 * @brief   解析 SPI1 全双工帧中命令槽的数据 → 改 AD9226 采样率
 * @note    命令通过 SPI1 MISO 在每帧开头的 16 字节命令槽中由 H723 从机发回。
 *          F407 做主机, spi.c 负责把命令槽原始字节交给本模块解析。
 *          单 SPI 全双工方案下命令槽从偏移 0 开始,无需再处理 SPI2 dummy 偏移。
 */
#include "spicmd.h"
#include "ad9226.h"
#include "h743_measurement_rx.h"
#include <stdio.h>
#include <string.h>

#define SPICMD_FRAME_LEN  22U   /* v3 命令槽：控制字段 + 请求代号 + 实际采样率 + 测量页载荷 */
#define SPICMD_MAGIC      0xA55AU
#define SPICMD_TYPE_SET_FS    0x81U
#define SPICMD_TYPE_SET_EQ_F0 0x02U   /* 等效采样:payload = 基频 f0(Hz) */

static volatile uint32_t s_ok_cnt  = 0;
static volatile uint32_t s_err_cnt = 0;
static volatile uint32_t s_meas_freq_hz = 0;
static volatile uint16_t s_meas_vpp_mv  = 0;
static volatile uint8_t  s_meas_valid   = 0;
static H743MeasurementRx s_measurement_rx;

/* ---- CRC16/CCITT-FALSE:poly=0x1021 init=0xFFFF refin=false refout=false xorout=0x0000 ----
 * 覆盖 [0..7] 与 [10..21]，输出 2 字节小端；与 H743 编码端一致。
 */
static uint16_t crc16_update(uint16_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i]) << 8;
        for (uint32_t b = 0; b < 8; b++) {
            if (crc & 0x8000U) crc = (crc << 1) ^ 0x1021U;
            else              crc = (crc << 1);
        }
    }
    return crc;
}

static uint16_t spicmd_frame_crc(const uint8_t *frame)
{
    uint16_t crc = crc16_update(0xFFFFU, frame, 8U);

    return crc16_update(crc, &frame[10], 12U);
}

void SpiCmd_Init(void)
{
    s_ok_cnt  = 0;
    s_err_cnt = 0;
    s_meas_freq_hz = 0;
    s_meas_vpp_mv  = 0;
    s_meas_valid   = 0;
    H743MeasurementRx_Init(&s_measurement_rx);
}

void SpiCmd_ProcessRx(const uint8_t *rx_buf, uint32_t len)
{
    if (rx_buf == NULL || len < SPICMD_FRAME_LEN) {
        return;
    }

    /*
     * 命令槽从偏移 0 开始。保留在 [0..1] 范围内搜索 magic 的能力，
     * 以防 H7 端偶尔有 1 字节前置偏移（旧 SPI2 方案残留兼容）。
     */
    uint32_t off = 0;
    bool     magic_found = false;
    for (uint32_t i = 0; i <= (len - SPICMD_FRAME_LEN); i++) {
        uint16_t m = (uint16_t)rx_buf[i] | ((uint16_t)rx_buf[i + 1] << 8);
        if (m == SPICMD_MAGIC) {
            off = i;
            magic_found = true;
            break;
        }
    }

    if (!magic_found) {
        /* 全 0xFF 视为空命令槽，不算错误 */
        bool all_ff = true;
        for (uint32_t i = 0; i < len; i++) {
            if (rx_buf[i] != 0xFFU) {
                all_ff = false;
                break;
            }
        }
        if (!all_ff) {
            s_err_cnt++;
            printf("[CMD] err no magic  raw=");
            for (uint32_t i = 0; i < len && i < 16U; i++) {
                printf("%02X", (unsigned)rx_buf[i]);
            }
            printf("\r\n");
        }
        return;
    }

    /* 解析 10 字节命令帧(小端) */
    uint16_t magic    = (uint16_t)rx_buf[off] | ((uint16_t)rx_buf[off + 1] << 8);
    uint8_t  type     = rx_buf[off + 2];
    uint8_t  flags    = rx_buf[off + 3];
    uint32_t fs_hz    =  (uint32_t)rx_buf[off + 4]
                      | ((uint32_t)rx_buf[off + 5] << 8)
                      | ((uint32_t)rx_buf[off + 6] << 16)
                      | ((uint32_t)rx_buf[off + 7] << 24);
    uint16_t crc_rx   = (uint16_t)rx_buf[off + 8] | ((uint16_t)rx_buf[off + 9] << 8);
    uint16_t crc_calc = spicmd_frame_crc(&rx_buf[off]);

    bool frame_ok = (magic == SPICMD_MAGIC) && (crc_rx == crc_calc);

    if (!frame_ok) {
        /* magic=0 视为空槽(NOP),不算错误 */
        if (magic != 0x0000U) {
            s_err_cnt++;
            printf("[CMD] err magic=0x%04X crc_rx=0x%04X crc_calc=0x%04X  raw=",
                   (unsigned)magic, (unsigned)crc_rx, (unsigned)crc_calc);
            for (uint32_t i = 0; i < len && i < 16U; i++) {
                printf("%02X", (unsigned)rx_buf[i]);
            }
            printf("\r\n");
        }
        return;
    }

    /*
     * 后 6 字节是五页测量模型。只有同一模型代号的 0~4 页全部通过
     * CRC、顺序和语义检查后，才更新兼容的 f0/Vpp 快照。
     */
    if (frame_ok && ((off + SPICMD_FRAME_LEN) <= len)
        && H743MeasurementRx_ProcessFrame(
            &s_measurement_rx,
            &rx_buf[off],
            SPICMD_FRAME_LEN))
    {
        H743MeasurementModel model;

        if (H743MeasurementRx_GetPublished(&s_measurement_rx, &model))
        {
            if (H743MeasurementModel_IsDisplayable(&model))
            {
                const uint32_t vpp_mv = (model.upp_uV + 500U) / 1000U;

                s_meas_freq_hz = model.fundamental_Hz;
                s_meas_vpp_mv = (vpp_mv > 65535U)
                    ? 65535U
                    : (uint16_t)vpp_mv;
                s_meas_valid = 1U;
            }
            else
            {
                s_meas_freq_hz = 0U;
                s_meas_vpp_mv = 0U;
                s_meas_valid = 0U;
            }
        }
    }

    if (type == SPICMD_TYPE_SET_FS) {
        AD9226_ClearEqMode();   /* 切回普通模式 */
        uint32_t old_fs = AD9226_GetSampleRate();
        if (AD9226_SetSampleRate(fs_hz) == HAL_OK) {
            s_ok_cnt++;
            /* 每页结果都捎带 SET_FS;只在实际采样率变化时打印,避免刷屏 */
            if (AD9226_GetSampleRate() != old_fs) printf("[CMD] SET_FS: %lu -> %lu Hz, meas f0=%lu Hz vpp=%u mV (%s)\r\n",
                   (unsigned long)old_fs, (unsigned long)AD9226_GetSampleRate(),
                   (unsigned long)s_meas_freq_hz, (unsigned)s_meas_vpp_mv,
                   (s_meas_valid != 0u) ? "valid" : "invalid");
        } else {
            s_err_cnt++;
            printf("[CMD] SET_FS rejected: %lu Hz (out of range)\r\n",
                   (unsigned long)fs_hz);
        }
    } else if (type == SPICMD_TYPE_SET_EQ_F0) {
        uint32_t f0_hz = fs_hz;   /* 字段复用:这里 payload 是基频 */
        if (AD9226_SetEqFundamental(f0_hz) == HAL_OK) {
            s_ok_cnt++;
            printf("[CMD] SET_EQ_F0: f0=%lu Hz, divider=%u, Fs=%lu Hz (eq mode ON)\r\n",
                   (unsigned long)f0_hz,
                   (unsigned)AD9226_GetEqDivider(),
                   (unsigned long)AD9226_GetSampleRate());
        } else {
            s_err_cnt++;
            printf("[CMD] SET_EQ_F0 rejected: f0=%lu Hz\r\n",
                   (unsigned long)f0_hz);
        }
    } else {
        s_err_cnt++;
        printf("[CMD] unknown type=0x%02X flags=0x%02X\r\n",
               (unsigned)type, (unsigned)flags);
    }
}

void SpiCmd_ResetMeasurement(void)
{
    s_meas_freq_hz = 0;
    s_meas_vpp_mv  = 0;
    s_meas_valid   = 0;
    H743MeasurementRx_Init(&s_measurement_rx);
}

uint32_t SpiCmd_GetOkCount(void)  { return s_ok_cnt;  }
uint32_t SpiCmd_GetErrCount(void) { return s_err_cnt; }

bool SpiCmd_GetMeasurement(uint32_t *freq_hz, uint16_t *vpp_mv)
{
    if ((freq_hz == NULL) || (vpp_mv == NULL))
    {
        return false;
    }

    if (s_meas_valid == 0)
    {
        return false;
    }

    *freq_hz = s_meas_freq_hz;
    *vpp_mv  = s_meas_vpp_mv;
    return true;
}

bool SpiCmd_GetMeasurementModel(H743MeasurementModel *model)
{
    return H743MeasurementRx_GetPublished(&s_measurement_rx, model);
}

bool SpiCmd_TakeSessionStart(void)
{
    return H743MeasurementRx_TakeSessionStart(&s_measurement_rx);
}

void SpiCmd_SetRequestGeneration(uint16_t request_generation)
{
    H743MeasurementRx_SetRequestGeneration(&s_measurement_rx,
                                           request_generation);
}
