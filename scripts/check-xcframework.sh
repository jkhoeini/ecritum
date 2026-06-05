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
if nm -gU "$binary" | grep -E ' (_ecritum_graal_version|_graal_create_isolate|_graal_tear_down_isolate)$'; then
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

smoke_dir="$(mktemp -d "${TMPDIR:-/tmp}/ecritum-smoke-XXXXXX")"
trap 'rm -rf "$smoke_dir"' EXIT
cat > "$smoke_dir/smoke.c" <<'C'
#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef int (*ecritum_version_fn)(char *, size_t);

enum {
    ECRITUM_OK = 0,
    ECRITUM_ERROR_INVALID_ARGUMENT = 1,
    ECRITUM_ERROR_BUFFER_TOO_SMALL = 2,
};

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

    char buffer[64];
    int status = version(buffer, sizeof(buffer));
    if (status != 0) {
        fprintf(stderr, "status=%d\n", status);
        return 5;
    }
    if (strcmp(buffer, "0.1.0-dev") != 0) {
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

    return 0;
}
C
clang -target arm64-apple-macos14.0 -mmacosx-version-min=14.0 "$smoke_dir/smoke.c" -o "$smoke_dir/smoke"
"$smoke_dir/smoke" "$binary"
