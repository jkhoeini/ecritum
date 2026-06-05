#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: release-check.sh

Run the M1 release gates. This command exits nonzero when any release blocker is
present, including unknown shipped licenses.
USAGE
}

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  usage
  exit 0
fi
if [ "$#" -ne 0 ]; then
  echo "unknown argument: $1" >&2
  usage >&2
  exit 2
fi

mkdir -p build/release
just test
just check-abi
just check-xcframework
just test-packaged-app-smoke
just inspect > build/release/inspect.json
just bench-cold-start > build/release/cold-start.json
just bench-first-eval > build/release/first-eval.json
just bench-idle-rss > build/release/idle-rss.json
just check-dep-delta > build/release/dependency-delta.json
just package-artifact > build/release/package.json
just license-report > build/release/licenses.spdx.json
python3 scripts/size-artifact.py --require-artifact > build/release/size.json
python3 scripts/license-report.py --strict > build/release/licenses-strict.spdx.json
