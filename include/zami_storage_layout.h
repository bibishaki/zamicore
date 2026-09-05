#ifndef ZAMI_STORAGE_LAYOUT_H
#define ZAMI_STORAGE_LAYOUT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ZamiCore v2.3 Storage Subsystem ABI
 * OpenZFS 4KB aligned physical layout (ashift=12, recordsize=4096).
 */

#define ZAMI_ZFS_PAGE_SIZE          4096U
#define ZAMI_BASINS_PER_PAGE        32U
#define ZAMI_BASIN_DESCRIPTOR_SIZE  128U
#define ZAMI_CENTROID_DIM           16U    /* Compact topological projection */

#define ZAMI_PAGE_TYPE_TRUNK        0x01U
#define ZAMI_PAGE_TYPE_BRANCH       0x02U
#define ZAMI_PAGE_TYPE_BASIN_LEAF   0x03U

#pragma pack(push, 1)

/* 
 * Single Basin Descriptor (128 bytes)
 * 32 basins * 128 bytes = exactly 4,096 bytes (1 physical ZFS record)
 */
typedef struct {
    uint32_t basin_id;                       /* Monotonic basin UID */
    uint32_t access_frequency;               /* Access counter for eviction/mitosis */
    float    accumulated_mass;               /* Mass parameter M */
    float    accretion_radius;               /* Accretion capture radius R_acc(M) */
    
    uint8_t  blake3_cas_root[32];            /* 256-bit root hash of immutable code canon */
    
    float    centroid_coords[ZAMI_CENTROID_DIM]; /* Barycenter projection: 16 * 4 = 64 bytes */
    
    uint8_t  flags;                          /* Status: Active, Splitting, Cold */
    uint8_t  reserved[15];                   /* Strict alignment pad to 128 bytes */
} zami_basin_descriptor_t;

/* Physical 4KB ZFS Storage Block */
typedef struct {
    zami_basin_descriptor_t basins[ZAMI_BASINS_PER_PAGE]; /* 32 * 128 = 4,096 bytes */
} zami_storage_page_t;

#pragma pack(pop)

/* Compile-time verification of strict physical alignment */
_Static_assert(sizeof(zami_basin_descriptor_t) == ZAMI_BASIN_DESCRIPTOR_SIZE,
               "ZamiCore Storage ABI Error: Basin descriptor must be exactly 128 bytes");
_Static_assert(sizeof(zami_storage_page_t) == ZAMI_ZFS_PAGE_SIZE,
               "ZamiCore Storage ABI Error: Storage page must be exactly 4,096 bytes (ZFS recordsize)");

#ifdef __cplusplus
}
#endif

#endif /* ZAMI_STORAGE_LAYOUT_H */
