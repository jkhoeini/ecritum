#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import stat
import zipfile
from pathlib import Path


NORMALIZED_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
NORMALIZED_TIMESTAMP_TEXT = "1980-01-01T00:00:00Z"
RUNTIME_METADATA_NAME = "ecritum-runtime.json"
LEGACY_LANE_METADATA_NAME = "ecritum-runtime-lane.json"
DEFAULT_INCLUDED_RUNTIMES = ["clojure", "javascript", "lua"]
PROFILE_RUNTIMES = {
    "core": ["clojure"],
    "full": DEFAULT_INCLUDED_RUNTIMES,
}


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_record(path, rel):
    file_stat = path.stat()
    mode = stat.S_IMODE(file_stat.st_mode) or 0o644
    return {
        "path": str(rel),
        "mode": oct(mode),
        "size": file_stat.st_size,
        "sha256": sha256(path),
    }


def directory_sha256(files):
    digest = hashlib.sha256()
    for path, rel in sorted(files, key=lambda item: str(item[1])):
        digest.update(str(rel).encode())
        digest.update(b"\0")
        with open(path, "rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        digest.update(b"\0")
    return digest.hexdigest()


def should_skip(path):
    parts = set(path.parts)
    return path.name == ".DS_Store" or "__MACOSX" in parts or path.name.startswith("._")


def read_json(path, label):
    try:
        return json.loads(path.read_text())
    except json.JSONDecodeError as error:
        raise SystemExit(f"invalid {label}: {path}: {error}") from error


def metadata_paths(artifact):
    resources = artifact / "macos-arm64" / "EcritumRuntime.framework" / "Resources"
    return resources, resources / RUNTIME_METADATA_NAME, resources / LEGACY_LANE_METADATA_NAME


def runtime_metadata(artifact):
    resources, metadata_path, legacy_path = metadata_paths(artifact)
    if metadata_path.exists():
        payload = read_json(metadata_path, "runtime metadata")
        artifact_kind = payload.get("artifactKind")
        profile = payload.get("implementationProfile")
        included = payload.get("includedRuntimes")
        if artifact_kind != "default":
            raise SystemExit(f"runtime metadata artifactKind must be 'default': {metadata_path}")
        if profile not in PROFILE_RUNTIMES:
            raise SystemExit(f"invalid runtime metadata implementationProfile {profile!r}: {metadata_path}")
        if not isinstance(included, list) or not all(isinstance(item, str) for item in included):
            raise SystemExit(f"runtime metadata includedRuntimes must be a list of strings: {metadata_path}")
        source = str(metadata_path)
    elif legacy_path.exists():
        legacy = read_json(legacy_path, "legacy runtime lane metadata")
        profile = legacy.get("releaseLane")
        if profile not in PROFILE_RUNTIMES:
            raise SystemExit(f"invalid legacy runtime implementation profile: {profile!r}")
        included = PROFILE_RUNTIMES[profile]
        artifact_kind = "default" if profile == "full" else "internal"
        source = str(legacy_path)
    else:
        raise SystemExit(f"missing runtime metadata: {metadata_path}")

    missing = [runtime for runtime in DEFAULT_INCLUDED_RUNTIMES if runtime not in included]
    if artifact_kind != "default" or missing:
        raise SystemExit(
            "default release artifact must include clojure, javascript, and lua; "
            + f"artifactKind={artifact_kind!r}, missing={missing}"
        )
    return {
        "artifactKind": artifact_kind,
        "implementationProfile": profile,
        "includedRuntimes": included,
        "metadataSource": source,
        "resources": resources,
    }


def resource_inventory(resources):
    inventory = []
    for root, _, filenames in os.walk(resources):
        for filename in filenames:
            path = Path(root) / filename
            rel = path.relative_to(resources)
            if should_skip(rel):
                continue
            if rel.name == "libecritum_graal.dylib":
                kind = "native-runtime"
            elif rel.parts and rel.parts[0] == "Licenses":
                kind = "license"
            elif rel.name == RUNTIME_METADATA_NAME:
                kind = "runtime-metadata"
            elif rel.name == LEGACY_LANE_METADATA_NAME:
                kind = "legacy-runtime-metadata"
            else:
                kind = "resource"
            inventory.append({
                "kind": kind,
                "path": str(rel),
                "sha256": sha256(path),
                "size": path.stat().st_size,
            })
    return sorted(inventory, key=lambda item: item["path"])


parser = argparse.ArgumentParser(description="Create a deterministic SwiftPM release zip for EcritumRuntime.")
parser.add_argument("--artifact", default="dist/local/EcritumRuntime.xcframework")
parser.add_argument("--output", default="dist/release/EcritumRuntime.xcframework.zip")
parser.add_argument("--manifest", help="JSON manifest path. Defaults to OUTPUT.json.")
parser.add_argument("--checksum-output", help="Checksum file path. Defaults to OUTPUT.checksum.")
args = parser.parse_args()

artifact = Path(args.artifact)
output = Path(args.output)
manifest = Path(args.manifest) if args.manifest else Path(str(output) + ".json")
checksum_output = Path(args.checksum_output) if args.checksum_output else Path(str(output) + ".checksum")
if not artifact.is_dir():
    raise SystemExit(f"missing artifact directory: {artifact}")
metadata = runtime_metadata(artifact)

output.parent.mkdir(parents=True, exist_ok=True)
manifest.parent.mkdir(parents=True, exist_ok=True)
checksum_output.parent.mkdir(parents=True, exist_ok=True)
if output.exists():
    output.unlink()

files = []
for root, _, filenames in os.walk(artifact):
    for filename in filenames:
        path = Path(root) / filename
        rel = path.relative_to(artifact.parent)
        if not should_skip(rel):
            files.append((path, rel))

with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for path, rel in sorted(files, key=lambda item: str(item[1])):
        info = zipfile.ZipInfo(str(rel), NORMALIZED_TIMESTAMP)
        mode = stat.S_IMODE(path.stat().st_mode)
        info.create_system = 3
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = (mode or 0o644) << 16
        with open(path, "rb") as handle:
            archive.writestr(info, handle.read())

checksum = sha256(output)
payload = {
    "artifactKind": metadata["artifactKind"],
    "formatVersion": 1,
    "artifact": str(artifact),
    "artifactSha256": directory_sha256(files),
    "checksumAlgorithm": "sha256",
    "checksumFile": str(checksum_output),
    "output": str(output),
    "manifest": str(manifest),
    "implementationProfile": metadata["implementationProfile"],
    "includedRuntimes": metadata["includedRuntimes"],
    "metadataSource": metadata["metadataSource"],
    "resourceInventory": resource_inventory(metadata["resources"]),
    "sha256": checksum,
    "swiftPackageChecksum": checksum,
    "entries": len(files),
    "files": [file_record(path, rel) for path, rel in sorted(files, key=lambda item: str(item[1]))],
    "root": artifact.name,
    "slices": sorted(
        path.name
        for path in artifact.glob("*")
        if path.is_dir() and not should_skip(path.relative_to(artifact))
    ),
    "normalizedTimestamp": NORMALIZED_TIMESTAMP_TEXT,
}
manifest.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
checksum_output.write_text(checksum + "\n")
print(json.dumps(payload, indent=2, sort_keys=True))
