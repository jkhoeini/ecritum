#include "ecritum.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static ecritum_bytes_t config_string(const char *json) {
    ecritum_bytes_t config = {(const uint8_t *)json, strlen(json)};
    return config;
}

static ecritum_bytes_t config_bytes(const uint8_t *bytes, size_t len) {
    ecritum_bytes_t config = {bytes, len};
    return config;
}

static ecritum_bytes_t null_data_config(size_t len) {
    ecritum_bytes_t config = {NULL, len};
    return config;
}

static void check_error(ecritum_error_t error, int expected_status, const char *expected_operation) {
    int status = 0;
    ecritum_string_view_t operation = {0};

    CHECK(error != 0);
    CHECK(ecritum_error_status(error, &status) == ECRITUM_OK);
    CHECK(status == expected_status);
    CHECK(ecritum_error_operation(error, &operation) == ECRITUM_OK);
    CHECK(operation.data != NULL);
    CHECK(operation.len == strlen(expected_operation));
    CHECK(strncmp(operation.data, expected_operation, operation.len) == 0);
    CHECK(ecritum_error_destroy(&error) == ECRITUM_OK);
    CHECK(error == 0);
}

static const char runtime_config_with_grants[] =
    "{"
    "\"schemaVersion\":1,"
    "\"languages\":[\"javascript\",\"clojure\"],"
    "\"policy\":{"
    "\"filesystem\":{\"mode\":\"read_write\",\"roots\":[{\"kind\":\"directory\",\"path\":\"/tmp/ecritum/scripts\"},{\"kind\":\"directory\",\"path\":\"/tmp/ecritum/data\"}]},"
    "\"network\":{\"mode\":\"allowed\",\"rules\":[{\"scheme\":\"https\",\"host\":\"api.example.com\",\"port\":443},{\"scheme\":\"https\",\"host\":\"upload.example.com\",\"port\":443}]},"
    "\"process\":{\"mode\":\"allowed\",\"commands\":[{\"path\":\"/usr/bin/true\"}]},"
    "\"environment\":{\"mode\":\"allowed\",\"keys\":[\"ECRITUM_MODE\",\"ECRITUM_TOKEN\"]},"
    "\"clock\":{\"mode\":\"allowed\"},"
    "\"random\":{\"mode\":\"allowed\"},"
    "\"log\":{\"mode\":\"allowed\"}"
    "},"
    "\"diagnostics\":{\"mode\":\"redacted\"},"
    "\"resourceLimits\":{\"executionTimeoutNanos\":1000000,\"maxOutputBytes\":1024}"
    "}";

static const char context_narrowing_config[] =
    "{"
    "\"schemaVersion\":1,"
    "\"policy\":{"
    "\"filesystem\":{\"mode\":\"read_only\",\"roots\":[{\"kind\":\"directory\",\"path\":\"/tmp/ecritum/scripts\"}]},"
    "\"network\":{\"mode\":\"allowed\",\"rules\":[{\"scheme\":\"https\",\"host\":\"api.example.com\",\"port\":443}]},"
    "\"process\":{\"mode\":\"denied\"},"
    "\"environment\":{\"mode\":\"allowed\",\"keys\":[\"ECRITUM_MODE\"]},"
    "\"clock\":{\"mode\":\"denied\"},"
    "\"random\":{\"mode\":\"denied\"},"
    "\"log\":{\"mode\":\"allowed\"}"
    "},"
    "\"resourceLimits\":{\"maxOutputBytes\":512}"
    "}";

static const char context_widening_config[] =
    "{"
    "\"schemaVersion\":1,"
    "\"policy\":{\"network\":{\"mode\":\"allowed\",\"rules\":[{\"scheme\":\"https\",\"host\":\"api.example.com\",\"port\":443}]}}"
    "}";

static char *repeat_char(char value, size_t count) {
    char *buffer = malloc(count + 1);
    CHECK(buffer != NULL);
    if (buffer == NULL) {
        return NULL;
    }
    memset(buffer, value, count);
    buffer[count] = '\0';
    return buffer;
}

static char *runtime_config_with_language_list(const char *languages_json) {
    const char *prefix =
        "{\"schemaVersion\":1,\"languages\":";
    const char *suffix =
        ",\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}";
    size_t len = strlen(prefix) + strlen(languages_json) + strlen(suffix);
    char *json = malloc(len + 1);
    CHECK(json != NULL);
    if (json == NULL) {
        return NULL;
    }
    snprintf(json, len + 1, "%s%s%s", prefix, languages_json, suffix);
    return json;
}

static char *runtime_config_with_filesystem_path(const char *path) {
    const char *prefix =
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"read_only\",\"roots\":[{\"kind\":\"directory\",\"path\":\"";
    const char *suffix =
        "\"}]},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}";
    size_t len = strlen(prefix) + strlen(path) + strlen(suffix);
    char *json = malloc(len + 1);
    CHECK(json != NULL);
    if (json == NULL) {
        return NULL;
    }
    snprintf(json, len + 1, "%s%s%s", prefix, path, suffix);
    return json;
}

static void expect_runtime_create(ecritum_bytes_t config, int expected_status) {
    ecritum_runtime_t runtime = 777;
    ecritum_error_t error = 0;
    int status = ecritum_runtime_create(config, &runtime, &error);

    CHECK(status == expected_status);
    if (expected_status == ECRITUM_OK) {
        CHECK(runtime != 0);
        CHECK(error == 0);
        CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
        CHECK(runtime == 0);
    } else {
        CHECK(runtime == 0);
        check_error(error, expected_status, "runtime_create");
    }
}

static void test_empty_runtime_config_variants_remain_valid(void) {
    static const uint8_t ignored = 0;
    ecritum_bytes_t non_null_empty = {&ignored, 0};

    expect_runtime_create(empty_config(), ECRITUM_OK);
    expect_runtime_create(non_null_empty, ECRITUM_OK);
}

static void test_valid_non_empty_runtime_config_creates_runtime(void) {
    expect_runtime_create(config_string(runtime_config_with_grants), ECRITUM_OK);
}

static void test_validation_statuses_are_deterministic(void) {
    static const uint8_t invalid_utf8[] = {0xff};
    uint8_t *too_large = malloc(65537);
    CHECK(too_large != NULL);
    if (too_large != NULL) {
        memset(too_large, ' ', 65537);
        expect_runtime_create(config_bytes(too_large, 65537), ECRITUM_ERROR_INPUT_TOO_LARGE);
        free(too_large);
    }

    expect_runtime_create(null_data_config(1), ECRITUM_ERROR_INVALID_ARGUMENT);
    expect_runtime_create(config_bytes(invalid_utf8, sizeof(invalid_utf8)), ECRITUM_ERROR_INVALID_UTF8);
    expect_runtime_create(config_string("{\"schemaVersion\":2}"), ECRITUM_ERROR_UNSUPPORTED_CONFIG_VERSION);
    expect_runtime_create(config_string("{\"schemaVersion\":1,\"languages\":[],\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}"), ECRITUM_ERROR_INVALID_CONFIG);
    expect_runtime_create(config_string("{\"schemaVersion\":1,\"schemaVersion\":1}"), ECRITUM_ERROR_INVALID_CONFIG);
    expect_runtime_create(config_string("{\"schemaVersion\":1,\"languages\":null}"), ECRITUM_ERROR_INVALID_CONFIG);
}

static void test_null_values_fail_closed_for_v1_fields(void) {
    const char *null_configs[] = {
        "{\"schemaVersion\":null}",
        "{\"schemaVersion\":1,\"languages\":null}",
        "{\"schemaVersion\":1,\"languages\":[null]}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":null,\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":null,\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":null},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"read_only\",\"roots\":null},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"read_only\",\"roots\":[{\"kind\":null,\"path\":\"/tmp/ecritum/scripts\"}]},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"read_only\",\"roots\":[{\"kind\":\"directory\",\"path\":null}]},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":null,\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":null},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"allowed\",\"rules\":null},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"allowed\",\"rules\":[{\"scheme\":null,\"host\":\"api.example.com\",\"port\":443}]},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"allowed\",\"rules\":[{\"scheme\":\"https\",\"host\":null,\"port\":443}]},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"allowed\",\"rules\":[{\"scheme\":\"https\",\"host\":\"api.example.com\",\"port\":null}]},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":null,\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":null},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"allowed\",\"commands\":null},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"allowed\",\"commands\":[{\"path\":null}]},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":null,\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":null},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"allowed\",\"keys\":null},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"allowed\",\"keys\":[null]},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":null,\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":null},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":null,\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":null},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":null},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":null}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":null,\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":null},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":null}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{\"executionTimeoutNanos\":null}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{\"maxInputBytes\":null}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{\"maxOutputBytes\":null}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{\"maxStackDepth\":null}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{\"maxHeapBytes\":null}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{\"maxCallbackQueueLength\":null}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{\"callbackTimeoutNanos\":null}}",
    };

    for (size_t index = 0; index < sizeof(null_configs) / sizeof(null_configs[0]); index++) {
        expect_runtime_create(config_string(null_configs[index]), ECRITUM_ERROR_INVALID_CONFIG);
    }
}

static void test_context_empty_config_inherits_runtime_effective_config(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_context_t context = 0;
    ecritum_error_t error = 0;

    CHECK(ecritum_runtime_create(config_string(runtime_config_with_grants), &runtime, &error) == ECRITUM_OK);
    CHECK(runtime != 0);
    CHECK(ecritum_context_create(runtime, empty_config(), &context, &error) == ECRITUM_OK);
    CHECK(context != 0);
    CHECK(ecritum_context_destroy(&context, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
}

static void test_context_narrowing_accepts_subsets(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_context_t context = 0;
    ecritum_error_t error = 0;

    CHECK(ecritum_runtime_create(config_string(runtime_config_with_grants), &runtime, &error) == ECRITUM_OK);
    CHECK(ecritum_context_create(runtime, config_string(context_narrowing_config), &context, &error) == ECRITUM_OK);
    CHECK(context != 0);
    CHECK(ecritum_context_destroy(&context, &error) == ECRITUM_OK);
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
}

static void test_context_widening_fails_closed(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_context_t context = 777;
    ecritum_error_t error = 0;

    CHECK(ecritum_runtime_create(empty_config(), &runtime, &error) == ECRITUM_OK);
    CHECK(ecritum_context_create(runtime, config_string(context_widening_config), &context, &error) == ECRITUM_ERROR_INVALID_CONFIG);
    CHECK(context == 0);
    check_error(error, ECRITUM_ERROR_INVALID_CONFIG, "context_create");
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
}

static void test_schema_version_edges_fail_closed(void) {
    expect_runtime_create(config_string("{\"schemaVersion\":0}"), ECRITUM_ERROR_INVALID_CONFIG);
    expect_runtime_create(config_string("{\"schemaVersion\":-1}"), ECRITUM_ERROR_INVALID_CONFIG);
    expect_runtime_create(config_string("{\"schemaVersion\":1.0}"), ECRITUM_ERROR_INVALID_CONFIG);
    expect_runtime_create(config_string("{\"schemaVersion\":1e0}"), ECRITUM_ERROR_INVALID_CONFIG);
    expect_runtime_create(config_string("{\"schemaVersion\":4294967296}"), ECRITUM_ERROR_INVALID_CONFIG);
    expect_runtime_create(config_string("{\"schemaVersion\":\"1\"}"), ECRITUM_ERROR_INVALID_CONFIG);
    expect_runtime_create(config_string("{\"schemaVersion\":true}"), ECRITUM_ERROR_INVALID_CONFIG);
}

static void test_parser_caps_fail_closed(void) {
    expect_runtime_create(config_string("[[[[[[[[[[[[[[[[[0]]]]]]]]]]]]]]]]]"), ECRITUM_ERROR_INVALID_CONFIG);

    char *languages = malloc(4096);
    CHECK(languages != NULL);
    if (languages == NULL) {
        return;
    }
    size_t offset = 0;
    languages[offset++] = '[';
    for (int index = 0; index < 257; index++) {
        offset += (size_t)snprintf(languages + offset, 4096 - offset, "%s\"l%d\"", index == 0 ? "" : ",", index);
    }
    languages[offset++] = ']';
    languages[offset] = '\0';
    char *too_many_languages = runtime_config_with_language_list(languages);
    if (too_many_languages != NULL) {
        expect_runtime_create(config_string(too_many_languages), ECRITUM_ERROR_INVALID_CONFIG);
        free(too_many_languages);
    }
    free(languages);

    char *long_string = repeat_char('a', 4097);
    if (long_string != NULL) {
        size_t len = strlen(long_string) + 3;
        char *json = malloc(len);
        CHECK(json != NULL);
        if (json != NULL) {
            snprintf(json, len, "\"%s\"", long_string);
            expect_runtime_create(config_string(json), ECRITUM_ERROR_INVALID_CONFIG);
            free(json);
        }
        free(long_string);
    }

    char *short_overflow = repeat_char('a', 256);
    if (short_overflow != NULL) {
        char *language_json = malloc(strlen(short_overflow) + 5);
        CHECK(language_json != NULL);
        if (language_json != NULL) {
            snprintf(language_json, strlen(short_overflow) + 5, "[\"%s\"]", short_overflow);
            char *config = runtime_config_with_language_list(language_json);
            if (config != NULL) {
                expect_runtime_create(config_string(config), ECRITUM_ERROR_INVALID_CONFIG);
                free(config);
            }
            free(language_json);
        }
        free(short_overflow);
    }
}

static void test_duplicate_sets_and_bad_paths_fail_closed(void) {
    char *duplicate_languages = runtime_config_with_language_list("[\"javascript\",\"javascript\"]");
    if (duplicate_languages != NULL) {
        expect_runtime_create(config_string(duplicate_languages), ECRITUM_ERROR_INVALID_CONFIG);
        free(duplicate_languages);
    }

    const char *duplicate_sets[] = {
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"read_only\",\"roots\":[{\"kind\":\"directory\",\"path\":\"/tmp/ecritum/scripts\"},{\"kind\":\"directory\",\"path\":\"/tmp/ecritum/scripts\"}]},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"allowed\",\"rules\":[{\"scheme\":\"https\",\"host\":\"api.example.com\",\"port\":443},{\"scheme\":\"https\",\"host\":\"api.example.com\",\"port\":443}]},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"allowed\",\"commands\":[{\"path\":\"/usr/bin/true\"},{\"path\":\"/usr/bin/true\"}]},\"environment\":{\"mode\":\"denied\"},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
        "{\"schemaVersion\":1,\"languages\":[],\"policy\":{\"filesystem\":{\"mode\":\"denied\"},\"network\":{\"mode\":\"denied\"},\"process\":{\"mode\":\"denied\"},\"environment\":{\"mode\":\"allowed\",\"keys\":[\"ECRITUM_MODE\",\"ECRITUM_MODE\"]},\"clock\":{\"mode\":\"denied\"},\"random\":{\"mode\":\"denied\"},\"log\":{\"mode\":\"denied\"}},\"diagnostics\":{\"mode\":\"redacted\"},\"resourceLimits\":{}}",
    };
    for (size_t index = 0; index < sizeof(duplicate_sets) / sizeof(duplicate_sets[0]); index++) {
        expect_runtime_create(config_string(duplicate_sets[index]), ECRITUM_ERROR_INVALID_CONFIG);
    }

    const char *bad_paths[] = {
        "relative",
        "/tmp//ecritum",
        "/tmp/./ecritum",
        "/tmp/../ecritum",
        "/tmp/ecritum/",
    };
    for (size_t index = 0; index < sizeof(bad_paths) / sizeof(bad_paths[0]); index++) {
        char *config = runtime_config_with_filesystem_path(bad_paths[index]);
        if (config != NULL) {
            expect_runtime_create(config_string(config), ECRITUM_ERROR_INVALID_CONFIG);
            free(config);
        }
    }
}

static void test_context_rejects_runtime_only_keys(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_context_t context = 777;
    ecritum_error_t error = 0;

    CHECK(ecritum_runtime_create(empty_config(), &runtime, &error) == ECRITUM_OK);
    CHECK(ecritum_context_create(runtime, config_string("{\"schemaVersion\":1,\"languages\":[]}"), &context, &error) == ECRITUM_ERROR_INVALID_CONFIG);
    CHECK(context == 0);
    check_error(error, ECRITUM_ERROR_INVALID_CONFIG, "context_create");
    context = 777;
    CHECK(ecritum_context_create(runtime, config_string("{\"schemaVersion\":1,\"diagnostics\":{\"mode\":\"raw\"}}"), &context, &error) == ECRITUM_ERROR_INVALID_CONFIG);
    CHECK(context == 0);
    check_error(error, ECRITUM_ERROR_INVALID_CONFIG, "context_create");
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
}

static void test_context_resource_process_and_toggle_narrowing(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_context_t context = 0;
    ecritum_error_t error = 0;

    CHECK(ecritum_runtime_create(config_string(runtime_config_with_grants), &runtime, &error) == ECRITUM_OK);
    CHECK(ecritum_context_create(runtime, config_string("{\"schemaVersion\":1,\"policy\":{\"process\":{\"mode\":\"allowed\",\"commands\":[{\"path\":\"/usr/bin/true\"}]}},\"resourceLimits\":{\"maxOutputBytes\":1024}}"), &context, &error) == ECRITUM_OK);
    CHECK(context != 0);
    CHECK(ecritum_context_destroy(&context, &error) == ECRITUM_OK);

    context = 777;
    CHECK(ecritum_context_create(runtime, config_string("{\"schemaVersion\":1,\"resourceLimits\":{\"maxOutputBytes\":2048}}"), &context, &error) == ECRITUM_ERROR_INVALID_CONFIG);
    CHECK(context == 0);
    check_error(error, ECRITUM_ERROR_INVALID_CONFIG, "context_create");

    context = 777;
    CHECK(ecritum_context_create(runtime, config_string("{\"schemaVersion\":1,\"policy\":{\"process\":{\"mode\":\"allowed\",\"commands\":[{\"path\":\"/usr/bin/false\"}]}}}"), &context, &error) == ECRITUM_ERROR_INVALID_CONFIG);
    CHECK(context == 0);
    check_error(error, ECRITUM_ERROR_INVALID_CONFIG, "context_create");
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);

    CHECK(ecritum_runtime_create(empty_config(), &runtime, &error) == ECRITUM_OK);
    context = 777;
    CHECK(ecritum_context_create(runtime, config_string("{\"schemaVersion\":1,\"policy\":{\"clock\":{\"mode\":\"allowed\"},\"random\":{\"mode\":\"allowed\"},\"log\":{\"mode\":\"allowed\"}}}"), &context, &error) == ECRITUM_ERROR_INVALID_CONFIG);
    CHECK(context == 0);
    check_error(error, ECRITUM_ERROR_INVALID_CONFIG, "context_create");
    CHECK(ecritum_runtime_destroy(&runtime, &error) == ECRITUM_OK);
}

int main(void) {
    test_empty_runtime_config_variants_remain_valid();
    test_valid_non_empty_runtime_config_creates_runtime();
    test_validation_statuses_are_deterministic();
    test_null_values_fail_closed_for_v1_fields();
    test_context_empty_config_inherits_runtime_effective_config();
    test_context_narrowing_accepts_subsets();
    test_context_widening_fails_closed();
    test_schema_version_edges_fail_closed();
    test_parser_caps_fail_closed();
    test_duplicate_sets_and_bad_paths_fail_closed();
    test_context_rejects_runtime_only_keys();
    test_context_resource_process_and_toggle_narrowing();
    return failures == 0 ? 0 : 1;
}
