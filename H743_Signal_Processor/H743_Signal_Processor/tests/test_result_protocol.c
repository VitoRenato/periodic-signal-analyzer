#include "h743_crc32.h"
#include "h743_result_protocol.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_PI 3.14159265358979323846
#define TEST_KNOWN_RESULT_FLAGS 0x13U

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
                "FAIL %s:%d: expected 0x%08lx, got 0x%08lx\n",                 \
                __FILE__,                                                        \
                __LINE__,                                                        \
                (unsigned long)expected_value,                                   \
                (unsigned long)actual_value);                                    \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(expected, actual, tolerance)                                  \
    do                                                                           \
    {                                                                            \
        const double expected_value = (double)(expected);                        \
        const double actual_value = (double)(actual);                            \
        const double difference = fabs(actual_value - expected_value);           \
        if (difference > (double)(tolerance))                                    \
        {                                                                        \
            (void)fprintf(                                                       \
                stderr,                                                          \
                "FAIL %s:%d: expected %.12g, got %.12g, error %.12g\n",        \
                __FILE__,                                                        \
                __LINE__,                                                        \
                expected_value,                                                  \
                actual_value,                                                    \
                difference);                                                     \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

static const uint8_t g_expected_valid_frame[H743_RESULT_FRAME_SIZE] = {
    0x53U, 0x48U, 0x01U, 0x21U, 0x58U, 0x00U, 0x00U, 0x00U,
    0x78U, 0x56U, 0x34U, 0x12U, 0x0cU, 0x43U, 0x1fU, 0x00U,
    0x00U, 0x00U, 0x02U, 0x00U, 0x7dU, 0xe1U, 0xf5U, 0x05U,
    0xa0U, 0x4aU, 0x0bU, 0x00U, 0x98U, 0xa5U, 0x03U, 0x00U,
    0xd2U, 0x04U, 0x00U, 0x00U, 0x87U, 0xa5U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x7dU, 0xe1U, 0xf5U, 0x05U,
    0x00U, 0xe2U, 0x04U, 0x00U, 0xc3U, 0x2cU, 0x00U, 0x00U,
    0xfaU, 0xc2U, 0xebU, 0x0bU, 0xb0U, 0xadU, 0x01U, 0x00U,
    0x55U, 0x63U, 0xffU, 0xffU, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x13U, 0x00U, 0x00U, 0x00U, 0xdfU, 0xbfU, 0x58U, 0xdcU
};

static SignalResult make_valid_result(void)
{
    SignalResult result = {0};

    result.status = SIGNAL_STATUS_VALID;
    result.sample_rate_Hz = 2048780U;
    result.fundamental_Hz = 100000.125f;
    result.upp_V = 0.740f;
    result.urms_V = 0.239f;
    result.residual_rms_V = 0.001234f;
    result.snr_dB = 42.375f;
    result.component_count = 2U;
    result.component[0] = (SignalComponent){100000.125f, 0.320f, 0.200f};
    result.component[1] = (SignalComponent){200000.250f, 0.110f, -0.700f};
    result.recommended_sample_rate_Hz = 0U;
    return result;
}

static uint32_t read_u32_le(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8U)
        | ((uint32_t)bytes[2] << 16U)
        | ((uint32_t)bytes[3] << 24U);
}

static void write_u16_le(uint8_t bytes[2], uint16_t value)
{
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
}

static void write_u32_le(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xffU);
    bytes[3] = (uint8_t)((value >> 24U) & 0xffU);
}

static uint32_t reference_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_C(0xffffffff);
    size_t byte_index;

    for (byte_index = 0U; byte_index < length; ++byte_index)
    {
        unsigned int bit_index;

        crc ^= (uint32_t)data[byte_index];
        for (bit_index = 0U; bit_index < 8U; ++bit_index)
        {
            crc = (crc >> 1U)
                ^ (((crc & 1U) != 0U) ? UINT32_C(0xedb88320) : 0U);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

static void refresh_test_crc(uint8_t frame[H743_RESULT_FRAME_SIZE])
{
    write_u32_le(&frame[84], reference_crc32(frame, 84U));
}

static void check_frame_bytes(
    const uint8_t expected[H743_RESULT_FRAME_SIZE],
    const uint8_t actual[H743_RESULT_FRAME_SIZE])
{
    size_t byte_index;

    for (byte_index = 0U;
         byte_index < H743_RESULT_FRAME_SIZE;
         ++byte_index)
    {
        if (actual[byte_index] != expected[byte_index])
        {
            (void)fprintf(
                stderr,
                "FAIL byte %lu: expected 0x%02x, got 0x%02x\n",
                (unsigned long)byte_index,
                (unsigned int)expected[byte_index],
                (unsigned int)actual[byte_index]);
            ++g_failures;
        }
    }
}

static void check_decode_rejected_without_output_changes(
    const char *name,
    const uint8_t frame[H743_RESULT_FRAME_SIZE])
{
    SignalResult result;
    SignalResult before;
    uint32_t sequence = UINT32_C(0x89abcdef);
    uint32_t result_flags = UINT32_C(0x76543210);

    (void)memset(&result, 0x5a, sizeof(result));
    before = result;
    if (H743ResultProtocol_Decode(
            frame,
            &result,
            &sequence,
            &result_flags))
    {
        (void)fprintf(stderr, "FAIL malformed frame accepted: %s\n", name);
        ++g_failures;
    }
    CHECK_TRUE(memcmp(&before, &result, sizeof(result)) == 0);
    CHECK_U32(UINT32_C(0x89abcdef), sequence);
    CHECK_U32(UINT32_C(0x76543210), result_flags);
}

static void check_encode_rejected_without_frame_changes(
    const char *name,
    const SignalResult *result,
    uint32_t result_flags)
{
    uint8_t frame[H743_RESULT_FRAME_SIZE];
    size_t byte_index;

    (void)memset(frame, 0xa5, sizeof(frame));
    if (H743ResultProtocol_Encode(
            result,
            UINT32_C(0x12345678),
            result_flags,
            frame))
    {
        (void)fprintf(stderr, "FAIL invalid result encoded: %s\n", name);
        ++g_failures;
    }
    for (byte_index = 0U; byte_index < sizeof(frame); ++byte_index)
    {
        if (frame[byte_index] != 0xa5U)
        {
            (void)fprintf(
                stderr,
                "FAIL rejected encode modified byte %lu: %s\n",
                (unsigned long)byte_index,
                name);
            ++g_failures;
            break;
        }
    }
}

static void test_crc32_iso_hdlc_known_vector(void)
{
    static const uint8_t vector[] = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9'
    };
    const uint8_t dummy = 0U;

    CHECK_U32(UINT32_C(0xcbf43926), H743Crc32_Compute(vector, 9U));
    CHECK_U32(0U, H743Crc32_Compute(&dummy, 0U));
    CHECK_U32(0U, H743Crc32_Compute(NULL, 1U));
}

static void test_encode_matches_literal_88_byte_vector(void)
{
    const SignalResult result = make_valid_result();
    uint8_t frame[H743_RESULT_FRAME_SIZE];

    (void)memset(frame, 0xa5, sizeof(frame));
    CHECK_TRUE(H743ResultProtocol_Encode(
        &result,
        UINT32_C(0x12345678),
        TEST_KNOWN_RESULT_FLAGS,
        frame));
    check_frame_bytes(g_expected_valid_frame, frame);
    CHECK_U32(UINT32_C(0xdc58bfdf), reference_crc32(frame, 84U));
}

static void test_decode_literal_vector_and_round_trip(void)
{
    SignalResult decoded;
    uint32_t sequence = 0U;
    uint32_t result_flags = 0U;
    uint8_t round_trip[H743_RESULT_FRAME_SIZE];

    CHECK_TRUE(H743ResultProtocol_Decode(
        g_expected_valid_frame,
        &decoded,
        &sequence,
        &result_flags));
    CHECK_U32(UINT32_C(0x12345678), sequence);
    CHECK_U32(TEST_KNOWN_RESULT_FLAGS, result_flags);
    CHECK_U32(SIGNAL_STATUS_VALID, decoded.status);
    CHECK_U32(2048780U, decoded.sample_rate_Hz);
    CHECK_U32(2U, decoded.component_count);
    CHECK_NEAR(100000.125, decoded.fundamental_Hz, 0.001);
    CHECK_NEAR(0.740, decoded.upp_V, 0.000001);
    CHECK_NEAR(0.239, decoded.urms_V, 0.000001);
    CHECK_NEAR(0.001234, decoded.residual_rms_V, 0.000001);
    CHECK_NEAR(42.375, decoded.snr_dB, 0.001);
    CHECK_NEAR(100000.125, decoded.component[0].frequency_Hz, 0.001);
    CHECK_NEAR(0.320, decoded.component[0].amplitude_peak_V, 0.000001);
    CHECK_NEAR(0.200, decoded.component[0].phase_rad, 0.00002);
    CHECK_NEAR(200000.250, decoded.component[1].frequency_Hz, 0.001);
    CHECK_NEAR(0.110, decoded.component[1].amplitude_peak_V, 0.000001);
    CHECK_NEAR(-0.700, decoded.component[1].phase_rad, 0.00002);
    CHECK_U32(0U, decoded.recommended_sample_rate_Hz);

    CHECK_TRUE(H743ResultProtocol_Encode(
        &decoded,
        sequence,
        result_flags,
        round_trip));
    check_frame_bytes(g_expected_valid_frame, round_trip);
}

static void test_encode_clears_unused_slots_and_saturates_finite_metrics(void)
{
    SignalResult result = make_valid_result();
    uint8_t frame[H743_RESULT_FRAME_SIZE];
    size_t byte_index;

    result.component_count = 1U;
    result.upp_V = FLT_MAX;
    result.urms_V = FLT_MAX;
    result.residual_rms_V = FLT_MAX;
    result.snr_dB = FLT_MAX;
    result.component[0].amplitude_peak_V = FLT_MAX;
    result.component[1] = (SignalComponent){NAN, NAN, NAN};
    result.component[2] = (SignalComponent){INFINITY, -1.0f, INFINITY};

    CHECK_TRUE(H743ResultProtocol_Encode(
        &result,
        1U,
        TEST_KNOWN_RESULT_FLAGS,
        frame));
    CHECK_U32(UINT32_MAX, read_u32_le(&frame[24]));
    CHECK_U32(UINT32_MAX, read_u32_le(&frame[28]));
    CHECK_U32(UINT32_MAX, read_u32_le(&frame[32]));
    CHECK_U32(INT32_MAX, read_u32_le(&frame[36]));
    CHECK_U32(UINT32_MAX, read_u32_le(&frame[48]));
    for (byte_index = 56U; byte_index < 80U; ++byte_index)
    {
        CHECK_U32(0U, frame[byte_index]);
    }

    result.snr_dB = -FLT_MAX;
    CHECK_TRUE(H743ResultProtocol_Encode(
        &result,
        2U,
        TEST_KNOWN_RESULT_FLAGS,
        frame));
    CHECK_U32(UINT32_C(0x80000000), read_u32_le(&frame[36]));
}

static void test_encode_rejects_invalid_inputs(void)
{
    SignalResult result = make_valid_result();
    uint8_t frame[H743_RESULT_FRAME_SIZE];

    CHECK_TRUE(!H743ResultProtocol_Encode(
        NULL,
        0U,
        0U,
        frame));
    CHECK_TRUE(!H743ResultProtocol_Encode(
        &result,
        0U,
        0U,
        NULL));

    result.status = (SignalStatus)7;
    check_encode_rejected_without_frame_changes("status", &result, 0U);
    result = make_valid_result();
    result.component_count = 4U;
    check_encode_rejected_without_frame_changes("component_count", &result, 0U);
    result = make_valid_result();
    check_encode_rejected_without_frame_changes("reserved_flags", &result, 0x40U);
    result.fundamental_Hz = NAN;
    check_encode_rejected_without_frame_changes("fundamental_nan", &result, 0U);
    result = make_valid_result();
    result.fundamental_Hz = -1.0f;
    check_encode_rejected_without_frame_changes("fundamental_negative", &result, 0U);
    result = make_valid_result();
    result.fundamental_Hz = 5000000.0f;
    check_encode_rejected_without_frame_changes("fundamental_overflow", &result, 0U);
    result = make_valid_result();
    result.upp_V = -1.0f;
    check_encode_rejected_without_frame_changes("upp_negative", &result, 0U);
    result = make_valid_result();
    result.urms_V = INFINITY;
    check_encode_rejected_without_frame_changes("urms_infinite", &result, 0U);
    result = make_valid_result();
    result.residual_rms_V = NAN;
    check_encode_rejected_without_frame_changes("residual_nan", &result, 0U);
    result = make_valid_result();
    result.snr_dB = NAN;
    check_encode_rejected_without_frame_changes("snr_nan", &result, 0U);
    result = make_valid_result();
    result.component[0].frequency_Hz = 0.0f;
    check_encode_rejected_without_frame_changes("component_frequency_zero", &result, 0U);
    result = make_valid_result();
    result.component[0].frequency_Hz = 5000000.0f;
    check_encode_rejected_without_frame_changes("component_frequency_overflow", &result, 0U);
    result = make_valid_result();
    result.component[0].amplitude_peak_V = -0.1f;
    check_encode_rejected_without_frame_changes("amplitude_negative", &result, 0U);
    result = make_valid_result();
    result.component[0].phase_rad = NAN;
    check_encode_rejected_without_frame_changes("phase_nan", &result, 0U);
    result = make_valid_result();
    result.component[0].phase_rad = (float)TEST_PI;
    check_encode_rejected_without_frame_changes("phase_positive_pi", &result, 0U);
}

static void test_decode_rejects_corruption_in_each_region(void)
{
    static const size_t corruption_offsets[] = {0U, 20U, 44U, 56U, 68U, 84U};
    uint8_t frame[H743_RESULT_FRAME_SIZE];
    size_t corruption_index;

    for (corruption_index = 0U;
         corruption_index
             < (sizeof(corruption_offsets) / sizeof(corruption_offsets[0]));
         ++corruption_index)
    {
        (void)memcpy(frame, g_expected_valid_frame, sizeof(frame));
        frame[corruption_offsets[corruption_index]] ^= 0x01U;
        check_decode_rejected_without_output_changes("crc_protected_region", frame);
    }
}

static void test_decode_rejects_semantic_errors_with_valid_crc(void)
{
    uint8_t frame[H743_RESULT_FRAME_SIZE];

    (void)memcpy(frame, g_expected_valid_frame, sizeof(frame));
    frame[0] ^= 0x01U;
    refresh_test_crc(frame);
    check_decode_rejected_without_output_changes("magic", frame);

    (void)memcpy(frame, g_expected_valid_frame, sizeof(frame));
    frame[2] = 2U;
    refresh_test_crc(frame);
    check_decode_rejected_without_output_changes("version", frame);

    (void)memcpy(frame, g_expected_valid_frame, sizeof(frame));
    frame[3] = 0x22U;
    refresh_test_crc(frame);
    check_decode_rejected_without_output_changes("message_type", frame);

    (void)memcpy(frame, g_expected_valid_frame, sizeof(frame));
    write_u16_le(&frame[4], 87U);
    refresh_test_crc(frame);
    check_decode_rejected_without_output_changes("total_length", frame);

    (void)memcpy(frame, g_expected_valid_frame, sizeof(frame));
    frame[6] = 1U;
    refresh_test_crc(frame);
    check_decode_rejected_without_output_changes("header_flags", frame);

    (void)memcpy(frame, g_expected_valid_frame, sizeof(frame));
    frame[19] = 1U;
    refresh_test_crc(frame);
    check_decode_rejected_without_output_changes("reserved0", frame);

    (void)memcpy(frame, g_expected_valid_frame, sizeof(frame));
    frame[18] = 4U;
    refresh_test_crc(frame);
    check_decode_rejected_without_output_changes("component_count", frame);

    (void)memcpy(frame, g_expected_valid_frame, sizeof(frame));
    write_u16_le(&frame[16], 7U);
    refresh_test_crc(frame);
    check_decode_rejected_without_output_changes("status", frame);

    (void)memcpy(frame, g_expected_valid_frame, sizeof(frame));
    frame[80] |= 0x40U;
    refresh_test_crc(frame);
    check_decode_rejected_without_output_changes("result_flags", frame);

    (void)memcpy(frame, g_expected_valid_frame, sizeof(frame));
    frame[68] = 1U;
    refresh_test_crc(frame);
    check_decode_rejected_without_output_changes("nonzero_unused_slot", frame);

    (void)memcpy(frame, g_expected_valid_frame, sizeof(frame));
    write_u32_le(&frame[52], 180000U);
    refresh_test_crc(frame);
    check_decode_rejected_without_output_changes("phase_out_of_range", frame);

    (void)memcpy(frame, g_expected_valid_frame, sizeof(frame));
    write_u32_le(&frame[44], 0U);
    refresh_test_crc(frame);
    check_decode_rejected_without_output_changes("zero_used_frequency", frame);
}

static void test_nonvalid_status_boundaries_round_trip(void)
{
    SignalResult source = make_valid_result();
    SignalResult decoded;
    uint8_t frame[H743_RESULT_FRAME_SIZE];
    uint32_t sequence = 0U;
    uint32_t result_flags = 0U;

    source.status = SIGNAL_STATUS_SAMPLE_RATE_UNSAFE;
    source.recommended_sample_rate_Hz = 10000000U;
    CHECK_TRUE(H743ResultProtocol_Encode(
        &source,
        17U,
        UINT32_C(0x33),
        frame));
    CHECK_TRUE(H743ResultProtocol_Decode(
        frame,
        &decoded,
        &sequence,
        &result_flags));
    CHECK_U32(SIGNAL_STATUS_SAMPLE_RATE_UNSAFE, decoded.status);
    CHECK_U32(10000000U, decoded.recommended_sample_rate_Hz);
    CHECK_U32(17U, sequence);
    CHECK_U32(UINT32_C(0x33), result_flags);

    source = (SignalResult){0};
    source.status = SIGNAL_STATUS_NO_SIGNAL;
    source.sample_rate_Hz = 2048780U;
    CHECK_TRUE(H743ResultProtocol_Encode(&source, 18U, 0U, frame));
    CHECK_TRUE(H743ResultProtocol_Decode(
        frame,
        &decoded,
        &sequence,
        &result_flags));
    CHECK_U32(SIGNAL_STATUS_NO_SIGNAL, decoded.status);
    CHECK_U32(0U, decoded.component_count);
    CHECK_U32(18U, sequence);
    CHECK_U32(0U, result_flags);
}

static void test_decode_rejects_null_outputs(void)
{
    SignalResult result;
    uint32_t sequence;
    uint32_t result_flags;

    CHECK_TRUE(!H743ResultProtocol_Decode(
        NULL,
        &result,
        &sequence,
        &result_flags));
    CHECK_TRUE(!H743ResultProtocol_Decode(
        g_expected_valid_frame,
        NULL,
        &sequence,
        &result_flags));
    CHECK_TRUE(!H743ResultProtocol_Decode(
        g_expected_valid_frame,
        &result,
        NULL,
        &result_flags));
    CHECK_TRUE(!H743ResultProtocol_Decode(
        g_expected_valid_frame,
        &result,
        &sequence,
        NULL));
}

int main(void)
{
    test_crc32_iso_hdlc_known_vector();
    test_encode_matches_literal_88_byte_vector();
    test_decode_literal_vector_and_round_trip();
    test_encode_clears_unused_slots_and_saturates_finite_metrics();
    test_encode_rejects_invalid_inputs();
    test_decode_rejects_corruption_in_each_region();
    test_decode_rejects_semantic_errors_with_valid_crc();
    test_nonvalid_status_boundaries_round_trip();
    test_decode_rejects_null_outputs();

    if (g_failures != 0U)
    {
        (void)fprintf(
            stderr,
            "%lu result protocol test(s) failed\n",
            (unsigned long)g_failures);
        return 1;
    }

    (void)puts("result_protocol_tests: PASS");
    return 0;
}