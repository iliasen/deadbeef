#ifndef ENDIANESS_H_INCLUDED
#define ENDIANESS_H_INCLUDED

#include <stdint.h>

static inline uint16_t bswap16(uint16_t x) {
    return (x >> 8) | (x << 8);
}

static inline uint32_t bswap32(uint32_t x) {
    return ((x >> 24) & 0xff) | ((x >> 8) & 0xff00) | ((x << 8) & 0xff0000) | ((x << 24) & 0xff000000);
}

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SWAP16(x) x = bswap16(x)
#define SWAP32(x) x = bswap32(x)
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SWAP16(x) (void)(x)
#define SWAP32(x) (void)(x)
#elif defined(_WIN32) || defined(__LITTLE_ENDIAN__)
#define SWAP16(x) x = bswap16(x)
#define SWAP32(x) x = bswap32(x)
#else
#define SWAP16(x) (void)(x)
#define SWAP32(x) (void)(x)
#endif

#endif