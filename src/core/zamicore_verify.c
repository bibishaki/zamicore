#include <stdio.h>
#include "zamicore_stratum.h"
#include "zamicore_primitives.h"

int main(void) {
    printf("[ZAMICORE] Verifying stratum physical contract...\n");
    printf("[OK] zami_stratum_t size: %zu bytes\n", sizeof(zami_stratum_t));
    printf("[OK] Header size: %zu bytes\n", sizeof(zami_stratum_header_t));

    if (sizeof(zami_stratum_t) != 4096) {
        fprintf(stderr, "[FAIL] Memory contract violated: size is not 4096 bytes\n");
        return 1;
    }
    return 0;
}
