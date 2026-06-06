#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: release-check.sh
       release-check.sh [--output-dir PATH] [--artifact PATH] [--release-zip PATH]

Run the release gates. This command exits nonzero when any release blocker is
present, including unknown shipped licenses.
USAGE
}

output_dir="build/release"
artifact="dist/local/EcritumRuntime.xcframework"
release_zip="dist/release/EcritumRuntime.xcframework.zip"
just_bin="${JUST:-just}"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --output-dir) output_dir="$2"; shift 2 ;;
    --artifact) artifact="$2"; shift 2 ;;
    --release-zip) release_zip="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

mkdir -p "$output_dir"
package_manifest="$release_zip.json"
package_checksum="$release_zip.checksum"
"$just_bin" test
"$just_bin" check-abi "$artifact"
"$just_bin" check-xcframework "$artifact"
"$just_bin" test-packaged-app-smoke
"$just_bin" inspect "$artifact" > "$output_dir/inspect.json"
"$just_bin" bench-cold-start > "$output_dir/cold-start.json"
"$just_bin" bench-first-eval > "$output_dir/first-eval.json"
"$just_bin" bench-idle-rss > "$output_dir/idle-rss.json"
"$just_bin" check-dep-delta > "$output_dir/dependency-delta.json"
"$just_bin" package-artifact "$artifact" "$release_zip" > "$output_dir/package.json"
cmp "$output_dir/package.json" "$package_manifest"
"$just_bin" package-artifact-verify "$artifact" > "$output_dir/package-reproducibility.json"
"$just_bin" checksum "$release_zip" > "$output_dir/swiftpm-checksum.txt"
release_checksum="$(tr -d '[:space:]' < "$output_dir/swiftpm-checksum.txt")"
if [[ ! "$release_checksum" =~ ^[0-9a-f]{64}$ ]]; then
  echo "invalid SwiftPM checksum in $output_dir/swiftpm-checksum.txt" >&2
  exit 1
fi
if [ "$release_checksum" != "$(tr -d '[:space:]' < "$package_checksum")" ]; then
  echo "SwiftPM checksum does not match package checksum sidecar" >&2
  exit 1
fi
# Test-only release URL evidence. M7-002 owns hosted SwiftPM resolution.
ECRITUM_RELEASE_RUNTIME_REQUIRED=1 \
  ECRITUM_RUNTIME_URL="https://example.invalid/EcritumRuntime.xcframework.zip" \
  ECRITUM_RUNTIME_CHECKSUM="$release_checksum" \
  swift package describe --type json > "$output_dir/release-manifest.json"
if [ -n "${ECRITUM_CONSUMER_ARTIFACT_URL:-}" ]; then
  "$just_bin" test-release-consumer-smoke "$ECRITUM_CONSUMER_ARTIFACT_URL" "${ECRITUM_CONSUMER_ARTIFACT_CHECKSUM:-$release_checksum}" > "$output_dir/clean-consumer.json"
elif [ -n "${ECRITUM_CONSUMER_ARTIFACT_CHECKSUM:-}" ]; then
  echo "ECRITUM_CONSUMER_ARTIFACT_CHECKSUM requires ECRITUM_CONSUMER_ARTIFACT_URL" >&2
  exit 1
else
  printf '%s\n' '{"ok":false,"skipped":true,"reason":"ECRITUM_CONSUMER_ARTIFACT_URL is not set; SwiftPM binary target URLs require https"}' > "$output_dir/clean-consumer.json"
fi
"$just_bin" license-report > "$output_dir/licenses.spdx.json"
"$just_bin" third-party-notices "$output_dir/THIRD_PARTY_NOTICES.md"
cmp "$output_dir/THIRD_PARTY_NOTICES.md" THIRD_PARTY_NOTICES.md
"$just_bin" check-license-texts "$artifact" "$output_dir/licenses.spdx.json" > "$output_dir/license-texts.json"
"$just_bin" check-license-texts-zip "$release_zip" "$output_dir/licenses.spdx.json" > "$output_dir/license-texts-zip.json"
python3 scripts/size-artifact.py --artifact "$artifact" --require-artifact > "$output_dir/size.json"
python3 scripts/license-report.py --strict > "$output_dir/licenses-strict.spdx.json"
