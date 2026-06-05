#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import stat
import zipfile
from pathlib import Path


NORMALIZED_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def should_skip(path):
    parts = set(path.parts)
    return path.name == ".DS_Store" or "__MACOSX" in parts or path.name.startswith("._")


parser = argparse.ArgumentParser(description="Create a deterministic SwiftPM release zip for EcritumRuntime.")
parser.add_argument("--artifact", default="dist/local/EcritumRuntime.xcframework")
parser.add_argument("--output", default="dist/release/EcritumRuntime.xcframework.zip")
args = parser.parse_args()

artifact = Path(args.artifact)
output = Path(args.output)
if not artifact.is_dir():
    raise SystemExit(f"missing artifact directory: {artifact}")

output.parent.mkdir(parents=True, exist_ok=True)
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
        info.external_attr = (mode or 0o644) << 16
        with open(path, "rb") as handle:
            archive.writestr(info, handle.read())

payload = {
    "artifact": str(artifact),
    "output": str(output),
    "sha256": sha256(output),
    "entries": len(files),
    "root": artifact.name,
    "normalizedTimestamp": "1980-01-01T00:00:00Z",
}
print(json.dumps(payload, indent=2, sort_keys=True))
