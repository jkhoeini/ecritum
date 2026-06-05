#include "ecritum.h"

#if defined(__APPLE__)
#define ECRITUM_WEAK_SYMBOL __attribute__((weak))
#else
#define ECRITUM_WEAK_SYMBOL
#endif

void ecritum_cecritum_shim_anchor(void) {}

ECRITUM_WEAK_SYMBOL int ecritum_version(char *buffer, size_t buffer_len) {
    if (buffer == NULL || buffer_len == 0) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }

    buffer[0] = '\0';
    return ECRITUM_ERROR_RUNTIME_UNAVAILABLE;
}
