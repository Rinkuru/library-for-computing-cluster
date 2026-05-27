#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

enum {
    ClusterSizeHeaderSize = 8
};

typedef struct {
    void *data;
    size_t size;
} ClusterPacket;

typedef void *(*Method)(void *data, size_t size);

static inline void ClusterEncodeSize(uint64_t value, unsigned char buffer[ClusterSizeHeaderSize]) {
    for (size_t i = 0U; i < ClusterSizeHeaderSize; ++i) {
        buffer[ClusterSizeHeaderSize - 1U - i] = (unsigned char)(value & 0xffU);
        value >>= 8U;
    }
}

static inline uint64_t ClusterDecodeSize(const unsigned char buffer[ClusterSizeHeaderSize]) {
    uint64_t value = 0U;
    for (size_t i = 0U; i < ClusterSizeHeaderSize; ++i) {
        value = (value << 8U) | (uint64_t)buffer[i];
    }
    return value;
}

static inline long NowMs(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fprintf(stderr, "[NowMs] Unable NowMs\n");
        return -1;
    }

    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static inline size_t SplitPoint(size_t size, int index, int parts)
{
    size_t base = size / (size_t)parts;
    size_t extra = size % (size_t)parts;
    return base * (size_t)index + (extra * (size_t)index) / (size_t)parts;
}

#endif
