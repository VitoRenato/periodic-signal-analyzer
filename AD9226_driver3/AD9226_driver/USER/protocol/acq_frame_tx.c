#include "acq_frame_tx.h"

/* CRC-32/ISO-HDLC 半字节查表：reflected 0xEDB88320，16 项表即可，8 KB 约 0.3 ms。 */
static const uint32_t s_crc32_nibble_table[16] = {
  0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
  0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
  0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
  0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU
};

static void AcqFrameTx_WriteU16Le(uint8_t *bytes, uint16_t value)
{
  bytes[0] = (uint8_t)(value & 0xFFU);
  bytes[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void AcqFrameTx_WriteU32Le(uint8_t *bytes, uint32_t value)
{
  bytes[0] = (uint8_t)(value & 0xFFU);
  bytes[1] = (uint8_t)((value >> 8) & 0xFFU);
  bytes[2] = (uint8_t)((value >> 16) & 0xFFU);
  bytes[3] = (uint8_t)((value >> 24) & 0xFFU);
}

uint32_t AcqFrameTx_Crc32(const uint8_t *data, size_t length)
{
  uint32_t crc = 0xFFFFFFFFU;
  size_t index;

  if ((data == NULL) && (length != 0U))
  {
    return 0U;
  }
  for (index = 0U; index < length; ++index)
  {
    crc ^= data[index];
    crc = (crc >> 4) ^ s_crc32_nibble_table[crc & 0x0FU];
    crc = (crc >> 4) ^ s_crc32_nibble_table[crc & 0x0FU];
  }
  return crc ^ 0xFFFFFFFFU;
}

uint16_t AcqFrameTx_Crc16(const uint8_t *data, size_t length)
{
  uint16_t crc = 0xFFFFU;
  size_t index;

  for (index = 0U; index < length; ++index)
  {
    uint8_t bit;

    crc ^= (uint16_t)((uint16_t)data[index] << 8);
    for (bit = 0U; bit < 8U; ++bit)
    {
      crc = ((crc & 0x8000U) != 0U)
          ? (uint16_t)((uint16_t)(crc << 1) ^ 0x1021U)
          : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

bool AcqFrameTx_Encode(const AcqFrameTxInfo *info,
                       const uint8_t *sample_bytes,
                       size_t sample_length,
                       uint8_t header[ACQ_FRAME_HEADER_BYTES])
{
  if ((info == NULL) || (sample_bytes == NULL) || (header == NULL)
      || (sample_length == 0U) || (info->timer_divider == 0U))
  {
    return false;
  }

  AcqFrameTx_WriteU16Le(&header[0], ACQ_FRAME_MAGIC);
  header[2] = ACQ_FRAME_VERSION;
  header[3] = info->flags;
  AcqFrameTx_WriteU16Le(&header[4], info->timer_divider);
  AcqFrameTx_WriteU32Le(&header[6], info->sequence);
  AcqFrameTx_WriteU32Le(&header[10], AcqFrameTx_Crc32(sample_bytes, sample_length));
  AcqFrameTx_WriteU16Le(&header[14], AcqFrameTx_Crc16(header, 14U));
  return true;
}
