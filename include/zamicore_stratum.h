/**
 * @file zamicore_stratum.h
 * @brief Спецификация бинарного формата 4KB-страта ZAMICORE (v1.0).
 * 
 * Строго ориентирован на сектор 4096 байт (Advanced Format 4Kn / NVMe LBA).
 * Предназначен для прямого синхронного I/O на OpenZFS (ashift=12, recordsize=4k).
 */

#ifndef ZAMICORE_STRATUM_H
#define ZAMICORE_STRATUM_H

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 1. КОНСТАНТЫ И ИДЕНТИФИКАТОРЫ ФОРМАТА
 * ========================================================================= */

#define ZAMI_MAGIC_BYTES            0x5A414D49U  /* "ZAMI" в Little Endian (ASCII) */
#define ZAMI_FORMAT_VERSION         1U           /* Текущая версия спецификации */
#define ZAMI_STRATUM_SIZE           4096U        /* Физический размер сектора ZFS */
#define ZAMI_HEADER_SIZE            64U          /* Базовый заголовок */
#define ZAMI_PAYLOAD_SIZE           (ZAMI_STRATUM_SIZE - ZAMI_HEADER_SIZE) /* 4032 B */

#define ZAMI_QWEN_DIM               896U         /* Размерность d_model Qwen2.5-0.5B */
#define ZAMI_GRAPH_MAX_NEXT         64U          /* Максимум переходов вперед (L -> L+1) */
#define ZAMI_GRAPH_MAX_PREV         16U          /* Максимум связей назад (L -> L-1) */

/* =========================================================================
 * 2. РАСШИРЕННАЯ БИТОВАЯ МАСКА ФЛАГОВ (uint16_t flags)
 * ========================================================================= */

typedef enum zami_stratum_flags {
    ZAMI_FLAG_NONE          = 0x0000,
    ZAMI_FLAG_EOS_LATCH     = 0x0001, /* Защелка останова: дно ямы (∇V = 0) */
    ZAMI_FLAG_BRANCH_ROOT   = 0x0002, /* Точка бифуркации / корень новой ветви */
    ZAMI_FLAG_RAW_STRATUM   = 0x0004, /* Сырой квантованный вектор документа/факта */
    ZAMI_FLAG_ROUTING_NODE  = 0x0008, /* Узел графа (содержит таблицу связей) */
    ZAMI_FLAG_MERKLE_ROOT   = 0x0010, /* Корневой узел версии документа (CoW) */
    ZAMI_FLAG_RAW_FP32      = 0x0020, /* Отладочный режим: вектор в FP32 без квантования */
    ZAMI_FLAG_CORRUPTED     = 0x8000  /* Карантин: ошибка CRC32 или повреждение */
} zami_stratum_flags_t;

/* =========================================================================
 * 3. БАЗОВЫЙ ЗАГОЛОВОК СТРАТА (64 БАЙТА)
 * ========================================================================= */

#pragma pack(push, 1)

typedef struct zami_header {
    uint32_t magic;             /* 0x00: Маркер формата: 0x5A414D49 */
    uint16_t version;           /* 0x04: Версия схемы (1) */
    uint16_t flags;             /* 0x06: Битовая маска zami_stratum_flags_t */
    uint64_t stratum_id;        /* 0x08: Монотонный индекс коржа в ветке */
    uint8_t  parent_hash[32];   /* 0x10: SHA-256 хеш родительского узла */
    float    quant_scale;       /* 0x30: Коэффициент деквантования FP32 */
    uint32_t vector_dim;        /* 0x34: Размерность вектора (для Qwen: 896) */
    uint32_t header_crc32;      /* 0x38: SSE4.2 CRC32 полей со смещением 0x00..0x37 */
    uint32_t reserved;          /* 0x3C: Выравнивание до границы 64 байт (всегда 0) */
} zami_header_t;

/* =========================================================================
 * 4. ПОЛЕЗНАЯ НАГРУЗКА: РЕЖИМЫ ХРАНЕНИЯ (4032 БАЙТА)
 * ========================================================================= */

/**
 * @brief Полезная нагрузка в режиме графа твердой памяти (DAG Topology).
 * Размер: 896 + 2 + 2 + 512 + 128 + 128 + 2364 = 4032 байта.
 */
typedef struct zami_graph_payload {
    int8_t   state_vector[ZAMI_QWEN_DIM];       /* 0x000: Квантованный вектор точки покоя INT8 (896 B) */
    uint16_t out_degree;                        /* 0x380: Количество исходящих ветвей вперед */
    uint16_t in_degree;                         /* 0x382: Количество входящих связей назад */
    uint64_t next_strata[ZAMI_GRAPH_MAX_NEXT];  /* 0x384: Прямые ID стратов на слой L+1 (512 B) */
    uint64_t prev_strata[ZAMI_GRAPH_MAX_PREV];  /* 0x584: Прямые ID стратов со слоя L-1 (128 B) */
    int16_t  edge_weights[ZAMI_GRAPH_MAX_NEXT]; /* 0x604: Фиксированные веса ребер Q8.8 (128 B) */
    uint8_t  reserved[2364];                    /* 0x684: Зарезервировано до границы 4032 байт */
} zami_graph_payload_t;

/**
 * @brief Полезная нагрузка в режиме ранней отладки без квантования (FP32).
 * Размер: 3584 + 448 = 4032 байта.
 */
typedef struct zami_fp32_payload {
    float   raw_vector[ZAMI_QWEN_DIM];          /* 0x000: 896 координат * 4 байта = 3584 B */
    uint8_t padding[448];                       /* 0xE00: Паддинг до границы 4032 байт */
} zami_fp32_payload_t;

/**
 * @brief Унифицированная монолитная структура страта ZAMICORE (4096 Байт).
 */
typedef struct zamicore_stratum {
    zami_header_t header; /* 0x0000: 64-байтный системный заголовок */

    union {
        uint8_t              raw[ZAMI_PAYLOAD_SIZE]; /* Сырой массив для direct read/write */
        zami_graph_payload_t graph;                  /* Топологический узел твердой памяти */
        zami_fp32_payload_t  fp32;                   /* Несжатый отладочный вектор */
    } payload;            /* 0x0040: 4032 байта данных */
} zamicore_stratum_t;

#pragma pack(pop)

/* =========================================================================
 * 5. КОМПИЛЯТОРНЫЕ ПРОВЕРКИ СМЕЩЕНИЙ И ВЫРАВНИВАНИЯ (STATIC_ASSERT)
 * ========================================================================= */

/* Проверка итоговых размеров ключевых контейнеров */
static_assert(sizeof(zami_header_t) == 64, 
              "Критическая ошибка: zami_header_t обязан быть ровно 64 байта!");
static_assert(sizeof(zami_graph_payload_t) == 4032, 
              "Критическая ошибка: zami_graph_payload_t обязан быть ровно 4032 байта!");
static_assert(sizeof(zami_fp32_payload_t) == 4032, 
              "Критическая ошибка: zami_fp32_payload_t обязан быть ровно 4032 байта!");
static_assert(sizeof(zamicore_stratum_t) == 4096, 
              "Критическая ошибка: zamicore_stratum_t обязан быть ровно 4096 байт!");

/* Проверка смещений полей заголовка */
static_assert(offsetof(zami_header_t, magic)        == 0x00, "Смещение magic нарушено!");
static_assert(offsetof(zami_header_t, version)      == 0x04, "Смещение version нарушено!");
static_assert(offsetof(zami_header_t, flags)        == 0x06, "Смещение flags нарушено!");
static_assert(offsetof(zami_header_t, stratum_id)   == 0x08, "Смещение stratum_id нарушено!");
static_assert(offsetof(zami_header_t, parent_hash)  == 0x10, "Смещение parent_hash нарушено!");
static_assert(offsetof(zami_header_t, quant_scale)  == 0x30, "Смещение quant_scale нарушено!");
static_assert(offsetof(zami_header_t, vector_dim)   == 0x34, "Смещение vector_dim нарушено!");
static_assert(offsetof(zami_header_t, header_crc32) == 0x38, "Смещение header_crc32 нарушено!");
static_assert(offsetof(zami_header_t, reserved)     == 0x3C, "Смещение reserved нарушено!");

/* Проверка смещения полезной нагрузки */
static_assert(offsetof(zamicore_stratum_t, payload) == 0x40, 
              "Смещение payload обязано начинаться строго с 64 байта (0x40)!");

/* Проверка смещений топологической структуры графа */
static_assert(offsetof(zami_graph_payload_t, state_vector) == 0x000, "Смещение state_vector нарушено!");
static_assert(offsetof(zami_graph_payload_t, out_degree)   == 0x380, "Смещение out_degree нарушено!");
static_assert(offsetof(zami_graph_payload_t, in_degree)    == 0x382, "Смещение in_degree нарушено!");
static_assert(offsetof(zami_graph_payload_t, next_strata)  == 0x384, "Смещение next_strata нарушено!");
static_assert(offsetof(zami_graph_payload_t, prev_strata)  == 0x584, "Смещение prev_strata нарушено!");
static_assert(offsetof(zami_graph_payload_t, edge_weights) == 0x604, "Смещение edge_weights нарушено!");
static_assert(offsetof(zami_graph_payload_t, reserved)     == 0x684, "Смещение reserved нарушено!");

#ifdef __cplusplus
}
#endif

#endif /* ZAMICORE_STRATUM_H */
