#include "h743_crc32.h"

#include <stddef.h>
#include <stdint.h>

uint32_t H743Crc32_Compute(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_C(0xffffffff);
    size_t byte_index;

    if ((data == NULL) && (length != 0U))
    {
        return 0U;
    }

    for (byte_index = 0U; byte_index < length; ++byte_index)
    {
        unsigned int bit_index;

        crc ^= (uint32_t)data[byte_index];
        for (bit_index = 0U; bit_index < 8U; ++bit_index)
        {
            const uint32_t reflected_polynomial =
                ((crc & 1U) != 0U) ? UINT32_C(0xedb88320) : 0U;

            crc = (crc >> 1U) ^ reflected_polynomial;
        }
    }

    return crc ^ UINT32_C(0xffffffff);
}