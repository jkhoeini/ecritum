#ifndef ECRITUM_H
#define ECRITUM_H

#include <stdint.h>
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

typedef uint64_t ecritum_runtime_t;
typedef uint64_t ecritum_context_t;
typedef uint64_t ecritum_error_t;

typedef struct {
    const uint8_t *data;
    size_t len;
} ecritum_bytes_t;

typedef struct {
    const char *data;
    size_t len;
} ecritum_string_view_t;

/**
 * Writes the Ecritum runtime version into `buffer`.
 *
 * The buffer is always owned by the caller. The runtime writes a null-terminated
 * UTF-8 string when `buffer_len` is large enough.
 */
int ecritum_version(char *buffer, size_t buffer_len);

/**
 * Creates an Ecritum runtime.
 *
 * `out_runtime` is required and is set to 0 before work starts. Empty
 * `config_json` means default configuration. Non-empty configuration fails
 * closed until the versioned policy parser lands.
 *
 * Public callers never manage GraalVM isolate threads. The wrapper owns one
 * isolate per runtime and attaches/detaches native threads internally when a
 * lifecycle operation needs to enter the isolate.
 */
int ecritum_runtime_create(ecritum_bytes_t config_json, ecritum_runtime_t *out_runtime, ecritum_error_t *out_error);

/**
 * Destroys an Ecritum runtime and zeros the caller's handle on success.
 *
 * NULL and zero handles are no-ops. Destroying a runtime with live contexts
 * returns ECRITUM_ERROR_CONTEXTS_ALIVE and leaves the handle valid. Teardown
 * failure returns ECRITUM_ERROR_TEARDOWN_FAILED and still zeros the caller's
 * handle because the runtime cannot be safely reused.
 */
int ecritum_runtime_destroy(ecritum_runtime_t *runtime, ecritum_error_t *out_error);

/**
 * Creates a lifecycle context under a runtime.
 *
 * Contexts cannot outlive their parent runtime. Empty `config_json` means
 * default configuration. Non-empty configuration fails closed until M2 policy
 * parsing lands.
 */
int ecritum_context_create(ecritum_runtime_t runtime, ecritum_bytes_t config_json, ecritum_context_t *out_context, ecritum_error_t *out_error);

/**
 * Destroys an Ecritum context and zeros the caller's handle on success.
 *
 * NULL and zero handles are no-ops. Context handles are not thread-safe public
 * objects; future eval work will return ECRITUM_ERROR_BUSY for overlapping
 * active operations.
 */
int ecritum_context_destroy(ecritum_context_t *context, ecritum_error_t *out_error);

/** Destroys an owned error object and zeros the caller's handle. */
int ecritum_error_destroy(ecritum_error_t *error);

/** Copies the stable status code from an error object. */
int ecritum_error_status(ecritum_error_t error, int *out_status);

/** Borrows the stable machine-readable category from an error object. */
int ecritum_error_category(ecritum_error_t error, ecritum_string_view_t *out_category);

/** Borrows the redacted user-facing message from an error object. */
int ecritum_error_message(ecritum_error_t error, ecritum_string_view_t *out_message);

/** Borrows the operation name from an error object. */
int ecritum_error_operation(ecritum_error_t error, ecritum_string_view_t *out_operation);

#ifdef __cplusplus
}
#endif

#endif
