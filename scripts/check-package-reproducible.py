#!/usr/bin/env python3
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


NORMALIZED_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(command):
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if completed.returncode != 0:
        raise RuntimeError(
            "command failed: "
            + " ".join(str(part) for part in command)
            + "\nstdout:\n"
            + completed.stdout
            + "\nstderr:\n"
            + completed.stderr
        )
    return completed.stdout


def package_once(script, artifact, output, manifest, checksum_file, release_lane):
    stdout = run([
        sys.executable,
        str(script),
        "--artifact",
        str(artifact),
        "--output",
        str(output),
        "--manifest",
        str(manifest),
        "--checksum-output",
        str(checksum_file),
        "--release-lane",
        release_lane,
    ])
    return json.loads(stdout)


def zip_metadata(path):
    with zipfile.ZipFile(path) as archive:
        return [
            {
                "filename": info.filename,
                "date_time": list(info.date_time),
                "compress_type": info.compress_type,
                "external_attr": info.external_attr,
                "file_size": info.file_size,
                "crc": info.CRC,
            }
            for info in archive.infolist()
        ]


def validate_metadata(records):
    errors = []
    names = [record["filename"] for record in records]
    if names != sorted(names):
        errors.append("zip entries are not sorted")
    for name in names:
        parts = set(Path(name).parts)
        if Path(name).name == ".DS_Store" or "__MACOSX" in parts or Path(name).name.startswith("._"):
            errors.append(f"macOS metadata entry leaked into zip: {name}")
    for record in records:
        if tuple(record["date_time"]) != NORMALIZED_TIMESTAMP:
            errors.append(f"zip entry timestamp is not normalized: {record['filename']}")
        if record["compress_type"] != zipfile.ZIP_DEFLATED:
            errors.append(f"zip entry is not deflated: {record['filename']}")
    return errors


parser = argparse.ArgumentParser(description="Verify deterministic EcritumRuntime release packaging.")
parser.add_argument("--artifact", default="dist/local/EcritumRuntime.xcframework")
parser.add_argument("--package-script", default="scripts/package-artifact.py")
parser.add_argument("--release-lane", choices=["core", "full"], default="core")
args = parser.parse_args()

artifact = Path(args.artifact)
script = Path(args.package_script)
if not artifact.is_dir():
    raise SystemExit(f"missing artifact directory: {artifact}")
if not script.is_file():
    raise SystemExit(f"missing package script: {script}")

with tempfile.TemporaryDirectory(prefix="ecritum-package-repro-") as tmp:
    root = Path(tmp)
    first_zip = root / "first.zip"
    second_zip = root / "second.zip"
    first_manifest = root / "first.json"
    second_manifest = root / "second.json"
    first_checksum = root / "first.checksum"
    second_checksum = root / "second.checksum"

    first = package_once(script, artifact, first_zip, first_manifest, first_checksum, args.release_lane)
    second = package_once(script, artifact, second_zip, second_manifest, second_checksum, args.release_lane)
    first_sha = sha256(first_zip)
    second_sha = sha256(second_zip)
    first_metadata = zip_metadata(first_zip)
    second_metadata = zip_metadata(second_zip)
    swiftpm_checksum = run(["swift", "package", "compute-checksum", str(first_zip)]).strip()

    errors = []
    if first_sha != second_sha:
        errors.append("repeated package sha256 values differ")
    if first_zip.read_bytes() != second_zip.read_bytes():
        errors.append("repeated package bytes differ")
    if first.get("sha256") != first_sha:
        errors.append("first package JSON sha256 does not match zip bytes")
    if second.get("sha256") != second_sha:
        errors.append("second package JSON sha256 does not match zip bytes")
    if first_checksum.read_text().strip() != first_sha:
        errors.append("first checksum file does not match zip bytes")
    if second_checksum.read_text().strip() != second_sha:
        errors.append("second checksum file does not match zip bytes")
    if swiftpm_checksum != first_sha:
        errors.append("SwiftPM checksum does not match package sha256")
    if first_metadata != second_metadata:
        errors.append("repeated package zip metadata differs")
    if first.get("releaseLane") != args.release_lane:
        errors.append("first package JSON releaseLane does not match requested lane")
    if second.get("releaseLane") != args.release_lane:
        errors.append("second package JSON releaseLane does not match requested lane")
    errors.extend(validate_metadata(first_metadata))

    payload = {
        "artifact": str(artifact),
        "entries": first.get("entries"),
        "releaseLane": args.release_lane,
        "normalizedTimestamp": first.get("normalizedTimestamp"),
        "ok": not errors,
        "packageSha256": first_sha,
        "swiftPackageChecksum": swiftpm_checksum,
        "violations": errors,
    }
    print(json.dumps(payload, indent=2, sort_keys=True))
    raise SystemExit(0 if not errors else 1)
