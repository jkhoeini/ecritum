#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path


def file_size(path):
    return path.stat().st_size if path.exists() else None


def directory_size(path):
    total = 0
    for root, _, filenames in os.walk(path):
        for filename in filenames:
            total += (Path(root) / filename).stat().st_size
    return total


parser = argparse.ArgumentParser(description="Emit the Ecritum runtime artifact size baseline as JSON.")
parser.add_argument("--artifact", default="dist/local/EcritumRuntime.xcframework")
parser.add_argument("--require-artifact", action="store_true")
parser.add_argument("--max-artifact-bytes", type=int, default=25_000_000)
parser.add_argument("--max-wrapper-bytes", type=int, default=1_000_000)
parser.add_argument("--max-private-runtime-bytes", type=int, default=20_000_000)
args = parser.parse_args()

artifact = Path(args.artifact)
framework = artifact / "macos-arm64" / "EcritumRuntime.framework"
wrapper = framework / "EcritumRuntime"
private_runtime = framework / "Resources" / "libecritum_graal.dylib"

payload = {
    "artifact": str(artifact),
    "exists": artifact.exists(),
    "budgets": {
        "artifact_bytes": args.max_artifact_bytes,
        "wrapper_bytes": args.max_wrapper_bytes,
        "private_runtime_bytes": args.max_private_runtime_bytes,
    },
    "sizes": {
        "artifact_bytes": directory_size(artifact) if artifact.exists() else None,
        "wrapper_bytes": file_size(wrapper),
        "private_runtime_bytes": file_size(private_runtime),
    },
    "violations": [],
}

if args.require_artifact and not artifact.exists():
    payload["violations"].append("artifact missing")

if artifact.exists():
    checks = [
        ("artifact_bytes", args.max_artifact_bytes),
        ("wrapper_bytes", args.max_wrapper_bytes),
        ("private_runtime_bytes", args.max_private_runtime_bytes),
    ]
    for key, budget in checks:
        value = payload["sizes"][key]
        if value is None:
            payload["violations"].append(f"{key} missing")
        elif value > budget:
            payload["violations"].append(f"{key} {value} exceeds budget {budget}")

payload["ok"] = not payload["violations"]
print(json.dumps(payload, indent=2, sort_keys=True))
raise SystemExit(0 if payload["ok"] else 1)
