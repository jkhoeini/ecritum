#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: check-xcframework.sh [--artifact PATH]

Validate the local EcritumRuntime XCFramework shape, symbols, install names,
public headers, minimum macOS version, and dlopen smoke path.
USAGE
}

artifact="dist/local/EcritumRuntime.xcframework"
while [ "$#" -gt 0 ]; do
  case "$1" in
    --artifact) artifact="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

framework="$artifact/macos-arm64/EcritumRuntime.framework"
binary="$framework/EcritumRuntime"
headers="$framework/Headers"
resources="$framework/Resources"
private_lib="$resources/libecritum_graal.dylib"

for path in "$artifact/Info.plist" "$binary" "$headers/ecritum.h" "$framework/Modules/module.modulemap" "$private_lib"; do
  if [ ! -e "$path" ]; then
    echo "missing artifact path: $path" >&2
    exit 1
  fi
done

if find "$headers" -type f \( -name 'graal*.h' -o -name 'libecritum*.h' \) | grep -q .; then
  echo "private Graal headers leaked into public framework headers" >&2
  exit 1
fi

nm -gU "$binary" | grep -q ' _ecritum_version$'
if nm -gU "$binary" | grep -E ' (_ecritum_graal_version|_ecritum_graal_eval_clojure|_ecritum_graal_eval_clojure_with_host|_graal_create_isolate|_graal_tear_down_isolate)$'; then
  echo "private Graal symbols leaked from public wrapper binary" >&2
  exit 1
fi
otool -D "$binary" | grep -q '@rpath/EcritumRuntime.framework/EcritumRuntime'
otool -L "$binary" | grep -q '@loader_path/Resources/libecritum_graal.dylib'
otool -D "$private_lib" | grep -q '@loader_path/Resources/libecritum_graal.dylib'
repo_root="$(pwd -P)"
install_name_payload="$({
  otool -D "$binary" | tail -n +2
  otool -L "$binary" | tail -n +2
  otool -D "$private_lib" | tail -n +2
})"
if printf '%s\n' "$install_name_payload" | grep -F "$repo_root"; then
  echo "artifact contains workspace-local install path" >&2
  exit 1
fi
otool -l "$binary" | grep -A4 'LC_BUILD_VERSION' | grep -q 'minos 14.0'
otool -l "$private_lib" | grep -A4 'LC_BUILD_VERSION' | grep -q 'minos 14.0'
codesign --verify --verbose=2 "$private_lib"
codesign --verify --verbose=2 "$framework"
codesign --verify --verbose=2 "$binary"

smoke_dir="$(mktemp -d "${TMPDIR:-/tmp}/ecritum-smoke-XXXXXX")"
trap 'rm -rf "$smoke_dir"' EXIT
cat > "$smoke_dir/smoke.c" <<'C'
#include <dlfcn.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef int (*ecritum_version_fn)(char *, size_t);
typedef uint64_t ecritum_runtime_t;
typedef uint64_t ecritum_context_t;
typedef uint64_t ecritum_namespace_t;
typedef uint64_t ecritum_function_t;
typedef uint64_t ecritum_value_t;
typedef uint64_t ecritum_call_t;
typedef uint64_t ecritum_error_t;
typedef struct {
    const uint8_t *data;
    size_t len;
} ecritum_bytes_t;
typedef struct {
    const char *data;
    size_t len;
} ecritum_string_view_t;
typedef int (*ecritum_host_fn_t)(ecritum_call_t, ecritum_value_t *, ecritum_error_t *, void *);
typedef void (*ecritum_user_data_destroy_fn_t)(void *);
typedef int (*ecritum_runtime_create_fn)(ecritum_bytes_t, ecritum_runtime_t *, ecritum_error_t *);
typedef int (*ecritum_runtime_destroy_fn)(ecritum_runtime_t *, ecritum_error_t *);
typedef int (*ecritum_context_create_fn)(ecritum_runtime_t, ecritum_bytes_t, ecritum_context_t *, ecritum_error_t *);
typedef int (*ecritum_context_destroy_fn)(ecritum_context_t *, ecritum_error_t *);
typedef int (*ecritum_namespace_create_fn)(ecritum_runtime_t, ecritum_string_view_t, ecritum_namespace_t *, ecritum_error_t *);
typedef int (*ecritum_namespace_destroy_fn)(ecritum_namespace_t *, ecritum_error_t *);
typedef int (*ecritum_namespace_register_function_fn)(
    ecritum_namespace_t,
    ecritum_string_view_t,
    ecritum_host_fn_t,
    void *,
    ecritum_user_data_destroy_fn_t,
    ecritum_function_t *,
    ecritum_error_t *
);
typedef int (*ecritum_function_destroy_fn)(ecritum_function_t *, ecritum_error_t *);
typedef int (*ecritum_error_destroy_fn)(ecritum_error_t *);
typedef int (*ecritum_error_status_fn)(ecritum_error_t, int *);

enum {
    ECRITUM_OK = 0,
    ECRITUM_ERROR_INVALID_ARGUMENT = 1,
    ECRITUM_ERROR_BUFFER_TOO_SMALL = 2,
    ECRITUM_ERROR_CONTEXTS_ALIVE = 10,
};

static int smoke_callback(ecritum_call_t call, ecritum_value_t *out_result, ecritum_error_t *out_error, void *user_data) {
    (void)call;
    (void)user_data;
    if (out_result != NULL) {
        *out_result = 0;
    }
    if (out_error != NULL) {
        *out_error = 0;
    }
    return ECRITUM_OK;
}

static void smoke_destroy(void *user_data) {
    int *destroy_count = (int *)user_data;
    if (destroy_count != NULL) {
        (*destroy_count)++;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }

    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        fprintf(stderr, "%s\n", dlerror());
        return 3;
    }

    ecritum_version_fn version = (ecritum_version_fn)dlsym(handle, "ecritum_version");
    if (version == NULL) {
        fprintf(stderr, "%s\n", dlerror());
        return 4;
    }
    ecritum_runtime_create_fn runtime_create = (ecritum_runtime_create_fn)dlsym(handle, "ecritum_runtime_create");
    ecritum_runtime_destroy_fn runtime_destroy = (ecritum_runtime_destroy_fn)dlsym(handle, "ecritum_runtime_destroy");
    ecritum_context_create_fn context_create = (ecritum_context_create_fn)dlsym(handle, "ecritum_context_create");
    ecritum_context_destroy_fn context_destroy = (ecritum_context_destroy_fn)dlsym(handle, "ecritum_context_destroy");
    ecritum_namespace_create_fn namespace_create = (ecritum_namespace_create_fn)dlsym(handle, "ecritum_namespace_create");
    ecritum_namespace_destroy_fn namespace_destroy = (ecritum_namespace_destroy_fn)dlsym(handle, "ecritum_namespace_destroy");
    ecritum_namespace_register_function_fn namespace_register_function = (ecritum_namespace_register_function_fn)dlsym(handle, "ecritum_namespace_register_function");
    ecritum_function_destroy_fn function_destroy = (ecritum_function_destroy_fn)dlsym(handle, "ecritum_function_destroy");
    ecritum_error_destroy_fn error_destroy = (ecritum_error_destroy_fn)dlsym(handle, "ecritum_error_destroy");
    ecritum_error_status_fn error_status = (ecritum_error_status_fn)dlsym(handle, "ecritum_error_status");
    if (runtime_create == NULL || runtime_destroy == NULL || context_create == NULL ||
        context_destroy == NULL || namespace_create == NULL || namespace_destroy == NULL ||
        namespace_register_function == NULL || function_destroy == NULL ||
        error_destroy == NULL || error_status == NULL) {
        fprintf(stderr, "%s\n", dlerror());
        return 10;
    }

    char buffer[64];
    int status = version(buffer, sizeof(buffer));
    if (status != 0) {
        fprintf(stderr, "status=%d\n", status);
        return 5;
    }
    if (strcmp(buffer, "0.1.0") != 0) {
        fprintf(stderr, "version=%s\n", buffer);
        return 6;
    }
    if (version(NULL, sizeof(buffer)) != ECRITUM_ERROR_INVALID_ARGUMENT) {
        return 7;
    }
    if (version(buffer, 0) != ECRITUM_ERROR_INVALID_ARGUMENT) {
        return 8;
    }
    if (version(buffer, 1) != ECRITUM_ERROR_BUFFER_TOO_SMALL) {
        return 9;
    }

    ecritum_bytes_t empty = {0};
    ecritum_runtime_t runtime = 0;
    ecritum_context_t context = 0;
    ecritum_error_t error = 0;
    if (runtime_create(empty, &runtime, &error) != ECRITUM_OK || runtime == 0 || error != 0) {
        return 11;
    }
    if (context_create(runtime, empty, &context, &error) != ECRITUM_OK || context == 0 || error != 0) {
        return 12;
    }
    ecritum_runtime_t runtime_before = runtime;
    if (runtime_destroy(&runtime, &error) != ECRITUM_ERROR_CONTEXTS_ALIVE || runtime != runtime_before || error == 0) {
        return 13;
    }
    int error_code = 0;
    if (error_status(error, &error_code) != ECRITUM_OK || error_code != ECRITUM_ERROR_CONTEXTS_ALIVE) {
        return 14;
    }
    if (error_destroy(&error) != ECRITUM_OK || error != 0) {
        return 15;
    }
    if (context_destroy(&context, &error) != ECRITUM_OK || context != 0 || error != 0) {
        return 16;
    }
    ecritum_string_view_t namespace_name = {"app", 3};
    ecritum_string_view_t first_function_name = {"notify", 6};
    ecritum_string_view_t second_function_name = {"replaceSelection", 16};
    ecritum_namespace_t namespace_handle = 0;
    ecritum_function_t function = 0;
    int destroy_count = 0;
    if (namespace_create(runtime, namespace_name, &namespace_handle, &error) != ECRITUM_OK || namespace_handle == 0 || error != 0) {
        return 18;
    }
    if (namespace_register_function(namespace_handle, first_function_name, smoke_callback, &destroy_count, smoke_destroy, &function, &error) != ECRITUM_OK || function == 0 || error != 0) {
        return 19;
    }
    if (function_destroy(&function, &error) != ECRITUM_OK || function != 0 || error != 0 || destroy_count != 1) {
        return 20;
    }
    if (namespace_register_function(namespace_handle, second_function_name, smoke_callback, &destroy_count, smoke_destroy, &function, &error) != ECRITUM_OK || function == 0 || error != 0) {
        return 21;
    }
    if (namespace_destroy(&namespace_handle, &error) != ECRITUM_OK || namespace_handle != 0 || error != 0 || destroy_count != 2) {
        return 22;
    }
    if (runtime_destroy(&runtime, &error) != ECRITUM_OK || runtime != 0 || error != 0) {
        return 17;
    }

    return 0;
}
C
clang -target arm64-apple-macos14.0 -mmacosx-version-min=14.0 "$smoke_dir/smoke.c" -o "$smoke_dir/smoke"
"$smoke_dir/smoke" "$binary"
