#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libecritum.h"

#define ECRITUM_OK 0
#define ECRITUM_ERROR_PERMISSION_DENIED 14
#define ECRITUM_ERROR_SCRIPT 17

static void check(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "native_ruby_probe failed: %s\n", message);
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

static int eval_ruby(
    graal_isolatethread_t *thread,
    const char *source,
    uint8_t *buffer,
    size_t buffer_size,
    long long *bytes_written
) {
    const char *source_name = "native-ruby-probe.rb";
    *bytes_written = 0;
    return ecritum_graal_eval_ruby_probe(
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
    int status = eval_ruby(thread, source, buffer, sizeof(buffer), &bytes_written);
    check(status == ECRITUM_OK, "probe entrypoint returned non-OK");
    check(bytes_written > 0 && bytes_written <= (long long)sizeof(buffer), "invalid encoded result length");

    const uint8_t *cursor = buffer;
    check(read_u32(&cursor) == 0x45435631u, "invalid backend result magic");
    check(read_u32(&cursor) == ECRITUM_OK, "expected OK backend status");
    check(*cursor++ == 2, "expected integer value kind");
    check(read_i64(&cursor) == expected, "unexpected integer result");
}

/* Decode just the backend status from an encoded result buffer. */
static uint32_t backend_status(graal_isolatethread_t *thread, const char *source) {
    uint8_t buffer[4096] = {0};
    long long bytes_written = 0;
    int status = eval_ruby(thread, source, buffer, sizeof(buffer), &bytes_written);
    check(status == ECRITUM_OK, "probe entrypoint returned non-OK for denied source");
    check(bytes_written > 0 && bytes_written <= (long long)sizeof(buffer), "invalid denied result length");

    const uint8_t *cursor = buffer;
    check(read_u32(&cursor) == 0x45435631u, "invalid denied result magic");
    return read_u32(&cursor);
}

/* Lexically-caught surfaces are mapped to PERMISSION_DENIED by the deniesSource() pre-filter. */
static void expect_permission_denied(graal_isolatethread_t *thread, const char *source) {
    check(backend_status(thread, source) == ECRITUM_ERROR_PERMISSION_DENIED,
          "expected permission-denied backend status");
}

/*
 * Lexical-BYPASS surfaces: built so they do NOT match DENIED_SOURCE_PATTERNS, so they reach the
 * real TruffleRuby runtime through the production evaluate() path at the C ABI boundary. The
 * runtime denies the underlying operation ("native access is not allowed") and the value never
 * escapes. NOTE: classify() maps that runtime guard to category "runtime" -> SCRIPT (17), not
 * PERMISSION_DENIED (14). Either way the operation is denied; we assert it is a denial (SCRIPT)
 * and explicitly NOT OK. This is the honest runtime-grade proof through the public ABI.
 * (Recorded in docs/security/ruby-probe-denial-matrix.md.)
 */
static void expect_runtime_denied_via_abi(graal_isolatethread_t *thread, const char *source) {
    uint32_t status = backend_status(thread, source);
    check(status != ECRITUM_OK, "lexical-bypass surface must NOT succeed (no escape) at the ABI");
    check(status == ECRITUM_ERROR_SCRIPT,
          "expected runtime native-access denial mapped to SCRIPT status at the ABI");
}

int main(void) {
    graal_isolate_t *isolate = NULL;
    graal_isolatethread_t *thread = NULL;
    check(graal_create_isolate(NULL, &isolate, &thread) == 0, "create isolate");

    expect_int_result(thread, "40 + 2", 42);

    /* Lexically-caught surfaces: blocked by the deniesSource() pre-filter -> PERMISSION_DENIED. */
    expect_permission_denied(thread, "Java.type('java.lang.System')");
    expect_permission_denied(thread, "Kernel.system('true')");
    /* Object.const_get(:File): the symbol :File still matches \\bFile\\b, so this is lexical too. */
    expect_permission_denied(thread, "Object.const_get(:File).read('/etc/hosts')");

    /*
     * Lexical-BYPASS surfaces: string-built constant/method names defeat the regex, so they run
     * in the real runtime through the production ABI. The runtime denies the underlying File.read
     * / system spawn and the value never escapes (mapped to SCRIPT today).
     */
    expect_runtime_denied_via_abi(thread, "Object.const_get(\"Fil\" + \"e\").read('/etc/hosts')");
    expect_runtime_denied_via_abi(thread, "send(\"sys\" + \"tem\",'true')");

    check(graal_tear_down_isolate(thread) == 0, "tear down isolate");
    return 0;
}
