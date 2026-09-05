#ifndef ZAMI_FRAME_H
#define ZAMI_FRAME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ZamiCore v2.3 ABI: Lockless V-Buffer Specification
 * Exact Frame Size: 23,488 bytes.
 * Cache-line aligned (64 bytes).
 */

#define ZAMI_MAGIC_HEADER       0x5A414D49U  /* 'ZAMI' */
#define ZAMI_FRAME_SIZE         23488U
#define ZAMI_EQUATOR_DIM        896U         /* d_model */
#define ZAMI_SCANOUT_DIM        4096U        /* Projection slice */

#define ZAMI_STATUS_READY       0x00U
#define ZAMI_STATUS_INFERRING   0x01U
#define ZAMI_STATUS_STABLE      0x02U
#define ZAMI_STATUS_FAULT       0xFFU        /* Circuit-Breaker Trigger */

#pragma pack(push, 1)

/* 64-byte aligned Frame Header */
typedef struct {
    uint32_t magic;                          /* 0x5A414D49 */
    uint32_t frame_index;                    /* Monotonic frame sequence */
    uint64_t timestamp_ns;                   /* FreeBSD CLOCK_MONOTONIC_FAST */
    uint8_t  status;                         /* Circuit-breaker status flag */
    uint8_t  reserved_align[55];             /* Pad to exact 64-byte cache line */
} zami_frame_header_t;

/* Ring V-Buffer Frame Payload (Total 23,488 bytes) */
typedef struct {
    zami_frame_header_t header;              /* 64 bytes */
    
    /* Equator Hidden State Slice: h^(l_eq) */
    float equator_state[ZAMI_EQUATOR_DIM];   /* 896 * 4 = 3,584 bytes */
    
    /* Scanout Slice: Projected topological anchor */
    float scanout_slice[ZAMI_SCANOUT_DIM];   /* 4096 * 4 = 16,384 bytes */
    
    /* Telemetry and Waveguide Attenuation State */
    float lambda_damping;                    /* Current damping parameter */
    float current_entropy;                   /* Real-time Shannon entropy H */
    float entropy_delta;                     /* Delta H / Delta l */
    float restore_force_norm;                /* ||F_restore|| */
    
    uint8_t zero_padding[3440];              /* Pad to strict 23,488 bytes */
} zami_vframe_t;

#pragma pack(pop)

/* Compile-time verification of strict memory boundaries */
_Static_assert(sizeof(zami_vframe_t) == ZAMI_FRAME_SIZE, 
               "ZamiCore ABI Error: zami_vframe_t must be exactly 23,488 bytes");
_Static_assert(offsetof(zami_vframe_t, equator_state) == 64, 
               "ZamiCore ABI Error: equator_state must align to 64-byte cache-line");

#ifdef __cplusplus
}
#endif

#endif /* ZAMI_FRAME_H */
