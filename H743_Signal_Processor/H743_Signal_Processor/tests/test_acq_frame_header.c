#include "acq_frame_header.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                          \
    do                                                                       \
    {                                                                        \
        if (!(expr))                                                         \
        {                                                                    \
            (void)fprintf(stderr, "FAIL %s:%d: %s\n",                        \
                          __FILE__, __LINE__, #expr);                        \
            abort();                                                         \
        }                                                                    \
    } while (0)

static void fill_known_frame(uint8_t frame[ACQ_FRAME_TOTAL_BYTES])
{
    static const uint8_t known_header[ACQ_FRAME_HEADER_BYTES] = {
        0xA5U, 0x5AU, 0x01U, 0x03U,
        0x52U, 0x00U,
        0x07U, 0x00U, 0x00U, 0x00U,
        0x17U, 0xDAU, 0xD5U, 0x7AU,
        0xA6U, 0x71U
    };
    size_t i;

    memcpy(frame, known_header, sizeof(known_header));
    for (i = 0U; i < 4096U; ++i)
    {
        const uint16_t code = (uint16_t)((i * 13U + 7U) & 0x0FFFU);
        frame[ACQ_FRAME_HEADER_BYTES + i * 2U + 0U] = (uint8_t)(code & 0xFFU);
        frame[ACQ_FRAME_HEADER_BYTES + i * 2U + 1U] = (uint8_t)((code >> 8) & 0xFFU);
    }
}

static void test_valid_frame_from_f407_vector(void)
{
    uint8_t frame[ACQ_FRAME_TOTAL_BYTES];
    AcqFrameHeader header;

    fill_known_frame(frame);
    CHECK(AcqFrameHeader_Validate(frame, &header) == ACQ_FRAME_OK);
    CHECK(header.flags == (ACQ_FRAME_FLAG_BOOT | ACQ_FRAME_FLAG_REARM));
    CHECK(AcqFrameHeader_IsNewSession(&header));
    CHECK(header.timer_divider == 82U);
    CHECK(header.sample_rate_Hz == 2048780U);
    CHECK(header.sequence == 7U);
}

static void test_corruption_and_misalignment_are_rejected(void)
{
    uint8_t frame[ACQ_FRAME_TOTAL_BYTES];
    AcqFrameHeader header;

    fill_known_frame(frame);
    frame[0] ^= 0x01U;
    CHECK(AcqFrameHeader_Validate(frame, &header) == ACQ_FRAME_ERR_MAGIC);

    fill_known_frame(frame);
    frame[2] = 99U;
    CHECK(AcqFrameHeader_Validate(frame, &header) == ACQ_FRAME_ERR_VERSION);

    fill_known_frame(frame);
    frame[6] ^= 0x10U;
    CHECK(AcqFrameHeader_Validate(frame, &header) == ACQ_FRAME_ERR_HEADER_CRC);

    fill_known_frame(frame);
    frame[ACQ_FRAME_HEADER_BYTES + 100U] ^= 0x01U;
    CHECK(AcqFrameHeader_Validate(frame, &header) == ACQ_FRAME_ERR_SAMPLE_CRC);

    /* 全 0 旧帧或字节错位帧必须被拒绝，不能带着错位的样本进算法。 */
    memset(frame, 0, sizeof(frame));
    CHECK(AcqFrameHeader_Validate(frame, &header) == ACQ_FRAME_ERR_MAGIC);
}

int main(void)
{
    test_valid_frame_from_f407_vector();
    test_corruption_and_misalignment_are_rejected();
    puts("acq_frame_header_tests: PASS");
    return 0;
}
