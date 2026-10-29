#include "h743_set_fs_protocol.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t g_failures = 0U;

#define CHECK_TRUE(expression)                                                   \
    do                                                                           \
    {                                                                            \
        if (!(expression))                                                       \
        {                                                                        \
            (void)fprintf(                                                       \
                stderr,                                                          \
                "FAIL %s:%d: %s\n",                                            \
                __FILE__,                                                        \
                __LINE__,                                                        \
                #expression);                                                    \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

#define CHECK_U32(expected, actual)                                              \
    do                                                                           \
    {                                                                            \
        const uint32_t expected_value = (uint32_t)(expected);                    \
        const uint32_t actual_value = (uint32_t)(actual);                        \
        if (actual_value != expected_value)                                      \
        {                                                                        \
            (void)fprintf(                                                       \
                stderr,                                                          \
                "FAIL %s:%d: expected %lu, got %lu\n",                         \
                __FILE__,                                                        \
                __LINE__,                                                        \
                (unsigned long)expected_value,                                   \
                (unsigned long)actual_value);                                    \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

static void test_known_2048780_hz_frame(void)
{
    /* 22 字节 v3 命令槽:CRC 覆盖控制字段、请求代号、实际分析采样率/载荷。 */
    static const uint8_t expected[H743_SET_FS_FRAME_SIZE] = {
        0x5AU, 0xA5U, 0x81U, 0x00U, 0x0CU,
        0x43U, 0x1FU, 0x00U, 0xE5U, 0x80U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    uint8_t frame[H743_SET_FS_FRAME_SIZE] = {0};

    CHECK_TRUE(H743SetFsProtocol_Encode(2048780U, frame));
    CHECK_TRUE(memcmp(expected, frame, sizeof(expected)) == 0);
}

static void test_crc_covers_payload_and_control_fields(void)
{
    uint8_t first[H743_SET_FS_FRAME_SIZE];
    uint8_t second[H743_SET_FS_FRAME_SIZE];
    static const uint8_t payload_a[6] = {1U, 2U, 3U, 4U, 5U, 6U};
    static const uint8_t payload_b[6] = {1U, 2U, 3U, 4U, 5U, 7U};

    CHECK_TRUE(H743SetFsProtocol_EncodePage(
        2048780U, 0x81U, 1U, 2048780U, payload_a, first));
    CHECK_TRUE(H743SetFsProtocol_EncodePage(
        2048780U, 0x81U, 1U, 2048780U, payload_b, second));
    CHECK_TRUE(memcmp(&first[8U], &second[8U], 2U) != 0);
    CHECK_TRUE(memcmp(&first[10U], &second[10U], 12U) != 0);
}

static void test_invalid_arguments_leave_output_unchanged(void)
{
    uint8_t frame[H743_SET_FS_FRAME_SIZE];
    uint8_t before[H743_SET_FS_FRAME_SIZE];

    (void)memset(frame, 0xa5, sizeof(frame));
    (void)memcpy(before, frame, sizeof(before));
    CHECK_TRUE(!H743SetFsProtocol_Encode(4999U, frame));
    CHECK_TRUE(memcmp(before, frame, sizeof(frame)) == 0);

    CHECK_TRUE(!H743SetFsProtocol_Encode(20000001U, frame));
    CHECK_TRUE(memcmp(before, frame, sizeof(frame)) == 0);
    CHECK_TRUE(!H743SetFsProtocol_Encode(2048780U, NULL));
}

static void test_actual_rate_uses_f407_integer_divider(void)
{
    CHECK_U32(0U, H743SetFsProtocol_ActualRate(4999U));
    CHECK_U32(5000U, H743SetFsProtocol_ActualRate(5000U));
    CHECK_U32(2048780U, H743SetFsProtocol_ActualRate(2048780U));
    CHECK_U32(10500000U, H743SetFsProtocol_ActualRate(10000000U));
    CHECK_U32(21000000U, H743SetFsProtocol_ActualRate(20000000U));
    CHECK_U32(0U, H743SetFsProtocol_ActualRate(20000001U));
}

int main(void)
{
    test_known_2048780_hz_frame();
    test_crc_covers_payload_and_control_fields();
    test_invalid_arguments_leave_output_unchanged();
    test_actual_rate_uses_f407_integer_divider();

    if (g_failures != 0U)
    {
        (void)fprintf(
            stderr,
            "%lu SET_FS protocol test(s) failed\n",
            (unsigned long)g_failures);
        return 1;
    }

    (void)printf("SET_FS protocol tests passed\n");
    return 0;
}
