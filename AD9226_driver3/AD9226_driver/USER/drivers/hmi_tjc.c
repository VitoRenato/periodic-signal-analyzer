#include "hmi_tjc.h"

#include "screen_config.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

#define HMI_RX_BUF_SIZE 256u
#define HMI_TX_TIMEOUT_MS 20u
#define HMI_ADDT_READY_TIMEOUT_MS 300u
#define HMI_ADDT_DONE_TIMEOUT_MS 300u
#define HMI_FRAME_BUF_SIZE 64u

static const uint8_t kHmiEndMarker[3] = {0xFFu, 0xFFu, 0xFFu};

static HMI_OnTouchCb s_touch_cb;
static uint8_t s_rx_byte;
static uint8_t s_rx_buf[HMI_RX_BUF_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static uint8_t s_frame_buf[HMI_FRAME_BUF_SIZE];
static uint16_t s_frame_pos;
static uint8_t s_frame_ff_count;

/* 保留最近一次透明传输状态，便于实板通过 SWD 定位屏幕通信问题。 */
static volatile uint8_t s_last_response_code;
static volatile uint16_t s_addt_success_count;
static volatile uint16_t s_addt_failure_count;

extern UART_HandleTypeDef HMI_UART_HANDLE;

/* Note: __io_putchar is intentionally NOT defined here.
 * The AD9226 F4 project keeps debug printf on USART1 via syscalls.c.
 * HAL_UART_RxCpltCallback is handled in main.c and feeds this module
 * through HMI_RxByte(). */

static bool HMI_RxReadFrame(uint8_t *frame, uint16_t frame_size, uint16_t *actual_len);
static void HMI_ParseFrame(const uint8_t *frame, uint16_t len);

static bool HMI_SendBytes(const uint8_t *data, uint16_t len)
{
  return HAL_UART_Transmit(&HMI_UART_HANDLE, (uint8_t *)data, len, HMI_TX_TIMEOUT_MS) == HAL_OK;
}

void HMI_RxByte(uint8_t byte)
{
  uint16_t next;

  next = (uint16_t)((s_rx_head + 1u) % HMI_RX_BUF_SIZE);
  if (next != s_rx_tail)
  {
    s_rx_buf[s_rx_head] = byte;
    s_rx_head = next;
  }
}

static bool HMI_SendCmdChecked(const char *cmd)
{
  if (cmd == NULL)
  {
    return false;
  }

  if (!HMI_SendBytes((const uint8_t *)cmd, (uint16_t)strlen(cmd)))
  {
    return false;
  }
  return HMI_SendBytes(kHmiEndMarker, 3u);
}

void HMI_SendCmd(const char *cmd)
{
  (void)HMI_SendCmdChecked(cmd);
}

void HMI_SetText(const char *obj, const char *text)
{
  char buf[128];

  if ((obj == NULL) || (text == NULL))
  {
    return;
  }

  (void)snprintf(buf, sizeof(buf), "%s.txt=\"%s\"", obj, text);
  HMI_SendCmd(buf);
}

void HMI_SetValue(const char *obj, int32_t value)
{
  char buf[64];

  if (obj == NULL)
  {
    return;
  }

  (void)snprintf(buf, sizeof(buf), "%s.val=%ld", obj, (long)value);
  HMI_SendCmd(buf);
}

void HMI_SetColor565(const char *obj, uint16_t color)
{
  char buf[64];

  if (obj == NULL)
  {
    return;
  }

  (void)snprintf(buf, sizeof(buf), "%s.pco=%u", obj, (unsigned int)color);
  HMI_SendCmd(buf);
}

void HMI_AddWavePoint(const char *obj, uint8_t channel, uint8_t value)
{
  char buf[64];

  if (obj == NULL)
  {
    return;
  }

  (void)snprintf(buf, sizeof(buf), "add %s.id,%u,%u", obj, (unsigned int)channel, (unsigned int)value);
  HMI_SendCmd(buf);
}

static bool HMI_WaitForResponse(uint8_t wanted_code, uint32_t timeout_ms)
{
  uint8_t frame[HMI_FRAME_BUF_SIZE];
  uint16_t frame_len;
  uint32_t start = HAL_GetTick();

  while ((uint32_t)(HAL_GetTick() - start) < timeout_ms)
  {
    while (HMI_RxReadFrame(frame, sizeof(frame), &frame_len))
    {
      HMI_ParseFrame(frame, frame_len);
      if (frame_len > 0u)
      {
        s_last_response_code = frame[0];
        if (frame[0] == wanted_code)
        {
          return true;
        }
      }
    }
    HAL_Delay(1u);
  }

  return false;
}

bool HMI_Addt_Send(const char *curve_id, uint8_t ch, const uint8_t *data, uint16_t len)
{
  char cmd_buf[32];

  if ((curve_id == NULL) || (data == NULL) || (len == 0u))
  {
    return false;
  }

  (void)snprintf(cmd_buf, sizeof(cmd_buf), "addt %s.id,%u,%u", curve_id, (unsigned int)ch, (unsigned int)len);
  if (!HMI_SendCmdChecked(cmd_buf) || !HMI_WaitForResponse(0xFEu, HMI_ADDT_READY_TIMEOUT_MS))
  {
    s_addt_failure_count++;
    return false;
  }

  if (!HMI_SendBytes(data, len) || !HMI_WaitForResponse(0xFDu, HMI_ADDT_DONE_TIMEOUT_MS))
  {
    s_addt_failure_count++;
    return false;
  }

  s_addt_success_count++;
  return true;
}

void HMI_ClearWave(const char *obj, uint8_t channel_mask)
{
  char buf[64];

  if (obj == NULL)
  {
    return;
  }

  (void)snprintf(buf, sizeof(buf), "cle %s.id,%u", obj, (unsigned int)channel_mask);
  HMI_SendCmd(buf);
}

void HMI_GotoPage(const char *page_name)
{
  char buf[64];

  if (page_name == NULL)
  {
    return;
  }

  (void)snprintf(buf, sizeof(buf), "page %s", page_name);
  HMI_SendCmd(buf);
}

void HMI_SetTouchCallback(HMI_OnTouchCb cb)
{
  s_touch_cb = cb;
}

static bool HMI_RxReadByte(uint8_t *out)
{
  if (s_rx_head == s_rx_tail)
  {
    return false;
  }

  *out = s_rx_buf[s_rx_tail];
  s_rx_tail = (uint16_t)((s_rx_tail + 1u) % HMI_RX_BUF_SIZE);
  return true;
}

static bool HMI_RxReadFrame(uint8_t *frame, uint16_t frame_size, uint16_t *actual_len)
{
  uint8_t b;

  while (HMI_RxReadByte(&b))
  {
    if (s_frame_pos >= sizeof(s_frame_buf))
    {
      s_frame_pos = 0u;
      s_frame_ff_count = 0u;
    }

    s_frame_buf[s_frame_pos++] = b;
    s_frame_ff_count = (b == 0xFFu) ? (uint8_t)(s_frame_ff_count + 1u) : 0u;
    if (s_frame_ff_count >= 3u)
    {
      if ((frame == NULL) || (actual_len == NULL) || (s_frame_pos > frame_size))
      {
        s_frame_pos = 0u;
        s_frame_ff_count = 0u;
        return false;
      }

      memcpy(frame, s_frame_buf, s_frame_pos);
      *actual_len = s_frame_pos;
      s_frame_pos = 0u;
      s_frame_ff_count = 0u;
      return true;
    }
  }

  return false;
}

static void HMI_ParseFrame(const uint8_t *frame, uint16_t len)
{
  if ((frame == NULL) || (len < 3u))
  {
    return;
  }

  if ((frame[0] == 0x65u) && (len >= 7u) && (s_touch_cb != NULL))
  {
    s_touch_cb(frame[1], frame[2], frame[3]);
  }
}

void HMI_Init(void)
{
  s_touch_cb = NULL;
  s_rx_head = 0u;
  s_rx_tail = 0u;
  s_rx_byte = 0u;
  s_frame_pos = 0u;
  s_frame_ff_count = 0u;
  s_last_response_code = 0u;
  s_addt_success_count = 0u;
  s_addt_failure_count = 0u;
}

void HMI_Task(void)
{
  uint8_t frame[64];
  uint16_t frame_len;

  while (HMI_RxReadFrame(frame, sizeof(frame), &frame_len))
  {
    HMI_ParseFrame(frame, frame_len);
  }
}
