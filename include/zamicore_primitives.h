#ifndef ZAMICORE_PRIMITIVES_H
#define ZAMICORE_PRIMITIVES_H

#include <stdint.h>
#include <stdbool.h>
#include "zamicore_stratum.h"

/* 7 неизменяемых когнитивных примитивов ядра */
typedef enum {
    PRIM_STRATUM_IO = 0x0001,
    PRIM_SEAL_CRC   = 0x0002,
    PRIM_VEC_METRIC = 0x0003,
    PRIM_BARYCENTER = 0x0004,
    PRIM_TENSION    = 0x0005,
    PRIM_FRAME_FLIP = 0x0006,
    PRIM_GRAPH_HOP  = 0x0007
} zami_primitive_id_t;

typedef struct {
    bool     success;
    uint16_t opcode;
    uint16_t status_flags;
    float    telemetry_metric;
} zami_op_result_t;

#endif /* ZAMICORE_PRIMITIVES_H */
