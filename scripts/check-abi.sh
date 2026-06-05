#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: check-abi.sh [--manifest PATH] [--artifact PATH]

Validate the public Ecritum C ABI header against the checked ABI manifest, the
Java status constants, and the packaged runtime symbols when an artifact exists.
USAGE
}

manifest="docs/abi/ecritum-c-abi.json"
artifact="dist/local/EcritumRuntime.xcframework"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --manifest) manifest="$2"; shift 2 ;;
    --artifact) artifact="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

python3 - "$manifest" "$artifact" <<'PY'
import json
import re
import subprocess
import sys
from pathlib import Path

manifest_path = Path(sys.argv[1])
artifact = Path(sys.argv[2])

def fail(message):
    print(message, file=sys.stderr)
    raise SystemExit(1)

if not manifest_path.exists():
    fail(f"missing ABI manifest: {manifest_path}")

manifest = json.loads(manifest_path.read_text())
header_path = Path(manifest["publicHeader"])
java_status_path = Path(manifest["javaStatus"])

if not header_path.exists():
    fail(f"missing public ABI header: {header_path}")
if not java_status_path.exists():
    fail(f"missing Java status source: {java_status_path}")

header = header_path.read_text()
java_status = java_status_path.read_text()

c_constants = {
    match.group(1): int(match.group(2))
    for match in re.finditer(r"^#define\s+(ECRITUM_[A-Z0-9_]+)\s+([0-9]+)\s*$", header, re.MULTILINE)
}
java_constants = {
    match.group(1): int(match.group(2))
    for match in re.finditer(r"public\s+static\s+final\s+int\s+([A-Z0-9_]+)\s*=\s*([0-9]+)\s*;", java_status)
}

errors = []
for entry in manifest["statusConstants"]:
    c_name = entry["c"]
    java_name = entry["java"]
    expected = int(entry["value"])
    if c_constants.get(c_name) != expected:
        errors.append(f"{c_name} expected {expected}, found {c_constants.get(c_name)!r}")
    if java_constants.get(java_name) != expected:
        errors.append(f"{java_name} expected {expected}, found {java_constants.get(java_name)!r}")

normalized_header = re.sub(r"\s+", " ", header)
for function in manifest["functions"]:
    declaration = re.sub(r"\s+", " ", function["declaration"]).strip()
    if declaration not in normalized_header:
        errors.append(f"missing public declaration: {function['declaration']}")

framework_binary = artifact / "macos-arm64" / "EcritumRuntime.framework" / "EcritumRuntime"
if artifact.exists():
    if not framework_binary.exists():
        errors.append(f"missing artifact binary: {framework_binary}")
    else:
        symbols = subprocess.check_output(["nm", "-gU", str(framework_binary)], text=True)
        for function in manifest["functions"]:
            symbol = function["machoSymbol"]
            if not re.search(rf"\sT\s+{re.escape(symbol)}$", symbols, re.MULTILINE):
                errors.append(f"missing public artifact symbol: {symbol}")
        for symbol in manifest["privateSymbols"]:
            if re.search(rf"\s[A-Za-z]\s+{re.escape(symbol)}$", symbols, re.MULTILINE):
                errors.append(f"private symbol leaked from artifact: {symbol}")

if errors:
    for error in errors:
        print(error, file=sys.stderr)
    raise SystemExit(1)
PY
