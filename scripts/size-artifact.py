#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path


# Budgets raised for the five-language default artifact (Clojure+JS+Lua+Python+
# Ruby) per ADR-0028. The owner accepted an 800 MB ceiling so the single default
# artifact (ADR-0025) is not size-blocked. baseline_artifact_bytes and the
# included-runtime sets reflect the measured five-language artifact integrated in
# M12-002 (476,886,393 bytes measured locally); warn_artifact_bytes is set ~15%
# above the measured baseline and remains well below the 800 MB hard cap.
DEFAULT_BUDGETS = {
    "max_artifact_bytes": 800_000_000,
    "max_wrapper_bytes": 262_144,
    "max_private_runtime_bytes": 760_000_000,
    "warn_artifact_bytes": 548_000_000,
    "baseline_artifact_bytes": 476_886_393,
}
DEFAULT_INCLUDED_RUNTIMES = ["clojure", "javascript", "lua", "python", "ruby"]
PROFILE_RUNTIMES = {
    "core": ["clojure"],
    "full": DEFAULT_INCLUDED_RUNTIMES,
}


def file_size(path):
    return path.stat().st_size if path.exists() else None


def directory_size(path):
    total = 0
    for root, _, filenames in os.walk(path):
        for filename in filenames:
            total += (Path(root) / filename).stat().st_size
    return total


def read_json(path):
    try:
        return json.loads(path.read_text())
    except json.JSONDecodeError:
        return None


def artifact_metadata(artifact):
    resources = artifact / "macos-arm64" / "EcritumRuntime.framework" / "Resources"
    metadata_path = resources / "ecritum-runtime.json"
    legacy_path = resources / "ecritum-runtime-lane.json"
    if metadata_path.exists():
        payload = read_json(metadata_path)
        if not isinstance(payload, dict):
            return {"artifactKind": "invalid", "implementationProfile": "invalid", "includedRuntimes": [], "metadataSource": str(metadata_path)}
        return {
            "artifactKind": payload.get("artifactKind"),
            "implementationProfile": payload.get("implementationProfile"),
            "includedRuntimes": payload.get("includedRuntimes") if isinstance(payload.get("includedRuntimes"), list) else [],
            "metadataSource": str(metadata_path),
        }
    if legacy_path.exists():
        payload = read_json(legacy_path)
        profile = payload.get("releaseLane") if isinstance(payload, dict) else "invalid"
        return {
            "artifactKind": "default" if profile == "full" else "internal",
            "implementationProfile": profile,
            "includedRuntimes": PROFILE_RUNTIMES.get(profile, []),
            "metadataSource": str(legacy_path),
        }
    return {"artifactKind": None, "implementationProfile": None, "includedRuntimes": [], "metadataSource": None}


parser = argparse.ArgumentParser(description="Emit the Ecritum runtime artifact size baseline as JSON.")
parser.add_argument("--artifact", default="dist/local/EcritumRuntime.xcframework")
parser.add_argument("--require-artifact", action="store_true")
parser.add_argument("--max-artifact-bytes", type=int)
parser.add_argument("--max-wrapper-bytes", type=int)
parser.add_argument("--max-private-runtime-bytes", type=int)
parser.add_argument("--warn-artifact-bytes", type=int)
parser.add_argument("--baseline-artifact-bytes", type=int)
args = parser.parse_args()

max_artifact_bytes = args.max_artifact_bytes if args.max_artifact_bytes is not None else DEFAULT_BUDGETS["max_artifact_bytes"]
max_wrapper_bytes = args.max_wrapper_bytes if args.max_wrapper_bytes is not None else DEFAULT_BUDGETS["max_wrapper_bytes"]
max_private_runtime_bytes = (
    args.max_private_runtime_bytes
    if args.max_private_runtime_bytes is not None
    else DEFAULT_BUDGETS["max_private_runtime_bytes"]
)
warn_artifact_bytes = args.warn_artifact_bytes if args.warn_artifact_bytes is not None else DEFAULT_BUDGETS["warn_artifact_bytes"]
baseline_artifact_bytes = (
    args.baseline_artifact_bytes
    if args.baseline_artifact_bytes is not None
    else DEFAULT_BUDGETS["baseline_artifact_bytes"]
)

artifact = Path(args.artifact)
framework = artifact / "macos-arm64" / "EcritumRuntime.framework"
wrapper = framework / "EcritumRuntime"
private_runtime = framework / "Resources" / "libecritum_graal.dylib"
metadata = artifact_metadata(artifact) if artifact.exists() else {"artifactKind": None, "implementationProfile": None, "includedRuntimes": [], "metadataSource": None}

payload = {
    "artifact": str(artifact),
    "artifactKind": metadata["artifactKind"],
    "exists": artifact.exists(),
    "implementationProfile": metadata["implementationProfile"],
    "includedRuntimes": metadata["includedRuntimes"],
    "metadataSource": metadata["metadataSource"],
    "budgets": {
        "artifact_bytes": max_artifact_bytes,
        "wrapper_bytes": max_wrapper_bytes,
        "private_runtime_bytes": max_private_runtime_bytes,
    },
    "sizes": {
        "artifact_bytes": directory_size(artifact) if artifact.exists() else None,
        "wrapper_bytes": file_size(wrapper),
        "private_runtime_bytes": file_size(private_runtime),
    },
    "violations": [],
    "warnings": [],
}

if args.require_artifact and not artifact.exists():
    payload["violations"].append("artifact missing")

if artifact.exists():
    if payload["artifactKind"] != "default":
        payload["violations"].append(f"artifactKind {payload['artifactKind']!r} is not 'default'")
    missing_runtimes = [runtime for runtime in DEFAULT_INCLUDED_RUNTIMES if runtime not in payload["includedRuntimes"]]
    if missing_runtimes:
        payload["violations"].append("includedRuntimes missing required default runtimes: " + ", ".join(missing_runtimes))
    checks = [
        ("artifact_bytes", max_artifact_bytes),
        ("wrapper_bytes", max_wrapper_bytes),
        ("private_runtime_bytes", max_private_runtime_bytes),
    ]
    for key, budget in checks:
        value = payload["sizes"][key]
        if value is None:
            payload["violations"].append(f"{key} missing")
        elif value > budget:
            payload["violations"].append(f"{key} {value} exceeds budget {budget}")

    artifact_size = payload["sizes"]["artifact_bytes"]
    if artifact_size is not None:
        if artifact_size > warn_artifact_bytes:
            payload["warnings"].append(f"artifact_bytes {artifact_size} exceeds warning threshold {warn_artifact_bytes}")
        ten_percent_growth = int(baseline_artifact_bytes * 1.10)
        if artifact_size > ten_percent_growth:
            payload["warnings"].append(f"artifact_bytes {artifact_size} exceeds 10% growth threshold {ten_percent_growth}")

payload["ok"] = not payload["violations"]
print(json.dumps(payload, indent=2, sort_keys=True))
raise SystemExit(0 if payload["ok"] else 1)
