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
#define ECRITUM_ERROR_INVALID_HANDLE 4
#define ECRITUM_ERROR_OUT_OF_MEMORY 5
#define ECRITUM_ERROR_INVALID_UTF8 6
#define ECRITUM_ERROR_INPUT_TOO_LARGE 7
#define ECRITUM_ERROR_INVALID_CONFIG 8
#define ECRITUM_ERROR_UNSUPPORTED_CONFIG_VERSION 9
#define ECRITUM_ERROR_CONTEXTS_ALIVE 10
#define ECRITUM_ERROR_CLOSED 11
#define ECRITUM_ERROR_BUSY 12
#define ECRITUM_ERROR_REENTRANT_CALL 13
#define ECRITUM_ERROR_PERMISSION_DENIED 14
#define ECRITUM_ERROR_TIMEOUT 15
#define ECRITUM_ERROR_CANCELLED 16
#define ECRITUM_ERROR_SCRIPT 17
#define ECRITUM_ERROR_CALLBACK 18
#define ECRITUM_ERROR_TEARDOWN_FAILED 19
#define ECRITUM_ERROR_INTERNAL 20

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
