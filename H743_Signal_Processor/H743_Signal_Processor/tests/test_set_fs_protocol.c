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
    /* 16 字节命令槽:前 10 字节为 SET_FS 命令,flags=0x01,后 6 字节为 0(未填测量值) */
    static const uint8_t expected[H743_SET_FS_FRAME_SIZE] = {
        0x5aU, 0xa5U, 0x01U, 0x01U, 0x0cU,
        0x43U, 0x1fU, 0x00U, 0xfdU, 0x20U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    uint8_t frame[H743_SET_FS_FRAME_SIZE] = {0};

    CHECK_TRUE(H743SetFsProtocol_Encode(2048780U, frame));
    CHECK_TRUE(memcmp(expected, frame, sizeof(expected)) == 0);
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
