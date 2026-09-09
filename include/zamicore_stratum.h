#ifndef ZAMICORE_STRATUM_H
#define ZAMICORE_STRATUM_H

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#define ZAMI_STRATUM_SIZE 4096
#define ZAMI_VECTOR_DIM   896

#pragma pack(push, 1)

typedef struct {
    uint32_t crc32c;              /* SSE4.2 CRC32C первых 56 байт */
    uint16_t quadrant_flags;      /* Флаги квадранта Q1-Q4 и типа узла */
    uint16_t version;             /* Версия структуры */
    uint64_t stratum_id;          /* Уникальный 64-битный идентификатор */
    uint64_t timestamp_ns;        /* Временная метка */
    uint64_t access_counter;      /* Счетчик обращений */
    uint8_t  parent_hash[32];     /* SHA-256 хеш родителя Merkle-DAG */
} zami_stratum_header_t;

static_assert(sizeof(zami_stratum_header_t) == 64, "Header must be 64 bytes");

typedef struct {
    zami_stratum_header_t header;
    uint8_t payload[ZAMI_STRATUM_SIZE - sizeof(zami_stratum_header_t)];
} zami_stratum_t;

static_assert(sizeof(zami_stratum_t) == ZAMI_STRATUM_SIZE, "Stratum must be 4096 bytes");

#pragma pack(pop)

#endif /* ZAMICORE_STRATUM_H */
