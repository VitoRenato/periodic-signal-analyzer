#include "hmi_tjc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

UART_HandleTypeDef huart2;

static uint32_t s_tick;
static char s_cmd[128];
static size_t s_cmd_len;
static uint16_t s_expected_raw_len;
static uint16_t s_raw_tx_count;
static uint16_t s_ready_count;
static uint16_t s_done_count;
static uint16_t s_touch_count;

static void feed_response(uint8_t code)
{
  HMI_RxByte(code);
  HMI_RxByte(0xFFu);
  HMI_RxByte(0xFFu);
  HMI_RxByte(0xFFu);
}

static void handle_command(void)
{
  s_cmd[s_cmd_len] = '\0';

  if (strncmp(s_cmd, "addt s_wave.id,0,", 17u) == 0)
  {
    s_expected_raw_len = (uint16_t)strtoul(&s_cmd[17], NULL, 10);
    s_ready_count++;
    feed_response(0xFEu);
  }
  else if (strncmp(s_cmd, "addt s_fft.id,0,", 16u) == 0)
  {
    s_expected_raw_len = (uint16_t)strtoul(&s_cmd[16], NULL, 10);
    s_ready_count++;
    feed_response(0xFEu);
  }
  else if (strncmp(s_cmd, "addt missing.id,0,", 18u) == 0)
  {
    feed_response(0x1Au);
  }
  else if (strcmp(s_cmd, "bad_name.txt=\"mV\"") == 0)
  {
    /* 模拟前置文本命令留下的“变量名无效”响应。 */
    feed_response(0x1Au);
  }

  s_cmd_len = 0u;
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart,
                                    uint8_t *data,
                                    uint16_t len,
                                    uint32_t timeout)
{
  (void)huart;
  (void)timeout;

  if (s_expected_raw_len != 0u)
  {
    assert(len == s_expected_raw_len);
    s_expected_raw_len = 0u;
    s_raw_tx_count++;
    s_done_count++;
    feed_response(0xFDu);
    return HAL_OK;
  }

  if ((len == 3u) && (data[0] == 0xFFu) && (data[1] == 0xFFu) && (data[2] == 0xFFu))
  {
    handle_command();
    return HAL_OK;
  }

  assert((s_cmd_len + len) < sizeof(s_cmd));
  memcpy(&s_cmd[s_cmd_len], data, len);
  s_cmd_len += len;
  return HAL_OK;
}

void HAL_Delay(uint32_t delay_ms)
{
  s_tick += delay_ms;
}

uint32_t HAL_GetTick(void)
{
  return s_tick++;
}

static void reset_fixture(void)
{
  s_tick = 0u;
  s_cmd_len = 0u;
  s_expected_raw_len = 0u;
  s_raw_tx_count = 0u;
  s_ready_count = 0u;
  s_done_count = 0u;
  s_touch_count = 0u;
  HMI_Init();
}

static void on_touch(uint8_t page_id, uint8_t component_id, uint8_t event_type)
{
  assert(page_id == 2u);
  assert(component_id == 3u);
  assert(event_type == 1u);
  s_touch_count++;
}

static void test_fragmented_response_is_not_discarded(void)
{
  static const uint8_t touch_frame[7] = {0x65u, 2u, 3u, 1u, 0xFFu, 0xFFu, 0xFFu};
  size_t i;

  reset_fixture();
  HMI_SetTouchCallback(on_touch);
  for (i = 0u; i < sizeof(touch_frame); ++i)
  {
    HMI_RxByte(touch_frame[i]);
    HMI_Task();
  }
  assert(s_touch_count == 1u);
}

static void test_two_addt_transfers_wait_for_ready_and_done(void)
{
  static const uint8_t wave[4] = {1u, 2u, 3u, 4u};
  static const uint8_t fft[3] = {9u, 8u, 7u};

  reset_fixture();
  HMI_SetText("bad_name", "mV");

  assert(HMI_Addt_Send("s_wave", 0u, wave, sizeof(wave)));
  assert(HMI_Addt_Send("s_fft", 0u, fft, sizeof(fft)));
  assert(s_ready_count == 2u);
  assert(s_raw_tx_count == 2u);
  assert(s_done_count == 2u);
}

static void test_invalid_curve_never_sends_raw_payload(void)
{
  static const uint8_t data[2] = {1u, 2u};

  reset_fixture();
  assert(!HMI_Addt_Send("missing", 0u, data, sizeof(data)));
  assert(s_ready_count == 0u);
  assert(s_raw_tx_count == 0u);
  assert(s_done_count == 0u);
}

int main(void)
{
  test_fragmented_response_is_not_discarded();
  test_two_addt_transfers_wait_for_ready_and_done();
  test_invalid_curve_never_sends_raw_payload();
  puts("hmi_tjc_tests: PASS");
  return 0;
}
