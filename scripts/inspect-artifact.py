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
    for root, _, filenames in os.walk(path):
        for filename in sorted(filenames):
            file_path = Path(root) / filename
            relpath = file_path.relative_to(path)
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

payload = {
    "artifact": str(artifact),
    "artifact_sha256": directory_sha256(artifact),
    "framework": str(framework),
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
    "headers": headers,
    "resources": resources,
    "public_symbols": [line.strip() for line in run(["nm", "-gU", str(binary)])],
    "install_name": run(["otool", "-D", str(binary)])[1:],
    "linked_dylibs": run(["otool", "-L", str(binary)])[1:],
    "embedded_runtime_list": ["GraalVM Native Image", "SCI Clojure", "GraalJS", "Ecritum C wrapper"],
}

print(json.dumps(payload, indent=2, sort_keys=True))
