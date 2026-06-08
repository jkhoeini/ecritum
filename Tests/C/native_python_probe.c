#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libecritum.h"

#define ECRITUM_OK 0
#define ECRITUM_ERROR_PERMISSION_DENIED 14

static void check(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "native_python_probe failed: %s\n", message);
        exit(1);
    }
}

static uint32_t read_u32(const uint8_t **cursor) {
    uint32_t value = ((uint32_t)(*cursor)[0] << 24)
        | ((uint32_t)(*cursor)[1] << 16)
        | ((uint32_t)(*cursor)[2] << 8)
        | (uint32_t)(*cursor)[3];
    *cursor += 4;
    return value;
}

static int64_t read_i64(const uint8_t **cursor) {
    uint64_t value = 0;
    for (int index = 0; index < 8; index++) {
        value = (value << 8) | (uint64_t)(*cursor)[index];
    }
    *cursor += 8;
    return (int64_t)value;
}

static int eval_python(
    graal_isolatethread_t *thread,
    const char *source,
    uint8_t *buffer,
    size_t buffer_size,
    long long *bytes_written
) {
    const char *source_name = "native-python-probe.py";
    *bytes_written = 0;
    return ecritum_graal_eval_python_probe(
        thread,
        (char *)source,
        strlen(source),
        (char *)source_name,
        strlen(source_name),
        (char *)buffer,
        buffer_size,
        bytes_written
    );
}

static void expect_int_result(graal_isolatethread_t *thread, const char *source, int64_t expected) {
    uint8_t buffer[4096] = {0};
    long long bytes_written = 0;
    int status = eval_python(thread, source, buffer, sizeof(buffer), &bytes_written);
    check(status == ECRITUM_OK, "probe entrypoint returned non-OK");
    check(bytes_written > 0 && bytes_written <= (long long)sizeof(buffer), "invalid encoded result length");

    const uint8_t *cursor = buffer;
    check(read_u32(&cursor) == 0x45435631u, "invalid backend result magic");
    check(read_u32(&cursor) == ECRITUM_OK, "expected OK backend status");
    check(*cursor++ == 2, "expected integer value kind");
    check(read_i64(&cursor) == expected, "unexpected integer result");
}

static void expect_permission_denied(graal_isolatethread_t *thread, const char *source) {
    uint8_t buffer[4096] = {0};
    long long bytes_written = 0;
    int status = eval_python(thread, source, buffer, sizeof(buffer), &bytes_written);
    check(status == ECRITUM_OK, "probe entrypoint returned non-OK for denied source");
    check(bytes_written > 0 && bytes_written <= (long long)sizeof(buffer), "invalid denied result length");

    const uint8_t *cursor = buffer;
    check(read_u32(&cursor) == 0x45435631u, "invalid denied result magic");
    check(read_u32(&cursor) == ECRITUM_ERROR_PERMISSION_DENIED, "expected permission-denied backend status");
}

int main(void) {
    graal_isolate_t *isolate = NULL;
    graal_isolatethread_t *thread = NULL;
    check(graal_create_isolate(NULL, &isolate, &thread) == 0, "create isolate");

    expect_int_result(thread, "40 + 2", 42);
    expect_int_result(thread, "import json\njson.loads('{\"answer\": 42}')['answer']", 42);
    expect_permission_denied(thread, "import java");

    check(graal_tear_down_isolate(thread) == 0, "tear down isolate");
    return 0;
}
