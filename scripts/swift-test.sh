#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
package_root="$(cd "$script_dir/.." && pwd)"
cd "$package_root"

usage() {
  cat <<'USAGE'
Usage: swift-test.sh --mode scaffold|runtime

Run Swift tests and reset SwiftPM only when local runtime artifact availability
changes. This avoids stale manifest state without forcing every test run to be a
clean rebuild.
USAGE
}

mode=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --mode) mode="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

case "$mode" in
  scaffold|runtime) ;;
  *) echo "--mode must be scaffold or runtime" >&2; usage >&2; exit 2 ;;
esac

artifact="dist/local/EcritumRuntime.xcframework"
state_file="build/swift-test/ecritum-runtime-artifact-state"
if [ -d "$artifact" ]; then
  artifact_fingerprint="$(python3 - "$artifact" <<'PY'
import hashlib
import os
import sys

root = sys.argv[1]
digest = hashlib.sha256()
for directory, _, filenames in os.walk(root):
    for filename in sorted(filenames):
        path = os.path.join(directory, filename)
        relpath = os.path.relpath(path, root)
        digest.update(relpath.encode())
        digest.update(b"\0")
        with open(path, "rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        digest.update(b"\0")
print(digest.hexdigest())
PY
)"
  state="v4:$mode:runtime-present:$artifact_fingerprint"
else
  state="v4:$mode:runtime-missing"
fi

if [ "$mode" = "runtime" ] && [ ! -d "$artifact" ]; then
  echo "missing $artifact; run mise exec -- just xcframework first" >&2
  exit 1
fi

if [ "$mode" = "runtime" ]; then
  export ECRITUM_LOCAL_RUNTIME=1
else
  export ECRITUM_LOCAL_RUNTIME=0
fi
export ECRITUM_LOCAL_RUNTIME_STATE="$state"

previous_state=""
if [ -f "$state_file" ]; then
  previous_state="$(cat "$state_file")"
fi

if [ "$previous_state" != "$state" ]; then
  swift package reset
  rm -rf .build
fi

mkdir -p "$(dirname "$state_file")"
swift test
printf '%s\n' "$state" > "$state_file"
