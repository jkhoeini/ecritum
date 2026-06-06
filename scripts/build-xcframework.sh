#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: build-xcframework.sh [--native-dir PATH] [--public-headers PATH] [--license-texts-dir PATH] [--output PATH] [--work-dir PATH] [--min-macos VERSION] [--sign-identity ID] [--skip-sign]

Build dist/local/EcritumRuntime.xcframework from the M1 native output.
Diagnostics go to stderr. The output path is printed to stdout.
USAGE
}

native_dir="build/native/macos-arm64"
public_headers="Sources/CEcritum/include"
license_texts_dir="THIRD_PARTY_LICENSES"
output="dist/local/EcritumRuntime.xcframework"
work_dir="build/xcframework"
min_macos="14.0"
sign_identity="${ECRITUM_CODESIGN_IDENTITY:--}"
skip_sign="0"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --native-dir) native_dir="$2"; shift 2 ;;
    --public-headers) public_headers="$2"; shift 2 ;;
    --license-texts-dir) license_texts_dir="$2"; shift 2 ;;
    --output) output="$2"; shift 2 ;;
    --work-dir) work_dir="$2"; shift 2 ;;
    --min-macos) min_macos="$2"; shift 2 ;;
    --sign-identity) sign_identity="$2"; shift 2 ;;
    --skip-sign) skip_sign="1"; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

native_lib="$native_dir/libecritum.dylib"
private_headers="$native_dir/include/private"
framework_dir="$work_dir/macos-arm64/EcritumRuntime.framework"
resources_dir="$framework_dir/Resources"
modules_dir="$framework_dir/Modules"

for path in "$native_lib" "$private_headers/libecritum.h" "$private_headers/graal_isolate.h" "$public_headers/ecritum.h" "$license_texts_dir/manifest.json" "scripts/ecritum_runtime_wrapper.c"; do
  if [ ! -e "$path" ]; then
    echo "missing required input: $path" >&2
    exit 1
  fi
done

if ! otool -l "$native_lib" | grep -A4 'LC_BUILD_VERSION' | grep -q "minos $min_macos"; then
  echo "native library does not declare macOS minimum $min_macos: $native_lib" >&2
  exit 1
fi

rm -rf "$work_dir" "$output"
mkdir -p "$framework_dir/Headers" "$modules_dir" "$resources_dir" "$(dirname "$output")"

cp "$public_headers/ecritum.h" "$framework_dir/Headers/ecritum.h"
cp "$native_lib" "$resources_dir/libecritum_graal.dylib"
cp -R "$license_texts_dir" "$resources_dir/Licenses"
install_name_tool -id "@loader_path/Resources/libecritum_graal.dylib" "$resources_dir/libecritum_graal.dylib"

MACOSX_DEPLOYMENT_TARGET="$min_macos" clang \
  -dynamiclib \
  -target arm64-apple-macos"$min_macos" \
  -mmacosx-version-min="$min_macos" \
  -fvisibility=hidden \
  -install_name "@rpath/EcritumRuntime.framework/EcritumRuntime" \
  -current_version 0.1.0 \
  -compatibility_version 0.1.0 \
  -I "$public_headers" \
  -I "$private_headers" \
  scripts/ecritum_runtime_wrapper.c \
  "$resources_dir/libecritum_graal.dylib" \
  -o "$framework_dir/EcritumRuntime"

cat > "$modules_dir/module.modulemap" <<'MODULEMAP'
framework module EcritumRuntime {
  umbrella header "ecritum.h"
  export *
  module * { export * }
}
MODULEMAP

cat > "$framework_dir/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>en</string>
  <key>CFBundleExecutable</key>
  <string>EcritumRuntime</string>
  <key>CFBundleIdentifier</key>
  <string>dev.ecritum.runtime</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>EcritumRuntime</string>
  <key>CFBundlePackageType</key>
  <string>FMWK</string>
  <key>CFBundleShortVersionString</key>
  <string>0.1.0</string>
  <key>CFBundleVersion</key>
  <string>0.1.0</string>
  <key>MinimumOSVersion</key>
  <string>$min_macos</string>
</dict>
</plist>
PLIST

if [ "$skip_sign" != "1" ]; then
  codesign_args=(--force --sign "$sign_identity")
  if [ "$sign_identity" != "-" ]; then
    codesign_args+=(--options runtime --timestamp)
  fi
  codesign "${codesign_args[@]}" "$resources_dir/libecritum_graal.dylib" >&2
  codesign "${codesign_args[@]}" "$framework_dir" >&2
fi

mkdir -p "$output/macos-arm64"
cp -R "$framework_dir" "$output/macos-arm64/EcritumRuntime.framework"
cat > "$output/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>AvailableLibraries</key>
  <array>
    <dict>
      <key>BinaryPath</key>
      <string>EcritumRuntime.framework/EcritumRuntime</string>
      <key>LibraryIdentifier</key>
      <string>macos-arm64</string>
      <key>LibraryPath</key>
      <string>EcritumRuntime.framework</string>
      <key>SupportedArchitectures</key>
      <array>
        <string>arm64</string>
      </array>
      <key>SupportedPlatform</key>
      <string>macos</string>
    </dict>
  </array>
  <key>CFBundlePackageType</key>
  <string>XFWK</string>
  <key>XCFrameworkFormatVersion</key>
  <string>1.0</string>
</dict>
</plist>
PLIST
printf '%s\n' "$output"
