#include "ecritum.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static ecritum_bytes_t empty_bytes(void) {
    ecritum_bytes_t value = {0};
    return value;
}

static ecritum_bytes_t bytes(const char *text) {
    ecritum_bytes_t value = {(const uint8_t *)text, strlen(text)};
    return value;
}

static ecritum_string_view_t view(const char *text) {
    ecritum_string_view_t value = {text, strlen(text)};
    return value;
}

static char *runtime_config_with_read_root(const char *root) {
    const char *prefix =
        "{\"schemaVersion\":1,\"languages\":[\"clojure\"],"
        "\"policy\":{\"filesystem\":{\"mode\":\"read_only\",\"roots\":[{\"kind\":\"directory\",\"path\":\"";
    const char *suffix =
        "\"}]},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},"
        "\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},"
        "\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}";
    size_t len = strlen(prefix) + strlen(root) + strlen(suffix);
    char *json = malloc(len + 1);
    CHECK(json != NULL);
    if (json == NULL) {
        return NULL;
    }
    snprintf(json, len + 1, "%s%s%s", prefix, root, suffix);
    return json;
}

static char *join_path(const char *root, const char *leaf) {
    size_t len = strlen(root) + 1 + strlen(leaf);
    char *path = malloc(len + 1);
    CHECK(path != NULL);
    if (path == NULL) {
        return NULL;
    }
    snprintf(path, len + 1, "%s/%s", root, leaf);
    return path;
}

static char *eval_source_with_path(const char *prefix, const char *path, const char *suffix) {
    size_t len = strlen(prefix) + strlen(path) + strlen(suffix);
    char *source = malloc(len + 1);
    CHECK(source != NULL);
    if (source == NULL) {
        return NULL;
    }
    snprintf(source, len + 1, "%s%s%s", prefix, path, suffix);
    return source;
}

static int is_terminal_state(int state) {
    return state == ECRITUM_JOB_SUCCEEDED
        || state == ECRITUM_JOB_FAILED
        || state == ECRITUM_JOB_CANCELLED
        || state == ECRITUM_JOB_TIMED_OUT
        || state == ECRITUM_JOB_POISONED;
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

static int wait_for_terminal_job(ecritum_job_t job, int *out_state, ecritum_error_t *error) {
    int status = ecritum_job_wait(job, 5000000000ULL, out_state, error);
    if (status != ECRITUM_OK) {
        return status;
    }
    return is_terminal_state(*out_state) ? ECRITUM_OK : ECRITUM_ERROR_TIMEOUT;
}

static void assert_int(ecritum_value_t value, int64_t expected) {
    if (value == 0) {
        CHECK(value != 0);
        return;
    }
    int kind = -1;
    int64_t actual = 0;
    CHECK(ecritum_value_kind(value, &kind) == ECRITUM_OK);
    CHECK(kind == ECRITUM_VALUE_KIND_INT);
    CHECK(ecritum_value_get_int(value, &actual) == ECRITUM_OK);
    CHECK(actual == expected);
}

static void assert_null(ecritum_value_t value) {
    int kind = -1;
    CHECK(ecritum_value_kind(value, &kind) == ECRITUM_OK);
    CHECK(kind == ECRITUM_VALUE_KIND_NULL);
}

static void assert_bool(ecritum_value_t value, int expected) {
    int kind = -1;
    int actual = -1;
    CHECK(ecritum_value_kind(value, &kind) == ECRITUM_OK);
    CHECK(kind == ECRITUM_VALUE_KIND_BOOL);
    CHECK(ecritum_value_get_bool(value, &actual) == ECRITUM_OK);
    CHECK(actual == expected);
}

static void assert_double(ecritum_value_t value, double expected) {
    int kind = -1;
    double actual = 0.0;
    CHECK(ecritum_value_kind(value, &kind) == ECRITUM_OK);
    CHECK(kind == ECRITUM_VALUE_KIND_DOUBLE);
    CHECK(ecritum_value_get_double(value, &actual) == ECRITUM_OK);
    CHECK(actual == expected);
}

static void assert_string(ecritum_value_t value, const char *expected) {
    int kind = -1;
    ecritum_string_view_t actual = {0};
    size_t expected_len = strlen(expected);
    CHECK(ecritum_value_kind(value, &kind) == ECRITUM_OK);
    CHECK(kind == ECRITUM_VALUE_KIND_STRING);
    CHECK(ecritum_value_get_string(value, &actual) == ECRITUM_OK);
    CHECK(actual.len == expected_len);
    CHECK(memcmp(actual.data, expected, expected_len) == 0);
}

static void write_text_file(const char *path, const char *text) {
    FILE *file = fopen(path, "w");
    CHECK(file != NULL);
    if (file == NULL) {
        return;
    }
    CHECK(fputs(text, file) >= 0);
    CHECK(fclose(file) == 0);
}

static void assert_data(ecritum_value_t value, const uint8_t *expected, size_t expected_len) {
    int kind = -1;
    ecritum_bytes_t actual = {0};
    CHECK(ecritum_value_kind(value, &kind) == ECRITUM_OK);
    CHECK(kind == ECRITUM_VALUE_KIND_DATA);
    CHECK(ecritum_value_get_data(value, &actual) == ECRITUM_OK);
    CHECK(actual.len == expected_len);
    CHECK(memcmp(actual.data, expected, expected_len) == 0);
}

static ecritum_value_t eval_language(ecritum_context_t context, const char *language, const char *source, const char *source_name) {
    ecritum_job_t job = 0;
    ecritum_error_t error = 0;
    ecritum_value_t result = 0;
    int state = -1;

    CHECK(ecritum_eval_start(context, view(language), bytes(source), view(source_name), empty_bytes(), &job, &error) == ECRITUM_OK);
    CHECK(job != 0);
    CHECK(error == 0);
    CHECK(wait_for_terminal_job(job, &state, &error) == ECRITUM_OK);
    CHECK(state == ECRITUM_JOB_SUCCEEDED);
    if (state != ECRITUM_JOB_SUCCEEDED) {
        ecritum_value_t failed_value = 0;
        int failed_status = ecritum_job_result(job, &failed_value, &error);
        fprintf(stderr, "eval failed for %s with state=%d status=%d\n", source, state, failed_status);
        if (error != 0) {
            ecritum_string_view_t message = {0};
            if (ecritum_error_message(error, &message) == ECRITUM_OK) {
                fprintf(stderr, "eval error: %.*s\n", (int)message.len, message.data);
            }
            CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
        }
        CHECK(ecritum_job_destroy(&job, &error) == ECRITUM_OK);
        return 0;
    }
    CHECK(ecritum_job_result(job, &result, &error) == ECRITUM_OK);
    CHECK(result != 0);
    CHECK(error == 0);
    CHECK(ecritum_job_destroy(&job, &error) == ECRITUM_OK);
    CHECK(job == 0);
    return result;
}

static ecritum_value_t eval(ecritum_context_t context, const char *source) {
    return eval_language(context, "clojure", source, "native-smoke.clj");
}

static ecritum_value_t eval_js(ecritum_context_t context, const char *source) {
    return eval_language(context, "javascript", source, "native-smoke.js");
}

static ecritum_value_t eval_lua(ecritum_context_t context, const char *source) {
    return eval_language(context, "lua", source, "native-smoke.lua");
}

static void expect_failed_eval(ecritum_context_t context, const char *source, const char *source_name, const char *category) {
    ecritum_job_t failed_job = 0;
    ecritum_error_t error = 0;
    int state = -1;
    ecritum_value_t failed_value = 99;

    CHECK(ecritum_eval_start(context, view("clojure"), bytes(source), view(source_name), empty_bytes(), &failed_job, &error) == ECRITUM_OK);
    CHECK(failed_job != 0);
    CHECK(wait_for_terminal_job(failed_job, &state, &error) == ECRITUM_OK);
    CHECK(state == ECRITUM_JOB_FAILED);
    CHECK(ecritum_job_result(failed_job, &failed_value, &error) == ECRITUM_ERROR_SCRIPT);
    CHECK(failed_value == 0);
    CHECK(error != 0);
    assert_error_field(error, ecritum_error_operation, "eval");
    assert_error_field(error, ecritum_error_language, "clojure");
    assert_error_field(error, ecritum_error_source_name, source_name);
    assert_error_field(error, ecritum_error_category, category);
    ecritum_string_view_t message = {0};
    CHECK(ecritum_error_message(error, &message) == ECRITUM_OK);
    CHECK(message.len >= strlen(source_name));
    CHECK(memmem(message.data, message.len, source_name, strlen(source_name)) != NULL);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_job_destroy(&failed_job, &error) == ECRITUM_OK);
}

static void expect_failed_eval_status_language(ecritum_context_t context, const char *language, const char *source, const char *source_name, int expected_status, const char *category);

static void expect_failed_eval_status(ecritum_context_t context, const char *source, const char *source_name, int expected_status, const char *category) {
    expect_failed_eval_status_language(context, "clojure", source, source_name, expected_status, category);
}

static void expect_failed_eval_status_language(ecritum_context_t context, const char *language, const char *source, const char *source_name, int expected_status, const char *category) {
    ecritum_job_t failed_job = 0;
    ecritum_error_t error = 0;
    int state = -1;
    ecritum_value_t failed_value = 99;

    CHECK(ecritum_eval_start(context, view(language), bytes(source), view(source_name), empty_bytes(), &failed_job, &error) == ECRITUM_OK);
    CHECK(failed_job != 0);
    CHECK(wait_for_terminal_job(failed_job, &state, &error) == ECRITUM_OK);
    CHECK(state == (expected_status == ECRITUM_ERROR_TIMEOUT ? ECRITUM_JOB_TIMED_OUT : ECRITUM_JOB_FAILED));
    CHECK(ecritum_job_result(failed_job, &failed_value, &error) == expected_status);
    CHECK(failed_value == 0);
    CHECK(error != 0);
    assert_error_field(error, ecritum_error_operation, "eval");
    assert_error_field(error, ecritum_error_language, language);
    assert_error_field(error, ecritum_error_source_name, source_name);
    assert_error_field(error, ecritum_error_category, category);
    ecritum_string_view_t message = {0};
    CHECK(ecritum_error_message(error, &message) == ECRITUM_OK);
    CHECK(message.len >= strlen(source_name));
    CHECK(memmem(message.data, message.len, source_name, strlen(source_name)) != NULL);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(ecritum_job_destroy(&failed_job, &error) == ECRITUM_OK);
}

static void expect_eval_start_error(ecritum_context_t context, ecritum_string_view_t language, ecritum_bytes_t options_json, int expected_status) {
    ecritum_job_t job = 123;
    ecritum_error_t error = 0;
    int status = ecritum_eval_start(context, language, bytes("(+ 1 2)"), view("rejected.clj"), options_json, &job, &error);
    CHECK(status == expected_status);
    CHECK(job == 0);
    CHECK(error != 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
}

static int host_answer(ecritum_call_t call, ecritum_value_t *out_result, ecritum_error_t *out_error, void *user_data) {
    (void)user_data;
    size_t count = 99;
    CHECK(ecritum_call_argument_count(call, &count, out_error) == ECRITUM_OK);
    CHECK(count == 0);
    return ecritum_value_make_int(42, out_result, out_error);
}

static int host_add_one(ecritum_call_t call, ecritum_value_t *out_result, ecritum_error_t *out_error, void *user_data) {
    (void)user_data;
    size_t count = 0;
    ecritum_value_t argument = 0;
    int64_t raw = 0;
    CHECK(ecritum_call_argument_count(call, &count, out_error) == ECRITUM_OK);
    CHECK(count == 1);
    CHECK(ecritum_call_argument(call, 0, &argument, out_error) == ECRITUM_OK);
    CHECK(ecritum_value_get_int(argument, &raw) == ECRITUM_OK);
    CHECK(ecritum_value_destroy(&argument) == ECRITUM_OK);
    return ecritum_value_make_int(raw + 1, out_result, out_error);
}

static int host_blob(ecritum_call_t call, ecritum_value_t *out_result, ecritum_error_t *out_error, void *user_data) {
    (void)call;
    (void)user_data;
    const uint8_t raw[] = {0, 1, 2, 255};
    ecritum_bytes_t data = {raw, sizeof(raw)};
    return ecritum_value_make_data(data, out_result, out_error);
}

static int host_fail(ecritum_call_t call, ecritum_value_t *out_result, ecritum_error_t *out_error, void *user_data) {
    (void)call;
    (void)out_result;
    (void)out_error;
    (void)user_data;
    return ECRITUM_ERROR_CALLBACK;
}

int main(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_context_t context = 0;
    ecritum_namespace_t namespace_handle = 0;
    ecritum_function_t answer_function = 0;
    ecritum_function_t add_one_function = 0;
    ecritum_function_t blob_function = 0;
    ecritum_function_t fail_function = 0;
    ecritum_error_t error = 0;

    CHECK(ecritum_runtime_create(empty_bytes(), &runtime, &error) == ECRITUM_OK);
    CHECK(runtime != 0);
    CHECK(ecritum_namespace_create(runtime, view("app"), &namespace_handle, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("answer"), host_answer, NULL, NULL, &answer_function, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("add_one"), host_add_one, NULL, NULL, &add_one_function, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("blob"), host_blob, NULL, NULL, &blob_function, &error) == ECRITUM_OK);
    CHECK(ecritum_namespace_register_function(namespace_handle, view("fail"), host_fail, NULL, NULL, &fail_function, &error) == ECRITUM_OK);
    CHECK(ecritum_context_create(runtime, empty_bytes(), &context, &error) == ECRITUM_OK);
    CHECK(context != 0);

    ecritum_value_t scalar = eval(context, "(+ 40 2)");
    assert_int(scalar, 42);
    CHECK(ecritum_value_destroy(&scalar) == ECRITUM_OK);

    ecritum_value_t nil_value = eval(context, "nil");
    assert_null(nil_value);
    CHECK(ecritum_value_destroy(&nil_value) == ECRITUM_OK);

    ecritum_value_t bool_value = eval(context, "true");
    assert_bool(bool_value, 1);
    CHECK(ecritum_value_destroy(&bool_value) == ECRITUM_OK);

    ecritum_value_t string_value = eval(context, "\"hello\"");
    assert_string(string_value, "hello");
    CHECK(ecritum_value_destroy(&string_value) == ECRITUM_OK);

    ecritum_value_t double_value = eval(context, "3.5");
    assert_double(double_value, 3.5);
    CHECK(ecritum_value_destroy(&double_value) == ECRITUM_OK);

    ecritum_value_t vector = eval(context, "[1 2 3]");
    int kind = -1;
    size_t count = 0;
    if (vector != 0) {
        CHECK(ecritum_value_kind(vector, &kind) == ECRITUM_OK);
        CHECK(kind == ECRITUM_VALUE_KIND_ARRAY);
        CHECK(ecritum_value_count(vector, &count) == ECRITUM_OK);
        CHECK(count == 3);
        ecritum_value_t first = 0;
        CHECK(ecritum_value_array_get(vector, 0, &first, &error) == ECRITUM_OK);
        assert_int(first, 1);
        CHECK(ecritum_value_destroy(&first) == ECRITUM_OK);
        CHECK(ecritum_value_destroy(&vector) == ECRITUM_OK);
    }

    ecritum_value_t object = eval(context, "{\"answer\" 42}");
    if (object != 0) {
        CHECK(ecritum_value_kind(object, &kind) == ECRITUM_OK);
        CHECK(kind == ECRITUM_VALUE_KIND_OBJECT);
        CHECK(ecritum_value_count(object, &count) == ECRITUM_OK);
        CHECK(count == 1);
        ecritum_string_view_t key = {0};
        ecritum_value_t object_value = 0;
        CHECK(ecritum_value_object_entry(object, 0, &key, &object_value, &error) == ECRITUM_OK);
        CHECK(key.len == 6);
        CHECK(memcmp(key.data, "answer", 6) == 0);
        assert_int(object_value, 42);
        CHECK(ecritum_value_destroy(&object_value) == ECRITUM_OK);
        CHECK(ecritum_value_destroy(&object) == ECRITUM_OK);
    }

    ecritum_value_t json_value = eval(context, "(ecritum.json/write-string {\"b\" 2 \"a\" 1})");
    assert_string(json_value, "{\"a\":1,\"b\":2}");
    CHECK(ecritum_value_destroy(&json_value) == ECRITUM_OK);

    ecritum_value_t time_value = eval(context, "(ecritum.time/format-instant (ecritum.time/parse-instant \"2026-06-05T00:00:00Z\"))");
    assert_string(time_value, "2026-06-05T00:00:00Z");
    CHECK(ecritum_value_destroy(&time_value) == ECRITUM_OK);

    ecritum_value_t require_value = eval(context, "(do (require '[ecritum.json :as json]) (json/write-string {\"a\" 1}))");
    assert_string(require_value, "{\"a\":1}");
    CHECK(ecritum_value_destroy(&require_value) == ECRITUM_OK);

    expect_failed_eval(context, "(/ 1 0)", "runtime-source.clj", "runtime");
    expect_failed_eval(context, "(defn", "syntax-source.clj", "syntax");
    expect_failed_eval_status(context, "(ecritum.time/now)", "time-denied.clj", ECRITUM_ERROR_PERMISSION_DENIED, "permission");
    expect_failed_eval_status(context, "(ecritum.fs/read-text \"/tmp/ecritum\")", "fs-denied.clj", ECRITUM_ERROR_PERMISSION_DENIED, "permission");
    expect_failed_eval_status(context, "(ecritum.http/request {\"url\" \"https://example.com\"})", "http-denied.clj", ECRITUM_ERROR_PERMISSION_DENIED, "permission");

    ecritum_value_t host_value = eval(context, "(app/answer)");
    assert_int(host_value, 42);
    CHECK(ecritum_value_destroy(&host_value) == ECRITUM_OK);

    ecritum_value_t host_arg_value = eval(context, "(app/add_one 41)");
    assert_int(host_arg_value, 42);
    CHECK(ecritum_value_destroy(&host_arg_value) == ECRITUM_OK);

    ecritum_value_t host_data = eval(context, "(app/blob)");
    const uint8_t expected_data[] = {0, 1, 2, 255};
    assert_data(host_data, expected_data, sizeof(expected_data));
    CHECK(ecritum_value_destroy(&host_data) == ECRITUM_OK);

    ecritum_value_t js_scalar = eval_js(context, "40 + 2");
    assert_int(js_scalar, 42);
    CHECK(ecritum_value_destroy(&js_scalar) == ECRITUM_OK);

    ecritum_value_t js_data = eval_js(context, "new Uint8Array([0, 1, 2, 255])");
    assert_data(js_data, expected_data, sizeof(expected_data));
    CHECK(ecritum_value_destroy(&js_data) == ECRITUM_OK);

    ecritum_value_t js_json = eval_js(context, "ecritum.json.writeString({b: 2, a: 1})");
    assert_string(js_json, "{\"a\":1,\"b\":2}");
    CHECK(ecritum_value_destroy(&js_json) == ECRITUM_OK);

    ecritum_value_t js_time = eval_js(context, "ecritum.time.formatInstant(ecritum.time.parseInstant('2026-06-05T00:00:00Z'))");
    assert_string(js_time, "2026-06-05T00:00:00Z");
    CHECK(ecritum_value_destroy(&js_time) == ECRITUM_OK);

    ecritum_value_t js_host_value = eval_js(context, "ecritum.app.answer()");
    assert_int(js_host_value, 42);
    CHECK(ecritum_value_destroy(&js_host_value) == ECRITUM_OK);

    ecritum_value_t js_host_arg_value = eval_js(context, "ecritum.app.add_one(41)");
    assert_int(js_host_arg_value, 42);
    CHECK(ecritum_value_destroy(&js_host_arg_value) == ECRITUM_OK);

    expect_failed_eval_status_language(context, "javascript", "ecritum.app.fail()", "callback-source.js", ECRITUM_ERROR_CALLBACK, "callback");
    expect_failed_eval_status_language(context, "javascript", "Java.type('java.lang.System')", "permission-source.js", ECRITUM_ERROR_PERMISSION_DENIED, "permission");
    expect_failed_eval_status_language(context, "javascript", "(async function(){ return 42; })()", "promise-source.js", ECRITUM_ERROR_SCRIPT, "runtime");

    ecritum_value_t lua_scalar = eval_lua(context, "return 40 + 2");
    assert_int(lua_scalar, 42);
    CHECK(ecritum_value_destroy(&lua_scalar) == ECRITUM_OK);

    ecritum_value_t lua_nil = eval_lua(context, "return nil");
    assert_null(lua_nil);
    CHECK(ecritum_value_destroy(&lua_nil) == ECRITUM_OK);

    ecritum_value_t lua_bool = eval_lua(context, "return true");
    assert_bool(lua_bool, 1);
    CHECK(ecritum_value_destroy(&lua_bool) == ECRITUM_OK);

    ecritum_value_t lua_string = eval_lua(context, "return 'hello'");
    assert_string(lua_string, "hello");
    CHECK(ecritum_value_destroy(&lua_string) == ECRITUM_OK);

    ecritum_value_t lua_double = eval_lua(context, "return 3.5");
    assert_double(lua_double, 3.5);
    CHECK(ecritum_value_destroy(&lua_double) == ECRITUM_OK);

    ecritum_value_t lua_array = eval_lua(context, "return {1, 2, 3}");
    if (lua_array != 0) {
        CHECK(ecritum_value_kind(lua_array, &kind) == ECRITUM_OK);
        CHECK(kind == ECRITUM_VALUE_KIND_ARRAY);
        CHECK(ecritum_value_count(lua_array, &count) == ECRITUM_OK);
        CHECK(count == 3);
        ecritum_value_t first = 0;
        CHECK(ecritum_value_array_get(lua_array, 0, &first, &error) == ECRITUM_OK);
        assert_int(first, 1);
        CHECK(ecritum_value_destroy(&first) == ECRITUM_OK);
        CHECK(ecritum_value_destroy(&lua_array) == ECRITUM_OK);
    }

    ecritum_value_t lua_object = eval_lua(context, "return {answer = 42}");
    if (lua_object != 0) {
        CHECK(ecritum_value_kind(lua_object, &kind) == ECRITUM_OK);
        CHECK(kind == ECRITUM_VALUE_KIND_OBJECT);
        CHECK(ecritum_value_count(lua_object, &count) == ECRITUM_OK);
        CHECK(count == 1);
        ecritum_string_view_t key = {0};
        ecritum_value_t object_value = 0;
        CHECK(ecritum_value_object_entry(lua_object, 0, &key, &object_value, &error) == ECRITUM_OK);
        CHECK(key.len == 6);
        CHECK(memcmp(key.data, "answer", 6) == 0);
        assert_int(object_value, 42);
        CHECK(ecritum_value_destroy(&object_value) == ECRITUM_OK);
        CHECK(ecritum_value_destroy(&lua_object) == ECRITUM_OK);
    }

    ecritum_value_t lua_json = eval_lua(context, "return ecritum.json.writeString({b = 2, a = 1})");
    assert_string(lua_json, "{\"a\":1,\"b\":2}");
    CHECK(ecritum_value_destroy(&lua_json) == ECRITUM_OK);

    ecritum_value_t lua_time = eval_lua(context, "return ecritum.time.formatInstant(ecritum.time.parseInstant('2026-06-05T00:00:00Z'))");
    assert_string(lua_time, "2026-06-05T00:00:00Z");
    CHECK(ecritum_value_destroy(&lua_time) == ECRITUM_OK);

    ecritum_value_t lua_host_value = eval_lua(context, "return ecritum.app.answer()");
    assert_int(lua_host_value, 42);
    CHECK(ecritum_value_destroy(&lua_host_value) == ECRITUM_OK);

    ecritum_value_t lua_host_arg_value = eval_lua(context, "return ecritum.app.add_one(41)");
    assert_int(lua_host_arg_value, 42);
    CHECK(ecritum_value_destroy(&lua_host_arg_value) == ECRITUM_OK);

    expect_failed_eval_status_language(context, "lua", "return ecritum.app.fail()", "callback-source.lua", ECRITUM_ERROR_CALLBACK, "callback");
    expect_failed_eval_status_language(context, "lua", "io.open('/tmp/ecritum')", "permission-source.lua", ECRITUM_ERROR_PERMISSION_DENIED, "permission");
    expect_failed_eval_status_language(context, "lua", "string.dump(function() end)", "dump-source.lua", ECRITUM_ERROR_PERMISSION_DENIED, "permission");
    expect_failed_eval_status_language(context, "lua", "coroutine.resume(coroutine.create(function() while true do end end))", "coroutine-source.lua", ECRITUM_ERROR_PERMISSION_DENIED, "permission");
    expect_failed_eval_status_language(context, "lua", "while true do end", "timeout-source.lua", ECRITUM_ERROR_TIMEOUT, "timeout");

    expect_failed_eval_status(context, "(app/fail)", "callback-source.clj", ECRITUM_ERROR_CALLBACK, "callback");
    expect_eval_start_error(context, view("python"), empty_bytes(), ECRITUM_ERROR_RUNTIME_UNAVAILABLE);
    expect_eval_start_error(context, view("clojure"), bytes("{\"rawSciOption\":true}"), ECRITUM_ERROR_INVALID_ARGUMENT);

    ecritum_runtime_t js_runtime = 0;
    ecritum_context_t js_context = 0;
    ecritum_bytes_t js_only_config = bytes(
        "{\"schemaVersion\":1,\"languages\":[\"javascript\"],"
        "\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},"
        "\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},"
        "\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}"
    );
    CHECK(ecritum_runtime_create(js_only_config, &js_runtime, &error) == ECRITUM_OK);
    CHECK(ecritum_context_create(js_runtime, empty_bytes(), &js_context, &error) == ECRITUM_OK);
    ecritum_value_t js_only_value = eval_language(js_context, "javascript", "40 + 2", "js-only.js");
    assert_int(js_only_value, 42);
    CHECK(ecritum_value_destroy(&js_only_value) == ECRITUM_OK);
    expect_eval_start_error(js_context, view("clojure"), empty_bytes(), ECRITUM_ERROR_PERMISSION_DENIED);
    CHECK(ecritum_context_destroy(&js_context, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&js_runtime, &error) == ECRITUM_OK);

    ecritum_runtime_t lua_runtime = 0;
    ecritum_context_t lua_context = 0;
    ecritum_bytes_t lua_only_config = bytes(
        "{\"schemaVersion\":1,\"languages\":[\"lua\"],"
        "\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},"
        "\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},"
        "\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}"
    );
    CHECK(ecritum_runtime_create(lua_only_config, &lua_runtime, &error) == ECRITUM_OK);
    CHECK(ecritum_context_create(lua_runtime, empty_bytes(), &lua_context, &error) == ECRITUM_OK);
    ecritum_value_t lua_only_value = eval_language(lua_context, "lua", "return 40 + 2", "lua-only.lua");
    assert_int(lua_only_value, 42);
    CHECK(ecritum_value_destroy(&lua_only_value) == ECRITUM_OK);
    expect_eval_start_error(lua_context, view("clojure"), empty_bytes(), ECRITUM_ERROR_PERMISSION_DENIED);
    CHECK(ecritum_context_destroy(&lua_context, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&lua_runtime, &error) == ECRITUM_OK);

    CHECK(ecritum_context_destroy(&context, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);

    char root_template[] = "/tmp/ecritumfacadesXXXXXX";
    char *root = mkdtemp(root_template);
    CHECK(root != NULL);
    if (root != NULL) {
        char *inside = join_path(root, "inside.txt");
        char *outside = join_path("/tmp", "ecritum-facades-outside.txt");
        char *link_path = join_path(root, "outside-link.txt");
        write_text_file(inside, "inside-data");
        write_text_file(outside, "outside-data");
        CHECK(symlink(outside, link_path) == 0);

        char *fs_config = runtime_config_with_read_root(root);
        ecritum_runtime_t fs_runtime = 0;
        ecritum_context_t fs_context = 0;
        CHECK(ecritum_runtime_create(bytes(fs_config), &fs_runtime, &error) == ECRITUM_OK);
        CHECK(ecritum_context_create(fs_runtime, empty_bytes(), &fs_context, &error) == ECRITUM_OK);

        char *read_inside_source = eval_source_with_path("(ecritum.fs/read-text \"", inside, "\")");
        ecritum_value_t read_inside = eval(fs_context, read_inside_source);
        assert_string(read_inside, "inside-data");
        CHECK(ecritum_value_destroy(&read_inside) == ECRITUM_OK);

        char *bytes_inside_source = eval_source_with_path("(ecritum.fs/read-bytes \"", inside, "\")");
        ecritum_value_t bytes_inside = eval(fs_context, bytes_inside_source);
        assert_data(bytes_inside, (const uint8_t *)"inside-data", strlen("inside-data"));
        CHECK(ecritum_value_destroy(&bytes_inside) == ECRITUM_OK);

        char *exists_inside_source = eval_source_with_path("(ecritum.fs/exists? \"", inside, "\")");
        ecritum_value_t exists_inside = eval(fs_context, exists_inside_source);
        assert_bool(exists_inside, 1);
        CHECK(ecritum_value_destroy(&exists_inside) == ECRITUM_OK);

        char *outside_source = eval_source_with_path("(ecritum.fs/read-text \"", outside, "\")");
        expect_failed_eval_status(fs_context, outside_source, "fs-outside.clj", ECRITUM_ERROR_PERMISSION_DENIED, "permission");

        char *traversal_source = eval_source_with_path("(ecritum.fs/read-text \"", root, "/../ecritum-facades-outside.txt\")");
        expect_failed_eval_status(fs_context, traversal_source, "fs-traversal.clj", ECRITUM_ERROR_PERMISSION_DENIED, "permission");

        char *link_source = eval_source_with_path("(ecritum.fs/read-text \"", link_path, "\")");
        expect_failed_eval_status(fs_context, link_source, "fs-symlink.clj", ECRITUM_ERROR_PERMISSION_DENIED, "permission");

        CHECK(ecritum_context_destroy(&fs_context, &error) == ECRITUM_OK);
        CHECK(ecritum_runtime_destroy(&fs_runtime, &error) == ECRITUM_OK);

        free(read_inside_source);
        free(bytes_inside_source);
        free(exists_inside_source);
        free(outside_source);
        free(traversal_source);
        free(link_source);
        free(fs_config);
        unlink(link_path);
        unlink(inside);
        unlink(outside);
        rmdir(root);
        free(inside);
        free(outside);
        free(link_path);
    }

    if (failures != 0) {
        fprintf(stderr, "native_eval_smoke failures=%d\n", failures);
        return 1;
    }
    return 0;
}
