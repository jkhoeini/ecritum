#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: release-check.sh
       release-check.sh [--lane core|full] [--output-dir PATH] [--artifact PATH] [--release-zip PATH] [--community] [--public] [--notary-submit-json PATH] [--notary-log-json PATH] [--stapling-exception-json PATH] [--stapler-evidence-json PATH]

Run the release gates. This command exits nonzero when any release blocker is
present, including unknown shipped licenses.
USAGE
}

lane="core"
output_dir=""
artifact="dist/local/EcritumRuntime.xcframework"
artifact_was_set="0"
release_zip=""
just_bin="${JUST:-just}"
community_release="0"
public_release="0"
notary_submit_json=""
notary_log_json=""
stapling_exception_json=""
stapler_evidence_json=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    --lane|--output-dir|--artifact|--release-zip|--notary-submit-json|--notary-log-json|--stapling-exception-json|--stapler-evidence-json)
      if [ "$#" -lt 2 ]; then
        echo "missing value for $1" >&2
        usage >&2
        exit 2
      fi
      case "$1" in
        --lane) lane="$2" ;;
        --output-dir) output_dir="$2" ;;
        --artifact) artifact="$2"; artifact_was_set="1" ;;
        --release-zip) release_zip="$2" ;;
        --notary-submit-json) notary_submit_json="$2" ;;
        --notary-log-json) notary_log_json="$2" ;;
        --stapling-exception-json) stapling_exception_json="$2" ;;
        --stapler-evidence-json) stapler_evidence_json="$2" ;;
      esac
      shift 2
      ;;
    --community|--community-release) community_release="1"; shift ;;
    --public|--public-release) public_release="1"; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done
if [ "$lane" != "core" ] && [ "$lane" != "full" ]; then
  echo "invalid release lane: $lane" >&2
  usage >&2
  exit 2
fi
if [ "$community_release" = "1" ] && [ "$public_release" = "1" ]; then
  echo "release mode cannot be both --community and --public" >&2
  usage >&2
  exit 2
fi
if [ -z "$output_dir" ]; then
  output_dir="build/release/$lane"
fi
if [ "$artifact_was_set" = "0" ]; then
  artifact="dist/$lane/EcritumRuntime.xcframework"
fi
if [ -z "$release_zip" ]; then
  release_zip="dist/release/$lane/EcritumRuntime.xcframework.zip"
fi
if [ "$public_release" = "1" ]; then
  if [ -z "$notary_submit_json" ] || [ -z "$notary_log_json" ]; then
    echo "public release requires --notary-submit-json and --notary-log-json" >&2
    usage >&2
    exit 2
  fi
  if [ -z "$stapling_exception_json" ] && [ -z "$stapler_evidence_json" ]; then
    echo "public release requires --stapling-exception-json or --stapler-evidence-json" >&2
    usage >&2
    exit 2
  fi
fi
if [ "$community_release" = "1" ] && [ -z "${ECRITUM_CONSUMER_ARTIFACT_URL:-}" ]; then
  echo "community release requires ECRITUM_CONSUMER_ARTIFACT_URL for hosted SwiftPM consumer validation" >&2
  exit 1
fi

mkdir -p "$output_dir"
package_manifest="$release_zip.json"
package_checksum="$release_zip.checksum"
sbom_file="$release_zip.spdx.json"
"$just_bin" test
"$just_bin" check-abi "$artifact"
"$just_bin" check-xcframework "$artifact"
"$just_bin" test-packaged-app-smoke
"$just_bin" inspect "$artifact" > "$output_dir/inspect.json"
"$just_bin" bench-cold-start > "$output_dir/cold-start.json"
"$just_bin" bench-first-eval > "$output_dir/first-eval.json"
"$just_bin" bench-idle-rss > "$output_dir/idle-rss.json"
"$just_bin" check-dep-delta "$lane" > "$output_dir/dependency-delta.json"
"$just_bin" package-artifact "$artifact" "$release_zip" "$lane" > "$output_dir/package.json"
cmp "$output_dir/package.json" "$package_manifest"
"$just_bin" package-artifact-verify "$artifact" "$lane" > "$output_dir/package-reproducibility.json"
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

release_manifest_url="https://example.invalid/EcritumRuntime.xcframework.zip"
release_manifest_checksum="$release_checksum"
if [ "$public_release" = "1" ] || [ "$community_release" = "1" ]; then
  if [ -z "${ECRITUM_CONSUMER_ARTIFACT_URL:-}" ]; then
    echo "public release requires ECRITUM_CONSUMER_ARTIFACT_URL for hosted SwiftPM consumer validation" >&2
    exit 1
  fi
  release_manifest_url="$ECRITUM_CONSUMER_ARTIFACT_URL"
  release_manifest_checksum="${ECRITUM_CONSUMER_ARTIFACT_CHECKSUM:-$release_checksum}"
fi
ECRITUM_RELEASE_RUNTIME_REQUIRED=1 \
  ECRITUM_RUNTIME_URL="$release_manifest_url" \
  ECRITUM_RUNTIME_CHECKSUM="$release_manifest_checksum" \
  swift package describe --type json > "$output_dir/release-manifest.json"
if [ -n "${ECRITUM_CONSUMER_ARTIFACT_URL:-}" ]; then
  "$just_bin" test-release-consumer-smoke "$ECRITUM_CONSUMER_ARTIFACT_URL" "${ECRITUM_CONSUMER_ARTIFACT_CHECKSUM:-$release_checksum}" "$release_zip" > "$output_dir/clean-consumer.json"
elif [ -n "${ECRITUM_CONSUMER_ARTIFACT_CHECKSUM:-}" ]; then
  echo "ECRITUM_CONSUMER_ARTIFACT_CHECKSUM requires ECRITUM_CONSUMER_ARTIFACT_URL" >&2
  exit 1
elif [ "$public_release" = "1" ]; then
  echo "public release requires hosted clean-consumer validation" >&2
  exit 1
else
  printf '%s\n' '{"ok":false,"skipped":true,"reason":"ECRITUM_CONSUMER_ARTIFACT_URL is not set; SwiftPM binary target URLs require https"}' > "$output_dir/clean-consumer.json"
fi
if [ "$public_release" = "1" ]; then
  "$just_bin" check-public-signing "$artifact" "$release_zip" "$notary_submit_json" "$notary_log_json" "$stapling_exception_json" "$stapler_evidence_json" "$package_manifest" > "$output_dir/public-signing.json"
elif [ "$community_release" = "1" ]; then
  printf '%s\n' '{"ok":false,"mode":"community","skipped":true,"reason":"community release does not claim Developer ID signing, notarization, or stapling"}' > "$output_dir/public-signing.json"
else
  printf '%s\n' '{"ok":false,"skipped":true,"reason":"release-check was not run in public mode"}' > "$output_dir/public-signing.json"
fi
"$just_bin" sbom "$output_dir/licenses.spdx.json" "$lane"
cp "$output_dir/licenses.spdx.json" "$sbom_file"
"$just_bin" third-party-notices "$output_dir/THIRD_PARTY_NOTICES.md" full
cmp "$output_dir/THIRD_PARTY_NOTICES.md" THIRD_PARTY_NOTICES.md
"$just_bin" check-license-texts "$artifact" "$output_dir/licenses.spdx.json" > "$output_dir/license-texts.json"
"$just_bin" check-license-texts-zip "$release_zip" "$output_dir/licenses.spdx.json" > "$output_dir/license-texts-zip.json"
"$just_bin" check-vulnerability-response "$release_zip" "$output_dir/licenses.spdx.json" "${ECRITUM_CONSUMER_ARTIFACT_URL:-}" > "$output_dir/vulnerability-response.json"
python3 scripts/size-artifact.py --artifact "$artifact" --lane "$lane" --require-artifact > "$output_dir/size.json"
"$just_bin" license-report-strict "$lane" > "$output_dir/licenses-strict.spdx.json"
