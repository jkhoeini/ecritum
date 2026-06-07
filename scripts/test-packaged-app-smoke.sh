#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: test-packaged-app-smoke.sh [--artifact PATH] [--build-dir PATH]

Build a minimal macOS .app consumer bundle, copy the packaged
EcritumRuntime.framework into Contents/Frameworks, and verify the app runs
Clojure plus JavaScript smoke scripts without DYLD_* runtime overrides.
USAGE
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
artifact="$repo_root/dist/local/EcritumRuntime.xcframework"
build_dir="$repo_root/build/packaged-app-smoke"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --artifact) artifact="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"; shift 2 ;;
    --build-dir) build_dir="$(mkdir -p "$2" && cd "$2" && pwd)"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

cd "$repo_root"

arch="$(uname -m)"
slice="macos-$arch"
framework_source="$artifact/$slice/EcritumRuntime.framework"
package_dir="$repo_root/Examples/MacSmokeApp"
swift_build_dir="$build_dir/swift-build"
app="$build_dir/EcritumSmoke.app"
contents="$app/Contents"
executable_name="EcritumSmokeApp"
executable="$contents/MacOS/$executable_name"
framework_destination="$contents/Frameworks/EcritumRuntime.framework"
private_runtime="$framework_destination/Resources/libecritum_graal.dylib"
run_root="$(mktemp -d "${TMPDIR:-/tmp}/ecritum-packaged-app-smoke-XXXXXX")"
run_app="$run_root/EcritumSmoke.app"
run_executable="$run_app/Contents/MacOS/$executable_name"
run_framework="$run_app/Contents/Frameworks/EcritumRuntime.framework"
run_private_runtime="$run_framework/Resources/libecritum_graal.dylib"
success_line="EcritumSmokeApp version=0.1.0 clojure=42 javascript=42"

trap 'rm -rf "$run_root"' EXIT

for path in "$artifact" "$framework_source" "$framework_source/EcritumRuntime" "$framework_source/Headers/ecritum.h" "$framework_source/Resources/libecritum_graal.dylib" "$package_dir/Package.swift"; do
  if [ ! -e "$path" ]; then
    echo "missing packaged app smoke input: $path" >&2
    exit 1
  fi
done

rm -rf "$build_dir"
mkdir -p "$contents/MacOS" "$contents/Frameworks" "$run_root/empty-bin"

export ECRITUM_LOCAL_RUNTIME=1
export ECRITUM_LOCAL_RUNTIME_STATE="v4:runtime:runtime-present:packaged-app-smoke"

swift build \
  --package-path "$package_dir" \
  --build-path "$swift_build_dir" \
  --configuration debug \
  --quiet

bin_dir="$(swift build \
  --package-path "$package_dir" \
  --build-path "$swift_build_dir" \
  --configuration debug \
  --show-bin-path)"

source_executable="$bin_dir/$executable_name"
if [ ! -x "$source_executable" ]; then
  echo "missing Swift smoke executable: $source_executable" >&2
  exit 1
fi

cp "$source_executable" "$executable"
cp -R "$framework_source" "$framework_destination"

if codesign -d "$executable" >/dev/null 2>&1; then
  codesign --remove-signature "$executable"
fi
if ! otool -l "$executable" | grep -A2 LC_RPATH | grep -q '@executable_path/../Frameworks'; then
  install_name_tool -add_rpath '@executable_path/../Frameworks' "$executable"
fi

cat > "$contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>en</string>
  <key>CFBundleExecutable</key>
  <string>EcritumSmokeApp</string>
  <key>CFBundleIdentifier</key>
  <string>dev.ecritum.smoke</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>EcritumSmoke</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>0.1.0</string>
  <key>CFBundleVersion</key>
  <string>0.1.0</string>
  <key>LSMinimumSystemVersion</key>
  <string>14.0</string>
</dict>
</plist>
PLIST

if command -v codesign >/dev/null 2>&1; then
  sign_log="$build_dir/codesign.log"
  sign_path() {
    local path="$1"
    if ! codesign --force --sign - --timestamp=none "$path" >"$sign_log" 2>&1; then
      cat "$sign_log" >&2
      exit 1
    fi
  }

  sign_path "$private_runtime"
  sign_path "$framework_destination/EcritumRuntime"
  sign_path "$executable"
  if ! codesign --force --sign - --timestamp=none --deep "$app" >"$sign_log" 2>&1; then
    cat "$sign_log" >&2
    exit 1
  fi
  codesign --verify --deep --strict "$app"
fi

cp -R "$app" "$run_app"
output="$(env -i HOME="${HOME:-}" TMPDIR="${TMPDIR:-/tmp}" PATH="$run_root/empty-bin" "$run_executable")"
if [ "$output" != "$success_line" ]; then
  echo "unexpected packaged app output: $output" >&2
  exit 1
fi

mv "$run_framework" "$run_framework.removed"
if { env -i HOME="${HOME:-}" TMPDIR="${TMPDIR:-/tmp}" PATH="$run_root/empty-bin" "$run_executable"; } >/dev/null 2>&1; then
  echo "packaged app unexpectedly launched after removing bundled EcritumRuntime.framework" >&2
  exit 1
fi
mv "$run_framework.removed" "$run_framework"

otool -L "$run_executable" | grep -q '@rpath/EcritumRuntime.framework/EcritumRuntime'
otool -l "$run_executable" | grep -A2 LC_RPATH | grep -q '@executable_path/../Frameworks'
otool -D "$run_framework/EcritumRuntime" | grep -q '@rpath/EcritumRuntime.framework/EcritumRuntime'
otool -L "$run_framework/EcritumRuntime" | grep -q '@loader_path/Resources/libecritum_graal.dylib'
otool -D "$run_private_runtime" | grep -q '@loader_path/Resources/libecritum_graal.dylib'

link_payload="$({
  otool -L "$run_executable" | tail -n +2
  otool -l "$run_executable" | tail -n +2
  otool -D "$run_framework/EcritumRuntime" | tail -n +2
  otool -L "$run_framework/EcritumRuntime" | tail -n +2
  otool -D "$run_private_runtime" | tail -n +2
  otool -L "$run_private_runtime" | tail -n +2
})"
if printf '%s\n' "$link_payload" | grep -F "$repo_root"; then
  echo "packaged app contains workspace-local install path" >&2
  exit 1
fi
if printf '%s\n' "$link_payload" | grep -E '/GraalVM|/jdk|/native/target|/build/native'; then
  echo "packaged app links a build-machine runtime path" >&2
  exit 1
fi

printf '%s\n' "$output"
