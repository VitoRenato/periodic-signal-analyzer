#include "acq_frame_tx.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 与 H743 端 test_acq_frame_header.c 共用的固定测试向量。 */
static void test_known_vector_and_header_encoding(void)
{
  static const uint8_t ascii_123456789[9] = {
    '1', '2', '3', '4', '5', '6', '7', '8', '9'
  };
  static const uint8_t expected_header[ACQ_FRAME_HEADER_BYTES] = {
    0xA5U, 0x5AU, 0x03U, 0x03U,
    0x52U, 0x00U,
    0x07U, 0x00U, 0x00U, 0x00U,
    0x17U, 0xDAU, 0xD5U, 0x7AU,
    0xB7U, 0x2BU,
    0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
  };
  uint8_t samples[8192];
  uint8_t header[ACQ_FRAME_HEADER_BYTES];
  AcqFrameTxInfo info;
  size_t i;

  assert(AcqFrameTx_Crc32(ascii_123456789, sizeof(ascii_123456789)) == 0xCBF43926U);
  assert(AcqFrameTx_Crc16(ascii_123456789, sizeof(ascii_123456789)) == 0x29B1U);

  for (i = 0U; i < 4096U; ++i)
  {
    const uint16_t code = (uint16_t)((i * 13U + 7U) & 0x0FFFU);
    samples[i * 2U + 0U] = (uint8_t)(code & 0xFFU);
    samples[i * 2U + 1U] = (uint8_t)((code >> 8) & 0xFFU);
  }

  info.flags = (uint8_t)(ACQ_FRAME_FLAG_BOOT | ACQ_FRAME_FLAG_REARM);
  info.timer_divider = 82U;
  info.sequence = 7U;
  info.request_generation = 1U;
  assert(AcqFrameTx_Encode(&info, samples, sizeof(samples), header));
  assert(memcmp(header, expected_header, sizeof(expected_header)) == 0);
}

static void test_header_version_and_reserved_bytes(void)
{
  uint8_t samples[2] = {0U, 0U};
  uint8_t header[ACQ_FRAME_HEADER_BYTES];
  AcqFrameTxInfo info = {0U, 82U, 1U, 1U};

  assert(AcqFrameTx_Encode(&info, samples, sizeof(samples), header));
  assert(header[2] == 3U);
  assert(header[16] == 1U);
  assert(header[17] == 0U);
  assert(header[18] == 0U);
  assert(header[19] == 0U);
  assert(header[20] == 0U);
  assert(header[21] == 0U);
}

int main(void)
{
  test_known_vector_and_header_encoding();
  test_header_version_and_reserved_bytes();
  puts("acq_frame_tx_tests: PASS");
  return 0;
}
