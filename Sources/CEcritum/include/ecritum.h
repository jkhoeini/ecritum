#ifndef ECRITUM_H
#define ECRITUM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ECRITUM_OK 0
#define ECRITUM_ERROR_INVALID_ARGUMENT 1
#define ECRITUM_ERROR_BUFFER_TOO_SMALL 2
#define ECRITUM_ERROR_RUNTIME_UNAVAILABLE 3

#define ECRITUM_VERSION_BUFFER_SIZE 64

/**
 * Writes the Ecritum runtime version into `buffer`.
 *
 * The buffer is always owned by the caller. The runtime writes a null-terminated
 * UTF-8 string when `buffer_len` is large enough.
 */
int ecritum_version(char *buffer, size_t buffer_len);

#ifdef __cplusplus
}
#endif

#endif

