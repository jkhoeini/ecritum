#include "ecritum.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int ecritum_test_force_teardown_failure;

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static ecritum_bytes_t empty_config(void) {
    ecritum_bytes_t config = {0};
    return config;
}

static ecritum_bytes_t non_empty_config(void) {
    static const uint8_t bytes[] = {'{', '}'};
    ecritum_bytes_t config = {bytes, sizeof(bytes)};
    return config;
}

static void check_error(ecritum_error_t error, int expected_status, const char *expected_operation) {
    int status = 0;
    ecritum_string_view_t category = {0};
    ecritum_string_view_t message = {0};
    ecritum_string_view_t operation = {0};

    CHECK(error != 0);
    CHECK(ecritum_error_status(error, &status) == ECRITUM_OK);
    CHECK(status == expected_status);
    CHECK(ecritum_error_category(error, NULL) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_error_message(error, NULL) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_error_operation(error, NULL) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_error_category(error, &category) == ECRITUM_OK);
    CHECK(category.data != NULL);
    CHECK(category.len > 0);
    CHECK(ecritum_error_message(error, &message) == ECRITUM_OK);
    CHECK(message.data != NULL);
    CHECK(message.len > 0);
    CHECK(ecritum_error_operation(error, &operation) == ECRITUM_OK);
    CHECK(operation.data != NULL);
    CHECK(operation.len == strlen(expected_operation));
    CHECK(strncmp(operation.data, expected_operation, operation.len) == 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(error == 0);
}

static void test_runtime_create_destroy(void) {
    ecritum_runtime_t runtime = 999;
    ecritum_error_t error = 999;

    CHECK(ecritum_runtime_create(empty_config(), &runtime, &error) == ECRITUM_OK);
    CHECK(runtime != 0);
    CHECK(error == 0);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
    CHECK(runtime == 0);
    CHECK(error == 0);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
    CHECK(runtime == 0);
}

static void test_nulls_and_invalid_config(void) {
    ecritum_runtime_t runtime = 999;
    ecritum_error_t error = 999;

    CHECK(ecritum_runtime_create(empty_config(), NULL, &error) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(error != 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(error == 0);

    CHECK(ecritum_runtime_create(non_empty_config(), &runtime, NULL) == ECRITUM_ERROR_INVALID_CONFIG);
    CHECK(runtime == 0);
    CHECK(ecritum_runtime_create(non_empty_config(), &runtime, &error) == ECRITUM_ERROR_INVALID_CONFIG);
    CHECK(runtime == 0);
    check_error(error, ECRITUM_ERROR_INVALID_CONFIG, "runtime_create");

    CHECK(ecritum_runtime_destroy(NULL, &error) == ECRITUM_OK);
    CHECK(error == 0);
}

static void test_context_lifecycle_and_live_parent(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_context_t context = 0;
    ecritum_error_t error = 0;

    CHECK(ecritum_runtime_create(empty_config(), &runtime, &error) == ECRITUM_OK);
    CHECK(ecritum_context_create(runtime, empty_config(), &context, &error) == ECRITUM_OK);
    CHECK(context != 0);

    ecritum_runtime_t runtime_before = runtime;
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_ERROR_CONTEXTS_ALIVE);
    CHECK(runtime == runtime_before);
    check_error(error, ECRITUM_ERROR_CONTEXTS_ALIVE, "runtime_destroy");

    CHECK(ecritum_context_destroy(&context, &error) == ECRITUM_OK);
    CHECK(context == 0);
    CHECK(ecritum_context_destroy(&context, &error) == ECRITUM_OK);
    CHECK(error == 0);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
    CHECK(runtime == 0);
}

static void test_stale_and_wrong_kind_handles(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_runtime_t copied_runtime = 0;
    ecritum_context_t context = 0;
    ecritum_context_t wrong_kind_context = 0;
    ecritum_error_t error = 0;

    CHECK(ecritum_context_create(0, empty_config(), &context, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    CHECK(context == 0);
    check_error(error, ECRITUM_ERROR_INVALID_HANDLE, "context_create");

    CHECK(ecritum_runtime_create(empty_config(), &runtime, &error) == ECRITUM_OK);
    copied_runtime = runtime;
    CHECK(ecritum_context_create(runtime, empty_config(), &context, &error) == ECRITUM_OK);
    wrong_kind_context = context;
    CHECK(ecritum_context_create((ecritum_runtime_t)wrong_kind_context, empty_config(), &context, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    check_error(error, ECRITUM_ERROR_INVALID_HANDLE, "context_create");

    CHECK(ecritum_context_destroy(&wrong_kind_context, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
    CHECK(ecritum_context_create(copied_runtime, empty_config(), &context, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    check_error(error, ECRITUM_ERROR_INVALID_HANDLE, "context_create");
}

static void test_teardown_failure_tombstones_runtime(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_runtime_t copied_runtime = 0;
    ecritum_context_t context = 0;
    ecritum_error_t error = 0;

    CHECK(ecritum_runtime_create(empty_config(), &runtime, &error) == ECRITUM_OK);
    copied_runtime = runtime;
    ecritum_test_force_teardown_failure = 1;
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_ERROR_TEARDOWN_FAILED);
    CHECK(runtime == 0);
    check_error(error, ECRITUM_ERROR_TEARDOWN_FAILED, "runtime_destroy");
    CHECK(ecritum_context_create(copied_runtime, empty_config(), &context, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    check_error(error, ECRITUM_ERROR_INVALID_HANDLE, "context_create");
}

static void test_generation_wrap_does_not_revalidate_stale_runtime(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_runtime_t stale_runtime = 0;
    ecritum_context_t context = 0;
    ecritum_error_t error = 0;

    CHECK(ecritum_runtime_create(empty_config(), &runtime, &error) == ECRITUM_OK);
    stale_runtime = runtime;
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);

    for (uint32_t i = 0; i < UINT16_MAX; i++) {
        CHECK(ecritum_runtime_create(empty_config(), &runtime, &error) == ECRITUM_OK);
        if (runtime == stale_runtime) {
            int status = ecritum_context_create(stale_runtime, empty_config(), &context, &error);
            CHECK(status == ECRITUM_ERROR_INVALID_HANDLE);
            if (status == ECRITUM_OK) {
                CHECK(ecritum_context_destroy(&context, &error) == ECRITUM_OK);
            } else {
                check_error(error, ECRITUM_ERROR_INVALID_HANDLE, "context_create");
            }
            CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
            return;
        }
        CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
    }

    CHECK(ecritum_context_create(stale_runtime, empty_config(), &context, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    check_error(error, ECRITUM_ERROR_INVALID_HANDLE, "context_create");
}

static void test_repeated_lifecycle_cycles(void) {
    for (int i = 0; i < 1000; i++) {
        ecritum_runtime_t runtime = 0;
        ecritum_context_t context = 0;
        ecritum_error_t error = 0;

        CHECK(ecritum_runtime_create(empty_config(), &runtime, &error) == ECRITUM_OK);
        CHECK(ecritum_context_create(runtime, empty_config(), &context, &error) == ECRITUM_OK);
        CHECK(ecritum_context_destroy(&context, &error) == ECRITUM_OK);
        CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
    }
}

int main(void) {
    test_runtime_create_destroy();
    test_nulls_and_invalid_config();
    test_context_lifecycle_and_live_parent();
    test_stale_and_wrong_kind_handles();
    test_teardown_failure_tombstones_runtime();
    test_generation_wrap_does_not_revalidate_stale_runtime();
    test_repeated_lifecycle_cycles();
    return failures == 0 ? 0 : 1;
}
