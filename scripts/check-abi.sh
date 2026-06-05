#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: check-abi.sh [--manifest PATH] [--artifact PATH]

Validate the public Ecritum C ABI header against the checked ABI manifest,
Swift and Java status mappings, and packaged runtime symbols when an artifact
exists.
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
swift_status_path = Path(manifest["swiftStatus"])
swift_job_state_path = Path(manifest["swiftJobState"])

for label, path in {
    "public ABI header": header_path,
    "Java status source": java_status_path,
    "Swift status source": swift_status_path,
    "Swift job state source": swift_job_state_path,
}.items():
    if not path.exists():
        fail(f"missing {label}: {path}")

header = header_path.read_text()
java_status = java_status_path.read_text()
swift_status = swift_status_path.read_text()
swift_job_state = swift_job_state_path.read_text()

def normalize(text):
    return re.sub(r"\s+", " ", text).strip()

def expected_map(entries, name_key):
    return {entry[name_key]: int(entry["value"]) for entry in entries}

def check_exact_map(label, expected, actual, errors):
    for name, value in expected.items():
        if actual.get(name) != value:
            errors.append(f"{label} {name} expected {value}, found {actual.get(name)!r}")
    extra = sorted(set(actual) - set(expected))
    missing = sorted(set(expected) - set(actual))
    if extra:
        errors.append(f"{label} has unmanifested constants: {', '.join(extra)}")
    if missing:
        errors.append(f"{label} is missing manifest constants: {', '.join(missing)}")

def swift_enum_cases(source, enum_name):
    match = re.search(rf"\benum\s+{re.escape(enum_name)}\b[^\{{]*\{{", source)
    if not match:
        return {}
    depth = 1
    index = match.end()
    while index < len(source) and depth > 0:
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
        index += 1
    body = source[match.end():index - 1]
    return {
        case.group(1): int(case.group(2))
        for case in re.finditer(r"\bcase\s+([A-Za-z][A-Za-z0-9_]*)\s*=\s*([0-9]+)", body)
    }

def command_text(args):
    return subprocess.check_output(args, text=True)

def exported_symbols(binary):
    output = command_text(["nm", "-gU", str(binary)])
    symbols = set()
    for line in output.splitlines():
        match = re.search(r"\s[A-Za-z]\s+(_[A-Za-z0-9_]+)$", line)
        if match:
            symbols.add(match.group(1))
    return symbols

def dylib_id_versions(binary):
    output = command_text(["otool", "-l", str(binary)])
    in_id_command = False
    current = None
    compatibility = None
    for line in output.splitlines():
        stripped = line.strip()
        if stripped.startswith("cmd "):
            in_id_command = stripped == "cmd LC_ID_DYLIB"
            continue
        if not in_id_command:
            continue
        if stripped.startswith("current version "):
            current = stripped.removeprefix("current version ").strip()
        elif stripped.startswith("compatibility version "):
            compatibility = stripped.removeprefix("compatibility version ").strip()
        if current is not None and compatibility is not None:
            break
    return current, compatibility

c_constants = {
    match.group(1): int(match.group(2))
    for match in re.finditer(r"^#define\s+(ECRITUM_[A-Z0-9_]+)\s+([0-9]+)\s*$", header, re.MULTILINE)
}
java_constants = {
    match.group(1): int(match.group(2))
    for match in re.finditer(r"public\s+static\s+final\s+int\s+([A-Z0-9_]+)\s*=\s*([0-9]+)\s*;", java_status)
}

errors = []

check_exact_map(
    "C status",
    expected_map(manifest["statusConstants"], "c"),
    {name: value for name, value in c_constants.items() if name == "ECRITUM_OK" or name.startswith("ECRITUM_ERROR_")},
    errors,
)
check_exact_map("Java status", expected_map(manifest["statusConstants"], "java"), java_constants, errors)
check_exact_map("Swift status", expected_map(manifest["statusConstants"], "swift"), swift_enum_cases(swift_status, "EcritumStatus"), errors)
check_exact_map(
    "C buffer constant",
    expected_map(manifest["bufferConstants"], "name"),
    {name: value for name, value in c_constants.items() if name.endswith("_BUFFER_SIZE")},
    errors,
)
check_exact_map(
    "C value kind",
    expected_map(manifest["valueKindConstants"], "c"),
    {name: value for name, value in c_constants.items() if name.startswith("ECRITUM_VALUE_KIND_")},
    errors,
)
check_exact_map(
    "C job state",
    expected_map(manifest["jobStateConstants"], "c"),
    {name: value for name, value in c_constants.items() if name.startswith("ECRITUM_JOB_")},
    errors,
)
check_exact_map("Swift job state", expected_map(manifest["jobStateConstants"], "swift"), swift_enum_cases(swift_job_state, "EcritumJobState"), errors)

normalized_header = normalize(header)

expected_typedefs = {entry["name"]: entry["underlying"] for entry in manifest["typedefs"]}
actual_typedefs = {
    match.group(2): match.group(1)
    for match in re.finditer(r"^typedef\s+([A-Za-z0-9_]+)\s+(ecritum_[A-Za-z0-9_]+_t)\s*;", header, re.MULTILINE)
}
for name, underlying in expected_typedefs.items():
    if actual_typedefs.get(name) != underlying:
        errors.append(f"typedef {name} expected {underlying}, found {actual_typedefs.get(name)!r}")
extra_typedefs = sorted(set(actual_typedefs) - set(expected_typedefs))
if extra_typedefs:
    errors.append(f"public handle typedefs not in manifest: {', '.join(extra_typedefs)}")

for struct in manifest["structs"]:
    if normalize(struct["declaration"]) not in normalized_header:
        errors.append(f"missing public struct declaration: {struct['declaration']}")

for callback in manifest["callbacks"]:
    if normalize(callback["declaration"]) not in normalized_header:
        errors.append(f"missing public callback declaration: {callback['declaration']}")

for function in manifest["functions"]:
    if normalize(function["declaration"]) not in normalized_header:
        errors.append(f"missing public declaration: {function['declaration']}")

expected_function_names = {function["name"] for function in manifest["functions"]}
actual_function_names = {
    match.group(1)
    for match in re.finditer(r"\bint\s+(ecritum_[A-Za-z0-9_]+)\s*\(", header)
}
extra_functions = sorted(actual_function_names - expected_function_names)
missing_functions = sorted(expected_function_names - actual_function_names)
if extra_functions:
    errors.append(f"public function declarations not in manifest: {', '.join(extra_functions)}")
if missing_functions:
    errors.append(f"manifest functions missing from header parser: {', '.join(missing_functions)}")

if artifact.exists():
    frameworks = sorted(artifact.glob("*/EcritumRuntime.framework"))
    if not frameworks:
        errors.append(f"missing framework slices in artifact: {artifact}")
    for framework in frameworks:
        binary = framework / "EcritumRuntime"
        packaged_header = framework / manifest.get("artifactHeader", "Headers/ecritum.h")
        if not binary.exists():
            errors.append(f"missing artifact binary: {binary}")
            continue
        if not packaged_header.exists():
            errors.append(f"missing artifact header: {packaged_header}")
        elif packaged_header.read_text() != header:
            errors.append(f"packaged public header differs from source header: {packaged_header}")

        symbols = exported_symbols(binary)
        expected_symbols = {function["machoSymbol"] for function in manifest["functions"]}
        for symbol in expected_symbols:
            if symbol not in symbols:
                errors.append(f"missing public artifact symbol: {symbol}")

        extra_public = sorted(symbol for symbol in symbols if symbol.startswith("_ecritum_") and symbol not in expected_symbols)
        if extra_public:
            errors.append(f"unmanifested public artifact symbols in {binary}: {', '.join(extra_public)}")

        for symbol in manifest["privateSymbols"]:
            if symbol in symbols:
                errors.append(f"private symbol leaked from artifact: {symbol}")
        for pattern in manifest.get("privateSymbolPatterns", []):
            leaked = sorted(symbol for symbol in symbols if re.match(pattern, symbol))
            if leaked:
                errors.append(f"private symbol pattern {pattern} leaked from artifact: {', '.join(leaked)}")

        macho = manifest.get("machO", {})
        current, compatibility = dylib_id_versions(binary)
        if macho.get("currentVersion") and current != macho["currentVersion"]:
            errors.append(f"{binary} current_version expected {macho['currentVersion']}, found {current!r}")
        if macho.get("compatibilityVersion") and compatibility != macho["compatibilityVersion"]:
            errors.append(f"{binary} compatibility_version expected {macho['compatibilityVersion']}, found {compatibility!r}")

if errors:
    for error in errors:
        print(error, file=sys.stderr)
    raise SystemExit(1)
PY
