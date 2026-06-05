#include "ecritum.h"

#include <limits.h>
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
#define ECRITUM_CONFIG_MAX_BYTES 65536u
#define ECRITUM_CONFIG_MAX_DEPTH 16u
#define ECRITUM_CONFIG_MAX_ARRAY_ITEMS 256u
#define ECRITUM_CONFIG_MAX_STRING_BYTES 4096u
#define ECRITUM_CONFIG_MAX_SHORT_STRING_BYTES 255u

typedef struct ecritum_policy_config ecritum_policy_config_t;

static void config_destroy(ecritum_policy_config_t *config);

typedef struct {
    graal_isolate_t *isolate;
    uint32_t live_contexts;
    ecritum_policy_config_t *config;
} ecritum_runtime_record_t;

typedef struct {
    uint32_t parent_slot;
    uint16_t parent_generation;
    ecritum_policy_config_t *config;
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
    if (entry->kind == ECRITUM_HANDLE_KIND_RUNTIME) {
        config_destroy(entry->value.runtime.config);
    } else if (entry->kind == ECRITUM_HANDLE_KIND_CONTEXT) {
        config_destroy(entry->value.context.config);
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
    memcpy(copy, data, len);
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
