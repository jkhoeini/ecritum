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
#define ECRITUM_HANDLE_KIND_NAMESPACE 4u
#define ECRITUM_HANDLE_KIND_FUNCTION 5u
#define ECRITUM_HANDLE_KIND_CALL 6u
#define ECRITUM_HANDLE_KIND_SHIFT 48u
#define ECRITUM_HANDLE_GENERATION_SHIFT 32u
#define ECRITUM_HANDLE_SLOT_MASK 0xffffffffULL
#define ECRITUM_MAX_HANDLE_SLOTS 4096u
#define ECRITUM_ERROR_TEXT_CAPACITY 160u
#define ECRITUM_ERROR_OPERATION_CAPACITY 64u
#define ECRITUM_NAME_CAPACITY 256u

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
    uint32_t parent_runtime_slot;
    uint16_t parent_runtime_generation;
    char name[ECRITUM_NAME_CAPACITY];
} ecritum_namespace_record_t;

typedef struct {
    uint32_t parent_namespace_slot;
    uint16_t parent_namespace_generation;
    ecritum_host_fn_t callback;
    void *user_data;
    ecritum_user_data_destroy_fn_t destroy_user_data;
    char name[ECRITUM_NAME_CAPACITY];
} ecritum_function_record_t;

typedef struct {
    uint32_t parent_function_slot;
    uint16_t parent_function_generation;
} ecritum_call_record_t;

typedef struct {
    ecritum_user_data_destroy_fn_t destroy_user_data;
    void *user_data;
} ecritum_cleanup_action_t;

typedef struct {
    uint16_t kind;
    uint16_t generation;
    union {
        ecritum_runtime_record_t runtime;
        ecritum_context_record_t context;
        ecritum_error_record_t error;
        ecritum_namespace_record_t namespace_record;
        ecritum_function_record_t function;
        ecritum_call_record_t call;
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
    case ECRITUM_ERROR_ALREADY_EXISTS: return "already_exists";
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

static int ascii_alpha(unsigned char value) {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

static int ascii_digit(unsigned char value) {
    return value >= '0' && value <= '9';
}

static unsigned char ascii_lower(unsigned char value) {
    if (value >= 'A' && value <= 'Z') {
        return (unsigned char)(value + ('a' - 'A'));
    }
    return value;
}

static int valid_identifier(const char *data, size_t len) {
    if (len == 0 || !ascii_alpha((unsigned char)data[0])) {
        return 0;
    }
    for (size_t index = 1; index < len; index++) {
        unsigned char value = (unsigned char)data[index];
        if (!ascii_alpha(value) && !ascii_digit(value) && value != '_') {
            return 0;
        }
    }
    return 1;
}

static int has_reserved_prefix(const char *data, size_t len) {
    static const char *reserved[] = {"ecritum", "java", "javax", "sun", "graal", "truffle"};
    for (size_t reserved_index = 0; reserved_index < sizeof(reserved) / sizeof(reserved[0]); reserved_index++) {
        const char *prefix = reserved[reserved_index];
        size_t prefix_len = strlen(prefix);
        if (len < prefix_len) {
            continue;
        }

        int matches = 1;
        for (size_t index = 0; index < prefix_len; index++) {
            if (ascii_lower((unsigned char)data[index]) != (unsigned char)prefix[index]) {
                matches = 0;
                break;
            }
        }
        if (matches) {
            return 1;
        }
    }
    return 0;
}

static int copy_ascii_name(ecritum_string_view_t name, char output[ECRITUM_NAME_CAPACITY], const char **message) {
    if (name.len == 0) {
        *message = "name is empty";
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    if (name.len >= ECRITUM_NAME_CAPACITY) {
        *message = "name is too large";
        return ECRITUM_ERROR_INPUT_TOO_LARGE;
    }
    if (name.data == NULL) {
        *message = "missing name data";
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }

    for (size_t index = 0; index < name.len; index++) {
        unsigned char value = (unsigned char)name.data[index];
        if (value == 0 || value > 0x7f) {
            *message = "name must be ASCII";
            return ECRITUM_ERROR_INVALID_ARGUMENT;
        }
        output[index] = (char)value;
    }
    output[name.len] = '\0';
    return ECRITUM_OK;
}

static int validate_namespace_name(ecritum_string_view_t name, char output[ECRITUM_NAME_CAPACITY], const char **message) {
    int status = copy_ascii_name(name, output, message);
    if (status != ECRITUM_OK) {
        return status;
    }

    size_t segment_start = 0;
    size_t first_segment_len = 0;
    for (size_t index = 0; index <= name.len; index++) {
        if (index != name.len && output[index] != '.') {
            continue;
        }

        size_t segment_len = index - segment_start;
        if (!valid_identifier(output + segment_start, segment_len)) {
            *message = "invalid namespace name";
            return ECRITUM_ERROR_INVALID_ARGUMENT;
        }
        if (segment_start == 0) {
            first_segment_len = segment_len;
        }
        segment_start = index + 1;
    }

    if (has_reserved_prefix(output, first_segment_len)) {
        *message = "namespace uses a reserved prefix";
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    return ECRITUM_OK;
}

static int validate_function_name(ecritum_string_view_t name, char output[ECRITUM_NAME_CAPACITY], const char **message) {
    int status = copy_ascii_name(name, output, message);
    if (status != ECRITUM_OK) {
        return status;
    }
    if (!valid_identifier(output, name.len)) {
        *message = "invalid function name";
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    if (has_reserved_prefix(output, name.len)) {
        *message = "function uses a reserved prefix";
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    return ECRITUM_OK;
}

static int namespace_matches_runtime(ecritum_handle_slot_t *entry, uint32_t runtime_slot, uint16_t runtime_generation) {
    return entry->kind == ECRITUM_HANDLE_KIND_NAMESPACE
        && entry->value.namespace_record.parent_runtime_slot == runtime_slot
        && entry->value.namespace_record.parent_runtime_generation == runtime_generation;
}

static int function_matches_namespace(ecritum_handle_slot_t *entry, uint32_t namespace_slot, uint16_t namespace_generation) {
    return entry->kind == ECRITUM_HANDLE_KIND_FUNCTION
        && entry->value.function.parent_namespace_slot == namespace_slot
        && entry->value.function.parent_namespace_generation == namespace_generation;
}

static void collect_function_cleanup_locked(ecritum_handle_slot_t *entry, ecritum_cleanup_action_t *actions, size_t *action_count) {
    if (entry->kind != ECRITUM_HANDLE_KIND_FUNCTION) {
        return;
    }
    if (entry->value.function.destroy_user_data != NULL && *action_count < ECRITUM_MAX_HANDLE_SLOTS) {
        actions[*action_count].destroy_user_data = entry->value.function.destroy_user_data;
        actions[*action_count].user_data = entry->value.function.user_data;
        (*action_count)++;
    }
    tombstone_slot_locked(entry);
}

static void collect_namespace_cleanup_locked(ecritum_handle_slot_t *namespace_entry, ecritum_cleanup_action_t *actions, size_t *action_count) {
    if (namespace_entry->kind != ECRITUM_HANDLE_KIND_NAMESPACE) {
        return;
    }

    uint32_t namespace_slot = (uint32_t)(namespace_entry - registry);
    uint16_t namespace_generation = namespace_entry->generation;
    for (uint32_t slot = 1; slot < ECRITUM_MAX_HANDLE_SLOTS; slot++) {
        ecritum_handle_slot_t *entry = &registry[slot];
        if (function_matches_namespace(entry, namespace_slot, namespace_generation)) {
            collect_function_cleanup_locked(entry, actions, action_count);
        }
    }
    tombstone_slot_locked(namespace_entry);
}

static void run_cleanup_actions(ecritum_cleanup_action_t *actions, size_t action_count) {
    for (size_t index = 0; index < action_count; index++) {
        actions[index].destroy_user_data(actions[index].user_data);
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

    ecritum_cleanup_action_t cleanup_actions[ECRITUM_MAX_HANDLE_SLOTS];
    size_t cleanup_count = 0;
    uint32_t runtime_slot = handle_slot(*runtime);
    uint16_t runtime_generation = handle_generation(*runtime);
    for (uint32_t slot = 1; slot < ECRITUM_MAX_HANDLE_SLOTS; slot++) {
        ecritum_handle_slot_t *namespace_entry = &registry[slot];
        if (namespace_matches_runtime(namespace_entry, runtime_slot, runtime_generation)) {
            collect_namespace_cleanup_locked(namespace_entry, cleanup_actions, &cleanup_count);
        }
    }

    graal_isolate_t *isolate = entry->value.runtime.isolate;
    tombstone_slot_locked(entry);
    *runtime = 0;
    pthread_mutex_unlock(&registry_mutex);

    run_cleanup_actions(cleanup_actions, cleanup_count);

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

__attribute__((visibility("default"))) int ecritum_namespace_create(ecritum_runtime_t runtime, ecritum_string_view_t name, ecritum_namespace_t *out_namespace, ecritum_error_t *out_error) {
    if (out_namespace == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "namespace_create", "missing namespace output", out_error);
    }

    *out_namespace = 0;
    clear_error(out_error);

    char copied_name[ECRITUM_NAME_CAPACITY];
    const char *message = "invalid namespace name";
    int name_status = validate_namespace_name(name, copied_name, &message);
    if (name_status != ECRITUM_OK) {
        return fail_with_error(name_status, "namespace_create", message, out_error);
    }

    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *runtime_entry = validate_locked(runtime, ECRITUM_HANDLE_KIND_RUNTIME);
    if (runtime_entry == NULL) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "namespace_create", "invalid runtime handle");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }

    uint32_t runtime_slot = handle_slot(runtime);
    uint16_t runtime_generation = handle_generation(runtime);
    for (uint32_t slot = 1; slot < ECRITUM_MAX_HANDLE_SLOTS; slot++) {
        ecritum_handle_slot_t *entry = &registry[slot];
        if (namespace_matches_runtime(entry, runtime_slot, runtime_generation)
            && strcmp(entry->value.namespace_record.name, copied_name) == 0) {
            if (out_error != NULL) {
                *out_error = create_error_locked(ECRITUM_ERROR_ALREADY_EXISTS, "namespace_create", "namespace already exists");
            }
            pthread_mutex_unlock(&registry_mutex);
            return ECRITUM_ERROR_ALREADY_EXISTS;
        }
    }

    ecritum_namespace_t handle = allocate_slot_locked(ECRITUM_HANDLE_KIND_NAMESPACE);
    if (handle == 0) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_OUT_OF_MEMORY, "namespace_create", "handle registry is full");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }

    ecritum_handle_slot_t *namespace_entry = validate_locked(handle, ECRITUM_HANDLE_KIND_NAMESPACE);
    namespace_entry->value.namespace_record.parent_runtime_slot = runtime_slot;
    namespace_entry->value.namespace_record.parent_runtime_generation = runtime_generation;
    copy_text(namespace_entry->value.namespace_record.name, sizeof(namespace_entry->value.namespace_record.name), copied_name);
    pthread_mutex_unlock(&registry_mutex);

    *out_namespace = handle;
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_namespace_destroy(ecritum_namespace_t *namespace_handle, ecritum_error_t *out_error) {
    if (namespace_handle == NULL || *namespace_handle == 0) {
        clear_error(out_error);
        return ECRITUM_OK;
    }

    clear_error(out_error);
    ecritum_cleanup_action_t cleanup_actions[ECRITUM_MAX_HANDLE_SLOTS];
    size_t cleanup_count = 0;

    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *namespace_entry = validate_locked(*namespace_handle, ECRITUM_HANDLE_KIND_NAMESPACE);
    if (namespace_entry == NULL) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "namespace_destroy", "invalid namespace handle");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }

    collect_namespace_cleanup_locked(namespace_entry, cleanup_actions, &cleanup_count);
    *namespace_handle = 0;
    pthread_mutex_unlock(&registry_mutex);

    run_cleanup_actions(cleanup_actions, cleanup_count);
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_namespace_register_function(
    ecritum_namespace_t namespace_handle,
    ecritum_string_view_t name,
    ecritum_host_fn_t callback,
    void *user_data,
    ecritum_user_data_destroy_fn_t destroy_user_data,
    ecritum_function_t *out_function,
    ecritum_error_t *out_error
) {
    if (out_function == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "function_register", "missing function output", out_error);
    }

    *out_function = 0;
    clear_error(out_error);

    if (callback == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "function_register", "missing host function callback", out_error);
    }

    char copied_name[ECRITUM_NAME_CAPACITY];
    const char *message = "invalid function name";
    int name_status = validate_function_name(name, copied_name, &message);
    if (name_status != ECRITUM_OK) {
        return fail_with_error(name_status, "function_register", message, out_error);
    }

    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *namespace_entry = validate_locked(namespace_handle, ECRITUM_HANDLE_KIND_NAMESPACE);
    if (namespace_entry == NULL) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "function_register", "invalid namespace handle");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }

    uint32_t namespace_slot = handle_slot(namespace_handle);
    uint16_t namespace_generation = handle_generation(namespace_handle);
    for (uint32_t slot = 1; slot < ECRITUM_MAX_HANDLE_SLOTS; slot++) {
        ecritum_handle_slot_t *entry = &registry[slot];
        if (function_matches_namespace(entry, namespace_slot, namespace_generation)
            && strcmp(entry->value.function.name, copied_name) == 0) {
            if (out_error != NULL) {
                *out_error = create_error_locked(ECRITUM_ERROR_ALREADY_EXISTS, "function_register", "function already exists");
            }
            pthread_mutex_unlock(&registry_mutex);
            return ECRITUM_ERROR_ALREADY_EXISTS;
        }
    }

    ecritum_function_t handle = allocate_slot_locked(ECRITUM_HANDLE_KIND_FUNCTION);
    if (handle == 0) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_OUT_OF_MEMORY, "function_register", "handle registry is full");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }

    ecritum_handle_slot_t *function_entry = validate_locked(handle, ECRITUM_HANDLE_KIND_FUNCTION);
    function_entry->value.function.parent_namespace_slot = namespace_slot;
    function_entry->value.function.parent_namespace_generation = namespace_generation;
    function_entry->value.function.callback = callback;
    function_entry->value.function.user_data = user_data;
    function_entry->value.function.destroy_user_data = destroy_user_data;
    copy_text(function_entry->value.function.name, sizeof(function_entry->value.function.name), copied_name);
    pthread_mutex_unlock(&registry_mutex);

    *out_function = handle;
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_function_destroy(ecritum_function_t *function, ecritum_error_t *out_error) {
    if (function == NULL || *function == 0) {
        clear_error(out_error);
        return ECRITUM_OK;
    }

    clear_error(out_error);
    ecritum_cleanup_action_t cleanup_actions[1];
    size_t cleanup_count = 0;

    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *function_entry = validate_locked(*function, ECRITUM_HANDLE_KIND_FUNCTION);
    if (function_entry == NULL) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "function_destroy", "invalid function handle");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }

    collect_function_cleanup_locked(function_entry, cleanup_actions, &cleanup_count);
    *function = 0;
    pthread_mutex_unlock(&registry_mutex);

    run_cleanup_actions(cleanup_actions, cleanup_count);
    return ECRITUM_OK;
}

#ifdef ECRITUM_TESTING
int ecritum_test_invoke_function(ecritum_function_t function, ecritum_error_t *out_error) {
    clear_error(out_error);

    ecritum_host_fn_t callback = NULL;
    void *user_data = NULL;
    ecritum_call_t call = 0;

    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *function_entry = validate_locked(function, ECRITUM_HANDLE_KIND_FUNCTION);
    if (function_entry == NULL) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "function_invoke", "invalid function handle");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }

    call = allocate_slot_locked(ECRITUM_HANDLE_KIND_CALL);
    if (call == 0) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_OUT_OF_MEMORY, "function_invoke", "handle registry is full");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }

    ecritum_handle_slot_t *call_entry = validate_locked(call, ECRITUM_HANDLE_KIND_CALL);
    call_entry->value.call.parent_function_slot = handle_slot(function);
    call_entry->value.call.parent_function_generation = handle_generation(function);
    callback = function_entry->value.function.callback;
    user_data = function_entry->value.function.user_data;
    pthread_mutex_unlock(&registry_mutex);

    ecritum_value_t result = 0;
    ecritum_error_t callback_error = 0;
    int callback_status = callback(call, &result, &callback_error, user_data);

    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *live_call_entry = validate_locked(call, ECRITUM_HANDLE_KIND_CALL);
    if (live_call_entry != NULL) {
        tombstone_slot_locked(live_call_entry);
    }
    pthread_mutex_unlock(&registry_mutex);

    if (callback_error != 0) {
        (void)ecritum_error_destroy(&callback_error);
    }
    if (callback_status == ECRITUM_OK) {
        return ECRITUM_OK;
    }
    return fail_with_error(ECRITUM_ERROR_CALLBACK, "function_invoke", "host function callback failed", out_error);
}
#endif

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
