#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path


def run(command):
    return subprocess.check_output(command, text=True).splitlines()


def run_status(command):
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    output = (completed.stdout + completed.stderr).strip()
    return {"ok": completed.returncode == 0, "output": output}


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def directory_sha256(path):
    digest = hashlib.sha256()
    files = []
    for root, _, filenames in os.walk(path):
        for filename in sorted(filenames):
            file_path = Path(root) / filename
            files.append((file_path.relative_to(path), file_path))
    for relpath, file_path in sorted(files, key=lambda item: str(item[0])):
        digest.update(str(relpath).encode())
        digest.update(b"\0")
        with open(file_path, "rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        digest.update(b"\0")
    return digest.hexdigest()


def macos_min_version(path):
    lines = run(["otool", "-l", str(path)])
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("minos "):
            return stripped.split()[1]
    return None


def archs(path):
    line = run(["lipo", "-archs", str(path)])
    return line[0].split() if line else []


def nm_lines(path):
    return [line.strip() for line in run(["nm", "-gU", str(path)])] if path.exists() else []


def symbol_names(lines):
    names = set()
    for line in lines:
        parts = line.split()
        if parts and parts[-1].startswith("_"):
            names.add(parts[-1])
    return names


def embedded_runtimes(private_symbols, has_wrapper):
    runtimes = []
    if private_symbols:
        runtimes.append("GraalVM Native Image")
    if "_ecritum_graal_eval_clojure" in private_symbols or "_ecritum_graal_eval_clojure_with_stdlib" in private_symbols:
        runtimes.append("SCI Clojure")
    if "_ecritum_graal_eval_javascript_with_stdlib" in private_symbols:
        runtimes.append("GraalJS")
    if "_ecritum_graal_eval_lua_with_stdlib" in private_symbols:
        runtimes.append("LuaJ JME")
    if has_wrapper:
        runtimes.append("Ecritum C wrapper")
    return runtimes


def runtime_metadata(framework):
    resources = framework / "Resources"
    metadata_path = resources / "ecritum-runtime.json"
    legacy_path = resources / "ecritum-runtime-lane.json"
    if metadata_path.exists():
        with open(metadata_path) as handle:
            payload = json.load(handle)
        payload["metadataSource"] = str(metadata_path)
        return payload
    if legacy_path.exists():
        with open(legacy_path) as handle:
            legacy = json.load(handle)
        profile = legacy.get("releaseLane")
        return {
            "artifactKind": "default" if profile == "full" else "internal",
            "formatVersion": 1,
            "implementationProfile": profile,
            "includedRuntimes": ["clojure", "javascript", "lua"] if profile == "full" else ["clojure"],
            "metadataSource": str(legacy_path),
        }
    return {
        "artifactKind": None,
        "formatVersion": None,
        "implementationProfile": None,
        "includedRuntimes": [],
        "metadataSource": None,
    }


parser = argparse.ArgumentParser(description="Inspect the local EcritumRuntime XCFramework.")
parser.add_argument("--artifact", default="dist/local/EcritumRuntime.xcframework")
args = parser.parse_args()

artifact = Path(args.artifact)
framework = artifact / "macos-arm64" / "EcritumRuntime.framework"
binary = framework / "EcritumRuntime"
private_lib = framework / "Resources" / "libecritum_graal.dylib"

if not binary.exists():
    raise SystemExit(f"missing artifact binary: {binary}")

headers = sorted(str(path.relative_to(framework)) for path in (framework / "Headers").glob("**/*") if path.is_file())
resources = sorted(str(path.relative_to(framework)) for path in (framework / "Resources").glob("**/*") if path.is_file())
public_symbols = nm_lines(binary)
private_symbols = symbol_names(nm_lines(private_lib))

payload = {
    "artifact": str(artifact),
    "artifact_sha256": directory_sha256(artifact),
    "framework": str(framework),
    "framework_codesign": run_status(["codesign", "--verify", "--verbose=2", str(framework)]),
    "binary": str(binary),
    "binary_architectures": archs(binary),
    "binary_macos_min": macos_min_version(binary),
    "binary_size_bytes": binary.stat().st_size,
    "binary_sha256": sha256(binary),
    "binary_codesign": run_status(["codesign", "--verify", "--verbose=2", str(binary)]),
    "private_runtime": str(private_lib),
    "private_runtime_architectures": archs(private_lib) if private_lib.exists() else [],
    "private_runtime_macos_min": macos_min_version(private_lib) if private_lib.exists() else None,
    "private_runtime_size_bytes": private_lib.stat().st_size if private_lib.exists() else None,
    "private_runtime_sha256": sha256(private_lib) if private_lib.exists() else None,
    "private_runtime_codesign": run_status(["codesign", "--verify", "--verbose=2", str(private_lib)]) if private_lib.exists() else None,
    "runtime_metadata": runtime_metadata(framework),
    "headers": headers,
    "resources": resources,
    "public_symbols": public_symbols,
    "install_name": run(["otool", "-D", str(binary)])[1:],
    "linked_dylibs": run(["otool", "-L", str(binary)])[1:],
    "embedded_runtime_list": embedded_runtimes(private_symbols, binary.exists()),
}

print(json.dumps(payload, indent=2, sort_keys=True))
