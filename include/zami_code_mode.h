#ifndef ZAMI_CODE_MODE_H
#define ZAMI_CODE_MODE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ZamiCore v2.3 Code Generation & Artifact Isolation ABI
 * Deterministic CAS grounding, AST Gate, and Repulsive Anti-Basins.
 */

#define ZAMI_CODE_MAGIC             0x5A434F44U  /* 'ZCOD' */
#define ZAMI_MAX_REFACTOR_TOKENS    2048U
#define ZAMI_BLAKE3_DIGEST_LEN      32U

/* Gateway State Enum */
typedef enum {
    ZAMI_GATE_DRAFT_IN_RAM          = 0x01,  /* P_tau buffer in isolated tmpfs */
    ZAMI_GATE_AST_VERIFYING         = 0x02,  /* Clang syntax-only in Capsicum sandbox */
    ZAMI_GATE_CANON_PROMOTED        = 0x03,  /* BLAKE3 validated, written to ZFS */
    ZAMI_GATE_REJECTED_ANTIBASIN    = 0x04   /* Compile error, vector pushed to Anti-Basin */
} zami_gate_status_t;

#pragma pack(push, 1)

/* Repulsive Anti-Basin Descriptor */
typedef struct {
    uint8_t  failed_code_hash[ZAMI_BLAKE3_DIGEST_LEN]; /* BLAKE3 of rejected draft */
    float    repulsion_center[16];                     /* Rejected state vector */
    float    repulsion_strength;                       /* Negative gradient scaling */
    uint32_t compiler_error_code;                      /* Diagnostic error identifier */
} zami_antibasin_record_t;

/* Refactoring Window and Canon Manifest */
typedef struct {
    uint32_t magic;                                    /* 0x5A434F44 */
    uint32_t version_epoch;                            /* Monotonic version counter */
    uint8_t  current_gate_state;                       /* zami_gate_status_t */
    
    uint8_t  canonical_hash[ZAMI_BLAKE3_DIGEST_LEN];   /* Active Canon P_C hash */
    uint8_t  parent_canon_hash[ZAMI_BLAKE3_DIGEST_LEN];/* Parent snapshot in DAG */
    
    uint32_t token_offset_start;                       /* Exact token span of refactoring */
    uint32_t token_offset_end;
    
    uint32_t antibasin_count;                          /* Active repulsive poles */
    zami_antibasin_record_t antibasins[8];             /* Rejection cache for backtrack */
    
    uint8_t  reserved[64];                             /* Future expansion */
} zami_code_manifest_t;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* ZAMI_CODE_MODE_H */
