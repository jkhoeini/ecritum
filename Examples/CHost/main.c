#include "ecritum.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    char buffer[ECRITUM_VERSION_BUFFER_SIZE];
    int status = ecritum_version(buffer, sizeof(buffer));
    if (status != ECRITUM_OK) {
        fprintf(stderr, "CHost status=%d\n", status);
        return 1;
    }
    if (strcmp(buffer, "0.2.0") != 0) {
        fprintf(stderr, "CHost version mismatch: %s\n", buffer);
        return 1;
    }
    char small_buffer[1];
    if (ecritum_version(small_buffer, sizeof(small_buffer)) != ECRITUM_ERROR_BUFFER_TOO_SMALL) {
        fprintf(stderr, "CHost invalid buffer check failed\n");
        return 1;
    }

    printf("CHost version=%s\n", buffer);
    return 0;
}
