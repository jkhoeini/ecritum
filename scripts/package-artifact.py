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
    "formatVersion": 1,
    "artifact": str(artifact),
    "artifactSha256": directory_sha256(files),
    "checksumAlgorithm": "sha256",
    "checksumFile": str(checksum_output),
    "output": str(output),
    "manifest": str(manifest),
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
