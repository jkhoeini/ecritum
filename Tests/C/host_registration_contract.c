#include "ecritum.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int ecritum_test_invoke_function(ecritum_function_t function, ecritum_error_t *out_error);

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

typedef struct {
    int callback_calls;
    int destroy_calls;
    int callback_status;
} callback_box_t;

static ecritum_bytes_t empty_config(void) {
    ecritum_bytes_t config = {0};
    return config;
}

static ecritum_string_view_t view(const char *text) {
    ecritum_string_view_t result = {text, strlen(text)};
    return result;
}

static ecritum_string_view_t bytes_view(const char *text, size_t len) {
    ecritum_string_view_t result = {text, len};
    return result;
}

static int callback_success(ecritum_call_t call, ecritum_value_t *out_result, ecritum_error_t *out_error, void *user_data) {
    (void)call;
    if (out_result != NULL) {
        *out_result = 0;
    }
    if (out_error != NULL) {
        *out_error = 0;
    }
    callback_box_t *box = user_data;
    box->callback_calls++;
    return box->callback_status;
}

static void destroy_box(void *user_data) {
    callback_box_t *box = user_data;
    box->destroy_calls++;
}

static void create_runtime_and_namespace(ecritum_runtime_t *runtime, ecritum_namespace_t *namespace_handle) {
    ecritum_error_t error = 0;
    CHECK(ecritum_runtime_create(empty_config(), runtime, &error) == ECRITUM_OK);
    CHECK(*runtime != 0);
    CHECK(ecritum_namespace_create(*runtime, view("app"), namespace_handle, &error) == ECRITUM_OK);
    CHECK(*namespace_handle != 0);
}

static void test_registration_success_invoke_and_function_destroy_cleanup(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_namespace_t namespace_handle = 0;
    ecritum_function_t function = 0;
    ecritum_error_t error = 0;
    callback_box_t box = {0, 0, ECRITUM_OK};

    create_runtime_and_namespace(&runtime, &namespace_handle);

    CHECK(ecritum_namespace_register_function(namespace_handle, view("notify"), callback_success, &box, destroy_box, &function, &error) == ECRITUM_OK);
    CHECK(function != 0);
    CHECK(error == 0);
    CHECK(box.destroy_calls == 0);
    CHECK(ecritum_test_invoke_function(function, &error) == ECRITUM_OK);
    CHECK(box.callback_calls == 1);
    CHECK(error == 0);

    CHECK(ecritum_function_destroy(&function, &error) == ECRITUM_OK);
    CHECK(function == 0);
    CHECK(box.destroy_calls == 1);
    CHECK(ecritum_namespace_destroy(&namespace_handle, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
}

static void test_callback_error_maps_to_callback_status(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_namespace_t namespace_handle = 0;
    ecritum_function_t function = 0;
    ecritum_error_t error = 0;
    callback_box_t box = {0, 0, ECRITUM_ERROR_INTERNAL};

    create_runtime_and_namespace(&runtime, &namespace_handle);

    CHECK(ecritum_namespace_register_function(namespace_handle, view("fail"), callback_success, &box, destroy_box, &function, &error) == ECRITUM_OK);
    CHECK(ecritum_test_invoke_function(function, &error) == ECRITUM_ERROR_CALLBACK);
    CHECK(box.callback_calls == 1);
    CHECK(error != 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_function_destroy(&function, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_destroy(&namespace_handle, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
}

static void test_failed_registration_does_not_cleanup_user_data(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_namespace_t namespace_handle = 0;
    ecritum_function_t function = 0;
    ecritum_function_t duplicate = 777;
    ecritum_error_t error = 0;
    callback_box_t box = {0, 0, ECRITUM_OK};

    create_runtime_and_namespace(&runtime, &namespace_handle);

    CHECK(ecritum_namespace_register_function(namespace_handle, view("notify"), callback_success, &box, destroy_box, &function, &error) == ECRITUM_OK);
    CHECK(function != 0);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("notify"), callback_success, &box, destroy_box, &duplicate, &error) == ECRITUM_ERROR_ALREADY_EXISTS);
    CHECK(box.destroy_calls == 0);
    CHECK(duplicate == 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);

    CHECK(ecritum_namespace_destroy(&namespace_handle, &error) == ECRITUM_OK);
    CHECK(box.destroy_calls == 1);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
}

static void test_duplicate_namespace_is_runtime_scoped(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_runtime_t other_runtime = 0;
    ecritum_namespace_t namespace_handle = 0;
    ecritum_namespace_t duplicate = 777;
    ecritum_namespace_t other_namespace = 0;
    ecritum_error_t error = 0;

    CHECK(ecritum_runtime_create(empty_config(), &runtime, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_create(empty_config(), &other_runtime, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_create(runtime, view("app"), &namespace_handle, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_create(runtime, view("app"), &duplicate, &error) == ECRITUM_ERROR_ALREADY_EXISTS);
    CHECK(duplicate == 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_namespace_create(other_runtime, view("app"), &other_namespace, &error) == ECRITUM_OK);

    CHECK(ecritum_namespace_destroy(&namespace_handle, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_destroy(&other_namespace, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&other_runtime, &error) == ECRITUM_OK);
}

static void test_namespace_destroy_and_runtime_destroy_cleanup_once(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_namespace_t namespace_handle = 0;
    ecritum_function_t first = 0;
    ecritum_function_t second = 0;
    ecritum_error_t error = 0;
    callback_box_t first_box = {0, 0, ECRITUM_OK};
    callback_box_t second_box = {0, 0, ECRITUM_OK};

    create_runtime_and_namespace(&runtime, &namespace_handle);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("first"), callback_success, &first_box, destroy_box, &first, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("second"), callback_success, &second_box, destroy_box, &second, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_destroy(&namespace_handle, &error) == ECRITUM_OK);
    CHECK(first_box.destroy_calls == 1);
    CHECK(second_box.destroy_calls == 1);
    CHECK(ecritum_function_destroy(&first, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);

    first_box.destroy_calls = 0;
    create_runtime_and_namespace(&runtime, &namespace_handle);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("third"), callback_success, &first_box, destroy_box, &first, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
    CHECK(first_box.destroy_calls == 1);
}

static void test_runtime_destroy_with_live_context_leaves_functions_alive(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_runtime_t original_runtime = 0;
    ecritum_context_t context = 0;
    ecritum_namespace_t namespace_handle = 0;
    ecritum_function_t function = 0;
    ecritum_error_t error = 0;
    callback_box_t box = {0, 0, ECRITUM_OK};

    create_runtime_and_namespace(&runtime, &namespace_handle);
    original_runtime = runtime;
    CHECK(ecritum_context_create(runtime, empty_config(), &context, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("notify"), callback_success, &box, destroy_box, &function, &error) == ECRITUM_OK);

    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_ERROR_CONTEXTS_ALIVE);
    CHECK(runtime == original_runtime);
    CHECK(box.destroy_calls == 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);

    CHECK(ecritum_context_destroy(&context, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
    CHECK(runtime == 0);
    CHECK(box.destroy_calls == 1);
}

static void test_invalid_names_and_nulls(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_namespace_t namespace_handle = 0;
    ecritum_function_t function = 0;
    ecritum_error_t error = 0;
    callback_box_t box = {0, 0, ECRITUM_OK};
    char overlong[256];
    memset(overlong, 'a', sizeof(overlong));

    CHECK(ecritum_runtime_create(empty_config(), &runtime, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_create(runtime, view(""), &namespace_handle, &error) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_namespace_create(runtime, view("app."), &namespace_handle, &error) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_namespace_create(runtime, view("java.tools"), &namespace_handle, &error) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_namespace_create(runtime, bytes_view(overlong, sizeof(overlong)), &namespace_handle, &error) == ECRITUM_ERROR_INPUT_TOO_LARGE);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_namespace_create(runtime, view("app"), NULL, &error) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_namespace_create(runtime, view("app"), &namespace_handle, &error) == ECRITUM_OK);

    CHECK(ecritum_namespace_register_function(namespace_handle, view(""), callback_success, &box, destroy_box, &function, &error) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(box.destroy_calls == 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("truffle"), callback_success, &box, destroy_box, &function, &error) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("ok"), NULL, &box, destroy_box, &function, &error) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("ok"), callback_success, &box, destroy_box, NULL, &error) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(namespace_handle, bytes_view("bad\0name", 8), callback_success, &box, destroy_box, &function, &error) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("ok"), callback_success, &box, NULL, &function, &error) == ECRITUM_OK);
    CHECK(ecritum_function_destroy(&function, &error) == ECRITUM_OK);
    CHECK(box.destroy_calls == 0);

    CHECK(ecritum_namespace_destroy(&namespace_handle, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
}

static void test_wrong_kind_stale_and_runtime_isolation(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_runtime_t other_runtime = 0;
    ecritum_namespace_t namespace_handle = 0;
    ecritum_namespace_t other_namespace = 0;
    ecritum_namespace_t stale_namespace = 0;
    ecritum_function_t function = 0;
    ecritum_error_t error = 0;
    callback_box_t box = {0, 0, ECRITUM_OK};
    callback_box_t other_box = {0, 0, ECRITUM_OK};

    create_runtime_and_namespace(&runtime, &namespace_handle);
    stale_namespace = namespace_handle;
    CHECK(ecritum_namespace_register_function((ecritum_namespace_t)runtime, view("bad"), callback_success, &box, destroy_box, &function, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_namespace_destroy(&namespace_handle, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(stale_namespace, view("bad"), callback_success, &box, destroy_box, &function, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);

    CHECK(ecritum_runtime_create(empty_config(), &other_runtime, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_create(runtime, view("shared"), &namespace_handle, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_create(other_runtime, view("shared"), &other_namespace, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("name"), callback_success, &box, destroy_box, &function, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(other_namespace, view("name"), callback_success, &other_box, destroy_box, &function, &error) == ECRITUM_OK);

    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
    CHECK(box.destroy_calls == 1);
    CHECK(other_box.destroy_calls == 0);
    CHECK(ecritum_runtime_destroy(&other_runtime, &error) == ECRITUM_OK);
    CHECK(other_box.destroy_calls == 1);
}

int main(void) {
    test_registration_success_invoke_and_function_destroy_cleanup();
    test_callback_error_maps_to_callback_status();
    test_failed_registration_does_not_cleanup_user_data();
    test_duplicate_namespace_is_runtime_scoped();
    test_namespace_destroy_and_runtime_destroy_cleanup_once();
    test_runtime_destroy_with_live_context_leaves_functions_alive();
    test_invalid_names_and_nulls();
    test_wrong_kind_stale_and_runtime_isolation();
    return failures == 0 ? 0 : 1;
}
