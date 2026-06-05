#include "ecritum.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ECRITUM_TESTING
typedef struct __graal_isolate_t {
    int marker;
} graal_isolate_t;

typedef struct __graal_isolatethread_t {
    graal_isolate_t *isolate;
} graal_isolatethread_t;

typedef struct {
    int unused;
} graal_create_isolate_params_t;

int ecritum_test_force_teardown_failure = 0;

int graal_create_isolate(graal_create_isolate_params_t *params, graal_isolate_t **isolate, graal_isolatethread_t **thread) {
    (void)params;
    graal_isolate_t *new_isolate = calloc(1, sizeof(graal_isolate_t));
    graal_isolatethread_t *new_thread = calloc(1, sizeof(graal_isolatethread_t));
    if (new_isolate == NULL || new_thread == NULL) {
        free(new_isolate);
        free(new_thread);
        return 1;
    }
    new_isolate->marker = 0xecc17;
    new_thread->isolate = new_isolate;
    if (isolate != NULL) {
        *isolate = new_isolate;
    }
    if (thread != NULL) {
        *thread = new_thread;
    }
    return 0;
}

graal_isolatethread_t *graal_get_current_thread(graal_isolate_t *isolate) {
    (void)isolate;
    return NULL;
}

int graal_attach_thread(graal_isolate_t *isolate, graal_isolatethread_t **thread) {
    if (isolate == NULL || thread == NULL) {
        return 1;
    }
    graal_isolatethread_t *new_thread = calloc(1, sizeof(graal_isolatethread_t));
    if (new_thread == NULL) {
        return 1;
    }
    new_thread->isolate = isolate;
    *thread = new_thread;
    return 0;
}

int graal_detach_thread(graal_isolatethread_t *thread) {
    free(thread);
    return 0;
}

int graal_tear_down_isolate(graal_isolatethread_t *thread) {
    if (thread == NULL) {
        return 1;
    }
    graal_isolate_t *isolate = thread->isolate;
    free(thread);
    free(isolate);
    if (ecritum_test_force_teardown_failure) {
        ecritum_test_force_teardown_failure = 0;
        return 1;
    }
    return 0;
}

int ecritum_graal_version(graal_isolatethread_t *thread, char *buffer, size_t buffer_len) {
    (void)thread;
    static const char *version = "0.1.0-dev";
    size_t required = strlen(version) + 1;
    if (buffer == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    if (buffer_len < required) {
        return ECRITUM_ERROR_BUFFER_TOO_SMALL;
    }
    memcpy(buffer, version, required);
    return ECRITUM_OK;
}
#else
#include "libecritum.h"
#endif

#define ECRITUM_HANDLE_KIND_RUNTIME 1u
#define ECRITUM_HANDLE_KIND_CONTEXT 2u
#define ECRITUM_HANDLE_KIND_ERROR 3u
#define ECRITUM_HANDLE_KIND_SHIFT 48u
#define ECRITUM_HANDLE_GENERATION_SHIFT 32u
#define ECRITUM_HANDLE_SLOT_MASK 0xffffffffULL
#define ECRITUM_MAX_HANDLE_SLOTS 4096u
#define ECRITUM_ERROR_TEXT_CAPACITY 160u
#define ECRITUM_ERROR_OPERATION_CAPACITY 64u

typedef struct {
    graal_isolate_t *isolate;
    uint32_t live_contexts;
} ecritum_runtime_record_t;

typedef struct {
    uint32_t parent_slot;
    uint16_t parent_generation;
} ecritum_context_record_t;

typedef struct {
    int status;
    const char *category;
    char message[ECRITUM_ERROR_TEXT_CAPACITY];
    char operation[ECRITUM_ERROR_OPERATION_CAPACITY];
} ecritum_error_record_t;

typedef struct {
    uint16_t kind;
    uint16_t generation;
    union {
        ecritum_runtime_record_t runtime;
        ecritum_context_record_t context;
        ecritum_error_record_t error;
    } value;
} ecritum_handle_slot_t;

static pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;
static ecritum_handle_slot_t registry[ECRITUM_MAX_HANDLE_SLOTS];

static uint64_t make_handle(uint16_t kind, uint32_t slot, uint16_t generation) {
    return ((uint64_t)kind << ECRITUM_HANDLE_KIND_SHIFT)
        | ((uint64_t)generation << ECRITUM_HANDLE_GENERATION_SHIFT)
        | (uint64_t)slot;
}

static uint16_t handle_kind(uint64_t handle) {
    return (uint16_t)(handle >> ECRITUM_HANDLE_KIND_SHIFT);
}

static uint16_t handle_generation(uint64_t handle) {
    return (uint16_t)(handle >> ECRITUM_HANDLE_GENERATION_SHIFT);
}

static uint32_t handle_slot(uint64_t handle) {
    return (uint32_t)(handle & ECRITUM_HANDLE_SLOT_MASK);
}

static const char *category_for_status(int status) {
    switch (status) {
    case ECRITUM_ERROR_INVALID_ARGUMENT: return "invalid_argument";
    case ECRITUM_ERROR_INVALID_HANDLE: return "invalid_handle";
    case ECRITUM_ERROR_BUFFER_TOO_SMALL: return "buffer_too_small";
    case ECRITUM_ERROR_OUT_OF_MEMORY: return "out_of_memory";
    case ECRITUM_ERROR_INVALID_UTF8: return "invalid_utf8";
    case ECRITUM_ERROR_INPUT_TOO_LARGE: return "input_too_large";
    case ECRITUM_ERROR_INVALID_CONFIG: return "invalid_config";
    case ECRITUM_ERROR_UNSUPPORTED_CONFIG_VERSION: return "unsupported_config_version";
    case ECRITUM_ERROR_CONTEXTS_ALIVE: return "contexts_alive";
    case ECRITUM_ERROR_CLOSED: return "closed";
    case ECRITUM_ERROR_BUSY: return "busy";
    case ECRITUM_ERROR_REENTRANT_CALL: return "reentrant_call";
    case ECRITUM_ERROR_PERMISSION_DENIED: return "permission_denied";
    case ECRITUM_ERROR_TIMEOUT: return "timeout";
    case ECRITUM_ERROR_CANCELLED: return "cancelled";
    case ECRITUM_ERROR_SCRIPT: return "script";
    case ECRITUM_ERROR_CALLBACK: return "callback";
    case ECRITUM_ERROR_RUNTIME_UNAVAILABLE: return "runtime_unavailable";
    case ECRITUM_ERROR_TEARDOWN_FAILED: return "teardown_failed";
    case ECRITUM_ERROR_INTERNAL: return "internal";
    default: return "unknown";
    }
}

static void copy_text(char *destination, size_t capacity, const char *source) {
    if (capacity == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    snprintf(destination, capacity, "%s", source);
}

static uint16_t next_generation(uint16_t generation) {
    if (generation == UINT16_MAX) {
        return 0;
    }
    return (uint16_t)(generation + 1);
}

static ecritum_handle_slot_t *validate_locked(uint64_t handle, uint16_t expected_kind) {
    if (handle == 0 || handle_kind(handle) != expected_kind) {
        return NULL;
    }

    uint32_t slot = handle_slot(handle);
    if (slot == 0 || slot >= ECRITUM_MAX_HANDLE_SLOTS) {
        return NULL;
    }

    ecritum_handle_slot_t *entry = &registry[slot];
    if (entry->kind != expected_kind || entry->generation != handle_generation(handle)) {
        return NULL;
    }

    return entry;
}

static uint64_t allocate_slot_locked(uint16_t kind) {
    for (uint32_t slot = 1; slot < ECRITUM_MAX_HANDLE_SLOTS; slot++) {
        ecritum_handle_slot_t *entry = &registry[slot];
        if (entry->kind == 0) {
            uint16_t generation = next_generation(entry->generation);
            if (generation == 0) {
                continue;
            }
            entry->generation = generation;
            entry->kind = kind;
            memset(&entry->value, 0, sizeof(entry->value));
            return make_handle(kind, slot, entry->generation);
        }
    }
    return 0;
}

static void tombstone_slot_locked(ecritum_handle_slot_t *entry) {
    entry->kind = 0;
    memset(&entry->value, 0, sizeof(entry->value));
}

static ecritum_error_t create_error_locked(int status, const char *operation, const char *message) {
    ecritum_error_t handle = allocate_slot_locked(ECRITUM_HANDLE_KIND_ERROR);
    if (handle == 0) {
        return 0;
    }

    ecritum_handle_slot_t *entry = validate_locked(handle, ECRITUM_HANDLE_KIND_ERROR);
    entry->value.error.status = status;
    entry->value.error.category = category_for_status(status);
    copy_text(entry->value.error.message, sizeof(entry->value.error.message), message);
    copy_text(entry->value.error.operation, sizeof(entry->value.error.operation), operation);
    return handle;
}

static int fail_with_error(int status, const char *operation, const char *message, ecritum_error_t *out_error) {
    if (out_error != NULL) {
        pthread_mutex_lock(&registry_mutex);
        *out_error = create_error_locked(status, operation, message);
        pthread_mutex_unlock(&registry_mutex);
    }
    return status;
}

static void clear_error(ecritum_error_t *out_error) {
    if (out_error != NULL) {
        *out_error = 0;
    }
}

static int config_is_invalid(ecritum_bytes_t config_json) {
    return config_json.len > 0;
}

static int detach_creation_thread(graal_isolatethread_t *thread) {
    if (thread == NULL) {
        return 1;
    }
    return graal_detach_thread(thread);
}

static int tear_down_isolate(graal_isolate_t *isolate) {
    if (isolate == NULL) {
        return 1;
    }

    graal_isolatethread_t *thread = graal_get_current_thread(isolate);
    if (thread == NULL) {
        if (graal_attach_thread(isolate, &thread) != 0 || thread == NULL) {
            return 1;
        }
    }

    return graal_tear_down_isolate(thread);
}

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

__attribute__((visibility("default"))) int ecritum_runtime_create(ecritum_bytes_t config_json, ecritum_runtime_t *out_runtime, ecritum_error_t *out_error) {
    if (out_runtime == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "runtime_create", "missing runtime output", out_error);
    }

    *out_runtime = 0;
    clear_error(out_error);

    if (config_is_invalid(config_json)) {
        return fail_with_error(ECRITUM_ERROR_INVALID_CONFIG, "runtime_create", "runtime configuration is not supported yet", out_error);
    }

    graal_isolate_t *isolate = NULL;
    graal_isolatethread_t *thread = NULL;
    if (graal_create_isolate(NULL, &isolate, &thread) != 0 || isolate == NULL || thread == NULL) {
        return fail_with_error(ECRITUM_ERROR_RUNTIME_UNAVAILABLE, "runtime_create", "runtime is unavailable", out_error);
    }

    if (detach_creation_thread(thread) != 0) {
        (void)graal_tear_down_isolate(thread);
        return fail_with_error(ECRITUM_ERROR_RUNTIME_UNAVAILABLE, "runtime_create", "runtime thread detach failed", out_error);
    }

    pthread_mutex_lock(&registry_mutex);
    ecritum_runtime_t handle = allocate_slot_locked(ECRITUM_HANDLE_KIND_RUNTIME);
    if (handle != 0) {
        ecritum_handle_slot_t *entry = validate_locked(handle, ECRITUM_HANDLE_KIND_RUNTIME);
        entry->value.runtime.isolate = isolate;
        entry->value.runtime.live_contexts = 0;
    }
    pthread_mutex_unlock(&registry_mutex);

    if (handle == 0) {
        (void)tear_down_isolate(isolate);
        return fail_with_error(ECRITUM_ERROR_OUT_OF_MEMORY, "runtime_create", "handle registry is full", out_error);
    }

    *out_runtime = handle;
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_runtime_destroy(ecritum_runtime_t *runtime, ecritum_error_t *out_error) {
    if (runtime == NULL || *runtime == 0) {
        clear_error(out_error);
        return ECRITUM_OK;
    }

    clear_error(out_error);
    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *entry = validate_locked(*runtime, ECRITUM_HANDLE_KIND_RUNTIME);
    if (entry == NULL) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "runtime_destroy", "invalid runtime handle");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }

    if (entry->value.runtime.live_contexts > 0) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_CONTEXTS_ALIVE, "runtime_destroy", "runtime has live contexts");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_CONTEXTS_ALIVE;
    }

    graal_isolate_t *isolate = entry->value.runtime.isolate;
    tombstone_slot_locked(entry);
    *runtime = 0;
    pthread_mutex_unlock(&registry_mutex);

    if (tear_down_isolate(isolate) != 0) {
        return fail_with_error(ECRITUM_ERROR_TEARDOWN_FAILED, "runtime_destroy", "runtime teardown failed", out_error);
    }

    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_context_create(ecritum_runtime_t runtime, ecritum_bytes_t config_json, ecritum_context_t *out_context, ecritum_error_t *out_error) {
    if (out_context == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "context_create", "missing context output", out_error);
    }

    *out_context = 0;
    clear_error(out_error);

    if (config_is_invalid(config_json)) {
        return fail_with_error(ECRITUM_ERROR_INVALID_CONFIG, "context_create", "context configuration is not supported yet", out_error);
    }

    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *runtime_entry = validate_locked(runtime, ECRITUM_HANDLE_KIND_RUNTIME);
    if (runtime_entry == NULL) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "context_create", "invalid runtime handle");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }

    ecritum_context_t handle = allocate_slot_locked(ECRITUM_HANDLE_KIND_CONTEXT);
    if (handle == 0) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_OUT_OF_MEMORY, "context_create", "handle registry is full");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }

    ecritum_handle_slot_t *context_entry = validate_locked(handle, ECRITUM_HANDLE_KIND_CONTEXT);
    context_entry->value.context.parent_slot = handle_slot(runtime);
    context_entry->value.context.parent_generation = handle_generation(runtime);
    runtime_entry->value.runtime.live_contexts++;
    pthread_mutex_unlock(&registry_mutex);

    *out_context = handle;
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_context_destroy(ecritum_context_t *context, ecritum_error_t *out_error) {
    if (context == NULL || *context == 0) {
        clear_error(out_error);
        return ECRITUM_OK;
    }

    clear_error(out_error);
    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *context_entry = validate_locked(*context, ECRITUM_HANDLE_KIND_CONTEXT);
    if (context_entry == NULL) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "context_destroy", "invalid context handle");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }

    uint32_t parent_slot = context_entry->value.context.parent_slot;
    uint16_t parent_generation = context_entry->value.context.parent_generation;
    if (parent_slot > 0 && parent_slot < ECRITUM_MAX_HANDLE_SLOTS) {
        ecritum_handle_slot_t *runtime_entry = &registry[parent_slot];
        if (runtime_entry->kind == ECRITUM_HANDLE_KIND_RUNTIME
            && runtime_entry->generation == parent_generation
            && runtime_entry->value.runtime.live_contexts > 0) {
            runtime_entry->value.runtime.live_contexts--;
        }
    }

    tombstone_slot_locked(context_entry);
    *context = 0;
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_error_destroy(ecritum_error_t *error) {
    if (error == NULL || *error == 0) {
        return ECRITUM_OK;
    }

    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *entry = validate_locked(*error, ECRITUM_HANDLE_KIND_ERROR);
    if (entry == NULL) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }

    tombstone_slot_locked(entry);
    *error = 0;
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_error_status(ecritum_error_t error, int *out_status) {
    if (out_status == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *entry = validate_locked(error, ECRITUM_HANDLE_KIND_ERROR);
    if (entry == NULL) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }

    *out_status = entry->value.error.status;
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

static const char *error_field(ecritum_error_record_t *error, int field) {
    switch (field) {
    case 0: return error->category;
    case 1: return error->message;
    case 2: return error->operation;
    default: return "";
    }
}

static int error_string_view(ecritum_error_t error, ecritum_string_view_t *out_view, int field) {
    if (out_view == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *entry = validate_locked(error, ECRITUM_HANDLE_KIND_ERROR);
    if (entry == NULL) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }

    const char *text = error_field(&entry->value.error, field);
    out_view->data = text;
    out_view->len = strlen(text);
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_error_category(ecritum_error_t error, ecritum_string_view_t *out_category) {
    return error_string_view(error, out_category, 0);
}

__attribute__((visibility("default"))) int ecritum_error_message(ecritum_error_t error, ecritum_string_view_t *out_message) {
    return error_string_view(error, out_message, 1);
}

__attribute__((visibility("default"))) int ecritum_error_operation(ecritum_error_t error, ecritum_string_view_t *out_operation) {
    return error_string_view(error, out_operation, 2);
}
