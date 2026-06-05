#include "ecritum.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
#define ECRITUM_HANDLE_KIND_VALUE 7u
#define ECRITUM_HANDLE_KIND_JOB 8u
#define ECRITUM_HANDLE_KIND_SHIFT 48u
#define ECRITUM_HANDLE_GENERATION_SHIFT 32u
#define ECRITUM_HANDLE_SLOT_MASK 0xffffffffULL
#define ECRITUM_MAX_HANDLE_SLOTS 4096u
#define ECRITUM_ERROR_TEXT_CAPACITY 160u
#define ECRITUM_ERROR_OPERATION_CAPACITY 64u
#define ECRITUM_NAME_CAPACITY 256u
#define ECRITUM_CONFIG_MAX_BYTES 65536u
#define ECRITUM_CONFIG_MAX_DEPTH 16u
#define ECRITUM_CONFIG_MAX_ARRAY_ITEMS 256u
#define ECRITUM_CONFIG_MAX_STRING_BYTES 4096u
#define ECRITUM_CONFIG_MAX_SHORT_STRING_BYTES 255u

typedef struct ecritum_policy_config ecritum_policy_config_t;
typedef struct ecritum_value_record ecritum_value_record_t;

static void config_destroy(ecritum_policy_config_t *config);
static void value_record_destroy(ecritum_value_record_t *record);

typedef struct {
    graal_isolate_t *isolate;
    uint32_t live_contexts;
    ecritum_policy_config_t *config;
} ecritum_runtime_record_t;

typedef struct {
    uint32_t parent_slot;
    uint16_t parent_generation;
    uint32_t active_job_slot;
    uint16_t active_job_generation;
    ecritum_policy_config_t *config;
} ecritum_context_record_t;

typedef struct {
    int status;
    const char *category;
    char message[ECRITUM_ERROR_TEXT_CAPACITY];
    char operation[ECRITUM_ERROR_OPERATION_CAPACITY];
    char language[ECRITUM_ERROR_OPERATION_CAPACITY];
    char source_name[ECRITUM_ERROR_TEXT_CAPACITY];
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
    uint32_t active_calls;
    uint32_t active_projections;
    char name[ECRITUM_NAME_CAPACITY];
} ecritum_function_record_t;

typedef struct {
    uint32_t parent_function_slot;
    uint16_t parent_function_generation;
    ecritum_value_record_t *arguments;
    size_t argument_count;
} ecritum_call_record_t;

typedef struct {
    uint8_t *data;
    size_t len;
} ecritum_owned_bytes_t;

typedef struct {
    uint32_t slot;
    uint16_t generation;
} ecritum_projection_pin_t;

struct ecritum_value_record {
    int kind;
    union {
        int bool_value;
        int64_t int_value;
        double double_value;
        ecritum_owned_bytes_t bytes;
        struct {
            ecritum_value_record_t *items;
            size_t count;
        } array;
        struct {
            char **keys;
            size_t *key_lengths;
            ecritum_value_record_t *values;
            size_t count;
        } object;
    } value;
};

typedef struct {
    uint32_t parent_context_slot;
    uint16_t parent_context_generation;
    int state;
    int drained;
    int terminal_status;
    int has_result;
    int condition_initialized;
    pthread_cond_t condition;
    ecritum_value_record_t result;
    char message[ECRITUM_ERROR_TEXT_CAPACITY];
    char language[ECRITUM_ERROR_OPERATION_CAPACITY];
    char category[ECRITUM_ERROR_OPERATION_CAPACITY];
    char source_name[ECRITUM_ERROR_TEXT_CAPACITY];
} ecritum_job_record_t;

typedef struct {
    ecritum_job_t job;
    graal_isolate_t *isolate;
    uint8_t *source;
    size_t source_len;
    char *source_name;
    size_t source_name_len;
    ecritum_runtime_t runtime;
    char *host_manifest;
    size_t host_manifest_len;
    ecritum_projection_pin_t *projection_pins;
    size_t projection_pin_count;
} ecritum_eval_work_t;

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
        ecritum_value_record_t value_record;
        ecritum_job_record_t job;
    } value;
} ecritum_handle_slot_t;

static void value_record_destroy(ecritum_value_record_t *record) {
    if (record == NULL) {
        return;
    }
    switch (record->kind) {
    case ECRITUM_VALUE_KIND_STRING:
    case ECRITUM_VALUE_KIND_DATA:
        free(record->value.bytes.data);
        break;
    case ECRITUM_VALUE_KIND_ARRAY:
        for (size_t index = 0; index < record->value.array.count; index++) {
            value_record_destroy(&record->value.array.items[index]);
        }
        free(record->value.array.items);
        break;
    case ECRITUM_VALUE_KIND_OBJECT:
        for (size_t index = 0; index < record->value.object.count; index++) {
            free(record->value.object.keys[index]);
            value_record_destroy(&record->value.object.values[index]);
        }
        free(record->value.object.keys);
        free(record->value.object.key_lengths);
        free(record->value.object.values);
        break;
    default:
        break;
    }
    memset(record, 0, sizeof(*record));
}

static void call_record_destroy(ecritum_call_record_t *call) {
    if (call == NULL) {
        return;
    }
    for (size_t index = 0; index < call->argument_count; index++) {
        value_record_destroy(&call->arguments[index]);
    }
    free(call->arguments);
    call->arguments = NULL;
    call->argument_count = 0;
}

static void job_record_destroy(ecritum_job_record_t *job) {
    if (job == NULL) {
        return;
    }
    if (job->has_result) {
        value_record_destroy(&job->result);
    }
    if (job->condition_initialized) {
        pthread_cond_destroy(&job->condition);
    }
    memset(job, 0, sizeof(*job));
}

static int job_record_init(ecritum_job_record_t *job) {
    if (job == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    if (pthread_cond_init(&job->condition, NULL) != 0) {
        return ECRITUM_ERROR_INTERNAL;
    }
    job->condition_initialized = 1;
    return ECRITUM_OK;
}

static void job_signal_locked(ecritum_job_record_t *job) {
    if (job != NULL && job->condition_initialized) {
        pthread_cond_broadcast(&job->condition);
    }
}

static pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;
static ecritum_handle_slot_t registry[ECRITUM_MAX_HANDLE_SLOTS];

#define ECRITUM_CALLBACK_RUNTIME_STACK_MAX 16u
static _Thread_local uint32_t callback_runtime_slots[ECRITUM_CALLBACK_RUNTIME_STACK_MAX];
static _Thread_local uint16_t callback_runtime_generations[ECRITUM_CALLBACK_RUNTIME_STACK_MAX];
static _Thread_local size_t callback_runtime_depth = 0;

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

static int callback_stack_contains_runtime(uint32_t runtime_slot, uint16_t runtime_generation) {
    if (runtime_slot == 0) {
        return 0;
    }
    for (size_t index = 0; index < callback_runtime_depth; index++) {
        if (callback_runtime_slots[index] == runtime_slot
            && callback_runtime_generations[index] == runtime_generation) {
            return 1;
        }
    }
    return 0;
}

static void callback_stack_push_runtime(uint32_t runtime_slot, uint16_t runtime_generation) {
    if (runtime_slot == 0 || callback_runtime_depth >= ECRITUM_CALLBACK_RUNTIME_STACK_MAX) {
        return;
    }
    callback_runtime_slots[callback_runtime_depth] = runtime_slot;
    callback_runtime_generations[callback_runtime_depth] = runtime_generation;
    callback_runtime_depth++;
}

static void callback_stack_pop_runtime(uint32_t runtime_slot, uint16_t runtime_generation) {
    if (runtime_slot == 0 || callback_runtime_depth == 0) {
        return;
    }
    size_t index = callback_runtime_depth - 1;
    if (callback_runtime_slots[index] == runtime_slot
        && callback_runtime_generations[index] == runtime_generation) {
        callback_runtime_depth--;
    }
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

static void copy_view_text(char *destination, size_t capacity, const char *source, size_t source_len) {
    if (capacity == 0) {
        return;
    }
    if (source == NULL || source_len == 0) {
        destination[0] = '\0';
        return;
    }
    size_t copy_len = source_len < capacity - 1 ? source_len : capacity - 1;
    memcpy(destination, source, copy_len);
    destination[copy_len] = '\0';
}

static int view_equals(ecritum_string_view_t view_value, const char *expected) {
    size_t expected_len = strlen(expected);
    return view_value.len == expected_len
        && view_value.data != NULL
        && memcmp(view_value.data, expected, expected_len) == 0;
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
    if (entry->kind == ECRITUM_HANDLE_KIND_RUNTIME) {
        config_destroy(entry->value.runtime.config);
    } else if (entry->kind == ECRITUM_HANDLE_KIND_CONTEXT) {
        config_destroy(entry->value.context.config);
    } else if (entry->kind == ECRITUM_HANDLE_KIND_VALUE) {
        value_record_destroy(&entry->value.value_record);
    } else if (entry->kind == ECRITUM_HANDLE_KIND_CALL) {
        call_record_destroy(&entry->value.call);
    } else if (entry->kind == ECRITUM_HANDLE_KIND_JOB) {
        job_record_destroy(&entry->value.job);
    }
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

static const char *stable_error_category(const char *category) {
    if (category == NULL || category[0] == '\0') {
        return NULL;
    }
    if (strcmp(category, "syntax") == 0) return "syntax";
    if (strcmp(category, "runtime") == 0) return "runtime";
    if (strcmp(category, "permission") == 0) return "permission";
    if (strcmp(category, "timeout") == 0) return "timeout";
    if (strcmp(category, "callback") == 0) return "callback";
    if (strcmp(category, "internal") == 0) return "internal";
    return NULL;
}

static ecritum_error_t create_error_with_category_locked(int status, const char *operation, const char *message, const char *category) {
    ecritum_error_t handle = create_error_locked(status, operation, message);
    const char *stable_category = stable_error_category(category);
    if (handle == 0 || stable_category == NULL) {
        return handle;
    }

    ecritum_handle_slot_t *entry = validate_locked(handle, ECRITUM_HANDLE_KIND_ERROR);
    if (entry != NULL) {
        entry->value.error.category = stable_category;
    }
    return handle;
}

static ecritum_error_t create_error_with_details_locked(
    int status,
    const char *operation,
    const char *message,
    const char *category,
    const char *language,
    const char *source_name
) {
    ecritum_error_t handle = create_error_with_category_locked(status, operation, message, category);
    ecritum_handle_slot_t *entry = validate_locked(handle, ECRITUM_HANDLE_KIND_ERROR);
    if (entry != NULL) {
        copy_text(entry->value.error.language, sizeof(entry->value.error.language), language);
        copy_text(entry->value.error.source_name, sizeof(entry->value.error.source_name), source_name);
    }
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

static int function_has_active_calls_locked(ecritum_handle_slot_t *entry) {
    return entry->kind == ECRITUM_HANDLE_KIND_FUNCTION
        && (entry->value.function.active_calls > 0 || entry->value.function.active_projections > 0);
}

static int context_matches_active_callback_runtime_locked(ecritum_handle_slot_t *context_entry);
static int job_matches_active_callback_runtime_locked(ecritum_handle_slot_t *job_entry);

static int namespace_has_active_calls_locked(ecritum_handle_slot_t *namespace_entry) {
    if (namespace_entry->kind != ECRITUM_HANDLE_KIND_NAMESPACE) {
        return 0;
    }
    uint32_t namespace_slot = (uint32_t)(namespace_entry - registry);
    uint16_t namespace_generation = namespace_entry->generation;
    for (uint32_t slot = 1; slot < ECRITUM_MAX_HANDLE_SLOTS; slot++) {
        ecritum_handle_slot_t *entry = &registry[slot];
        if (function_matches_namespace(entry, namespace_slot, namespace_generation)
            && function_has_active_calls_locked(entry)) {
            return 1;
        }
    }
    return 0;
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

static int create_host_projection_snapshot_locked(
    uint32_t runtime_slot,
    uint16_t runtime_generation,
    ecritum_eval_work_t *work
) {
    if (work == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }

    size_t projection_count = 0;
    size_t manifest_len = 0;
    for (uint32_t namespace_slot = 1; namespace_slot < ECRITUM_MAX_HANDLE_SLOTS; namespace_slot++) {
        ecritum_handle_slot_t *namespace_entry = &registry[namespace_slot];
        if (!namespace_matches_runtime(namespace_entry, runtime_slot, runtime_generation)) {
            continue;
        }
        size_t namespace_len = strlen(namespace_entry->value.namespace_record.name);
        for (uint32_t function_slot = 1; function_slot < ECRITUM_MAX_HANDLE_SLOTS; function_slot++) {
            ecritum_handle_slot_t *function_entry = &registry[function_slot];
            if (!function_matches_namespace(function_entry, namespace_slot, namespace_entry->generation)) {
                continue;
            }
            size_t function_len = strlen(function_entry->value.function.name);
            if (manifest_len > ECRITUM_CONFIG_MAX_BYTES - namespace_len - function_len - 2u) {
                return ECRITUM_ERROR_INPUT_TOO_LARGE;
            }
            manifest_len += namespace_len + 1u + function_len + 1u;
            projection_count++;
        }
    }

    if (projection_count == 0) {
        return ECRITUM_OK;
    }
    char *manifest = malloc(manifest_len + 1u);
    ecritum_projection_pin_t *pins = calloc(projection_count, sizeof(ecritum_projection_pin_t));
    if (manifest == NULL || pins == NULL) {
        free(manifest);
        free(pins);
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }

    size_t offset = 0;
    size_t pin_index = 0;
    for (uint32_t namespace_slot = 1; namespace_slot < ECRITUM_MAX_HANDLE_SLOTS; namespace_slot++) {
        ecritum_handle_slot_t *namespace_entry = &registry[namespace_slot];
        if (!namespace_matches_runtime(namespace_entry, runtime_slot, runtime_generation)) {
            continue;
        }
        const char *namespace_name = namespace_entry->value.namespace_record.name;
        size_t namespace_len = strlen(namespace_name);
        for (uint32_t function_slot = 1; function_slot < ECRITUM_MAX_HANDLE_SLOTS; function_slot++) {
            ecritum_handle_slot_t *function_entry = &registry[function_slot];
            if (!function_matches_namespace(function_entry, namespace_slot, namespace_entry->generation)) {
                continue;
            }
            const char *function_name = function_entry->value.function.name;
            size_t function_len = strlen(function_name);
            memcpy(manifest + offset, namespace_name, namespace_len);
            offset += namespace_len;
            manifest[offset++] = '/';
            memcpy(manifest + offset, function_name, function_len);
            offset += function_len;
            manifest[offset++] = '\n';
            function_entry->value.function.active_projections++;
            pins[pin_index].slot = function_slot;
            pins[pin_index].generation = function_entry->generation;
            pin_index++;
        }
    }
    manifest[offset] = '\0';
    work->host_manifest = manifest;
    work->host_manifest_len = offset;
    work->projection_pins = pins;
    work->projection_pin_count = pin_index;
    return ECRITUM_OK;
}

static void release_host_projection_snapshot_locked(ecritum_eval_work_t *work) {
    if (work == NULL) {
        return;
    }
    for (size_t index = 0; index < work->projection_pin_count; index++) {
        ecritum_projection_pin_t pin = work->projection_pins[index];
        if (pin.slot == 0 || pin.slot >= ECRITUM_MAX_HANDLE_SLOTS) {
            continue;
        }
        ecritum_handle_slot_t *entry = &registry[pin.slot];
        if (entry->kind == ECRITUM_HANDLE_KIND_FUNCTION
            && entry->generation == pin.generation
            && entry->value.function.active_projections > 0) {
            entry->value.function.active_projections--;
        }
    }
    work->projection_pin_count = 0;
}

typedef struct {
    char **items;
    size_t count;
} ecritum_string_set_t;

enum {
    ECRITUM_FS_DENIED = 0,
    ECRITUM_FS_READ_ONLY = 1,
    ECRITUM_FS_READ_WRITE = 2
};

enum {
    ECRITUM_LIMIT_EXECUTION_TIMEOUT_NANOS = 0,
    ECRITUM_LIMIT_MAX_INPUT_BYTES = 1,
    ECRITUM_LIMIT_MAX_OUTPUT_BYTES = 2,
    ECRITUM_LIMIT_MAX_STACK_DEPTH = 3,
    ECRITUM_LIMIT_MAX_HEAP_BYTES = 4,
    ECRITUM_LIMIT_MAX_CALLBACK_QUEUE_LENGTH = 5,
    ECRITUM_LIMIT_CALLBACK_TIMEOUT_NANOS = 6,
    ECRITUM_LIMIT_COUNT = 7
};

struct ecritum_policy_config {
    ecritum_string_set_t languages;
    int filesystem_mode;
    ecritum_string_set_t filesystem_roots;
    int network_allowed;
    ecritum_string_set_t network_rules;
    int process_allowed;
    ecritum_string_set_t process_commands;
    int environment_allowed;
    ecritum_string_set_t environment_keys;
    int clock_allowed;
    int random_allowed;
    int log_allowed;
    int diagnostics_raw;
    uint64_t limits[ECRITUM_LIMIT_COUNT];
    int limit_present[ECRITUM_LIMIT_COUNT];
};

typedef enum {
    JSON_OBJECT,
    JSON_ARRAY,
    JSON_STRING,
    JSON_NUMBER,
    JSON_TRUE_VALUE,
    JSON_FALSE_VALUE,
    JSON_NULL_VALUE
} json_type_t;

typedef struct json_value json_value_t;

typedef struct json_member {
    char *key;
    json_value_t *value;
    struct json_member *next;
} json_member_t;

struct json_value {
    json_type_t type;
    char *string;
    size_t string_len;
    json_value_t **items;
    size_t item_count;
    json_member_t *members;
};

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
    const char *message;
} json_parser_t;

static int fail_config(const char **message, const char *text) {
    if (message != NULL) {
        *message = text;
    }
    return ECRITUM_ERROR_INVALID_CONFIG;
}

static char *copy_string_len(const char *data, size_t len) {
    char *copy = malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }
    if (len > 0) {
        memcpy(copy, data, len);
    }
    copy[len] = '\0';
    return copy;
}

static void string_set_destroy(ecritum_string_set_t *set) {
    if (set == NULL) {
        return;
    }
    for (size_t index = 0; index < set->count; index++) {
        free(set->items[index]);
    }
    free(set->items);
    set->items = NULL;
    set->count = 0;
}

static int string_set_contains(const ecritum_string_set_t *set, const char *item) {
    for (size_t index = 0; index < set->count; index++) {
        if (strcmp(set->items[index], item) == 0) {
            return 1;
        }
    }
    return 0;
}

static int string_set_add_unique(ecritum_string_set_t *set, const char *item, const char **message) {
    if (string_set_contains(set, item)) {
        return fail_config(message, "duplicate config set item");
    }
    char *copy = copy_string_len(item, strlen(item));
    if (copy == NULL) {
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    char **items = realloc(set->items, sizeof(char *) * (set->count + 1));
    if (items == NULL) {
        free(copy);
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    set->items = items;
    set->items[set->count] = copy;
    set->count++;
    return ECRITUM_OK;
}

static int string_set_clone(ecritum_string_set_t *destination, const ecritum_string_set_t *source) {
    destination->items = NULL;
    destination->count = 0;
    for (size_t index = 0; index < source->count; index++) {
        int status = string_set_add_unique(destination, source->items[index], NULL);
        if (status != ECRITUM_OK) {
            string_set_destroy(destination);
            return status;
        }
    }
    return ECRITUM_OK;
}

static int string_set_is_subset(const ecritum_string_set_t *child, const ecritum_string_set_t *parent) {
    for (size_t index = 0; index < child->count; index++) {
        if (!string_set_contains(parent, child->items[index])) {
            return 0;
        }
    }
    return 1;
}

static void string_set_replace(ecritum_string_set_t *destination, ecritum_string_set_t *source) {
    string_set_destroy(destination);
    *destination = *source;
    source->items = NULL;
    source->count = 0;
}

static ecritum_policy_config_t *config_create_default(void) {
    ecritum_policy_config_t *config = calloc(1, sizeof(ecritum_policy_config_t));
    if (config == NULL) {
        return NULL;
    }
    config->filesystem_mode = ECRITUM_FS_DENIED;
    return config;
}

static ecritum_policy_config_t *config_clone(const ecritum_policy_config_t *source) {
    ecritum_policy_config_t *copy = calloc(1, sizeof(ecritum_policy_config_t));
    if (copy == NULL) {
        return NULL;
    }
    *copy = *source;
    memset(&copy->languages, 0, sizeof(copy->languages));
    memset(&copy->filesystem_roots, 0, sizeof(copy->filesystem_roots));
    memset(&copy->network_rules, 0, sizeof(copy->network_rules));
    memset(&copy->process_commands, 0, sizeof(copy->process_commands));
    memset(&copy->environment_keys, 0, sizeof(copy->environment_keys));
    if (string_set_clone(&copy->languages, &source->languages) != ECRITUM_OK
        || string_set_clone(&copy->filesystem_roots, &source->filesystem_roots) != ECRITUM_OK
        || string_set_clone(&copy->network_rules, &source->network_rules) != ECRITUM_OK
        || string_set_clone(&copy->process_commands, &source->process_commands) != ECRITUM_OK
        || string_set_clone(&copy->environment_keys, &source->environment_keys) != ECRITUM_OK) {
        config_destroy(copy);
        return NULL;
    }
    return copy;
}

static void config_destroy(ecritum_policy_config_t *config) {
    if (config == NULL) {
        return;
    }
    string_set_destroy(&config->languages);
    string_set_destroy(&config->filesystem_roots);
    string_set_destroy(&config->network_rules);
    string_set_destroy(&config->process_commands);
    string_set_destroy(&config->environment_keys);
    free(config);
}

static int valid_utf8(const uint8_t *data, size_t len) {
    size_t index = 0;
    while (index < len) {
        uint8_t first = data[index++];
        if (first <= 0x7f) {
            continue;
        }
        if (first >= 0xc2 && first <= 0xdf) {
            if (index >= len || (data[index++] & 0xc0) != 0x80) {
                return 0;
            }
            continue;
        }
        if (first >= 0xe0 && first <= 0xef) {
            if (index + 1 >= len) {
                return 0;
            }
            uint8_t second = data[index++];
            uint8_t third = data[index++];
            if ((second & 0xc0) != 0x80 || (third & 0xc0) != 0x80) {
                return 0;
            }
            if (first == 0xe0 && second < 0xa0) {
                return 0;
            }
            if (first == 0xed && second >= 0xa0) {
                return 0;
            }
            continue;
        }
        if (first >= 0xf0 && first <= 0xf4) {
            if (index + 2 >= len) {
                return 0;
            }
            uint8_t second = data[index++];
            uint8_t third = data[index++];
            uint8_t fourth = data[index++];
            if ((second & 0xc0) != 0x80 || (third & 0xc0) != 0x80 || (fourth & 0xc0) != 0x80) {
                return 0;
            }
            if (first == 0xf0 && second < 0x90) {
                return 0;
            }
            if (first == 0xf4 && second >= 0x90) {
                return 0;
            }
            continue;
        }
        return 0;
    }
    return 1;
}

static int json_append_byte(char **buffer, size_t *len, size_t *capacity, char byte) {
    if (*len >= ECRITUM_CONFIG_MAX_STRING_BYTES) {
        return ECRITUM_ERROR_INVALID_CONFIG;
    }
    if (*len + 1 >= *capacity) {
        size_t new_capacity = *capacity == 0 ? 32 : *capacity * 2;
        char *new_buffer = realloc(*buffer, new_capacity);
        if (new_buffer == NULL) {
            return ECRITUM_ERROR_OUT_OF_MEMORY;
        }
        *buffer = new_buffer;
        *capacity = new_capacity;
    }
    (*buffer)[(*len)++] = byte;
    (*buffer)[*len] = '\0';
    return ECRITUM_OK;
}

static int json_append_codepoint(char **buffer, size_t *len, size_t *capacity, uint32_t codepoint) {
    if (codepoint == 0 || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
        return ECRITUM_ERROR_INVALID_CONFIG;
    }
    if (codepoint <= 0x7f) {
        return json_append_byte(buffer, len, capacity, (char)codepoint);
    }
    if (codepoint <= 0x7ff) {
        int status = json_append_byte(buffer, len, capacity, (char)(0xc0 | (codepoint >> 6)));
        if (status != ECRITUM_OK) return status;
        return json_append_byte(buffer, len, capacity, (char)(0x80 | (codepoint & 0x3f)));
    }
    if (codepoint <= 0xffff) {
        int status = json_append_byte(buffer, len, capacity, (char)(0xe0 | (codepoint >> 12)));
        if (status != ECRITUM_OK) return status;
        status = json_append_byte(buffer, len, capacity, (char)(0x80 | ((codepoint >> 6) & 0x3f)));
        if (status != ECRITUM_OK) return status;
        return json_append_byte(buffer, len, capacity, (char)(0x80 | (codepoint & 0x3f)));
    }
    int status = json_append_byte(buffer, len, capacity, (char)(0xf0 | (codepoint >> 18)));
    if (status != ECRITUM_OK) return status;
    status = json_append_byte(buffer, len, capacity, (char)(0x80 | ((codepoint >> 12) & 0x3f)));
    if (status != ECRITUM_OK) return status;
    status = json_append_byte(buffer, len, capacity, (char)(0x80 | ((codepoint >> 6) & 0x3f)));
    if (status != ECRITUM_OK) return status;
    return json_append_byte(buffer, len, capacity, (char)(0x80 | (codepoint & 0x3f)));
}

static int json_hex_value(uint8_t value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int json_parse_hex4(json_parser_t *parser, uint32_t *out_value) {
    if (parser->pos + 4 > parser->len) {
        return ECRITUM_ERROR_INVALID_CONFIG;
    }
    uint32_t value = 0;
    for (size_t index = 0; index < 4; index++) {
        int digit = json_hex_value(parser->data[parser->pos++]);
        if (digit < 0) {
            return ECRITUM_ERROR_INVALID_CONFIG;
        }
        value = (value << 4) | (uint32_t)digit;
    }
    *out_value = value;
    return ECRITUM_OK;
}

static void json_skip_ws(json_parser_t *parser) {
    while (parser->pos < parser->len) {
        uint8_t value = parser->data[parser->pos];
        if (value != ' ' && value != '\n' && value != '\r' && value != '\t') {
            return;
        }
        parser->pos++;
    }
}

static int json_parse_string(json_parser_t *parser, char **out_string, size_t *out_len) {
    if (parser->pos >= parser->len || parser->data[parser->pos] != '"') {
        parser->message = "expected JSON string";
        return ECRITUM_ERROR_INVALID_CONFIG;
    }
    parser->pos++;
    char *buffer = NULL;
    size_t len = 0;
    size_t capacity = 0;
    int status = json_append_byte(&buffer, &len, &capacity, '\0');
    if (status != ECRITUM_OK) {
        free(buffer);
        return status;
    }
    len = 0;
    buffer[0] = '\0';

    while (parser->pos < parser->len) {
        uint8_t value = parser->data[parser->pos++];
        if (value == '"') {
            *out_string = buffer;
            *out_len = len;
            return ECRITUM_OK;
        }
        if (value < 0x20) {
            parser->message = "unescaped control character in JSON string";
            free(buffer);
            return ECRITUM_ERROR_INVALID_CONFIG;
        }
        if (value != '\\') {
            status = json_append_byte(&buffer, &len, &capacity, (char)value);
            if (status != ECRITUM_OK) {
                parser->message = "JSON string is too large";
                free(buffer);
                return status;
            }
            continue;
        }
        if (parser->pos >= parser->len) {
            parser->message = "unterminated JSON escape";
            free(buffer);
            return ECRITUM_ERROR_INVALID_CONFIG;
        }
        uint8_t escaped = parser->data[parser->pos++];
        switch (escaped) {
        case '"': status = json_append_byte(&buffer, &len, &capacity, '"'); break;
        case '\\': status = json_append_byte(&buffer, &len, &capacity, '\\'); break;
        case '/': status = json_append_byte(&buffer, &len, &capacity, '/'); break;
        case 'b': status = json_append_byte(&buffer, &len, &capacity, '\b'); break;
        case 'f': status = json_append_byte(&buffer, &len, &capacity, '\f'); break;
        case 'n': status = json_append_byte(&buffer, &len, &capacity, '\n'); break;
        case 'r': status = json_append_byte(&buffer, &len, &capacity, '\r'); break;
        case 't': status = json_append_byte(&buffer, &len, &capacity, '\t'); break;
        case 'u': {
            uint32_t codepoint = 0;
            status = json_parse_hex4(parser, &codepoint);
            if (status != ECRITUM_OK) {
                parser->message = "invalid unicode escape";
                free(buffer);
                return status;
            }
            if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                if (parser->pos + 6 > parser->len || parser->data[parser->pos] != '\\' || parser->data[parser->pos + 1] != 'u') {
                    parser->message = "invalid unicode surrogate";
                    free(buffer);
                    return ECRITUM_ERROR_INVALID_CONFIG;
                }
                parser->pos += 2;
                uint32_t low = 0;
                status = json_parse_hex4(parser, &low);
                if (status != ECRITUM_OK || low < 0xdc00 || low > 0xdfff) {
                    parser->message = "invalid unicode surrogate";
                    free(buffer);
                    return ECRITUM_ERROR_INVALID_CONFIG;
                }
                codepoint = 0x10000 + (((codepoint - 0xd800) << 10) | (low - 0xdc00));
            }
            status = json_append_codepoint(&buffer, &len, &capacity, codepoint);
            break;
        }
        default:
            parser->message = "invalid JSON escape";
            free(buffer);
            return ECRITUM_ERROR_INVALID_CONFIG;
        }
        if (status != ECRITUM_OK) {
            parser->message = "JSON string is too large";
            free(buffer);
            return status;
        }
    }

    parser->message = "unterminated JSON string";
    free(buffer);
    return ECRITUM_ERROR_INVALID_CONFIG;
}

static void json_value_destroy(json_value_t *value) {
    if (value == NULL) {
        return;
    }
    free(value->string);
    for (size_t index = 0; index < value->item_count; index++) {
        json_value_destroy(value->items[index]);
    }
    free(value->items);
    json_member_t *member = value->members;
    while (member != NULL) {
        json_member_t *next = member->next;
        free(member->key);
        json_value_destroy(member->value);
        free(member);
        member = next;
    }
    free(value);
}

static json_value_t *json_new(json_type_t type) {
    json_value_t *value = calloc(1, sizeof(json_value_t));
    if (value != NULL) {
        value->type = type;
    }
    return value;
}

static int json_parse_value(json_parser_t *parser, size_t depth, json_value_t **out_value);

static int json_parse_array(json_parser_t *parser, size_t depth, json_value_t **out_value) {
    if (depth > ECRITUM_CONFIG_MAX_DEPTH) {
        parser->message = "JSON nesting is too deep";
        return ECRITUM_ERROR_INVALID_CONFIG;
    }
    parser->pos++;
    json_value_t *array = json_new(JSON_ARRAY);
    if (array == NULL) {
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    json_skip_ws(parser);
    if (parser->pos < parser->len && parser->data[parser->pos] == ']') {
        parser->pos++;
        *out_value = array;
        return ECRITUM_OK;
    }
    while (parser->pos < parser->len) {
        if (array->item_count >= ECRITUM_CONFIG_MAX_ARRAY_ITEMS) {
            parser->message = "JSON array has too many items";
            json_value_destroy(array);
            return ECRITUM_ERROR_INVALID_CONFIG;
        }
        json_value_t *item = NULL;
        int status = json_parse_value(parser, depth + 1, &item);
        if (status != ECRITUM_OK) {
            json_value_destroy(array);
            return status;
        }
        json_value_t **items = realloc(array->items, sizeof(json_value_t *) * (array->item_count + 1));
        if (items == NULL) {
            json_value_destroy(item);
            json_value_destroy(array);
            return ECRITUM_ERROR_OUT_OF_MEMORY;
        }
        array->items = items;
        array->items[array->item_count++] = item;
        json_skip_ws(parser);
        if (parser->pos < parser->len && parser->data[parser->pos] == ',') {
            parser->pos++;
            json_skip_ws(parser);
            continue;
        }
        if (parser->pos < parser->len && parser->data[parser->pos] == ']') {
            parser->pos++;
            *out_value = array;
            return ECRITUM_OK;
        }
        break;
    }
    parser->message = "unterminated JSON array";
    json_value_destroy(array);
    return ECRITUM_ERROR_INVALID_CONFIG;
}

static int json_object_has_key(json_value_t *object, const char *key) {
    for (json_member_t *member = object->members; member != NULL; member = member->next) {
        if (strcmp(member->key, key) == 0) {
            return 1;
        }
    }
    return 0;
}

static int json_parse_object(json_parser_t *parser, size_t depth, json_value_t **out_value) {
    if (depth > ECRITUM_CONFIG_MAX_DEPTH) {
        parser->message = "JSON nesting is too deep";
        return ECRITUM_ERROR_INVALID_CONFIG;
    }
    parser->pos++;
    json_value_t *object = json_new(JSON_OBJECT);
    if (object == NULL) {
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    json_skip_ws(parser);
    if (parser->pos < parser->len && parser->data[parser->pos] == '}') {
        parser->pos++;
        *out_value = object;
        return ECRITUM_OK;
    }
    while (parser->pos < parser->len) {
        char *key = NULL;
        size_t key_len = 0;
        int status = json_parse_string(parser, &key, &key_len);
        if (status != ECRITUM_OK) {
            json_value_destroy(object);
            return status;
        }
        if (key_len > ECRITUM_CONFIG_MAX_SHORT_STRING_BYTES) {
            parser->message = "JSON object key is too large";
            free(key);
            json_value_destroy(object);
            return ECRITUM_ERROR_INVALID_CONFIG;
        }
        if (json_object_has_key(object, key)) {
            parser->message = "duplicate JSON object key";
            free(key);
            json_value_destroy(object);
            return ECRITUM_ERROR_INVALID_CONFIG;
        }
        json_skip_ws(parser);
        if (parser->pos >= parser->len || parser->data[parser->pos] != ':') {
            parser->message = "expected JSON object colon";
            free(key);
            json_value_destroy(object);
            return ECRITUM_ERROR_INVALID_CONFIG;
        }
        parser->pos++;
        json_skip_ws(parser);
        json_value_t *member_value = NULL;
        status = json_parse_value(parser, depth + 1, &member_value);
        if (status != ECRITUM_OK) {
            free(key);
            json_value_destroy(object);
            return status;
        }
        json_member_t *member = calloc(1, sizeof(json_member_t));
        if (member == NULL) {
            free(key);
            json_value_destroy(member_value);
            json_value_destroy(object);
            return ECRITUM_ERROR_OUT_OF_MEMORY;
        }
        member->key = key;
        member->value = member_value;
        member->next = object->members;
        object->members = member;
        json_skip_ws(parser);
        if (parser->pos < parser->len && parser->data[parser->pos] == ',') {
            parser->pos++;
            json_skip_ws(parser);
            continue;
        }
        if (parser->pos < parser->len && parser->data[parser->pos] == '}') {
            parser->pos++;
            *out_value = object;
            return ECRITUM_OK;
        }
        break;
    }
    parser->message = "unterminated JSON object";
    json_value_destroy(object);
    return ECRITUM_ERROR_INVALID_CONFIG;
}

static int json_parse_number(json_parser_t *parser, json_value_t **out_value) {
    size_t start = parser->pos;
    if (parser->data[parser->pos] == '-') {
        parser->pos++;
    }
    if (parser->pos >= parser->len) {
        parser->message = "invalid JSON number";
        return ECRITUM_ERROR_INVALID_CONFIG;
    }
    if (parser->data[parser->pos] == '0') {
        parser->pos++;
        if (parser->pos < parser->len && parser->data[parser->pos] >= '0' && parser->data[parser->pos] <= '9') {
            parser->message = "invalid JSON number";
            return ECRITUM_ERROR_INVALID_CONFIG;
        }
    } else if (parser->data[parser->pos] >= '1' && parser->data[parser->pos] <= '9') {
        while (parser->pos < parser->len && parser->data[parser->pos] >= '0' && parser->data[parser->pos] <= '9') {
            parser->pos++;
        }
    } else {
        parser->message = "invalid JSON number";
        return ECRITUM_ERROR_INVALID_CONFIG;
    }
    if (parser->pos < parser->len && parser->data[parser->pos] == '.') {
        parser->pos++;
        if (parser->pos >= parser->len || parser->data[parser->pos] < '0' || parser->data[parser->pos] > '9') {
            parser->message = "invalid JSON number";
            return ECRITUM_ERROR_INVALID_CONFIG;
        }
        while (parser->pos < parser->len && parser->data[parser->pos] >= '0' && parser->data[parser->pos] <= '9') {
            parser->pos++;
        }
    }
    if (parser->pos < parser->len && (parser->data[parser->pos] == 'e' || parser->data[parser->pos] == 'E')) {
        parser->pos++;
        if (parser->pos < parser->len && (parser->data[parser->pos] == '+' || parser->data[parser->pos] == '-')) {
            parser->pos++;
        }
        if (parser->pos >= parser->len || parser->data[parser->pos] < '0' || parser->data[parser->pos] > '9') {
            parser->message = "invalid JSON number";
            return ECRITUM_ERROR_INVALID_CONFIG;
        }
        while (parser->pos < parser->len && parser->data[parser->pos] >= '0' && parser->data[parser->pos] <= '9') {
            parser->pos++;
        }
    }
    json_value_t *number = json_new(JSON_NUMBER);
    if (number == NULL) {
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    number->string = copy_string_len((const char *)(parser->data + start), parser->pos - start);
    number->string_len = parser->pos - start;
    if (number->string == NULL) {
        json_value_destroy(number);
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    *out_value = number;
    return ECRITUM_OK;
}

static int json_parse_literal(json_parser_t *parser, const char *literal, json_type_t type, json_value_t **out_value) {
    size_t len = strlen(literal);
    if (parser->pos + len > parser->len || memcmp(parser->data + parser->pos, literal, len) != 0) {
        parser->message = "invalid JSON literal";
        return ECRITUM_ERROR_INVALID_CONFIG;
    }
    parser->pos += len;
    json_value_t *value = json_new(type);
    if (value == NULL) {
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    *out_value = value;
    return ECRITUM_OK;
}

static int json_parse_value(json_parser_t *parser, size_t depth, json_value_t **out_value) {
    if (depth > ECRITUM_CONFIG_MAX_DEPTH) {
        parser->message = "JSON nesting is too deep";
        return ECRITUM_ERROR_INVALID_CONFIG;
    }
    json_skip_ws(parser);
    if (parser->pos >= parser->len) {
        parser->message = "unexpected end of JSON";
        return ECRITUM_ERROR_INVALID_CONFIG;
    }
    uint8_t value = parser->data[parser->pos];
    if (value == '{') {
        return json_parse_object(parser, depth, out_value);
    }
    if (value == '[') {
        return json_parse_array(parser, depth, out_value);
    }
    if (value == '"') {
        json_value_t *string = json_new(JSON_STRING);
        if (string == NULL) {
            return ECRITUM_ERROR_OUT_OF_MEMORY;
        }
        int status = json_parse_string(parser, &string->string, &string->string_len);
        if (status != ECRITUM_OK) {
            json_value_destroy(string);
            return status;
        }
        *out_value = string;
        return ECRITUM_OK;
    }
    if (value == '-' || (value >= '0' && value <= '9')) {
        return json_parse_number(parser, out_value);
    }
    if (value == 't') {
        return json_parse_literal(parser, "true", JSON_TRUE_VALUE, out_value);
    }
    if (value == 'f') {
        return json_parse_literal(parser, "false", JSON_FALSE_VALUE, out_value);
    }
    if (value == 'n') {
        return json_parse_literal(parser, "null", JSON_NULL_VALUE, out_value);
    }
    parser->message = "invalid JSON value";
    return ECRITUM_ERROR_INVALID_CONFIG;
}

static int json_parse_document(ecritum_bytes_t bytes, json_value_t **out_value, const char **message) {
    json_parser_t parser = {bytes.data, bytes.len, 0, "invalid JSON"};
    int status = json_parse_value(&parser, 1, out_value);
    if (status != ECRITUM_OK) {
        if (message != NULL) *message = parser.message;
        return status;
    }
    json_skip_ws(&parser);
    if (parser.pos != parser.len) {
        json_value_destroy(*out_value);
        *out_value = NULL;
        if (message != NULL) *message = "trailing data after JSON";
        return ECRITUM_ERROR_INVALID_CONFIG;
    }
    return ECRITUM_OK;
}

static json_member_t *json_find_member(json_value_t *object, const char *key) {
    if (object == NULL || object->type != JSON_OBJECT) {
        return NULL;
    }
    for (json_member_t *member = object->members; member != NULL; member = member->next) {
        if (strcmp(member->key, key) == 0) {
            return member;
        }
    }
    return NULL;
}

static json_value_t *json_get(json_value_t *object, const char *key) {
    json_member_t *member = json_find_member(object, key);
    return member == NULL ? NULL : member->value;
}

static int json_has_only_keys(json_value_t *object, const char **keys, size_t key_count, const char **message) {
    if (object == NULL || object->type != JSON_OBJECT) {
        return fail_config(message, "expected JSON object");
    }
    for (json_member_t *member = object->members; member != NULL; member = member->next) {
        int allowed = 0;
        for (size_t index = 0; index < key_count; index++) {
            if (strcmp(member->key, keys[index]) == 0) {
                allowed = 1;
                break;
            }
        }
        if (!allowed) {
            return fail_config(message, "unknown configuration key");
        }
    }
    return ECRITUM_OK;
}

static int json_require(json_value_t *object, const char *key, json_value_t **out_value, const char **message) {
    json_value_t *value = json_get(object, key);
    if (value == NULL) {
        return fail_config(message, "missing required configuration key");
    }
    *out_value = value;
    return ECRITUM_OK;
}

static int json_number_is_digits_only(json_value_t *value) {
    if (value == NULL || value->type != JSON_NUMBER || value->string_len == 0) {
        return 0;
    }
    for (size_t index = 0; index < value->string_len; index++) {
        if (value->string[index] < '0' || value->string[index] > '9') {
            return 0;
        }
    }
    return 1;
}

static int json_parse_uint64_digits(json_value_t *value, uint64_t *out_value, const char **message) {
    if (!json_number_is_digits_only(value)) {
        return fail_config(message, "expected unsigned integer");
    }
    uint64_t parsed = 0;
    for (size_t index = 0; index < value->string_len; index++) {
        uint64_t digit = (uint64_t)(value->string[index] - '0');
        if (parsed > (UINT64_MAX - digit) / 10u) {
            return fail_config(message, "integer is too large");
        }
        parsed = parsed * 10u + digit;
    }
    *out_value = parsed;
    return ECRITUM_OK;
}

static int validate_schema_version(json_value_t *root, const char **message) {
    json_value_t *version = NULL;
    int status = json_require(root, "schemaVersion", &version, message);
    if (status != ECRITUM_OK) {
        return status;
    }
    uint64_t raw_version = 0;
    status = json_parse_uint64_digits(version, &raw_version, message);
    if (status != ECRITUM_OK) {
        return status;
    }
    if (raw_version == 0 || raw_version > UINT32_MAX) {
        return fail_config(message, "invalid configuration schema version");
    }
    if (raw_version > 1) {
        if (message != NULL) *message = "unsupported configuration schema version";
        return ECRITUM_ERROR_UNSUPPORTED_CONFIG_VERSION;
    }
    return ECRITUM_OK;
}

static int expect_object_value(json_value_t *value, const char **message) {
    if (value == NULL || value->type != JSON_OBJECT) {
        return fail_config(message, "expected JSON object");
    }
    return ECRITUM_OK;
}

static int expect_array_value(json_value_t *value, const char **message) {
    if (value == NULL || value->type != JSON_ARRAY) {
        return fail_config(message, "expected JSON array");
    }
    return ECRITUM_OK;
}

static int expect_string_value(json_value_t *value, const char **out_string, const char **message) {
    if (value == NULL || value->type != JSON_STRING) {
        return fail_config(message, "expected JSON string");
    }
    *out_string = value->string;
    return ECRITUM_OK;
}

static int expect_short_string_value(json_value_t *value, const char **out_string, const char **message) {
    int status = expect_string_value(value, out_string, message);
    if (status != ECRITUM_OK) {
        return status;
    }
    if (value->string_len > ECRITUM_CONFIG_MAX_SHORT_STRING_BYTES) {
        return fail_config(message, "configuration string is too large");
    }
    return ECRITUM_OK;
}

static int expect_mode(json_value_t *object, const char **mode, const char **message) {
    json_value_t *mode_value = NULL;
    int status = json_require(object, "mode", &mode_value, message);
    if (status != ECRITUM_OK) {
        return status;
    }
    return expect_short_string_value(mode_value, mode, message);
}

static int valid_ascii_identifier_string(const char *value) {
    return valid_identifier(value, strlen(value));
}

static int valid_lower_identifier_string(const char *value) {
    size_t len = strlen(value);
    if (len == 0 || value[0] < 'a' || value[0] > 'z') {
        return 0;
    }
    for (size_t index = 1; index < len; index++) {
        unsigned char c = (unsigned char)value[index];
        if (!((c >= 'a' && c <= 'z') || ascii_digit(c) || c == '_')) {
            return 0;
        }
    }
    return 1;
}

static int valid_host_string(const char *value) {
    size_t len = strlen(value);
    if (len == 0) {
        return 0;
    }
    for (size_t index = 0; index < len; index++) {
        unsigned char c = (unsigned char)value[index];
        if (c == '*' || c <= 0x20 || c > 0x7e) {
            return 0;
        }
    }
    return 1;
}

static int valid_path_string(const char *path, int strict_components) {
    size_t len = strlen(path);
    if (len == 0 || path[0] != '/') {
        return 0;
    }
    if (len == 1) {
        return 1;
    }
    if (path[len - 1] == '/') {
        return 0;
    }
    size_t segment_start = 1;
    for (size_t index = 1; index <= len; index++) {
        if (index < len && path[index] != '/') {
            continue;
        }
        size_t segment_len = index - segment_start;
        if (segment_len == 0) {
            return 0;
        }
        if (strict_components
            && ((segment_len == 1 && path[segment_start] == '.')
                || (segment_len == 2 && path[segment_start] == '.' && path[segment_start + 1] == '.'))) {
            return 0;
        }
        segment_start = index + 1;
    }
    return 1;
}

static int parse_mode_only(json_value_t *value, int *out_allowed, const char **message) {
    static const char *keys[] = {"mode"};
    int status = expect_object_value(value, message);
    if (status != ECRITUM_OK) return status;
    status = json_has_only_keys(value, keys, sizeof(keys) / sizeof(keys[0]), message);
    if (status != ECRITUM_OK) return status;
    const char *mode = NULL;
    status = expect_mode(value, &mode, message);
    if (status != ECRITUM_OK) return status;
    if (strcmp(mode, "denied") == 0) {
        *out_allowed = 0;
        return ECRITUM_OK;
    }
    if (strcmp(mode, "allowed") == 0) {
        *out_allowed = 1;
        return ECRITUM_OK;
    }
    return fail_config(message, "unknown policy mode");
}

static int parse_filesystem_policy(json_value_t *value, int *out_mode, ecritum_string_set_t *out_roots, const char **message) {
    int status = expect_object_value(value, message);
    if (status != ECRITUM_OK) return status;
    const char *mode = NULL;
    status = expect_mode(value, &mode, message);
    if (status != ECRITUM_OK) return status;
    if (strcmp(mode, "denied") == 0) {
        static const char *keys[] = {"mode"};
        status = json_has_only_keys(value, keys, sizeof(keys) / sizeof(keys[0]), message);
        if (status != ECRITUM_OK) return status;
        *out_mode = ECRITUM_FS_DENIED;
        return ECRITUM_OK;
    }
    if (strcmp(mode, "read_only") != 0 && strcmp(mode, "read_write") != 0) {
        return fail_config(message, "unknown filesystem mode");
    }
    static const char *keys[] = {"mode", "roots"};
    status = json_has_only_keys(value, keys, sizeof(keys) / sizeof(keys[0]), message);
    if (status != ECRITUM_OK) return status;
    json_value_t *roots = NULL;
    status = json_require(value, "roots", &roots, message);
    if (status != ECRITUM_OK) return status;
    status = expect_array_value(roots, message);
    if (status != ECRITUM_OK) return status;
    if (roots->item_count == 0) {
        return fail_config(message, "filesystem roots are empty");
    }
    for (size_t index = 0; index < roots->item_count; index++) {
        json_value_t *root = roots->items[index];
        status = expect_object_value(root, message);
        if (status != ECRITUM_OK) return status;
        static const char *root_keys[] = {"kind", "path"};
        status = json_has_only_keys(root, root_keys, sizeof(root_keys) / sizeof(root_keys[0]), message);
        if (status != ECRITUM_OK) return status;
        json_value_t *kind_value = NULL;
        json_value_t *path_value = NULL;
        const char *kind = NULL;
        const char *path = NULL;
        status = json_require(root, "kind", &kind_value, message);
        if (status != ECRITUM_OK) return status;
        status = json_require(root, "path", &path_value, message);
        if (status != ECRITUM_OK) return status;
        status = expect_short_string_value(kind_value, &kind, message);
        if (status != ECRITUM_OK) return status;
        status = expect_string_value(path_value, &path, message);
        if (status != ECRITUM_OK) return status;
        if (strcmp(kind, "directory") != 0 || !valid_path_string(path, 1)) {
            return fail_config(message, "invalid filesystem root");
        }
        status = string_set_add_unique(out_roots, path, message);
        if (status != ECRITUM_OK) return status;
    }
    *out_mode = strcmp(mode, "read_only") == 0 ? ECRITUM_FS_READ_ONLY : ECRITUM_FS_READ_WRITE;
    return ECRITUM_OK;
}

static int parse_network_policy(json_value_t *value, int *out_allowed, ecritum_string_set_t *out_rules, const char **message) {
    int status = expect_object_value(value, message);
    if (status != ECRITUM_OK) return status;
    const char *mode = NULL;
    status = expect_mode(value, &mode, message);
    if (status != ECRITUM_OK) return status;
    if (strcmp(mode, "denied") == 0) {
        static const char *keys[] = {"mode"};
        status = json_has_only_keys(value, keys, sizeof(keys) / sizeof(keys[0]), message);
        if (status != ECRITUM_OK) return status;
        *out_allowed = 0;
        return ECRITUM_OK;
    }
    if (strcmp(mode, "allowed") != 0) {
        return fail_config(message, "unknown network mode");
    }
    static const char *keys[] = {"mode", "rules"};
    status = json_has_only_keys(value, keys, sizeof(keys) / sizeof(keys[0]), message);
    if (status != ECRITUM_OK) return status;
    json_value_t *rules = NULL;
    status = json_require(value, "rules", &rules, message);
    if (status != ECRITUM_OK) return status;
    status = expect_array_value(rules, message);
    if (status != ECRITUM_OK) return status;
    if (rules->item_count == 0) {
        return fail_config(message, "network rules are empty");
    }
    for (size_t index = 0; index < rules->item_count; index++) {
        json_value_t *rule = rules->items[index];
        status = expect_object_value(rule, message);
        if (status != ECRITUM_OK) return status;
        static const char *rule_keys[] = {"scheme", "host", "port"};
        status = json_has_only_keys(rule, rule_keys, sizeof(rule_keys) / sizeof(rule_keys[0]), message);
        if (status != ECRITUM_OK) return status;
        json_value_t *scheme_value = NULL;
        json_value_t *host_value = NULL;
        json_value_t *port_value = NULL;
        const char *scheme = NULL;
        const char *host = NULL;
        uint64_t port = 0;
        status = json_require(rule, "scheme", &scheme_value, message);
        if (status != ECRITUM_OK) return status;
        status = json_require(rule, "host", &host_value, message);
        if (status != ECRITUM_OK) return status;
        status = json_require(rule, "port", &port_value, message);
        if (status != ECRITUM_OK) return status;
        status = expect_short_string_value(scheme_value, &scheme, message);
        if (status != ECRITUM_OK) return status;
        status = expect_short_string_value(host_value, &host, message);
        if (status != ECRITUM_OK) return status;
        status = json_parse_uint64_digits(port_value, &port, message);
        if (status != ECRITUM_OK) return status;
        if (!valid_lower_identifier_string(scheme) || !valid_host_string(host) || port == 0 || port > 65535) {
            return fail_config(message, "invalid network rule");
        }
        size_t needed = strlen(scheme) + strlen(host) + 24;
        char *canonical = malloc(needed);
        if (canonical == NULL) return ECRITUM_ERROR_OUT_OF_MEMORY;
        snprintf(canonical, needed, "%s|%s|%llu", scheme, host, (unsigned long long)port);
        status = string_set_add_unique(out_rules, canonical, message);
        free(canonical);
        if (status != ECRITUM_OK) return status;
    }
    *out_allowed = 1;
    return ECRITUM_OK;
}

static int parse_process_policy(json_value_t *value, int *out_allowed, ecritum_string_set_t *out_commands, const char **message) {
    int status = expect_object_value(value, message);
    if (status != ECRITUM_OK) return status;
    const char *mode = NULL;
    status = expect_mode(value, &mode, message);
    if (status != ECRITUM_OK) return status;
    if (strcmp(mode, "denied") == 0) {
        static const char *keys[] = {"mode"};
        status = json_has_only_keys(value, keys, sizeof(keys) / sizeof(keys[0]), message);
        if (status != ECRITUM_OK) return status;
        *out_allowed = 0;
        return ECRITUM_OK;
    }
    if (strcmp(mode, "allowed") != 0) {
        return fail_config(message, "unknown process mode");
    }
    static const char *keys[] = {"mode", "commands"};
    status = json_has_only_keys(value, keys, sizeof(keys) / sizeof(keys[0]), message);
    if (status != ECRITUM_OK) return status;
    json_value_t *commands = NULL;
    status = json_require(value, "commands", &commands, message);
    if (status != ECRITUM_OK) return status;
    status = expect_array_value(commands, message);
    if (status != ECRITUM_OK) return status;
    if (commands->item_count == 0) {
        return fail_config(message, "process commands are empty");
    }
    for (size_t index = 0; index < commands->item_count; index++) {
        json_value_t *command = commands->items[index];
        status = expect_object_value(command, message);
        if (status != ECRITUM_OK) return status;
        static const char *command_keys[] = {"path"};
        status = json_has_only_keys(command, command_keys, sizeof(command_keys) / sizeof(command_keys[0]), message);
        if (status != ECRITUM_OK) return status;
        json_value_t *path_value = NULL;
        const char *path = NULL;
        status = json_require(command, "path", &path_value, message);
        if (status != ECRITUM_OK) return status;
        status = expect_string_value(path_value, &path, message);
        if (status != ECRITUM_OK) return status;
        if (!valid_path_string(path, 0)) {
            return fail_config(message, "invalid process command");
        }
        status = string_set_add_unique(out_commands, path, message);
        if (status != ECRITUM_OK) return status;
    }
    *out_allowed = 1;
    return ECRITUM_OK;
}

static int parse_environment_policy(json_value_t *value, int *out_allowed, ecritum_string_set_t *out_keys, const char **message) {
    int status = expect_object_value(value, message);
    if (status != ECRITUM_OK) return status;
    const char *mode = NULL;
    status = expect_mode(value, &mode, message);
    if (status != ECRITUM_OK) return status;
    if (strcmp(mode, "denied") == 0) {
        static const char *keys[] = {"mode"};
        status = json_has_only_keys(value, keys, sizeof(keys) / sizeof(keys[0]), message);
        if (status != ECRITUM_OK) return status;
        *out_allowed = 0;
        return ECRITUM_OK;
    }
    if (strcmp(mode, "allowed") != 0) {
        return fail_config(message, "unknown environment mode");
    }
    static const char *keys[] = {"mode", "keys"};
    status = json_has_only_keys(value, keys, sizeof(keys) / sizeof(keys[0]), message);
    if (status != ECRITUM_OK) return status;
    json_value_t *array = NULL;
    status = json_require(value, "keys", &array, message);
    if (status != ECRITUM_OK) return status;
    status = expect_array_value(array, message);
    if (status != ECRITUM_OK) return status;
    if (array->item_count == 0) {
        return fail_config(message, "environment keys are empty");
    }
    for (size_t index = 0; index < array->item_count; index++) {
        const char *key = NULL;
        status = expect_short_string_value(array->items[index], &key, message);
        if (status != ECRITUM_OK) return status;
        if (!valid_ascii_identifier_string(key)) {
            return fail_config(message, "invalid environment key");
        }
        status = string_set_add_unique(out_keys, key, message);
        if (status != ECRITUM_OK) return status;
    }
    *out_allowed = 1;
    return ECRITUM_OK;
}

static int parse_diagnostics(json_value_t *value, int *out_raw, const char **message) {
    static const char *keys[] = {"mode"};
    int status = expect_object_value(value, message);
    if (status != ECRITUM_OK) return status;
    status = json_has_only_keys(value, keys, sizeof(keys) / sizeof(keys[0]), message);
    if (status != ECRITUM_OK) return status;
    const char *mode = NULL;
    status = expect_mode(value, &mode, message);
    if (status != ECRITUM_OK) return status;
    if (strcmp(mode, "redacted") == 0) {
        *out_raw = 0;
        return ECRITUM_OK;
    }
    if (strcmp(mode, "raw") == 0) {
        *out_raw = 1;
        return ECRITUM_OK;
    }
    return fail_config(message, "unknown diagnostics mode");
}

static int resource_limit_index(const char *key) {
    if (strcmp(key, "executionTimeoutNanos") == 0) return ECRITUM_LIMIT_EXECUTION_TIMEOUT_NANOS;
    if (strcmp(key, "maxInputBytes") == 0) return ECRITUM_LIMIT_MAX_INPUT_BYTES;
    if (strcmp(key, "maxOutputBytes") == 0) return ECRITUM_LIMIT_MAX_OUTPUT_BYTES;
    if (strcmp(key, "maxStackDepth") == 0) return ECRITUM_LIMIT_MAX_STACK_DEPTH;
    if (strcmp(key, "maxHeapBytes") == 0) return ECRITUM_LIMIT_MAX_HEAP_BYTES;
    if (strcmp(key, "maxCallbackQueueLength") == 0) return ECRITUM_LIMIT_MAX_CALLBACK_QUEUE_LENGTH;
    if (strcmp(key, "callbackTimeoutNanos") == 0) return ECRITUM_LIMIT_CALLBACK_TIMEOUT_NANOS;
    return -1;
}

static int parse_resource_limits(json_value_t *value, uint64_t limits[ECRITUM_LIMIT_COUNT], int present[ECRITUM_LIMIT_COUNT], const char **message) {
    int status = expect_object_value(value, message);
    if (status != ECRITUM_OK) return status;
    for (json_member_t *member = value->members; member != NULL; member = member->next) {
        int index = resource_limit_index(member->key);
        if (index < 0) {
            return fail_config(message, "unknown resource limit");
        }
        uint64_t limit = 0;
        status = json_parse_uint64_digits(member->value, &limit, message);
        if (status != ECRITUM_OK) return status;
        if ((index == ECRITUM_LIMIT_EXECUTION_TIMEOUT_NANOS || index == ECRITUM_LIMIT_CALLBACK_TIMEOUT_NANOS)
            && limit == UINT64_MAX) {
            return fail_config(message, "reserved resource limit value");
        }
        limits[index] = limit;
        present[index] = 1;
    }
    return ECRITUM_OK;
}

static int parse_languages(json_value_t *value, ecritum_string_set_t *languages, const char **message) {
    int status = expect_array_value(value, message);
    if (status != ECRITUM_OK) return status;
    for (size_t index = 0; index < value->item_count; index++) {
        const char *language = NULL;
        status = expect_short_string_value(value->items[index], &language, message);
        if (status != ECRITUM_OK) return status;
        if (!valid_ascii_identifier_string(language)) {
            return fail_config(message, "invalid language name");
        }
        status = string_set_add_unique(languages, language, message);
        if (status != ECRITUM_OK) return status;
    }
    return ECRITUM_OK;
}

static int parse_runtime_policy(json_value_t *policy, ecritum_policy_config_t *config, const char **message) {
    static const char *policy_keys[] = {"filesystem", "network", "process", "environment", "clock", "random", "log"};
    int status = expect_object_value(policy, message);
    if (status != ECRITUM_OK) return status;
    status = json_has_only_keys(policy, policy_keys, sizeof(policy_keys) / sizeof(policy_keys[0]), message);
    if (status != ECRITUM_OK) return status;
    for (size_t index = 0; index < sizeof(policy_keys) / sizeof(policy_keys[0]); index++) {
        json_value_t *required = NULL;
        status = json_require(policy, policy_keys[index], &required, message);
        if (status != ECRITUM_OK) return status;
    }
    status = parse_filesystem_policy(json_get(policy, "filesystem"), &config->filesystem_mode, &config->filesystem_roots, message);
    if (status != ECRITUM_OK) return status;
    status = parse_network_policy(json_get(policy, "network"), &config->network_allowed, &config->network_rules, message);
    if (status != ECRITUM_OK) return status;
    status = parse_process_policy(json_get(policy, "process"), &config->process_allowed, &config->process_commands, message);
    if (status != ECRITUM_OK) return status;
    status = parse_environment_policy(json_get(policy, "environment"), &config->environment_allowed, &config->environment_keys, message);
    if (status != ECRITUM_OK) return status;
    status = parse_mode_only(json_get(policy, "clock"), &config->clock_allowed, message);
    if (status != ECRITUM_OK) return status;
    status = parse_mode_only(json_get(policy, "random"), &config->random_allowed, message);
    if (status != ECRITUM_OK) return status;
    return parse_mode_only(json_get(policy, "log"), &config->log_allowed, message);
}

static int apply_filesystem_narrowing(ecritum_policy_config_t *effective, int child_mode, ecritum_string_set_t *child_roots, const char **message) {
    if (child_mode == ECRITUM_FS_DENIED) {
        effective->filesystem_mode = ECRITUM_FS_DENIED;
        string_set_destroy(&effective->filesystem_roots);
        return ECRITUM_OK;
    }
    if (effective->filesystem_mode == ECRITUM_FS_DENIED || child_mode > effective->filesystem_mode
        || !string_set_is_subset(child_roots, &effective->filesystem_roots)) {
        return fail_config(message, "context filesystem policy widens runtime policy");
    }
    effective->filesystem_mode = child_mode;
    string_set_replace(&effective->filesystem_roots, child_roots);
    return ECRITUM_OK;
}

static int apply_allowed_set_narrowing(int *effective_allowed, ecritum_string_set_t *effective_set, int child_allowed, ecritum_string_set_t *child_set, const char **message) {
    if (!child_allowed) {
        *effective_allowed = 0;
        string_set_destroy(effective_set);
        return ECRITUM_OK;
    }
    if (!*effective_allowed || !string_set_is_subset(child_set, effective_set)) {
        return fail_config(message, "context policy widens runtime policy");
    }
    string_set_replace(effective_set, child_set);
    return ECRITUM_OK;
}

static int apply_toggle_narrowing(int *effective_allowed, int child_allowed, const char **message) {
    if (child_allowed && !*effective_allowed) {
        return fail_config(message, "context policy widens runtime policy");
    }
    *effective_allowed = child_allowed;
    return ECRITUM_OK;
}

static int apply_resource_limit_narrowing(ecritum_policy_config_t *effective, uint64_t limits[ECRITUM_LIMIT_COUNT], int present[ECRITUM_LIMIT_COUNT], const char **message) {
    for (size_t index = 0; index < ECRITUM_LIMIT_COUNT; index++) {
        if (!present[index]) {
            continue;
        }
        if (effective->limit_present[index] && limits[index] > effective->limits[index]) {
            return fail_config(message, "context resource limit widens runtime limit");
        }
        effective->limits[index] = limits[index];
        effective->limit_present[index] = 1;
    }
    return ECRITUM_OK;
}

static int parse_context_policy(json_value_t *policy, ecritum_policy_config_t *effective, const char **message) {
    static const char *policy_keys[] = {"filesystem", "network", "process", "environment", "clock", "random", "log"};
    int status = expect_object_value(policy, message);
    if (status != ECRITUM_OK) return status;
    status = json_has_only_keys(policy, policy_keys, sizeof(policy_keys) / sizeof(policy_keys[0]), message);
    if (status != ECRITUM_OK) return status;
    json_value_t *value = json_get(policy, "filesystem");
    if (value != NULL) {
        int mode = ECRITUM_FS_DENIED;
        ecritum_string_set_t roots = {0};
        status = parse_filesystem_policy(value, &mode, &roots, message);
        if (status == ECRITUM_OK) status = apply_filesystem_narrowing(effective, mode, &roots, message);
        string_set_destroy(&roots);
        if (status != ECRITUM_OK) return status;
    }
    value = json_get(policy, "network");
    if (value != NULL) {
        int allowed = 0;
        ecritum_string_set_t rules = {0};
        status = parse_network_policy(value, &allowed, &rules, message);
        if (status == ECRITUM_OK) status = apply_allowed_set_narrowing(&effective->network_allowed, &effective->network_rules, allowed, &rules, message);
        string_set_destroy(&rules);
        if (status != ECRITUM_OK) return status;
    }
    value = json_get(policy, "process");
    if (value != NULL) {
        int allowed = 0;
        ecritum_string_set_t commands = {0};
        status = parse_process_policy(value, &allowed, &commands, message);
        if (status == ECRITUM_OK) status = apply_allowed_set_narrowing(&effective->process_allowed, &effective->process_commands, allowed, &commands, message);
        string_set_destroy(&commands);
        if (status != ECRITUM_OK) return status;
    }
    value = json_get(policy, "environment");
    if (value != NULL) {
        int allowed = 0;
        ecritum_string_set_t keys = {0};
        status = parse_environment_policy(value, &allowed, &keys, message);
        if (status == ECRITUM_OK) status = apply_allowed_set_narrowing(&effective->environment_allowed, &effective->environment_keys, allowed, &keys, message);
        string_set_destroy(&keys);
        if (status != ECRITUM_OK) return status;
    }
    value = json_get(policy, "clock");
    if (value != NULL) {
        int allowed = 0;
        status = parse_mode_only(value, &allowed, message);
        if (status != ECRITUM_OK) return status;
        status = apply_toggle_narrowing(&effective->clock_allowed, allowed, message);
        if (status != ECRITUM_OK) return status;
    }
    value = json_get(policy, "random");
    if (value != NULL) {
        int allowed = 0;
        status = parse_mode_only(value, &allowed, message);
        if (status != ECRITUM_OK) return status;
        status = apply_toggle_narrowing(&effective->random_allowed, allowed, message);
        if (status != ECRITUM_OK) return status;
    }
    value = json_get(policy, "log");
    if (value != NULL) {
        int allowed = 0;
        status = parse_mode_only(value, &allowed, message);
        if (status != ECRITUM_OK) return status;
        return apply_toggle_narrowing(&effective->log_allowed, allowed, message);
    }
    return ECRITUM_OK;
}

static int validate_config_input(ecritum_bytes_t config_json, const char **message) {
    if (config_json.data == NULL && config_json.len > 0) {
        if (message != NULL) *message = "missing configuration data";
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    if (config_json.len == 0) {
        return ECRITUM_OK;
    }
    if (config_json.len > ECRITUM_CONFIG_MAX_BYTES) {
        if (message != NULL) *message = "configuration is too large";
        return ECRITUM_ERROR_INPUT_TOO_LARGE;
    }
    if (!valid_utf8(config_json.data, config_json.len)) {
        if (message != NULL) *message = "configuration is not valid UTF-8";
        return ECRITUM_ERROR_INVALID_UTF8;
    }
    return ECRITUM_OK;
}

static int parse_runtime_configuration(ecritum_bytes_t config_json, ecritum_policy_config_t **out_config, const char **message) {
    *out_config = NULL;
    int status = validate_config_input(config_json, message);
    if (status != ECRITUM_OK || config_json.len == 0) {
        if (status == ECRITUM_OK) {
            *out_config = config_create_default();
            if (*out_config == NULL) {
                return ECRITUM_ERROR_OUT_OF_MEMORY;
            }
        }
        return status;
    }

    json_value_t *root = NULL;
    status = json_parse_document(config_json, &root, message);
    if (status != ECRITUM_OK) {
        return status;
    }
    status = expect_object_value(root, message);
    if (status != ECRITUM_OK) {
        json_value_destroy(root);
        return status;
    }
    status = validate_schema_version(root, message);
    if (status != ECRITUM_OK) {
        json_value_destroy(root);
        return status;
    }
    static const char *top_keys[] = {"schemaVersion", "languages", "policy", "diagnostics", "resourceLimits"};
    status = json_has_only_keys(root, top_keys, sizeof(top_keys) / sizeof(top_keys[0]), message);
    if (status != ECRITUM_OK) {
        json_value_destroy(root);
        return status;
    }

    ecritum_policy_config_t *config = config_create_default();
    if (config == NULL) {
        json_value_destroy(root);
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    json_value_t *value = NULL;
    status = json_require(root, "languages", &value, message);
    if (status == ECRITUM_OK) status = parse_languages(value, &config->languages, message);
    if (status == ECRITUM_OK) status = json_require(root, "policy", &value, message);
    if (status == ECRITUM_OK) status = parse_runtime_policy(value, config, message);
    if (status == ECRITUM_OK) status = json_require(root, "diagnostics", &value, message);
    if (status == ECRITUM_OK) status = parse_diagnostics(value, &config->diagnostics_raw, message);
    if (status == ECRITUM_OK) status = json_require(root, "resourceLimits", &value, message);
    if (status == ECRITUM_OK) status = parse_resource_limits(value, config->limits, config->limit_present, message);
    json_value_destroy(root);
    if (status != ECRITUM_OK) {
        config_destroy(config);
        return status;
    }
    *out_config = config;
    return ECRITUM_OK;
}

static int parse_context_configuration(ecritum_bytes_t config_json, const ecritum_policy_config_t *parent, ecritum_policy_config_t **out_config, const char **message) {
    *out_config = NULL;
    int status = validate_config_input(config_json, message);
    if (status != ECRITUM_OK || config_json.len == 0) {
        if (status == ECRITUM_OK) {
            *out_config = config_clone(parent);
            if (*out_config == NULL) {
                return ECRITUM_ERROR_OUT_OF_MEMORY;
            }
        }
        return status;
    }

    json_value_t *root = NULL;
    status = json_parse_document(config_json, &root, message);
    if (status != ECRITUM_OK) {
        return status;
    }
    status = expect_object_value(root, message);
    if (status != ECRITUM_OK) {
        json_value_destroy(root);
        return status;
    }
    status = validate_schema_version(root, message);
    if (status != ECRITUM_OK) {
        json_value_destroy(root);
        return status;
    }
    static const char *top_keys[] = {"schemaVersion", "policy", "resourceLimits"};
    status = json_has_only_keys(root, top_keys, sizeof(top_keys) / sizeof(top_keys[0]), message);
    if (status != ECRITUM_OK) {
        json_value_destroy(root);
        return status;
    }

    ecritum_policy_config_t *effective = config_clone(parent);
    if (effective == NULL) {
        json_value_destroy(root);
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    json_value_t *value = json_get(root, "policy");
    if (value != NULL) {
        status = parse_context_policy(value, effective, message);
    }
    value = json_get(root, "resourceLimits");
    if (status == ECRITUM_OK && value != NULL) {
        uint64_t limits[ECRITUM_LIMIT_COUNT] = {0};
        int present[ECRITUM_LIMIT_COUNT] = {0};
        status = parse_resource_limits(value, limits, present, message);
        if (status == ECRITUM_OK) {
            status = apply_resource_limit_narrowing(effective, limits, present, message);
        }
    }
    json_value_destroy(root);
    if (status != ECRITUM_OK) {
        config_destroy(effective);
        return status;
    }
    *out_config = effective;
    return ECRITUM_OK;
}

static int validate_bytes_input(ecritum_bytes_t value) {
    return value.data != NULL || value.len == 0;
}

static int validate_string_input(ecritum_string_view_t value) {
    if (value.data == NULL && value.len > 0) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    if (value.len > ECRITUM_CONFIG_MAX_STRING_BYTES) {
        return ECRITUM_ERROR_INPUT_TOO_LARGE;
    }
    if (value.len > 0 && !valid_utf8((const uint8_t *)value.data, value.len)) {
        return ECRITUM_ERROR_INVALID_UTF8;
    }
    return ECRITUM_OK;
}

static int copy_value_bytes(ecritum_owned_bytes_t *destination, const uint8_t *data, size_t len, int nul_terminate) {
    destination->data = NULL;
    destination->len = 0;
    size_t extra = nul_terminate ? 1u : 0u;
    if (len == 0 && extra == 0) {
        return ECRITUM_OK;
    }
    uint8_t *copy = malloc(len + extra);
    if (copy == NULL) {
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    if (len > 0) {
        memcpy(copy, data, len);
    }
    if (nul_terminate) {
        copy[len] = 0;
    }
    destination->data = copy;
    destination->len = len;
    return ECRITUM_OK;
}

static int value_record_copy(ecritum_value_record_t *destination, const ecritum_value_record_t *source) {
    memset(destination, 0, sizeof(*destination));
    destination->kind = source->kind;
    switch (source->kind) {
    case ECRITUM_VALUE_KIND_NULL:
        return ECRITUM_OK;
    case ECRITUM_VALUE_KIND_BOOL:
        destination->value.bool_value = source->value.bool_value;
        return ECRITUM_OK;
    case ECRITUM_VALUE_KIND_INT:
        destination->value.int_value = source->value.int_value;
        return ECRITUM_OK;
    case ECRITUM_VALUE_KIND_DOUBLE:
        destination->value.double_value = source->value.double_value;
        return ECRITUM_OK;
    case ECRITUM_VALUE_KIND_STRING:
        return copy_value_bytes(&destination->value.bytes, source->value.bytes.data, source->value.bytes.len, 1);
    case ECRITUM_VALUE_KIND_DATA:
        return copy_value_bytes(&destination->value.bytes, source->value.bytes.data, source->value.bytes.len, 0);
    case ECRITUM_VALUE_KIND_ARRAY: {
        size_t count = source->value.array.count;
        destination->value.array.count = count;
        if (count == 0) {
            destination->value.array.items = NULL;
            return ECRITUM_OK;
        }
        destination->value.array.items = calloc(count, sizeof(ecritum_value_record_t));
        if (destination->value.array.items == NULL) {
            return ECRITUM_ERROR_OUT_OF_MEMORY;
        }
        for (size_t index = 0; index < count; index++) {
            int status = value_record_copy(&destination->value.array.items[index], &source->value.array.items[index]);
            if (status != ECRITUM_OK) {
                value_record_destroy(destination);
                return status;
            }
        }
        return ECRITUM_OK;
    }
    case ECRITUM_VALUE_KIND_OBJECT: {
        size_t count = source->value.object.count;
        destination->value.object.count = count;
        if (count == 0) {
            return ECRITUM_OK;
        }
        destination->value.object.keys = calloc(count, sizeof(char *));
        destination->value.object.key_lengths = calloc(count, sizeof(size_t));
        destination->value.object.values = calloc(count, sizeof(ecritum_value_record_t));
        if (destination->value.object.keys == NULL
            || destination->value.object.key_lengths == NULL
            || destination->value.object.values == NULL) {
            value_record_destroy(destination);
            return ECRITUM_ERROR_OUT_OF_MEMORY;
        }
        for (size_t index = 0; index < count; index++) {
            size_t key_len = source->value.object.key_lengths[index];
            destination->value.object.keys[index] = copy_string_len(source->value.object.keys[index], key_len);
            if (destination->value.object.keys[index] == NULL) {
                value_record_destroy(destination);
                return ECRITUM_ERROR_OUT_OF_MEMORY;
            }
            destination->value.object.key_lengths[index] = key_len;
            int status = value_record_copy(&destination->value.object.values[index], &source->value.object.values[index]);
            if (status != ECRITUM_OK) {
                value_record_destroy(destination);
                return status;
            }
        }
        return ECRITUM_OK;
    }
    default:
        memset(destination, 0, sizeof(*destination));
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
}

static int value_record_copy_from_handle_locked(ecritum_value_record_t *destination, ecritum_value_t handle) {
    ecritum_handle_slot_t *entry = validate_locked(handle, ECRITUM_HANDLE_KIND_VALUE);
    if (entry == NULL) {
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    return value_record_copy(destination, &entry->value.value_record);
}

static int create_value_handle_from_record_locked(const ecritum_value_record_t *record, ecritum_value_t *out_value) {
    ecritum_value_t handle = allocate_slot_locked(ECRITUM_HANDLE_KIND_VALUE);
    if (handle == 0) {
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    ecritum_handle_slot_t *entry = validate_locked(handle, ECRITUM_HANDLE_KIND_VALUE);
    int status = value_record_copy(&entry->value.value_record, record);
    if (status != ECRITUM_OK) {
        tombstone_slot_locked(entry);
        return status;
    }
    *out_value = handle;
    return ECRITUM_OK;
}

static int create_value_handle_locked(ecritum_value_record_t *record, ecritum_value_t *out_value) {
    ecritum_value_t handle = allocate_slot_locked(ECRITUM_HANDLE_KIND_VALUE);
    if (handle == 0) {
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    ecritum_handle_slot_t *entry = validate_locked(handle, ECRITUM_HANDLE_KIND_VALUE);
    entry->value.value_record = *record;
    memset(record, 0, sizeof(*record));
    *out_value = handle;
    return ECRITUM_OK;
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

static int attach_runtime_thread(graal_isolate_t *isolate, graal_isolatethread_t **out_thread, int *out_attached) {
    if (out_thread == NULL || out_attached == NULL || isolate == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    *out_thread = graal_get_current_thread(isolate);
    *out_attached = 0;
    if (*out_thread != NULL) {
        return ECRITUM_OK;
    }
    if (graal_attach_thread(isolate, out_thread) != 0 || *out_thread == NULL) {
        return ECRITUM_ERROR_RUNTIME_UNAVAILABLE;
    }
    *out_attached = 1;
    return ECRITUM_OK;
}

static void detach_runtime_thread_if_needed(graal_isolatethread_t *thread, int attached) {
    if (attached && thread != NULL) {
        (void)graal_detach_thread(thread);
    }
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

    const char *config_message = "invalid runtime configuration";
    ecritum_policy_config_t *parsed_config = NULL;
    int config_status = parse_runtime_configuration(config_json, &parsed_config, &config_message);
    if (config_status != ECRITUM_OK) {
        return fail_with_error(config_status, "runtime_create", config_message, out_error);
    }

    graal_isolate_t *isolate = NULL;
    graal_isolatethread_t *thread = NULL;
    if (graal_create_isolate(NULL, &isolate, &thread) != 0 || isolate == NULL || thread == NULL) {
        config_destroy(parsed_config);
        return fail_with_error(ECRITUM_ERROR_RUNTIME_UNAVAILABLE, "runtime_create", "runtime is unavailable", out_error);
    }

    if (detach_creation_thread(thread) != 0) {
        (void)graal_tear_down_isolate(thread);
        config_destroy(parsed_config);
        return fail_with_error(ECRITUM_ERROR_RUNTIME_UNAVAILABLE, "runtime_create", "runtime thread detach failed", out_error);
    }

    pthread_mutex_lock(&registry_mutex);
    ecritum_runtime_t handle = allocate_slot_locked(ECRITUM_HANDLE_KIND_RUNTIME);
    if (handle != 0) {
        ecritum_handle_slot_t *entry = validate_locked(handle, ECRITUM_HANDLE_KIND_RUNTIME);
        entry->value.runtime.isolate = isolate;
        entry->value.runtime.live_contexts = 0;
        entry->value.runtime.config = parsed_config;
        parsed_config = NULL;
    }
    pthread_mutex_unlock(&registry_mutex);

    if (handle == 0) {
        config_destroy(parsed_config);
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
    if (callback_stack_contains_runtime(handle_slot(*runtime), handle_generation(*runtime))) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_REENTRANT_CALL, "runtime_destroy", "runtime is active in a callback");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_REENTRANT_CALL;
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
            if (namespace_has_active_calls_locked(namespace_entry)) {
                if (out_error != NULL) {
                    *out_error = create_error_locked(ECRITUM_ERROR_BUSY, "runtime_destroy", "runtime has active callbacks");
                }
                pthread_mutex_unlock(&registry_mutex);
                return ECRITUM_ERROR_BUSY;
            }
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

    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *runtime_entry = validate_locked(runtime, ECRITUM_HANDLE_KIND_RUNTIME);
    if (runtime_entry == NULL) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "context_create", "invalid runtime handle");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    ecritum_policy_config_t *parent_config = config_clone(runtime_entry->value.runtime.config);
    pthread_mutex_unlock(&registry_mutex);
    if (parent_config == NULL) {
        return fail_with_error(ECRITUM_ERROR_OUT_OF_MEMORY, "context_create", "configuration copy failed", out_error);
    }

    const char *config_message = "invalid context configuration";
    ecritum_policy_config_t *effective_config = NULL;
    int config_status = parse_context_configuration(config_json, parent_config, &effective_config, &config_message);
    config_destroy(parent_config);
    if (config_status != ECRITUM_OK) {
        return fail_with_error(config_status, "context_create", config_message, out_error);
    }

    pthread_mutex_lock(&registry_mutex);
    runtime_entry = validate_locked(runtime, ECRITUM_HANDLE_KIND_RUNTIME);
    if (runtime_entry == NULL) {
        config_destroy(effective_config);
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "context_create", "invalid runtime handle");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    ecritum_context_t handle = allocate_slot_locked(ECRITUM_HANDLE_KIND_CONTEXT);
    if (handle == 0) {
        config_destroy(effective_config);
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_OUT_OF_MEMORY, "context_create", "handle registry is full");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }

    ecritum_handle_slot_t *context_entry = validate_locked(handle, ECRITUM_HANDLE_KIND_CONTEXT);
    context_entry->value.context.parent_slot = handle_slot(runtime);
    context_entry->value.context.parent_generation = handle_generation(runtime);
    context_entry->value.context.config = effective_config;
    effective_config = NULL;
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
    if (context_matches_active_callback_runtime_locked(context_entry)) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_REENTRANT_CALL, "context_destroy", "context runtime is active in a callback");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_REENTRANT_CALL;
    }
    if (context_entry->value.context.active_job_slot != 0) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_BUSY, "context_destroy", "context has a live job");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_BUSY;
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
    if (namespace_has_active_calls_locked(namespace_entry)) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_BUSY, "namespace_destroy", "namespace has active callbacks");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_BUSY;
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
    if (function_has_active_calls_locked(function_entry)) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_BUSY, "function_destroy", "function has active callbacks");
        }
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_BUSY;
    }

    collect_function_cleanup_locked(function_entry, cleanup_actions, &cleanup_count);
    *function = 0;
    pthread_mutex_unlock(&registry_mutex);

    run_cleanup_actions(cleanup_actions, cleanup_count);
    return ECRITUM_OK;
}

static int finish_value_make(ecritum_value_record_t *record, ecritum_value_t *out_value, ecritum_error_t *out_error, const char *operation) {
    if (out_value == NULL) {
        value_record_destroy(record);
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, operation, "missing value output", out_error);
    }
    *out_value = 0;
    clear_error(out_error);
    pthread_mutex_lock(&registry_mutex);
    int status = create_value_handle_locked(record, out_value);
    if (status != ECRITUM_OK && out_error != NULL) {
        *out_error = create_error_locked(status, operation, "value allocation failed");
    }
    pthread_mutex_unlock(&registry_mutex);
    if (status != ECRITUM_OK) {
        value_record_destroy(record);
    }
    return status;
}

__attribute__((visibility("default"))) int ecritum_value_make_null(ecritum_value_t *out_value, ecritum_error_t *out_error) {
    ecritum_value_record_t record = {0};
    record.kind = ECRITUM_VALUE_KIND_NULL;
    return finish_value_make(&record, out_value, out_error, "value_make_null");
}

__attribute__((visibility("default"))) int ecritum_value_make_bool(int value, ecritum_value_t *out_value, ecritum_error_t *out_error) {
    ecritum_value_record_t record = {0};
    record.kind = ECRITUM_VALUE_KIND_BOOL;
    record.value.bool_value = value ? 1 : 0;
    return finish_value_make(&record, out_value, out_error, "value_make_bool");
}

__attribute__((visibility("default"))) int ecritum_value_make_int(int64_t value, ecritum_value_t *out_value, ecritum_error_t *out_error) {
    ecritum_value_record_t record = {0};
    record.kind = ECRITUM_VALUE_KIND_INT;
    record.value.int_value = value;
    return finish_value_make(&record, out_value, out_error, "value_make_int");
}

__attribute__((visibility("default"))) int ecritum_value_make_double(double value, ecritum_value_t *out_value, ecritum_error_t *out_error) {
    ecritum_value_record_t record = {0};
    record.kind = ECRITUM_VALUE_KIND_DOUBLE;
    record.value.double_value = value;
    return finish_value_make(&record, out_value, out_error, "value_make_double");
}

__attribute__((visibility("default"))) int ecritum_value_make_string(ecritum_string_view_t value, ecritum_value_t *out_value, ecritum_error_t *out_error) {
    if (out_value != NULL) {
        *out_value = 0;
    }
    clear_error(out_error);
    int validation = validate_string_input(value);
    if (validation != ECRITUM_OK) {
        return fail_with_error(validation, "value_make_string", "invalid string value", out_error);
    }
    ecritum_value_record_t record = {0};
    record.kind = ECRITUM_VALUE_KIND_STRING;
    int status = copy_value_bytes(&record.value.bytes, (const uint8_t *)value.data, value.len, 1);
    if (status != ECRITUM_OK) {
        return fail_with_error(status, "value_make_string", "value allocation failed", out_error);
    }
    return finish_value_make(&record, out_value, out_error, "value_make_string");
}

__attribute__((visibility("default"))) int ecritum_value_make_data(ecritum_bytes_t value, ecritum_value_t *out_value, ecritum_error_t *out_error) {
    if (out_value != NULL) {
        *out_value = 0;
    }
    clear_error(out_error);
    if (!validate_bytes_input(value)) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "value_make_data", "missing data bytes", out_error);
    }
    ecritum_value_record_t record = {0};
    record.kind = ECRITUM_VALUE_KIND_DATA;
    int status = copy_value_bytes(&record.value.bytes, value.data, value.len, 0);
    if (status != ECRITUM_OK) {
        return fail_with_error(status, "value_make_data", "value allocation failed", out_error);
    }
    return finish_value_make(&record, out_value, out_error, "value_make_data");
}

__attribute__((visibility("default"))) int ecritum_value_make_array(const ecritum_value_t *items, size_t count, ecritum_value_t *out_value, ecritum_error_t *out_error) {
    if (out_value != NULL) {
        *out_value = 0;
    }
    clear_error(out_error);
    if (out_value == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "value_make_array", "missing value output", out_error);
    }
    if (items == NULL && count > 0) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "value_make_array", "missing array items", out_error);
    }
    ecritum_value_record_t record = {0};
    record.kind = ECRITUM_VALUE_KIND_ARRAY;
    record.value.array.count = count;
    if (count > 0) {
        record.value.array.items = calloc(count, sizeof(ecritum_value_record_t));
        if (record.value.array.items == NULL) {
            return fail_with_error(ECRITUM_ERROR_OUT_OF_MEMORY, "value_make_array", "value allocation failed", out_error);
        }
    }
    pthread_mutex_lock(&registry_mutex);
    int status = ECRITUM_OK;
    for (size_t index = 0; index < count; index++) {
        status = value_record_copy_from_handle_locked(&record.value.array.items[index], items[index]);
        if (status != ECRITUM_OK) {
            break;
        }
    }
    if (status == ECRITUM_OK) {
        status = create_value_handle_locked(&record, out_value);
    }
    if (status != ECRITUM_OK && out_error != NULL) {
        *out_error = create_error_locked(status, "value_make_array", "array value creation failed");
    }
    pthread_mutex_unlock(&registry_mutex);
    if (status != ECRITUM_OK) {
        value_record_destroy(&record);
    }
    return status;
}

static int object_key_duplicate(const ecritum_object_entry_t *entries, size_t index) {
    for (size_t prior = 0; prior < index; prior++) {
        if (entries[prior].key.len == entries[index].key.len
            && (entries[index].key.len == 0
                || (entries[prior].key.data != NULL
                    && entries[index].key.data != NULL
                    && memcmp(entries[prior].key.data, entries[index].key.data, entries[index].key.len) == 0))) {
            return 1;
        }
    }
    return 0;
}

__attribute__((visibility("default"))) int ecritum_value_make_object(const ecritum_object_entry_t *entries, size_t count, ecritum_value_t *out_value, ecritum_error_t *out_error) {
    if (out_value != NULL) {
        *out_value = 0;
    }
    clear_error(out_error);
    if (out_value == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "value_make_object", "missing value output", out_error);
    }
    if (entries == NULL && count > 0) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "value_make_object", "missing object entries", out_error);
    }
    ecritum_value_record_t record = {0};
    record.kind = ECRITUM_VALUE_KIND_OBJECT;
    record.value.object.count = count;
    if (count > 0) {
        record.value.object.keys = calloc(count, sizeof(char *));
        record.value.object.key_lengths = calloc(count, sizeof(size_t));
        record.value.object.values = calloc(count, sizeof(ecritum_value_record_t));
        if (record.value.object.keys == NULL || record.value.object.key_lengths == NULL || record.value.object.values == NULL) {
            value_record_destroy(&record);
            return fail_with_error(ECRITUM_ERROR_OUT_OF_MEMORY, "value_make_object", "value allocation failed", out_error);
        }
    }
    for (size_t index = 0; index < count; index++) {
        int validation = validate_string_input(entries[index].key);
        if (validation != ECRITUM_OK) {
            value_record_destroy(&record);
            return fail_with_error(validation, "value_make_object", "invalid object key", out_error);
        }
        if (object_key_duplicate(entries, index)) {
            value_record_destroy(&record);
            return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "value_make_object", "duplicate object key", out_error);
        }
        record.value.object.keys[index] = copy_string_len(entries[index].key.data, entries[index].key.len);
        if (record.value.object.keys[index] == NULL) {
            value_record_destroy(&record);
            return fail_with_error(ECRITUM_ERROR_OUT_OF_MEMORY, "value_make_object", "value allocation failed", out_error);
        }
        record.value.object.key_lengths[index] = entries[index].key.len;
    }
    pthread_mutex_lock(&registry_mutex);
    int status = ECRITUM_OK;
    for (size_t index = 0; index < count; index++) {
        status = value_record_copy_from_handle_locked(&record.value.object.values[index], entries[index].value);
        if (status != ECRITUM_OK) {
            break;
        }
    }
    if (status == ECRITUM_OK) {
        status = create_value_handle_locked(&record, out_value);
    }
    if (status != ECRITUM_OK && out_error != NULL) {
        *out_error = create_error_locked(status, "value_make_object", "object value creation failed");
    }
    pthread_mutex_unlock(&registry_mutex);
    if (status != ECRITUM_OK) {
        value_record_destroy(&record);
    }
    return status;
}

__attribute__((visibility("default"))) int ecritum_value_destroy(ecritum_value_t *value) {
    if (value == NULL || *value == 0) {
        return ECRITUM_OK;
    }
    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *entry = validate_locked(*value, ECRITUM_HANDLE_KIND_VALUE);
    if (entry == NULL) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    tombstone_slot_locked(entry);
    *value = 0;
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_value_kind(ecritum_value_t value, int *out_kind) {
    if (out_kind == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *entry = validate_locked(value, ECRITUM_HANDLE_KIND_VALUE);
    if (entry == NULL) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    *out_kind = entry->value.value_record.kind;
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

static ecritum_value_record_t *borrow_value_record_locked(ecritum_value_t value) {
    ecritum_handle_slot_t *entry = validate_locked(value, ECRITUM_HANDLE_KIND_VALUE);
    return entry == NULL ? NULL : &entry->value.value_record;
}

__attribute__((visibility("default"))) int ecritum_value_get_bool(ecritum_value_t value, int *out_value) {
    if (out_value == NULL) return ECRITUM_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&registry_mutex);
    ecritum_value_record_t *record = borrow_value_record_locked(value);
    if (record == NULL) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    if (record->kind != ECRITUM_VALUE_KIND_BOOL) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    *out_value = record->value.bool_value;
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_value_get_int(ecritum_value_t value, int64_t *out_value) {
    if (out_value == NULL) return ECRITUM_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&registry_mutex);
    ecritum_value_record_t *record = borrow_value_record_locked(value);
    if (record == NULL) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    if (record->kind != ECRITUM_VALUE_KIND_INT) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    *out_value = record->value.int_value;
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_value_get_double(ecritum_value_t value, double *out_value) {
    if (out_value == NULL) return ECRITUM_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&registry_mutex);
    ecritum_value_record_t *record = borrow_value_record_locked(value);
    if (record == NULL) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    if (record->kind != ECRITUM_VALUE_KIND_DOUBLE) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    *out_value = record->value.double_value;
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_value_get_string(ecritum_value_t value, ecritum_string_view_t *out_value) {
    if (out_value == NULL) return ECRITUM_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&registry_mutex);
    ecritum_value_record_t *record = borrow_value_record_locked(value);
    if (record == NULL) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    if (record->kind != ECRITUM_VALUE_KIND_STRING) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    out_value->data = (const char *)record->value.bytes.data;
    out_value->len = record->value.bytes.len;
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_value_get_data(ecritum_value_t value, ecritum_bytes_t *out_value) {
    if (out_value == NULL) return ECRITUM_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&registry_mutex);
    ecritum_value_record_t *record = borrow_value_record_locked(value);
    if (record == NULL) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    if (record->kind != ECRITUM_VALUE_KIND_DATA) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    out_value->data = record->value.bytes.data;
    out_value->len = record->value.bytes.len;
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_value_count(ecritum_value_t value, size_t *out_count) {
    if (out_count == NULL) return ECRITUM_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&registry_mutex);
    ecritum_value_record_t *record = borrow_value_record_locked(value);
    if (record == NULL) {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    if (record->kind == ECRITUM_VALUE_KIND_ARRAY) {
        *out_count = record->value.array.count;
    } else if (record->kind == ECRITUM_VALUE_KIND_OBJECT) {
        *out_count = record->value.object.count;
    } else {
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_value_array_get(ecritum_value_t value, size_t index, ecritum_value_t *out_item, ecritum_error_t *out_error) {
    if (out_item == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "value_array_get", "missing item output", out_error);
    }
    *out_item = 0;
    clear_error(out_error);
    pthread_mutex_lock(&registry_mutex);
    ecritum_value_record_t *record = borrow_value_record_locked(value);
    if (record == NULL) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "value_array_get", "invalid value handle");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    if (record->kind != ECRITUM_VALUE_KIND_ARRAY || index >= record->value.array.count) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INVALID_ARGUMENT, "value_array_get", "invalid array access");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    int status = create_value_handle_from_record_locked(&record->value.array.items[index], out_item);
    if (status != ECRITUM_OK && out_error != NULL) {
        *out_error = create_error_locked(status, "value_array_get", "value allocation failed");
    }
    pthread_mutex_unlock(&registry_mutex);
    return status;
}

__attribute__((visibility("default"))) int ecritum_value_object_entry(ecritum_value_t value, size_t index, ecritum_string_view_t *out_key, ecritum_value_t *out_value, ecritum_error_t *out_error) {
    if (out_key == NULL || out_value == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "value_object_entry", "missing object output", out_error);
    }
    out_key->data = NULL;
    out_key->len = 0;
    *out_value = 0;
    clear_error(out_error);
    pthread_mutex_lock(&registry_mutex);
    ecritum_value_record_t *record = borrow_value_record_locked(value);
    if (record == NULL) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "value_object_entry", "invalid value handle");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    if (record->kind != ECRITUM_VALUE_KIND_OBJECT || index >= record->value.object.count) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INVALID_ARGUMENT, "value_object_entry", "invalid object access");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    int status = create_value_handle_from_record_locked(&record->value.object.values[index], out_value);
    if (status == ECRITUM_OK) {
        out_key->data = record->value.object.keys[index];
        out_key->len = record->value.object.key_lengths[index];
    } else if (out_error != NULL) {
        *out_error = create_error_locked(status, "value_object_entry", "value allocation failed");
    }
    pthread_mutex_unlock(&registry_mutex);
    return status;
}

__attribute__((visibility("default"))) int ecritum_call_argument_count(ecritum_call_t call, size_t *out_count, ecritum_error_t *out_error) {
    if (out_count == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "call_argument_count", "missing argument count output", out_error);
    }
    clear_error(out_error);
    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *entry = validate_locked(call, ECRITUM_HANDLE_KIND_CALL);
    if (entry == NULL) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "call_argument_count", "invalid call handle");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    *out_count = entry->value.call.argument_count;
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_call_argument(ecritum_call_t call, size_t index, ecritum_value_t *out_argument, ecritum_error_t *out_error) {
    if (out_argument == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "call_argument", "missing argument output", out_error);
    }
    *out_argument = 0;
    clear_error(out_error);
    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *entry = validate_locked(call, ECRITUM_HANDLE_KIND_CALL);
    if (entry == NULL) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "call_argument", "invalid call handle");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    if (index >= entry->value.call.argument_count) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INVALID_ARGUMENT, "call_argument", "argument index is out of range");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    int status = create_value_handle_from_record_locked(&entry->value.call.arguments[index], out_argument);
    if (status != ECRITUM_OK && out_error != NULL) {
        *out_error = create_error_locked(status, "call_argument", "value allocation failed");
    }
    pthread_mutex_unlock(&registry_mutex);
    return status;
}

static int view_is_valid_utf8(ecritum_string_view_t view_value) {
    return view_value.data != NULL || view_value.len == 0
        ? validate_string_input(view_value)
        : ECRITUM_ERROR_INVALID_ARGUMENT;
}

static int bytes_input_is_valid_utf8(ecritum_bytes_t value) {
    if (value.data == NULL && value.len > 0) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    if (value.len > ECRITUM_CONFIG_MAX_BYTES) {
        return ECRITUM_ERROR_INPUT_TOO_LARGE;
    }
    if (value.len > 0 && !valid_utf8(value.data, value.len)) {
        return ECRITUM_ERROR_INVALID_UTF8;
    }
    return ECRITUM_OK;
}

static int source_matches(ecritum_bytes_t source, const char *fixture) {
    size_t len = strlen(fixture);
    return source.len == len && source.data != NULL && memcmp(source.data, fixture, len) == 0;
}

static int copy_bytes_as_c_string(const uint8_t *data, size_t len, uint8_t **out_data) {
    if (out_data == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    *out_data = NULL;
    uint8_t *copy = malloc(len + 1u);
    if (copy == NULL) {
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    if (len > 0) {
        memcpy(copy, data, len);
    }
    copy[len] = '\0';
    *out_data = copy;
    return ECRITUM_OK;
}

static int copy_view_as_c_string(ecritum_string_view_t view_value, char **out_data) {
    return copy_bytes_as_c_string((const uint8_t *)view_value.data, view_value.len, (uint8_t **)out_data);
}

static void eval_work_destroy(ecritum_eval_work_t *work) {
    if (work == NULL) {
        return;
    }
    free(work->source);
    free(work->source_name);
    free(work->host_manifest);
    free(work->projection_pins);
    free(work);
}

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t offset;
} ecritum_backend_reader_t;

typedef struct {
    uint8_t *data;
    size_t len;
    size_t offset;
} ecritum_backend_writer_t;

#define ECRITUM_BACKEND_RESULT_MAGIC 0x45435631u
#define ECRITUM_BACKEND_RESULT_MAX_BYTES 65536u

static int backend_has_bytes(ecritum_backend_reader_t *reader, size_t count) {
    return reader != NULL && count <= reader->len && reader->offset <= reader->len - count;
}

static int backend_read_u8(ecritum_backend_reader_t *reader, uint8_t *out_value) {
    if (!backend_has_bytes(reader, 1) || out_value == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    *out_value = reader->data[reader->offset];
    reader->offset++;
    return ECRITUM_OK;
}

static int backend_read_u32(ecritum_backend_reader_t *reader, uint32_t *out_value) {
    if (!backend_has_bytes(reader, 4) || out_value == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    const uint8_t *data = reader->data + reader->offset;
    *out_value = ((uint32_t)data[0] << 24)
        | ((uint32_t)data[1] << 16)
        | ((uint32_t)data[2] << 8)
        | (uint32_t)data[3];
    reader->offset += 4;
    return ECRITUM_OK;
}

static int backend_read_u64(ecritum_backend_reader_t *reader, uint64_t *out_value) {
    if (!backend_has_bytes(reader, 8) || out_value == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    const uint8_t *data = reader->data + reader->offset;
    *out_value = ((uint64_t)data[0] << 56)
        | ((uint64_t)data[1] << 48)
        | ((uint64_t)data[2] << 40)
        | ((uint64_t)data[3] << 32)
        | ((uint64_t)data[4] << 24)
        | ((uint64_t)data[5] << 16)
        | ((uint64_t)data[6] << 8)
        | (uint64_t)data[7];
    reader->offset += 8;
    return ECRITUM_OK;
}

static int backend_write_bytes_raw(ecritum_backend_writer_t *writer, const uint8_t *data, size_t len) {
    if (writer == NULL || (data == NULL && len > 0) || len > writer->len || writer->offset > writer->len - len) {
        return ECRITUM_ERROR_BUFFER_TOO_SMALL;
    }
    if (len > 0) {
        memcpy(writer->data + writer->offset, data, len);
    }
    writer->offset += len;
    return ECRITUM_OK;
}

static int backend_write_u8(ecritum_backend_writer_t *writer, uint8_t value) {
    return backend_write_bytes_raw(writer, &value, sizeof(value));
}

static int backend_write_u32(ecritum_backend_writer_t *writer, uint32_t value) {
    uint8_t bytes[] = {
        (uint8_t)((value >> 24) & 0xffu),
        (uint8_t)((value >> 16) & 0xffu),
        (uint8_t)((value >> 8) & 0xffu),
        (uint8_t)(value & 0xffu)
    };
    return backend_write_bytes_raw(writer, bytes, sizeof(bytes));
}

static int backend_write_u64(ecritum_backend_writer_t *writer, uint64_t value) {
    uint8_t bytes[] = {
        (uint8_t)((value >> 56) & 0xffu),
        (uint8_t)((value >> 48) & 0xffu),
        (uint8_t)((value >> 40) & 0xffu),
        (uint8_t)((value >> 32) & 0xffu),
        (uint8_t)((value >> 24) & 0xffu),
        (uint8_t)((value >> 16) & 0xffu),
        (uint8_t)((value >> 8) & 0xffu),
        (uint8_t)(value & 0xffu)
    };
    return backend_write_bytes_raw(writer, bytes, sizeof(bytes));
}

static int backend_write_counted_bytes(ecritum_backend_writer_t *writer, const uint8_t *data, size_t len) {
    if (len > ECRITUM_BACKEND_RESULT_MAX_BYTES || len > UINT32_MAX) {
        return ECRITUM_ERROR_INPUT_TOO_LARGE;
    }
    int status = backend_write_u32(writer, (uint32_t)len);
    if (status == ECRITUM_OK) {
        status = backend_write_bytes_raw(writer, data, len);
    }
    return status;
}

static int backend_write_string(ecritum_backend_writer_t *writer, const char *value) {
    const char *safe_value = value == NULL ? "" : value;
    return backend_write_counted_bytes(writer, (const uint8_t *)safe_value, strlen(safe_value));
}

static int backend_read_string(ecritum_backend_reader_t *reader, char **out_value, size_t *out_len) {
    if (out_value == NULL || out_len == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    *out_value = NULL;
    *out_len = 0;
    uint32_t len = 0;
    int status = backend_read_u32(reader, &len);
    if (status != ECRITUM_OK) {
        return status;
    }
    if (len > ECRITUM_BACKEND_RESULT_MAX_BYTES || !backend_has_bytes(reader, len)) {
        return ECRITUM_ERROR_INPUT_TOO_LARGE;
    }
    const uint8_t *data = reader->data + reader->offset;
    if (len > 0 && !valid_utf8(data, len)) {
        return ECRITUM_ERROR_INVALID_UTF8;
    }
    char *copy = malloc((size_t)len + 1u);
    if (copy == NULL) {
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    if (len > 0) {
        memcpy(copy, data, len);
    }
    copy[len] = '\0';
    reader->offset += len;
    *out_value = copy;
    *out_len = len;
    return ECRITUM_OK;
}

static int backend_read_bytes(ecritum_backend_reader_t *reader, uint8_t **out_value, size_t *out_len) {
    if (out_value == NULL || out_len == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    *out_value = NULL;
    *out_len = 0;
    uint32_t len = 0;
    int status = backend_read_u32(reader, &len);
    if (status != ECRITUM_OK) {
        return status;
    }
    if (len > ECRITUM_BACKEND_RESULT_MAX_BYTES || !backend_has_bytes(reader, len)) {
        return ECRITUM_ERROR_INPUT_TOO_LARGE;
    }
    uint8_t *copy = NULL;
    if (len > 0) {
        copy = malloc((size_t)len);
        if (copy == NULL) {
            return ECRITUM_ERROR_OUT_OF_MEMORY;
        }
        memcpy(copy, reader->data + reader->offset, len);
    }
    reader->offset += len;
    *out_value = copy;
    *out_len = len;
    return ECRITUM_OK;
}

static int backend_parse_value(ecritum_backend_reader_t *reader, ecritum_value_record_t *record);
static int backend_write_value(ecritum_backend_writer_t *writer, const ecritum_value_record_t *record);

static int backend_parse_array(ecritum_backend_reader_t *reader, ecritum_value_record_t *record) {
    uint32_t count = 0;
    int status = backend_read_u32(reader, &count);
    if (status != ECRITUM_OK) {
        return status;
    }
    if (count > ECRITUM_CONFIG_MAX_ARRAY_ITEMS) {
        return ECRITUM_ERROR_INPUT_TOO_LARGE;
    }
    record->kind = ECRITUM_VALUE_KIND_ARRAY;
    record->value.array.count = count;
    if (count == 0) {
        return ECRITUM_OK;
    }
    record->value.array.items = calloc(count, sizeof(ecritum_value_record_t));
    if (record->value.array.items == NULL) {
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    for (size_t index = 0; index < count; index++) {
        status = backend_parse_value(reader, &record->value.array.items[index]);
        if (status != ECRITUM_OK) {
            value_record_destroy(record);
            return status;
        }
    }
    return ECRITUM_OK;
}

static int backend_parse_object(ecritum_backend_reader_t *reader, ecritum_value_record_t *record) {
    uint32_t count = 0;
    int status = backend_read_u32(reader, &count);
    if (status != ECRITUM_OK) {
        return status;
    }
    if (count > ECRITUM_CONFIG_MAX_ARRAY_ITEMS) {
        return ECRITUM_ERROR_INPUT_TOO_LARGE;
    }
    record->kind = ECRITUM_VALUE_KIND_OBJECT;
    record->value.object.count = count;
    if (count == 0) {
        return ECRITUM_OK;
    }
    record->value.object.keys = calloc(count, sizeof(char *));
    record->value.object.key_lengths = calloc(count, sizeof(size_t));
    record->value.object.values = calloc(count, sizeof(ecritum_value_record_t));
    if (record->value.object.keys == NULL || record->value.object.key_lengths == NULL || record->value.object.values == NULL) {
        value_record_destroy(record);
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    for (size_t index = 0; index < count; index++) {
        char *key = NULL;
        size_t key_len = 0;
        status = backend_read_string(reader, &key, &key_len);
        if (status != ECRITUM_OK) {
            value_record_destroy(record);
            return status;
        }
        for (size_t prior = 0; prior < index; prior++) {
            if (record->value.object.key_lengths[prior] == key_len
                && memcmp(record->value.object.keys[prior], key, key_len) == 0) {
                free(key);
                value_record_destroy(record);
                return ECRITUM_ERROR_INVALID_ARGUMENT;
            }
        }
        record->value.object.keys[index] = key;
        record->value.object.key_lengths[index] = key_len;
        status = backend_parse_value(reader, &record->value.object.values[index]);
        if (status != ECRITUM_OK) {
            value_record_destroy(record);
            return status;
        }
    }
    return ECRITUM_OK;
}

static int backend_parse_value(ecritum_backend_reader_t *reader, ecritum_value_record_t *record) {
    if (record == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    memset(record, 0, sizeof(*record));
    uint8_t kind = 0;
    int status = backend_read_u8(reader, &kind);
    if (status != ECRITUM_OK) {
        return status;
    }
    switch (kind) {
    case ECRITUM_VALUE_KIND_NULL:
        record->kind = ECRITUM_VALUE_KIND_NULL;
        return ECRITUM_OK;
    case ECRITUM_VALUE_KIND_BOOL: {
        uint8_t bool_value = 0;
        status = backend_read_u8(reader, &bool_value);
        if (status != ECRITUM_OK) {
            return status;
        }
        record->kind = ECRITUM_VALUE_KIND_BOOL;
        record->value.bool_value = bool_value != 0;
        return ECRITUM_OK;
    }
    case ECRITUM_VALUE_KIND_INT: {
        uint64_t bits = 0;
        status = backend_read_u64(reader, &bits);
        if (status != ECRITUM_OK) {
            return status;
        }
        record->kind = ECRITUM_VALUE_KIND_INT;
        record->value.int_value = (int64_t)bits;
        return ECRITUM_OK;
    }
    case ECRITUM_VALUE_KIND_DOUBLE: {
        uint64_t bits = 0;
        status = backend_read_u64(reader, &bits);
        if (status != ECRITUM_OK) {
            return status;
        }
        record->kind = ECRITUM_VALUE_KIND_DOUBLE;
        memcpy(&record->value.double_value, &bits, sizeof(record->value.double_value));
        return ECRITUM_OK;
    }
    case ECRITUM_VALUE_KIND_STRING: {
        char *string_value = NULL;
        size_t string_len = 0;
        status = backend_read_string(reader, &string_value, &string_len);
        if (status != ECRITUM_OK) {
            return status;
        }
        record->kind = ECRITUM_VALUE_KIND_STRING;
        record->value.bytes.data = (uint8_t *)string_value;
        record->value.bytes.len = string_len;
        return ECRITUM_OK;
    }
    case ECRITUM_VALUE_KIND_DATA: {
        uint8_t *bytes_value = NULL;
        size_t bytes_len = 0;
        status = backend_read_bytes(reader, &bytes_value, &bytes_len);
        if (status != ECRITUM_OK) {
            return status;
        }
        record->kind = ECRITUM_VALUE_KIND_DATA;
        record->value.bytes.data = bytes_value;
        record->value.bytes.len = bytes_len;
        return ECRITUM_OK;
    }
    case ECRITUM_VALUE_KIND_ARRAY:
        return backend_parse_array(reader, record);
    case ECRITUM_VALUE_KIND_OBJECT:
        return backend_parse_object(reader, record);
    default:
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
}

static int backend_write_array(ecritum_backend_writer_t *writer, const ecritum_value_record_t *record) {
    if (record->value.array.count > UINT32_MAX) {
        return ECRITUM_ERROR_INPUT_TOO_LARGE;
    }
    int status = backend_write_u32(writer, (uint32_t)record->value.array.count);
    for (size_t index = 0; status == ECRITUM_OK && index < record->value.array.count; index++) {
        status = backend_write_value(writer, &record->value.array.items[index]);
    }
    return status;
}

static int backend_write_object(ecritum_backend_writer_t *writer, const ecritum_value_record_t *record) {
    if (record->value.object.count > UINT32_MAX) {
        return ECRITUM_ERROR_INPUT_TOO_LARGE;
    }
    int status = backend_write_u32(writer, (uint32_t)record->value.object.count);
    for (size_t index = 0; status == ECRITUM_OK && index < record->value.object.count; index++) {
        status = backend_write_counted_bytes(
            writer,
            (const uint8_t *)record->value.object.keys[index],
            record->value.object.key_lengths[index]
        );
        if (status == ECRITUM_OK) {
            status = backend_write_value(writer, &record->value.object.values[index]);
        }
    }
    return status;
}

static int backend_write_value(ecritum_backend_writer_t *writer, const ecritum_value_record_t *record) {
    if (writer == NULL || record == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    int status = backend_write_u8(writer, (uint8_t)record->kind);
    if (status != ECRITUM_OK) {
        return status;
    }
    switch (record->kind) {
    case ECRITUM_VALUE_KIND_NULL:
        return ECRITUM_OK;
    case ECRITUM_VALUE_KIND_BOOL:
        return backend_write_u8(writer, record->value.bool_value ? 1u : 0u);
    case ECRITUM_VALUE_KIND_INT:
        return backend_write_u64(writer, (uint64_t)record->value.int_value);
    case ECRITUM_VALUE_KIND_DOUBLE: {
        uint64_t bits = 0;
        memcpy(&bits, &record->value.double_value, sizeof(bits));
        return backend_write_u64(writer, bits);
    }
    case ECRITUM_VALUE_KIND_STRING:
        return backend_write_counted_bytes(writer, record->value.bytes.data, record->value.bytes.len);
    case ECRITUM_VALUE_KIND_DATA:
        return backend_write_counted_bytes(writer, record->value.bytes.data, record->value.bytes.len);
    case ECRITUM_VALUE_KIND_ARRAY:
        return backend_write_array(writer, record);
    case ECRITUM_VALUE_KIND_OBJECT:
        return backend_write_object(writer, record);
    default:
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
}

static int backend_write_result(
    uint8_t *out_buffer,
    size_t out_buffer_size,
    long long *out_bytes_written,
    int status,
    const ecritum_value_record_t *value,
    const char *language,
    const char *source_name,
    const char *category,
    const char *message
) {
    if (out_buffer == NULL || out_bytes_written == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    *out_bytes_written = 0;
    ecritum_backend_writer_t writer = {out_buffer, out_buffer_size, 0};
    int write_status = backend_write_u32(&writer, ECRITUM_BACKEND_RESULT_MAGIC);
    if (write_status == ECRITUM_OK) write_status = backend_write_u32(&writer, (uint32_t)status);
    if (write_status == ECRITUM_OK) {
        if (status == ECRITUM_OK) {
            write_status = backend_write_value(&writer, value);
        } else {
            write_status = backend_write_string(&writer, language);
            if (write_status == ECRITUM_OK) write_status = backend_write_string(&writer, source_name);
            if (write_status == ECRITUM_OK) write_status = backend_write_string(&writer, category);
            if (write_status == ECRITUM_OK) write_status = backend_write_string(&writer, message);
        }
    }
    *out_bytes_written = (long long)writer.offset;
    return write_status;
}

static int backend_decode_ok_value(const uint8_t *data, size_t len, ecritum_value_record_t *out_value) {
    if (data == NULL || out_value == NULL || len == 0 || len > ECRITUM_BACKEND_RESULT_MAX_BYTES) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    ecritum_backend_reader_t reader = {data, len, 0};
    uint32_t magic = 0;
    uint32_t raw_status = 0;
    int status = backend_read_u32(&reader, &magic);
    if (status == ECRITUM_OK) status = backend_read_u32(&reader, &raw_status);
    if (status != ECRITUM_OK || magic != ECRITUM_BACKEND_RESULT_MAGIC || raw_status != ECRITUM_OK) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    status = backend_parse_value(&reader, out_value);
    if (status == ECRITUM_OK && reader.offset != reader.len) {
        value_record_destroy(out_value);
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    return status;
}

static int backend_status_is_known(int status) {
    return status >= ECRITUM_ERROR_INVALID_ARGUMENT && status <= ECRITUM_ERROR_ALREADY_EXISTS;
}

static int backend_apply_result_to_job(const uint8_t *data, size_t len, ecritum_job_record_t *job) {
    if (data == NULL || job == NULL || len == 0 || len > ECRITUM_BACKEND_RESULT_MAX_BYTES) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    ecritum_backend_reader_t reader = {data, len, 0};
    uint32_t magic = 0;
    uint32_t raw_status = 0;
    int status = backend_read_u32(&reader, &magic);
    if (status == ECRITUM_OK) status = backend_read_u32(&reader, &raw_status);
    if (status != ECRITUM_OK || magic != ECRITUM_BACKEND_RESULT_MAGIC) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    if ((int)raw_status == ECRITUM_OK) {
        ecritum_value_record_t value = {0};
        status = backend_parse_value(&reader, &value);
        if (status != ECRITUM_OK) {
            value_record_destroy(&value);
            return status;
        }
        if (reader.offset != reader.len) {
            value_record_destroy(&value);
            return ECRITUM_ERROR_INVALID_ARGUMENT;
        }
        value_record_destroy(&job->result);
        job->result = value;
        job->has_result = 1;
        job->state = ECRITUM_JOB_SUCCEEDED;
        job->terminal_status = ECRITUM_OK;
        copy_text(job->message, sizeof(job->message), "eval succeeded");
        return ECRITUM_OK;
    }

    char *language = NULL;
    char *source_name = NULL;
    char *category = NULL;
    char *message = NULL;
    size_t ignored_len = 0;
    status = backend_read_string(&reader, &language, &ignored_len);
    if (status == ECRITUM_OK) status = backend_read_string(&reader, &source_name, &ignored_len);
    if (status == ECRITUM_OK) status = backend_read_string(&reader, &category, &ignored_len);
    if (status == ECRITUM_OK) status = backend_read_string(&reader, &message, &ignored_len);
    if (status == ECRITUM_OK && reader.offset != reader.len) {
        status = ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    if (status == ECRITUM_OK) {
        int terminal_status = backend_status_is_known((int)raw_status) ? (int)raw_status : ECRITUM_ERROR_INTERNAL;
        job->state = ECRITUM_JOB_FAILED;
        job->terminal_status = terminal_status;
        copy_text(job->language, sizeof(job->language), language);
        copy_text(job->source_name, sizeof(job->source_name), source_name);
        copy_text(job->category, sizeof(job->category), category);
        copy_text(job->message, sizeof(job->message), message);
    }
    free(language);
    free(source_name);
    free(category);
    free(message);
    return status;
}

static int job_state_is_terminal(int state) {
    return state == ECRITUM_JOB_SUCCEEDED
        || state == ECRITUM_JOB_FAILED
        || state == ECRITUM_JOB_CANCELLED
        || state == ECRITUM_JOB_TIMED_OUT
        || state == ECRITUM_JOB_POISONED;
}

static void release_job_context_locked(ecritum_job_t job, ecritum_job_record_t *job_record) {
    uint32_t context_slot = job_record->parent_context_slot;
    uint16_t context_generation = job_record->parent_context_generation;
    if (context_slot == 0 || context_slot >= ECRITUM_MAX_HANDLE_SLOTS) {
        return;
    }
    ecritum_handle_slot_t *context_entry = &registry[context_slot];
    if (context_entry->kind != ECRITUM_HANDLE_KIND_CONTEXT || context_entry->generation != context_generation) {
        return;
    }
    if (context_entry->value.context.active_job_slot == handle_slot(job)
        && context_entry->value.context.active_job_generation == handle_generation(job)) {
        context_entry->value.context.active_job_slot = 0;
        context_entry->value.context.active_job_generation = 0;
    }
}

static int context_matches_active_callback_runtime_locked(ecritum_handle_slot_t *context_entry) {
    if (context_entry == NULL || context_entry->kind != ECRITUM_HANDLE_KIND_CONTEXT) {
        return 0;
    }
    return callback_stack_contains_runtime(
        context_entry->value.context.parent_slot,
        context_entry->value.context.parent_generation
    );
}

static int job_matches_active_callback_runtime_locked(ecritum_handle_slot_t *job_entry) {
    if (job_entry == NULL || job_entry->kind != ECRITUM_HANDLE_KIND_JOB) {
        return 0;
    }
    uint32_t context_slot = job_entry->value.job.parent_context_slot;
    uint16_t context_generation = job_entry->value.job.parent_context_generation;
    if (context_slot == 0 || context_slot >= ECRITUM_MAX_HANDLE_SLOTS) {
        return 0;
    }
    ecritum_handle_slot_t *context_entry = &registry[context_slot];
    if (context_entry->kind != ECRITUM_HANDLE_KIND_CONTEXT
        || context_entry->generation != context_generation) {
        return 0;
    }
    return context_matches_active_callback_runtime_locked(context_entry);
}

static void job_set_failure_with_category(ecritum_job_record_t *job, int state, int terminal_status, const char *message, const char *category);

static void job_set_int_result(ecritum_job_record_t *job, int64_t value) {
    job->state = ECRITUM_JOB_SUCCEEDED;
    job->terminal_status = ECRITUM_OK;
    job->has_result = 1;
    job->result.kind = ECRITUM_VALUE_KIND_INT;
    job->result.value.int_value = value;
    copy_text(job->message, sizeof(job->message), "eval succeeded");
    job_signal_locked(job);
}

static void job_set_data_result(ecritum_job_record_t *job, const uint8_t *data, size_t len) {
    ecritum_value_record_t record = {0};
    record.kind = ECRITUM_VALUE_KIND_DATA;
    if (copy_value_bytes(&record.value.bytes, data, len, 0) != ECRITUM_OK) {
        job_set_failure_with_category(job, ECRITUM_JOB_FAILED, ECRITUM_ERROR_OUT_OF_MEMORY, "value allocation failed", "internal");
        return;
    }
    value_record_destroy(&job->result);
    job->result = record;
    job->state = ECRITUM_JOB_SUCCEEDED;
    job->terminal_status = ECRITUM_OK;
    job->has_result = 1;
    copy_text(job->message, sizeof(job->message), "eval succeeded");
    job_signal_locked(job);
}

static void job_set_failure(ecritum_job_record_t *job, int state, int terminal_status, const char *message) {
    job->state = state;
    job->terminal_status = terminal_status;
    copy_text(job->message, sizeof(job->message), message);
    job->category[0] = '\0';
    job_signal_locked(job);
}

static void job_set_failure_with_category(ecritum_job_record_t *job, int state, int terminal_status, const char *message, const char *category) {
    job_set_failure(job, state, terminal_status, message);
    copy_text(job->category, sizeof(job->category), category);
}

static void add_nanos_to_timespec(struct timespec *deadline, uint64_t nanos) {
    deadline->tv_sec += (time_t)(nanos / 1000000000ULL);
    deadline->tv_nsec += (long)(nanos % 1000000000ULL);
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec += 1;
        deadline->tv_nsec -= 1000000000L;
    }
}

#ifndef ECRITUM_TESTING
static int host_call_bridge_from_graal(
    size_t runtime_handle,
    char *namespace_name,
    size_t namespace_name_len,
    char *function_name,
    size_t function_name_len,
    char *arguments,
    size_t arguments_len,
    char *out_buffer,
    size_t out_buffer_size,
    long long *out_bytes_written
);

static void *eval_worker_main(void *raw_work) {
    ecritum_eval_work_t *work = raw_work;
    if (work == NULL) {
        return NULL;
    }

    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *precheck_entry = validate_locked(work->job, ECRITUM_HANDLE_KIND_JOB);
    if (precheck_entry == NULL || job_state_is_terminal(precheck_entry->value.job.state)) {
        release_host_projection_snapshot_locked(work);
        pthread_mutex_unlock(&registry_mutex);
        eval_work_destroy(work);
        return NULL;
    }
    if (precheck_entry->value.job.state == ECRITUM_JOB_CANCEL_REQUESTED) {
        job_set_failure(&precheck_entry->value.job, ECRITUM_JOB_CANCELLED, ECRITUM_ERROR_CANCELLED, "job was cancelled");
        release_host_projection_snapshot_locked(work);
        pthread_mutex_unlock(&registry_mutex);
        eval_work_destroy(work);
        return NULL;
    }
    pthread_mutex_unlock(&registry_mutex);

    graal_isolatethread_t *thread = NULL;
    int attached_thread = 0;
    int backend_status = attach_runtime_thread(work->isolate, &thread, &attached_thread);
    uint8_t backend_buffer[ECRITUM_BACKEND_RESULT_MAX_BYTES] = {0};
    long long backend_bytes_written = 0;

    if (backend_status == ECRITUM_OK) {
        backend_status = ecritum_graal_eval_clojure_with_host(
            thread,
            (char *)work->source,
            work->source_len,
            work->source_name,
            work->source_name_len,
            work->host_manifest,
            work->host_manifest_len,
            host_call_bridge_from_graal,
            (size_t)work->runtime,
            (char *)backend_buffer,
            sizeof(backend_buffer),
            &backend_bytes_written
        );
    }
    detach_runtime_thread_if_needed(thread, attached_thread);

    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *job_entry = validate_locked(work->job, ECRITUM_HANDLE_KIND_JOB);
    if (job_entry != NULL) {
        ecritum_job_record_t *job = &job_entry->value.job;
        if (job->state == ECRITUM_JOB_CANCEL_REQUESTED) {
            job_set_failure(job, ECRITUM_JOB_CANCELLED, ECRITUM_ERROR_CANCELLED, "job was cancelled");
        } else if (!job_state_is_terminal(job->state)) {
            if (backend_status == ECRITUM_OK && backend_bytes_written > 0 && (size_t)backend_bytes_written <= sizeof(backend_buffer)) {
                int parse_status = backend_apply_result_to_job(backend_buffer, (size_t)backend_bytes_written, job);
                if (parse_status != ECRITUM_OK) {
                    job_set_failure_with_category(job, ECRITUM_JOB_FAILED, parse_status, "backend result parsing failed", "internal");
                } else {
                    job_signal_locked(job);
                }
            } else if (backend_status == ECRITUM_ERROR_BUFFER_TOO_SMALL || backend_bytes_written > (long long)sizeof(backend_buffer)) {
                job_set_failure_with_category(job, ECRITUM_JOB_FAILED, ECRITUM_ERROR_INPUT_TOO_LARGE, "backend result is too large", "runtime");
            } else {
                job_set_failure_with_category(job, ECRITUM_JOB_FAILED, ECRITUM_ERROR_RUNTIME_UNAVAILABLE, "eval backend is not available", "internal");
            }
        }
    }
    release_host_projection_snapshot_locked(work);
    pthread_mutex_unlock(&registry_mutex);

    eval_work_destroy(work);
    return NULL;
}
#endif

__attribute__((visibility("default"))) int ecritum_eval_start(
    ecritum_context_t context,
    ecritum_string_view_t language,
    ecritum_bytes_t source,
    ecritum_string_view_t source_name,
    ecritum_bytes_t options_json,
    ecritum_job_t *out_job,
    ecritum_error_t *out_error
) {
    if (out_job == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "eval_start", "missing job output", out_error);
    }
    *out_job = 0;
    clear_error(out_error);

    int validation = view_is_valid_utf8(language);
    if (validation == ECRITUM_OK) validation = bytes_input_is_valid_utf8(source);
    if (validation == ECRITUM_OK) validation = view_is_valid_utf8(source_name);
    if (validation == ECRITUM_OK) validation = bytes_input_is_valid_utf8(options_json);
    if (validation != ECRITUM_OK) {
        return fail_with_error(validation, "eval_start", "invalid eval input", out_error);
    }
    if (!view_equals(language, "clojure")) {
        return fail_with_error(ECRITUM_ERROR_RUNTIME_UNAVAILABLE, "eval_start", "language is not available in this artifact", out_error);
    }
    if (options_json.len != 0) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "eval_start", "eval options are not implemented", out_error);
    }

#ifndef ECRITUM_TESTING
    ecritum_eval_work_t *work = calloc(1, sizeof(*work));
    if (work == NULL) {
        return fail_with_error(ECRITUM_ERROR_OUT_OF_MEMORY, "eval_start", "eval work allocation failed", out_error);
    }
    int copy_status = copy_bytes_as_c_string(source.data, source.len, &work->source);
    if (copy_status == ECRITUM_OK) {
        copy_status = copy_view_as_c_string(source_name, &work->source_name);
    }
    if (copy_status != ECRITUM_OK) {
        eval_work_destroy(work);
        return fail_with_error(copy_status, "eval_start", "eval input copy failed", out_error);
    }
    work->source_len = source.len;
    work->source_name_len = source_name.len;

    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *context_entry = validate_locked(context, ECRITUM_HANDLE_KIND_CONTEXT);
    if (context_entry == NULL) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "eval_start", "invalid context handle");
        pthread_mutex_unlock(&registry_mutex);
        eval_work_destroy(work);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    if (context_matches_active_callback_runtime_locked(context_entry)) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_REENTRANT_CALL, "eval_start", "context runtime is active in a callback");
        pthread_mutex_unlock(&registry_mutex);
        eval_work_destroy(work);
        return ECRITUM_ERROR_REENTRANT_CALL;
    }
    if (context_entry->value.context.active_job_slot != 0) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_BUSY, "eval_start", "context has a live job");
        pthread_mutex_unlock(&registry_mutex);
        eval_work_destroy(work);
        return ECRITUM_ERROR_BUSY;
    }
    uint32_t runtime_slot = context_entry->value.context.parent_slot;
    uint16_t runtime_generation = context_entry->value.context.parent_generation;
    ecritum_handle_slot_t *runtime_entry = validate_locked(
        make_handle(ECRITUM_HANDLE_KIND_RUNTIME, runtime_slot, runtime_generation),
        ECRITUM_HANDLE_KIND_RUNTIME
    );
    if (runtime_entry == NULL || runtime_entry->value.runtime.isolate == NULL) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "eval_start", "invalid runtime handle");
        pthread_mutex_unlock(&registry_mutex);
        eval_work_destroy(work);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    if (runtime_entry->value.runtime.config != NULL
        && runtime_entry->value.runtime.config->languages.count > 0
        && !string_set_contains(&runtime_entry->value.runtime.config->languages, "clojure")) {
        if (out_error != NULL) {
            *out_error = create_error_locked(ECRITUM_ERROR_PERMISSION_DENIED, "eval_start", "language is not enabled by runtime configuration");
        }
        pthread_mutex_unlock(&registry_mutex);
        eval_work_destroy(work);
        return ECRITUM_ERROR_PERMISSION_DENIED;
    }
    graal_isolate_t *isolate = runtime_entry->value.runtime.isolate;
    ecritum_job_t job = allocate_slot_locked(ECRITUM_HANDLE_KIND_JOB);
    if (job == 0) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_OUT_OF_MEMORY, "eval_start", "handle registry is full");
        pthread_mutex_unlock(&registry_mutex);
        eval_work_destroy(work);
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    ecritum_handle_slot_t *job_entry = validate_locked(job, ECRITUM_HANDLE_KIND_JOB);
    int job_status = job_record_init(&job_entry->value.job);
    if (job_status != ECRITUM_OK) {
        tombstone_slot_locked(job_entry);
        if (out_error != NULL) *out_error = create_error_locked(job_status, "eval_start", "job initialization failed");
        pthread_mutex_unlock(&registry_mutex);
        eval_work_destroy(work);
        return job_status;
    }
    job_entry->value.job.parent_context_slot = handle_slot(context);
    job_entry->value.job.parent_context_generation = handle_generation(context);
    job_entry->value.job.state = ECRITUM_JOB_RUNNING;
    job_entry->value.job.terminal_status = ECRITUM_OK;
    copy_view_text(job_entry->value.job.language, sizeof(job_entry->value.job.language), language.data, language.len);
    copy_view_text(job_entry->value.job.source_name, sizeof(job_entry->value.job.source_name), source_name.data, source_name.len);
    context_entry->value.context.active_job_slot = handle_slot(job);
    context_entry->value.context.active_job_generation = handle_generation(job);
    work->job = job;
    work->isolate = isolate;
    work->runtime = make_handle(ECRITUM_HANDLE_KIND_RUNTIME, runtime_slot, runtime_generation);
    int projection_status = create_host_projection_snapshot_locked(runtime_slot, runtime_generation, work);
    if (projection_status != ECRITUM_OK) {
        release_job_context_locked(job, &job_entry->value.job);
        tombstone_slot_locked(job_entry);
        if (out_error != NULL) *out_error = create_error_locked(projection_status, "eval_start", "host projection snapshot failed");
        pthread_mutex_unlock(&registry_mutex);
        eval_work_destroy(work);
        return projection_status;
    }

    pthread_t worker_thread;
    pthread_attr_t worker_attr;
    int pthread_status = pthread_attr_init(&worker_attr);
    int worker_attr_initialized = pthread_status == 0;
    if (pthread_status == 0) {
        pthread_status = pthread_attr_setdetachstate(&worker_attr, PTHREAD_CREATE_DETACHED);
    }
    if (pthread_status == 0) {
        pthread_status = pthread_create(&worker_thread, &worker_attr, eval_worker_main, work);
    }
    if (pthread_status != 0) {
        release_host_projection_snapshot_locked(work);
        release_job_context_locked(job, &job_entry->value.job);
        tombstone_slot_locked(job_entry);
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_RUNTIME_UNAVAILABLE, "eval_start", "eval worker creation failed");
        pthread_mutex_unlock(&registry_mutex);
        if (worker_attr_initialized) {
            pthread_attr_destroy(&worker_attr);
        }
        eval_work_destroy(work);
        return ECRITUM_ERROR_RUNTIME_UNAVAILABLE;
    }
    if (worker_attr_initialized) {
        pthread_attr_destroy(&worker_attr);
    }
    pthread_mutex_unlock(&registry_mutex);
    *out_job = job;
    return ECRITUM_OK;
#else
    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *context_entry = validate_locked(context, ECRITUM_HANDLE_KIND_CONTEXT);
    if (context_entry == NULL) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "eval_start", "invalid context handle");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    if (context_matches_active_callback_runtime_locked(context_entry)) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_REENTRANT_CALL, "eval_start", "context runtime is active in a callback");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_REENTRANT_CALL;
    }
    if (context_entry->value.context.active_job_slot != 0) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_BUSY, "eval_start", "context has a live job");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_BUSY;
    }
    ecritum_job_t job = allocate_slot_locked(ECRITUM_HANDLE_KIND_JOB);
    if (job == 0) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_OUT_OF_MEMORY, "eval_start", "handle registry is full");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    ecritum_handle_slot_t *job_entry = validate_locked(job, ECRITUM_HANDLE_KIND_JOB);
    int job_status = job_record_init(&job_entry->value.job);
    if (job_status != ECRITUM_OK) {
        tombstone_slot_locked(job_entry);
        if (out_error != NULL) *out_error = create_error_locked(job_status, "eval_start", "job initialization failed");
        pthread_mutex_unlock(&registry_mutex);
        return job_status;
    }
    job_entry->value.job.parent_context_slot = handle_slot(context);
    job_entry->value.job.parent_context_generation = handle_generation(context);
    job_entry->value.job.state = ECRITUM_JOB_RUNNING;
    job_entry->value.job.terminal_status = ECRITUM_OK;
    copy_view_text(job_entry->value.job.language, sizeof(job_entry->value.job.language), language.data, language.len);
    copy_view_text(job_entry->value.job.source_name, sizeof(job_entry->value.job.source_name), source_name.data, source_name.len);
    if (source_matches(source, "fixture:pending")) {
        job_entry->value.job.state = ECRITUM_JOB_RUNNING;
        copy_text(job_entry->value.job.message, sizeof(job_entry->value.job.message), "eval is running");
    } else if (source_matches(source, "fixture:int:1")) {
        job_set_int_result(&job_entry->value.job, 1);
    } else if (source_matches(source, "fixture:int:2")) {
        job_set_int_result(&job_entry->value.job, 2);
    } else if (source_matches(source, "fixture:int:7")) {
        job_set_int_result(&job_entry->value.job, 7);
    } else if (source_matches(source, "fixture:int:42")) {
        job_set_int_result(&job_entry->value.job, 42);
    } else if (source_matches(source, "fixture:data")) {
        static const uint8_t fixture_data[] = {0u, 1u, 2u, 255u};
        job_set_data_result(&job_entry->value.job, fixture_data, sizeof(fixture_data));
    } else {
        job_set_failure(&job_entry->value.job, ECRITUM_JOB_FAILED, ECRITUM_ERROR_SCRIPT, "script evaluation failed");
    }
    context_entry->value.context.active_job_slot = handle_slot(job);
    context_entry->value.context.active_job_generation = handle_generation(job);
    pthread_mutex_unlock(&registry_mutex);
    *out_job = job;
    return ECRITUM_OK;
#endif
}

__attribute__((visibility("default"))) int ecritum_job_poll(ecritum_job_t job, int *out_state, ecritum_error_t *out_error) {
    if (out_state == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "job_poll", "missing state output", out_error);
    }
    clear_error(out_error);
    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *entry = validate_locked(job, ECRITUM_HANDLE_KIND_JOB);
    if (entry == NULL) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "job_poll", "invalid job handle");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    *out_state = entry->value.job.state;
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_job_wait(ecritum_job_t job, uint64_t wait_timeout_nanos, int *out_state, ecritum_error_t *out_error) {
    if (wait_timeout_nanos == UINT64_MAX) {
        if (out_state != NULL) *out_state = 0;
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "job_wait", "reserved wait timeout", out_error);
    }
    if (out_state == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "job_wait", "missing state output", out_error);
    }
    clear_error(out_error);
    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *entry = validate_locked(job, ECRITUM_HANDLE_KIND_JOB);
    if (entry == NULL) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "job_wait", "invalid job handle");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    if (job_matches_active_callback_runtime_locked(entry)) {
        *out_state = 0;
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_REENTRANT_CALL, "job_wait", "job runtime is active in a callback");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_REENTRANT_CALL;
    }
    if (wait_timeout_nanos > 0 && !job_state_is_terminal(entry->value.job.state)) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        add_nanos_to_timespec(&deadline, wait_timeout_nanos);
        while (!job_state_is_terminal(entry->value.job.state)) {
            int wait_status = pthread_cond_timedwait(&entry->value.job.condition, &registry_mutex, &deadline);
            if (wait_status == ETIMEDOUT) {
                break;
            }
            if (wait_status != 0) {
                *out_state = 0;
                if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INTERNAL, "job_wait", "job wait failed");
                pthread_mutex_unlock(&registry_mutex);
                return ECRITUM_ERROR_INTERNAL;
            }
        }
    }
    *out_state = entry->value.job.state;
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_job_cancel(ecritum_job_t job, ecritum_error_t *out_error) {
    clear_error(out_error);
    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *entry = validate_locked(job, ECRITUM_HANDLE_KIND_JOB);
    if (entry == NULL) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "job_cancel", "invalid job handle");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    if (job_matches_active_callback_runtime_locked(entry)) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_REENTRANT_CALL, "job_cancel", "job runtime is active in a callback");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_REENTRANT_CALL;
    }
    if (!job_state_is_terminal(entry->value.job.state)) {
#ifdef ECRITUM_TESTING
        job_set_failure(&entry->value.job, ECRITUM_JOB_CANCELLED, ECRITUM_ERROR_CANCELLED, "job was cancelled");
#else
        entry->value.job.state = ECRITUM_JOB_CANCEL_REQUESTED;
        entry->value.job.terminal_status = ECRITUM_ERROR_CANCELLED;
        copy_text(entry->value.job.message, sizeof(entry->value.job.message), "job cancellation requested");
        job_signal_locked(&entry->value.job);
#endif
    }
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

__attribute__((visibility("default"))) int ecritum_job_result(ecritum_job_t job, ecritum_value_t *out_result, ecritum_error_t *out_error) {
    if (out_result == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "job_result", "missing result output", out_error);
    }
    *out_result = 0;
    clear_error(out_error);
    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *entry = validate_locked(job, ECRITUM_HANDLE_KIND_JOB);
    if (entry == NULL) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "job_result", "invalid job handle");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    ecritum_job_record_t *job_record = &entry->value.job;
    if (job_record->drained) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_CLOSED, "job_result", "job result is already drained");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_CLOSED;
    }
    if (!job_state_is_terminal(job_record->state)) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_BUSY, "job_result", "job is not terminal");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_BUSY;
    }
    if (job_record->state == ECRITUM_JOB_SUCCEEDED) {
        int status = create_value_handle_from_record_locked(&job_record->result, out_result);
        if (status == ECRITUM_OK) {
            value_record_destroy(&job_record->result);
            job_record->has_result = 0;
            job_record->drained = 1;
        } else if (out_error != NULL) {
            *out_error = create_error_locked(status, "job_result", "value allocation failed");
        }
        pthread_mutex_unlock(&registry_mutex);
        return status;
    }
    int status = job_record->terminal_status == ECRITUM_OK ? ECRITUM_ERROR_SCRIPT : job_record->terminal_status;
    if (out_error != NULL) {
        *out_error = create_error_with_details_locked(
            status,
            "eval",
            job_record->message,
            job_record->category,
            job_record->language,
            job_record->source_name
        );
    }
    job_record->drained = 1;
    pthread_mutex_unlock(&registry_mutex);
    return status;
}

__attribute__((visibility("default"))) int ecritum_job_destroy(ecritum_job_t *job, ecritum_error_t *out_error) {
    if (job == NULL || *job == 0) {
        clear_error(out_error);
        return ECRITUM_OK;
    }
    clear_error(out_error);
    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *entry = validate_locked(*job, ECRITUM_HANDLE_KIND_JOB);
    if (entry == NULL) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_INVALID_HANDLE, "job_destroy", "invalid job handle");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    if (!job_state_is_terminal(entry->value.job.state)) {
        if (out_error != NULL) *out_error = create_error_locked(ECRITUM_ERROR_BUSY, "job_destroy", "job is still active");
        pthread_mutex_unlock(&registry_mutex);
        return ECRITUM_ERROR_BUSY;
    }
    release_job_context_locked(*job, &entry->value.job);
    tombstone_slot_locked(entry);
    *job = 0;
    pthread_mutex_unlock(&registry_mutex);
    return ECRITUM_OK;
}

static int invoke_function_with_args_internal(
    ecritum_function_t function,
    const ecritum_value_t *arguments,
    size_t argument_count,
    ecritum_value_t *out_result,
    ecritum_error_t *out_error
) {
    if (out_result == NULL) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "function_invoke", "missing result output", out_error);
    }
    *out_result = 0;
    clear_error(out_error);
    if (arguments == NULL && argument_count > 0) {
        return fail_with_error(ECRITUM_ERROR_INVALID_ARGUMENT, "function_invoke", "missing call arguments", out_error);
    }

    ecritum_host_fn_t callback = NULL;
    void *user_data = NULL;
    ecritum_call_t call = 0;
    uint32_t callback_runtime_slot = 0;
    uint16_t callback_runtime_generation = 0;

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
    call_entry->value.call.argument_count = argument_count;
    if (argument_count > 0) {
        call_entry->value.call.arguments = calloc(argument_count, sizeof(ecritum_value_record_t));
        if (call_entry->value.call.arguments == NULL) {
            tombstone_slot_locked(call_entry);
            if (out_error != NULL) {
                *out_error = create_error_locked(ECRITUM_ERROR_OUT_OF_MEMORY, "function_invoke", "call argument allocation failed");
            }
            pthread_mutex_unlock(&registry_mutex);
            return ECRITUM_ERROR_OUT_OF_MEMORY;
        }
        for (size_t index = 0; index < argument_count; index++) {
            int status = value_record_copy_from_handle_locked(&call_entry->value.call.arguments[index], arguments[index]);
            if (status != ECRITUM_OK) {
                tombstone_slot_locked(call_entry);
                if (out_error != NULL) {
                    *out_error = create_error_locked(status, "function_invoke", "invalid call argument");
                }
                pthread_mutex_unlock(&registry_mutex);
                return status;
            }
        }
    }
    function_entry->value.function.active_calls++;
    callback = function_entry->value.function.callback;
    user_data = function_entry->value.function.user_data;
    uint32_t namespace_slot = function_entry->value.function.parent_namespace_slot;
    uint16_t namespace_generation = function_entry->value.function.parent_namespace_generation;
    if (namespace_slot > 0 && namespace_slot < ECRITUM_MAX_HANDLE_SLOTS) {
        ecritum_handle_slot_t *namespace_entry = &registry[namespace_slot];
        if (namespace_entry->kind == ECRITUM_HANDLE_KIND_NAMESPACE
            && namespace_entry->generation == namespace_generation) {
            callback_runtime_slot = namespace_entry->value.namespace_record.parent_runtime_slot;
            callback_runtime_generation = namespace_entry->value.namespace_record.parent_runtime_generation;
        }
    }
    pthread_mutex_unlock(&registry_mutex);

    ecritum_value_t result = 0;
    ecritum_error_t callback_error = 0;
    callback_stack_push_runtime(callback_runtime_slot, callback_runtime_generation);
    int callback_status = callback(call, &result, &callback_error, user_data);
    callback_stack_pop_runtime(callback_runtime_slot, callback_runtime_generation);

    pthread_mutex_lock(&registry_mutex);
    ecritum_handle_slot_t *live_call_entry = validate_locked(call, ECRITUM_HANDLE_KIND_CALL);
    if (live_call_entry != NULL) {
        tombstone_slot_locked(live_call_entry);
    }
    ecritum_handle_slot_t *live_function_entry = validate_locked(function, ECRITUM_HANDLE_KIND_FUNCTION);
    if (live_function_entry != NULL && live_function_entry->value.function.active_calls > 0) {
        live_function_entry->value.function.active_calls--;
    }
    pthread_mutex_unlock(&registry_mutex);

    if (callback_status == ECRITUM_OK && callback_error == 0) {
        if (result == 0) {
            int null_status = ecritum_value_make_null(&result, out_error);
            if (null_status != ECRITUM_OK) {
                return null_status;
            }
        } else {
            pthread_mutex_lock(&registry_mutex);
            int valid_result = validate_locked(result, ECRITUM_HANDLE_KIND_VALUE) != NULL;
            pthread_mutex_unlock(&registry_mutex);
            if (!valid_result) {
                return fail_with_error(ECRITUM_ERROR_CALLBACK, "function_invoke", "host function returned an invalid value handle", out_error);
            }
        }
        *out_result = result;
        return ECRITUM_OK;
    }
    if (result != 0) {
        (void)ecritum_value_destroy(&result);
    }
    if (callback_error != 0) {
        (void)ecritum_error_destroy(&callback_error);
    }
    return fail_with_error(ECRITUM_ERROR_CALLBACK, "function_invoke", "host function callback failed", out_error);
}

#ifndef ECRITUM_TESTING
static int find_projected_function_locked(
    ecritum_runtime_t runtime,
    const char *namespace_name,
    size_t namespace_name_len,
    const char *function_name,
    size_t function_name_len,
    ecritum_function_t *out_function
) {
    if (out_function == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    *out_function = 0;
    ecritum_handle_slot_t *runtime_entry = validate_locked(runtime, ECRITUM_HANDLE_KIND_RUNTIME);
    if (runtime_entry == NULL) {
        return ECRITUM_ERROR_INVALID_HANDLE;
    }
    uint32_t runtime_slot = handle_slot(runtime);
    uint16_t runtime_generation = handle_generation(runtime);
    for (uint32_t namespace_slot = 1; namespace_slot < ECRITUM_MAX_HANDLE_SLOTS; namespace_slot++) {
        ecritum_handle_slot_t *namespace_entry = &registry[namespace_slot];
        if (!namespace_matches_runtime(namespace_entry, runtime_slot, runtime_generation)) {
            continue;
        }
        if (strlen(namespace_entry->value.namespace_record.name) != namespace_name_len
            || memcmp(namespace_entry->value.namespace_record.name, namespace_name, namespace_name_len) != 0) {
            continue;
        }
        for (uint32_t function_slot = 1; function_slot < ECRITUM_MAX_HANDLE_SLOTS; function_slot++) {
            ecritum_handle_slot_t *function_entry = &registry[function_slot];
            if (!function_matches_namespace(function_entry, namespace_slot, namespace_entry->generation)) {
                continue;
            }
            if (strlen(function_entry->value.function.name) == function_name_len
                && memcmp(function_entry->value.function.name, function_name, function_name_len) == 0) {
                *out_function = make_handle(ECRITUM_HANDLE_KIND_FUNCTION, function_slot, function_entry->generation);
                return ECRITUM_OK;
            }
        }
    }
    return ECRITUM_ERROR_PERMISSION_DENIED;
}

static int create_argument_handles_from_record(
    const ecritum_value_record_t *arguments_record,
    ecritum_value_t **out_arguments,
    size_t *out_argument_count
) {
    if (arguments_record == NULL || out_arguments == NULL || out_argument_count == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    *out_arguments = NULL;
    *out_argument_count = 0;
    if (arguments_record->kind != ECRITUM_VALUE_KIND_ARRAY) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    size_t count = arguments_record->value.array.count;
    if (count == 0) {
        return ECRITUM_OK;
    }
    ecritum_value_t *arguments = calloc(count, sizeof(ecritum_value_t));
    if (arguments == NULL) {
        return ECRITUM_ERROR_OUT_OF_MEMORY;
    }
    pthread_mutex_lock(&registry_mutex);
    int status = ECRITUM_OK;
    for (size_t index = 0; index < count; index++) {
        status = create_value_handle_from_record_locked(&arguments_record->value.array.items[index], &arguments[index]);
        if (status != ECRITUM_OK) {
            break;
        }
    }
    pthread_mutex_unlock(&registry_mutex);
    if (status != ECRITUM_OK) {
        for (size_t index = 0; index < count; index++) {
            if (arguments[index] != 0) {
                (void)ecritum_value_destroy(&arguments[index]);
            }
        }
        free(arguments);
        return status;
    }
    *out_arguments = arguments;
    *out_argument_count = count;
    return ECRITUM_OK;
}

static void destroy_argument_handles(ecritum_value_t *arguments, size_t argument_count) {
    if (arguments == NULL) {
        return;
    }
    for (size_t index = 0; index < argument_count; index++) {
        if (arguments[index] != 0) {
            (void)ecritum_value_destroy(&arguments[index]);
        }
    }
    free(arguments);
}

static int write_bridge_error(
    char *out_buffer,
    size_t out_buffer_size,
    long long *out_bytes_written,
    int status,
    const char *category,
    const char *message
) {
    return backend_write_result(
        (uint8_t *)out_buffer,
        out_buffer_size,
        out_bytes_written,
        status,
        NULL,
        "clojure",
        "",
        category,
        message
    );
}

static int host_call_bridge_from_graal(
    size_t runtime_handle,
    char *namespace_name,
    size_t namespace_name_len,
    char *function_name,
    size_t function_name_len,
    char *arguments,
    size_t arguments_len,
    char *out_buffer,
    size_t out_buffer_size,
    long long *out_bytes_written
) {
    if (namespace_name == NULL || function_name == NULL || arguments == NULL || out_buffer == NULL || out_bytes_written == NULL) {
        return ECRITUM_ERROR_INVALID_ARGUMENT;
    }
    *out_bytes_written = 0;

    ecritum_value_record_t arguments_record = {0};
    int status = backend_decode_ok_value((const uint8_t *)arguments, arguments_len, &arguments_record);
    if (status != ECRITUM_OK) {
        return write_bridge_error(out_buffer, out_buffer_size, out_bytes_written, ECRITUM_ERROR_CALLBACK, "callback", "host callback arguments are invalid");
    }

    ecritum_value_t *argument_handles = NULL;
    size_t argument_count = 0;
    status = create_argument_handles_from_record(&arguments_record, &argument_handles, &argument_count);
    value_record_destroy(&arguments_record);
    if (status != ECRITUM_OK) {
        return write_bridge_error(out_buffer, out_buffer_size, out_bytes_written, ECRITUM_ERROR_CALLBACK, "callback", "host callback argument conversion failed");
    }

    ecritum_function_t function = 0;
    pthread_mutex_lock(&registry_mutex);
    status = find_projected_function_locked((ecritum_runtime_t)runtime_handle, namespace_name, namespace_name_len, function_name, function_name_len, &function);
    pthread_mutex_unlock(&registry_mutex);
    if (status != ECRITUM_OK) {
        destroy_argument_handles(argument_handles, argument_count);
        return write_bridge_error(out_buffer, out_buffer_size, out_bytes_written, ECRITUM_ERROR_PERMISSION_DENIED, "permission", "host function is not projected");
    }

    ecritum_value_t result = 0;
    ecritum_error_t callback_error = 0;
    status = invoke_function_with_args_internal(function, argument_handles, argument_count, &result, &callback_error);
    destroy_argument_handles(argument_handles, argument_count);
    if (status != ECRITUM_OK) {
        if (callback_error != 0) {
            (void)ecritum_error_destroy(&callback_error);
        }
        return write_bridge_error(out_buffer, out_buffer_size, out_bytes_written, ECRITUM_ERROR_CALLBACK, "callback", "host function callback failed");
    }

    ecritum_value_record_t result_record = {0};
    pthread_mutex_lock(&registry_mutex);
    status = value_record_copy_from_handle_locked(&result_record, result);
    pthread_mutex_unlock(&registry_mutex);
    (void)ecritum_value_destroy(&result);
    if (status != ECRITUM_OK) {
        return write_bridge_error(out_buffer, out_buffer_size, out_bytes_written, ECRITUM_ERROR_CALLBACK, "callback", "host function returned invalid result");
    }
    status = backend_write_result((uint8_t *)out_buffer, out_buffer_size, out_bytes_written, ECRITUM_OK, &result_record, "", "", "", "");
    value_record_destroy(&result_record);
    return status;
}
#endif

#ifdef ECRITUM_TESTING
int ecritum_test_invoke_function_with_args(
    ecritum_function_t function,
    const ecritum_value_t *arguments,
    size_t argument_count,
    ecritum_value_t *out_result,
    ecritum_error_t *out_error
) {
    return invoke_function_with_args_internal(function, arguments, argument_count, out_result, out_error);
}

int ecritum_test_invoke_function(ecritum_function_t function, ecritum_error_t *out_error) {
    ecritum_value_t result = 0;
    int status = ecritum_test_invoke_function_with_args(function, NULL, 0, &result, out_error);
    if (result != 0) {
        (void)ecritum_value_destroy(&result);
    }
    return status;
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
    case 3: return error->language;
    case 4: return error->source_name;
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

__attribute__((visibility("default"))) int ecritum_error_language(ecritum_error_t error, ecritum_string_view_t *out_language) {
    return error_string_view(error, out_language, 3);
}

__attribute__((visibility("default"))) int ecritum_error_source_name(ecritum_error_t error, ecritum_string_view_t *out_source_name) {
    return error_string_view(error, out_source_name, 4);
}
