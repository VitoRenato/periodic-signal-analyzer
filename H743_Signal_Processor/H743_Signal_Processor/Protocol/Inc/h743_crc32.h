#ifndef H743_CRC32_H
#define H743_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 计算CRC-32/ISO-HDLC校验值。
 *
 * 参数固定为reflected 0xEDB88320、init 0xFFFFFFFF、xorout 0xFFFFFFFF。
 * data为NULL且length非0时返回0；零长度数据的标准校验值也为0。
 */
uint32_t H743Crc32_Compute(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif