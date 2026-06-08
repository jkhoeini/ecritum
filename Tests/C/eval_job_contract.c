#include "ecritum.h"

#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int ecritum_test_invoke_function_with_args(
    ecritum_function_t function,
    const ecritum_value_t *arguments,
    size_t argument_count,
    ecritum_value_t *out_result,
    ecritum_error_t *out_error
);

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static void timeout_handler(int signal_number) {
    (void)signal_number;
    const char message[] = "eval_job_contract timed out; probable callback or handle-lock deadlock\n";
    (void)write(STDERR_FILENO, message, sizeof(message) - 1);
    _Exit(124);
}

static ecritum_bytes_t empty_config(void) {
    ecritum_bytes_t config = {0};
    return config;
}

static ecritum_bytes_t bytes(const char *text) {
    ecritum_bytes_t result = {(const uint8_t *)text, strlen(text)};
    return result;
}

static ecritum_string_view_t view(const char *text) {
    ecritum_string_view_t result = {text, strlen(text)};
    return result;
}

static ecritum_string_view_t empty_null_view(void) {
    ecritum_string_view_t result = {NULL, 0};
    return result;
}

static ecritum_value_t make_int(int64_t raw_value) {
    ecritum_value_t value = 0;
    ecritum_error_t error = 0;
    CHECK(ecritum_value_make_int(raw_value, &value, &error) == ECRITUM_OK);
    CHECK(value != 0);
    CHECK(error == 0);
    return value;
}

static ecritum_value_t make_string(const char *raw_value) {
    ecritum_value_t value = 0;
    ecritum_error_t error = 0;
    CHECK(ecritum_value_make_string(view(raw_value), &value, &error) == ECRITUM_OK);
    CHECK(value != 0);
    CHECK(error == 0);
    return value;
}

static void assert_int_value(ecritum_value_t value, int64_t expected) {
    int kind = -1;
    int64_t actual = 0;
    CHECK(ecritum_value_kind(value, &kind) == ECRITUM_OK);
    CHECK(kind == ECRITUM_VALUE_KIND_INT);
    CHECK(ecritum_value_get_int(value, &actual) == ECRITUM_OK);
    CHECK(actual == expected);
}

static void assert_data_value(ecritum_value_t value, const uint8_t *expected, size_t expected_len) {
    int kind = -1;
    ecritum_bytes_t actual = {0};
    CHECK(ecritum_value_kind(value, &kind) == ECRITUM_OK);
    CHECK(kind == ECRITUM_VALUE_KIND_DATA);
    CHECK(ecritum_value_get_data(value, &actual) == ECRITUM_OK);
    CHECK(actual.len == expected_len);
    CHECK(actual.data != NULL);
    CHECK(memcmp(actual.data, expected, expected_len) == 0);
}

typedef int (*error_view_fn_t)(ecritum_error_t, ecritum_string_view_t *);

static void assert_view(ecritum_string_view_t actual, const char *expected) {
    size_t expected_len = strlen(expected);
    CHECK(actual.len == expected_len);
    CHECK(actual.data != NULL);
    CHECK(memcmp(actual.data, expected, expected_len) == 0);
}

static void assert_error_field(ecritum_error_t error, error_view_fn_t accessor, const char *expected) {
    ecritum_string_view_t actual = {0};
    CHECK(accessor(error, &actual) == ECRITUM_OK);
    assert_view(actual, expected);
}

static void create_runtime_and_context(ecritum_runtime_t *runtime, ecritum_context_t *context) {
    ecritum_error_t error = 0;
    CHECK(ecritum_runtime_create(empty_config(), runtime, &error) == ECRITUM_OK);
    CHECK(*runtime != 0);
    CHECK(ecritum_context_create(*runtime, empty_config(), context, &error) == ECRITUM_OK);
    CHECK(*context != 0);
}

static void test_value_scalar_array_and_object_accessors(void) {
    ecritum_value_t null_value = 0;
    ecritum_value_t bool_value = 0;
    ecritum_value_t int_value = 0;
    ecritum_value_t double_value = 0;
    ecritum_value_t string_value = 0;
    ecritum_value_t data_value = 0;
    ecritum_value_t array_value = 0;
    ecritum_value_t object_value = 0;
    ecritum_error_t error = 0;
    int kind = -1;
    int bool_out = 0;
    int64_t int_out = 0;
    double double_out = 0.0;
    size_t count = 0;
    ecritum_string_view_t string_out = {0};
    ecritum_bytes_t data_out = {0};

    CHECK(ecritum_value_make_null(&null_value, &error) == ECRITUM_OK);
    CHECK(ecritum_value_kind(null_value, &kind) == ECRITUM_OK);
    CHECK(kind == ECRITUM_VALUE_KIND_NULL);

    CHECK(ecritum_value_make_bool(1, &bool_value, &error) == ECRITUM_OK);
    CHECK(ecritum_value_kind(bool_value, &kind) == ECRITUM_OK);
    CHECK(kind == ECRITUM_VALUE_KIND_BOOL);
    CHECK(ecritum_value_get_bool(bool_value, &bool_out) == ECRITUM_OK);
    CHECK(bool_out == 1);

    CHECK(ecritum_value_make_int(42, &int_value, &error) == ECRITUM_OK);
    CHECK(ecritum_value_get_int(int_value, &int_out) == ECRITUM_OK);
    CHECK(int_out == 42);

    CHECK(ecritum_value_make_double(3.5, &double_value, &error) == ECRITUM_OK);
    CHECK(ecritum_value_kind(double_value, &kind) == ECRITUM_OK);
    CHECK(kind == ECRITUM_VALUE_KIND_DOUBLE);
    CHECK(ecritum_value_get_double(double_value, &double_out) == ECRITUM_OK);
    CHECK(double_out == 3.5);

    CHECK(ecritum_value_make_string(view("hello"), &string_value, &error) == ECRITUM_OK);
    CHECK(ecritum_value_get_string(string_value, &string_out) == ECRITUM_OK);
    CHECK(string_out.len == 5);
    CHECK(memcmp(string_out.data, "hello", 5) == 0);

    CHECK(ecritum_value_make_data(bytes("blob"), &data_value, &error) == ECRITUM_OK);
    CHECK(ecritum_value_get_data(data_value, &data_out) == ECRITUM_OK);
    CHECK(data_out.len == 4);
    CHECK(memcmp(data_out.data, "blob", 4) == 0);

    ecritum_value_t items[3] = {int_value, string_value, bool_value};
    CHECK(ecritum_value_make_array(items, 3, &array_value, &error) == ECRITUM_OK);
    CHECK(ecritum_value_destroy(&int_value) == ECRITUM_OK);
    CHECK(ecritum_value_destroy(&string_value) == ECRITUM_OK);
    CHECK(ecritum_value_destroy(&bool_value) == ECRITUM_OK);
    CHECK(ecritum_value_count(array_value, &count) == ECRITUM_OK);
    CHECK(count == 3);
    ecritum_value_t copied_item = 0;
    CHECK(ecritum_value_array_get(array_value, 0, &copied_item, &error) == ECRITUM_OK);
    assert_int_value(copied_item, 42);
    CHECK(ecritum_value_destroy(&copied_item) == ECRITUM_OK);

    ecritum_value_t answer = make_int(42);
    ecritum_object_entry_t entries[1] = {{view("answer"), answer}};
    CHECK(ecritum_value_make_object(entries, 1, &object_value, &error) == ECRITUM_OK);
    CHECK(ecritum_value_destroy(&answer) == ECRITUM_OK);
    CHECK(ecritum_value_count(object_value, &count) == ECRITUM_OK);
    CHECK(count == 1);
    ecritum_string_view_t key = {0};
    ecritum_value_t copied_value = 0;
    CHECK(ecritum_value_object_entry(object_value, 0, &key, &copied_value, &error) == ECRITUM_OK);
    CHECK(key.len == 6);
    CHECK(memcmp(key.data, "answer", 6) == 0);
    assert_int_value(copied_value, 42);
    CHECK(ecritum_value_destroy(&copied_value) == ECRITUM_OK);

    CHECK(ecritum_value_destroy(&null_value) == ECRITUM_OK);
    CHECK(ecritum_value_destroy(&double_value) == ECRITUM_OK);
    CHECK(ecritum_value_destroy(&data_value) == ECRITUM_OK);
    CHECK(ecritum_value_destroy(&array_value) == ECRITUM_OK);
    CHECK(ecritum_value_destroy(&object_value) == ECRITUM_OK);
}

static void test_value_invalid_inputs_and_wrong_kind_paths(void) {
    ecritum_error_t error = 0;
    ecritum_value_t string_value = make_string("not-an-int");
    int64_t int_out = 0;
    int kind = 0;
    unsigned char invalid_utf8[] = {0xff};

    CHECK(ecritum_value_make_int(1, NULL, &error) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_value_get_int(string_value, &int_out) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_value_kind(0, &kind) == ECRITUM_ERROR_INVALID_HANDLE);
    CHECK(ecritum_value_kind(0x12345678ULL, &kind) == ECRITUM_ERROR_INVALID_HANDLE);
    ecritum_value_t invalid_string = 0;
    CHECK(ecritum_value_make_string((ecritum_string_view_t){(const char *)invalid_utf8, 1}, &invalid_string, &error) == ECRITUM_ERROR_INVALID_UTF8);
    CHECK(invalid_string == 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);

    ecritum_value_t first = make_int(1);
    ecritum_value_t second = make_int(2);
    ecritum_object_entry_t duplicate_entries[2] = {
        {view("dup"), first},
        {view("dup"), second}
    };
    ecritum_object_entry_t duplicate_empty_entries[2] = {
        {empty_null_view(), first},
        {view(""), second}
    };
    ecritum_value_t object_value = 0;
    CHECK(ecritum_value_make_object(duplicate_entries, 2, &object_value, &error) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(object_value == 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_value_make_object(duplicate_empty_entries, 2, &object_value, &error) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(object_value == 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_value_destroy(&first) == ECRITUM_OK);
    CHECK(ecritum_value_destroy(&second) == ECRITUM_OK);
    CHECK(ecritum_value_destroy(&string_value) == ECRITUM_OK);
}

static void test_eval_job_success_single_drain_and_busy_context(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_context_t context = 0;
    ecritum_job_t job = 0;
    ecritum_job_t blocked_job = 777;
    ecritum_error_t error = 0;
    int state = -1;
    ecritum_value_t result = 0;

    create_runtime_and_context(&runtime, &context);

    CHECK(ecritum_eval_start(context, view("clojure"), bytes("fixture:int:42"), view("smoke.clj"), empty_config(), &job, &error) == ECRITUM_OK);
    CHECK(job != 0);
    CHECK(error == 0);
    CHECK(ecritum_job_poll(job, &state, &error) == ECRITUM_OK);
    CHECK(state == ECRITUM_JOB_SUCCEEDED);
    CHECK(ecritum_eval_start(context, view("clojure"), bytes("fixture:int:7"), view("blocked.clj"), empty_config(), &blocked_job, &error) == ECRITUM_ERROR_BUSY);
    CHECK(blocked_job == 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);

    CHECK(ecritum_job_result(job, &result, &error) == ECRITUM_OK);
    assert_int_value(result, 42);
    CHECK(ecritum_value_destroy(&result) == ECRITUM_OK);
    CHECK(ecritum_job_result(job, &result, &error) == ECRITUM_ERROR_CLOSED);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_job_destroy(&job, &error) == ECRITUM_OK);
    CHECK(job == 0);

    static const uint8_t expected_data[] = {0u, 1u, 2u, 255u};
    CHECK(ecritum_eval_start(context, view("clojure"), bytes("fixture:data"), view("data.clj"), empty_config(), &job, &error) == ECRITUM_OK);
    CHECK(ecritum_job_result(job, &result, &error) == ECRITUM_OK);
    assert_data_value(result, expected_data, sizeof(expected_data));
    CHECK(ecritum_value_destroy(&result) == ECRITUM_OK);
    CHECK(ecritum_job_destroy(&job, &error) == ECRITUM_OK);
    CHECK(job == 0);

#ifndef ECRITUM_RUNTIME_LANE_CORE
    CHECK(ecritum_eval_start(context, view("python"), bytes("fixture:int:42"), view("smoke.py"), empty_config(), &job, &error) == ECRITUM_OK);
    CHECK(ecritum_job_result(job, &result, &error) == ECRITUM_OK);
    assert_int_value(result, 42);
    CHECK(ecritum_value_destroy(&result) == ECRITUM_OK);
    CHECK(ecritum_job_destroy(&job, &error) == ECRITUM_OK);
    CHECK(job == 0);
#endif

    CHECK(ecritum_context_destroy(&context, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
}

#ifdef ECRITUM_RUNTIME_LANE_CORE
static void test_core_lane_rejects_full_only_languages_before_job_creation(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_context_t context = 0;
    ecritum_job_t job = 123;
    ecritum_error_t error = 0;

    create_runtime_and_context(&runtime, &context);

    CHECK(ecritum_eval_start(context, view("javascript"), bytes("fixture:int:42"), view("core.js"), empty_config(), &job, &error) == ECRITUM_ERROR_RUNTIME_UNAVAILABLE);
    CHECK(job == 0);
    CHECK(error != 0);
    assert_error_field(error, ecritum_error_operation, "eval_start");
    assert_error_field(error, ecritum_error_category, "runtime_unavailable");
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);

    job = 123;
    CHECK(ecritum_eval_start(context, view("python"), bytes("fixture:int:42"), view("core.py"), empty_config(), &job, &error) == ECRITUM_ERROR_RUNTIME_UNAVAILABLE);
    CHECK(job == 0);
    CHECK(error != 0);
    assert_error_field(error, ecritum_error_operation, "eval_start");
    assert_error_field(error, ecritum_error_category, "runtime_unavailable");
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);

    job = 123;
    CHECK(ecritum_eval_start(context, view("lua"), bytes("fixture:int:42"), view("core.lua"), empty_config(), &job, &error) == ECRITUM_ERROR_RUNTIME_UNAVAILABLE);
    CHECK(job == 0);
    CHECK(error != 0);
    assert_error_field(error, ecritum_error_operation, "eval_start");
    assert_error_field(error, ecritum_error_category, "runtime_unavailable");
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);

    CHECK(ecritum_context_destroy(&context, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
}
#endif

static void test_job_cancel_wait_destroy_and_invalid_handles(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_context_t context = 0;
    ecritum_job_t job = 0;
    ecritum_error_t error = 0;
    int state = -1;
    ecritum_value_t result = 123;

    create_runtime_and_context(&runtime, &context);
    CHECK(ecritum_eval_start(context, view("clojure"), bytes("fixture:pending"), view("pending.clj"), empty_config(), &job, &error) == ECRITUM_OK);
    CHECK(ecritum_job_poll(job, &state, &error) == ECRITUM_OK);
    CHECK(state == ECRITUM_JOB_RUNNING);
    CHECK(ecritum_job_wait(job, UINT64_MAX, &state, &error) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_context_destroy(&context, &error) == ECRITUM_ERROR_BUSY);
    CHECK(context != 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);

    CHECK(ecritum_job_cancel(job, &error) == ECRITUM_OK);
    CHECK(ecritum_job_cancel(job, &error) == ECRITUM_OK);
    CHECK(ecritum_job_wait(job, 0, &state, &error) == ECRITUM_OK);
    CHECK(state == ECRITUM_JOB_CANCELLED);
    CHECK(ecritum_job_result(job, &result, &error) == ECRITUM_ERROR_CANCELLED);
    CHECK(result == 0);
    CHECK(error != 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_job_destroy(&job, &error) == ECRITUM_OK);

    CHECK(ecritum_job_poll((ecritum_job_t)runtime, &state, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    ecritum_job_t arbitrary = 0x999999ULL;
    CHECK(ecritum_job_destroy(&arbitrary, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);

    CHECK(ecritum_context_destroy(&context, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
}

static void test_undrained_result_destroy_releases_context(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_context_t context = 0;
    ecritum_job_t first = 0;
    ecritum_job_t second = 0;
    ecritum_error_t error = 0;

    create_runtime_and_context(&runtime, &context);
    CHECK(ecritum_eval_start(context, view("clojure"), bytes("fixture:int:1"), view("first.clj"), empty_config(), &first, &error) == ECRITUM_OK);
    CHECK(ecritum_job_destroy(&first, &error) == ECRITUM_OK);
    CHECK(ecritum_eval_start(context, view("clojure"), bytes("fixture:int:2"), view("second.clj"), empty_config(), &second, &error) == ECRITUM_OK);
    CHECK(ecritum_job_destroy(&second, &error) == ECRITUM_OK);
    CHECK(ecritum_context_destroy(&context, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
}

static ecritum_call_t captured_call = 0;

static int callback_add_one(ecritum_call_t call, ecritum_value_t *out_result, ecritum_error_t *out_error, void *user_data) {
    (void)user_data;
    captured_call = call;
    size_t count = 0;
    ecritum_value_t argument = 0;
    int64_t raw = 0;
    CHECK(ecritum_call_argument_count(call, &count, out_error) == ECRITUM_OK);
    CHECK(count == 1);
    CHECK(ecritum_call_argument_count(call, NULL, NULL) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_call_argument(call, 9, &argument, NULL) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_call_argument(call, 0, &argument, out_error) == ECRITUM_OK);
    CHECK(ecritum_value_get_int(argument, &raw) == ECRITUM_OK);
    CHECK(ecritum_value_destroy(&argument) == ECRITUM_OK);
    return ecritum_value_make_int(raw + 1, out_result, out_error);
}

static int callback_returns_status_with_result(ecritum_call_t call, ecritum_value_t *out_result, ecritum_error_t *out_error, void *user_data) {
    (void)call;
    (void)user_data;
    CHECK(ecritum_value_make_int(99, out_result, out_error) == ECRITUM_OK);
    return ECRITUM_ERROR_INTERNAL;
}

static int callback_returns_supplied_handle(ecritum_call_t call, ecritum_value_t *out_result, ecritum_error_t *out_error, void *user_data) {
    (void)call;
    (void)out_error;
    *out_result = *(ecritum_value_t *)user_data;
    return ECRITUM_OK;
}

typedef struct {
    ecritum_runtime_t runtime;
    ecritum_namespace_t namespace_handle;
    ecritum_function_t function;
    int function_destroy_status;
    int namespace_destroy_status;
    int runtime_destroy_status;
} active_destroy_box_t;

static int callback_attempts_active_destroy(ecritum_call_t call, ecritum_value_t *out_result, ecritum_error_t *out_error, void *user_data) {
    (void)call;
    active_destroy_box_t *box = user_data;
    ecritum_function_t function_copy = box->function;
    ecritum_namespace_t namespace_copy = box->namespace_handle;
    ecritum_runtime_t runtime_copy = box->runtime;
    box->function_destroy_status = ecritum_function_destroy(&function_copy, NULL);
    box->namespace_destroy_status = ecritum_namespace_destroy(&namespace_copy, NULL);
    box->runtime_destroy_status = ecritum_runtime_destroy(&runtime_copy, NULL);
    return ecritum_value_make_null(out_result, out_error);
}

typedef struct {
    ecritum_context_t context;
    ecritum_job_t job;
    ecritum_job_t eval_job;
    int eval_status;
    int wait_status;
    int wait_state;
    int cancel_status;
    int context_destroy_status;
} callback_reentry_box_t;

static int callback_reenters_job_and_context(ecritum_call_t call, ecritum_value_t *out_result, ecritum_error_t *out_error, void *user_data) {
    (void)call;
    (void)out_error;
    callback_reentry_box_t *box = user_data;
    ecritum_context_t context_copy = box->context;
    ecritum_error_t local_error = 0;
    box->eval_job = 777;
    box->eval_status = ecritum_eval_start(box->context, view("clojure"), bytes("fixture:int:1"), view("reentrant.clj"), empty_config(), &box->eval_job, &local_error);
    if (local_error != 0) CHECK(ecritum_error_destroy(&local_error) == ECRITUM_OK);
    box->wait_state = -1;
    box->wait_status = ecritum_job_wait(box->job, 0, &box->wait_state, &local_error);
    if (local_error != 0) CHECK(ecritum_error_destroy(&local_error) == ECRITUM_OK);
    box->cancel_status = ecritum_job_cancel(box->job, &local_error);
    if (local_error != 0) CHECK(ecritum_error_destroy(&local_error) == ECRITUM_OK);
    box->context_destroy_status = ecritum_context_destroy(&context_copy, NULL);
    return ecritum_value_make_int(7, out_result, out_error);
}

static void test_callback_arguments_result_precedence_and_active_cleanup(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_namespace_t namespace_handle = 0;
    ecritum_function_t function = 0;
    ecritum_error_t error = 0;
    ecritum_value_t argument = make_int(41);
    ecritum_value_t invalid_argument = 0;
    ecritum_value_t result = 0;
    size_t count = 0;

    CHECK(ecritum_runtime_create(empty_config(), &runtime, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_create(runtime, view("app"), &namespace_handle, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("add_one"), callback_add_one, NULL, NULL, &function, &error) == ECRITUM_OK);
    CHECK(ecritum_test_invoke_function_with_args(function, &argument, 1, &result, &error) == ECRITUM_OK);
    assert_int_value(result, 42);
    CHECK(ecritum_call_argument_count(captured_call, &count, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_call_argument(captured_call, 0, &invalid_argument, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_call_argument_count(0, &count, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_call_argument(0, 0, &invalid_argument, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    ecritum_call_t arbitrary_call = 0x999999ULL;
    CHECK(ecritum_call_argument_count(arbitrary_call, &count, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_call_argument(arbitrary_call, 0, &invalid_argument, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_call_argument_count((ecritum_call_t)runtime, &count, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_call_argument((ecritum_call_t)runtime, 0, &invalid_argument, &error) == ECRITUM_ERROR_INVALID_HANDLE);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_call_argument(captured_call, 0, NULL, NULL) == ECRITUM_ERROR_INVALID_ARGUMENT);
    CHECK(ecritum_value_destroy(&result) == ECRITUM_OK);
    CHECK(ecritum_function_destroy(&function, &error) == ECRITUM_OK);

    CHECK(ecritum_namespace_register_function(namespace_handle, view("bad_result"), callback_returns_status_with_result, NULL, NULL, &function, &error) == ECRITUM_OK);
    result = 777;
    CHECK(ecritum_test_invoke_function_with_args(function, NULL, 0, &result, &error) == ECRITUM_ERROR_CALLBACK);
    CHECK(result == 0);
    CHECK(error != 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_function_destroy(&function, &error) == ECRITUM_OK);

    ecritum_value_t arbitrary = 0x999999ULL;
    CHECK(ecritum_namespace_register_function(namespace_handle, view("arbitrary_result"), callback_returns_supplied_handle, &arbitrary, NULL, &function, &error) == ECRITUM_OK);
    result = 777;
    CHECK(ecritum_test_invoke_function_with_args(function, NULL, 0, &result, &error) == ECRITUM_ERROR_CALLBACK);
    CHECK(result == 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_function_destroy(&function, &error) == ECRITUM_OK);

    ecritum_value_t wrong_kind = (ecritum_value_t)runtime;
    CHECK(ecritum_namespace_register_function(namespace_handle, view("wrong_kind_result"), callback_returns_supplied_handle, &wrong_kind, NULL, &function, &error) == ECRITUM_OK);
    result = 777;
    CHECK(ecritum_test_invoke_function_with_args(function, NULL, 0, &result, &error) == ECRITUM_ERROR_CALLBACK);
    CHECK(result == 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_function_destroy(&function, &error) == ECRITUM_OK);

    ecritum_value_t stale = make_int(9);
    ecritum_value_t stale_copy = stale;
    CHECK(ecritum_value_destroy(&stale) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("stale_result"), callback_returns_supplied_handle, &stale_copy, NULL, &function, &error) == ECRITUM_OK);
    result = 777;
    CHECK(ecritum_test_invoke_function_with_args(function, NULL, 0, &result, &error) == ECRITUM_ERROR_CALLBACK);
    CHECK(result == 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_function_destroy(&function, &error) == ECRITUM_OK);

    active_destroy_box_t box = {runtime, namespace_handle, 0, ECRITUM_OK, ECRITUM_OK, ECRITUM_OK};
    CHECK(ecritum_namespace_register_function(namespace_handle, view("active_destroy"), callback_attempts_active_destroy, &box, NULL, &function, &error) == ECRITUM_OK);
    box.function = function;
    CHECK(ecritum_test_invoke_function_with_args(function, NULL, 0, &result, &error) == ECRITUM_OK);
    CHECK(box.function_destroy_status == ECRITUM_ERROR_BUSY);
    CHECK(box.namespace_destroy_status == ECRITUM_ERROR_BUSY);
    CHECK(box.runtime_destroy_status == ECRITUM_ERROR_REENTRANT_CALL);
    CHECK(ecritum_value_destroy(&result) == ECRITUM_OK);
    CHECK(ecritum_function_destroy(&function, &error) == ECRITUM_OK);

    CHECK(ecritum_value_destroy(&argument) == ECRITUM_OK);
    CHECK(ecritum_namespace_destroy(&namespace_handle, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
}

static void test_callback_reentry_does_not_hold_registry_lock(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_context_t context = 0;
    ecritum_namespace_t namespace_handle = 0;
    ecritum_function_t function = 0;
    ecritum_job_t job = 0;
    ecritum_error_t error = 0;
    ecritum_value_t result = 0;
    int state = -1;

    create_runtime_and_context(&runtime, &context);
    CHECK(ecritum_namespace_create(runtime, view("app"), &namespace_handle, &error) == ECRITUM_OK);
    CHECK(ecritum_eval_start(context, view("clojure"), bytes("fixture:pending"), view("pending.clj"), empty_config(), &job, &error) == ECRITUM_OK);

    callback_reentry_box_t box = {context, job, 0, ECRITUM_OK, ECRITUM_ERROR_INTERNAL, -1, ECRITUM_ERROR_INTERNAL, ECRITUM_OK};
    CHECK(ecritum_namespace_register_function(namespace_handle, view("reenter"), callback_reenters_job_and_context, &box, NULL, &function, &error) == ECRITUM_OK);
    CHECK(ecritum_test_invoke_function_with_args(function, NULL, 0, &result, &error) == ECRITUM_OK);
    assert_int_value(result, 7);
    CHECK(ecritum_value_destroy(&result) == ECRITUM_OK);
    CHECK(box.eval_status == ECRITUM_ERROR_REENTRANT_CALL);
    CHECK(box.eval_job == 0);
    CHECK(box.wait_status == ECRITUM_ERROR_REENTRANT_CALL);
    CHECK(box.wait_state == 0);
    CHECK(box.cancel_status == ECRITUM_ERROR_REENTRANT_CALL);
    CHECK(box.context_destroy_status == ECRITUM_ERROR_REENTRANT_CALL);

    CHECK(ecritum_job_cancel(job, &error) == ECRITUM_OK);
    CHECK(ecritum_job_wait(job, 0, &state, &error) == ECRITUM_OK);
    CHECK(state == ECRITUM_JOB_CANCELLED);
    CHECK(ecritum_job_result(job, &result, &error) == ECRITUM_ERROR_CANCELLED);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_job_destroy(&job, &error) == ECRITUM_OK);
    CHECK(ecritum_function_destroy(&function, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_destroy(&namespace_handle, &error) == ECRITUM_OK);
    CHECK(ecritum_context_destroy(&context, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
}

static void test_registry_exhaustion_does_not_double_free_partial_values(void) {
    enum { test_value_capacity = 4095 };
    ecritum_value_t values[test_value_capacity] = {0};
    ecritum_value_t overflow = 0;
    ecritum_error_t error = 0;

    for (size_t index = 0; index < test_value_capacity; index++) {
        CHECK(ecritum_value_make_null(&values[index], &error) == ECRITUM_OK);
    }

    CHECK(ecritum_value_make_array(&values[0], 1, &overflow, &error) == ECRITUM_ERROR_OUT_OF_MEMORY);
    CHECK(overflow == 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);

    for (size_t index = 0; index < test_value_capacity; index++) {
        CHECK(ecritum_value_destroy(&values[index]) == ECRITUM_OK);
    }
}

int main(void) {
    signal(SIGALRM, timeout_handler);
    alarm(10);
    test_value_scalar_array_and_object_accessors();
    test_value_invalid_inputs_and_wrong_kind_paths();
    test_eval_job_success_single_drain_and_busy_context();
#ifdef ECRITUM_RUNTIME_LANE_CORE
    test_core_lane_rejects_full_only_languages_before_job_creation();
#endif
    test_job_cancel_wait_destroy_and_invalid_handles();
    test_undrained_result_destroy_releases_context();
    test_callback_arguments_result_precedence_and_active_cleanup();
    test_callback_reentry_does_not_hold_registry_lock();
    test_registry_exhaustion_does_not_double_free_partial_values();
    alarm(0);
    return failures == 0 ? 0 : 1;
}
