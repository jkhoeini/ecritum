#include "ecritum.h"
#include "libecritum.h"

__attribute__((visibility("default"))) int ecritum_version(char *buffer, size_t buffer_len) {
    if (buffer == NULL || buffer_len == 0) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }

    graal_isolate_t *isolate = NULL;
    graal_isolatethread_t *thread = NULL;
    if (graal_create_isolate(NULL, &isolate, &thread) != 0 || thread == NULL) {
        buffer[0] = '\0';
        return ECRITUM_ERROR_RUNTIME_UNAVAILABLE;
    }

    int status = ecritum_graal_version(thread, buffer, buffer_len);
    if (graal_tear_down_isolate(thread) != 0 && status == ECRITUM_OK) {
        buffer[0] = '\0';
        return ECRITUM_ERROR_RUNTIME_UNAVAILABLE;
    }

    return status;
}
