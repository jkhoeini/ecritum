#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
import zipfile
from pathlib import Path


EXPECTED_TEXTS = {
    "GPL-2.0-only WITH Classpath-exception-2.0": {
        "file": "GPL-2.0-only_WITH_Classpath-exception-2.0.txt",
        "sha256": "11a8fe0c63dcff8bd8674b89a5895dfbcf5f7e5453cf0a33566c4b3fb64e404c",
        "source": "GraalVM Community 25.0.2 LICENSE_NATIVEIMAGE.txt",
    },
    "EPL-1.0": {
        "file": "EPL-1.0.txt",
        "sha256": "cc07bd2bd6ba843a9a2865ed891d5a3b5835a64bab6fa90945403ee53965d46f",
        "source": "org.babashka:sci:0.12.51 META-INF/leiningen/org.babashka/sci/LICENSE",
    },
    "EPL-2.0": {
        "file": "EPL-2.0.txt",
        "sha256": "5aa4cd44c111add178d1c2e2fe36d58a484012c80167df925f826cd64d411bf0",
        "source": "org.junit.jupiter:junit-jupiter:5.14.1 META-INF/LICENSE.md",
    },
    "ICU": {
        "file": "ICU.txt",
        "sha256": "e55522d81edc687a341a4411e0776e54ca654e90147f354a90458aaced4116af",
        "source": "https://raw.githubusercontent.com/unicode-org/icu/main/LICENSE",
    },
    "MIT": {
        "file": "MIT.txt",
        "sha256": "c3b1b78bc8bd3ea13aa4bc9778442d16560270afa235006d816e5e88cef24db4",
        "source": "https://spdx.org/licenses/MIT.txt",
    },
    "UPL-1.0": {
        "file": "UPL-1.0.txt",
        "sha256": "8ecddac84b6852e812a14695b27bbaa231451958508c2f75427d7c1b0db9ab7f",
        "source": "https://spdx.org/licenses/UPL-1.0.txt",
    },
}

EXTRA_TEXTS = {
    "LuaJ-MIT": {
        "file": "LuaJ-MIT.txt",
        "sha256": "94e20766f076ab59a1a444f385c204e4ae00d10ca6aa82323ee561a53b455909",
        "source": "http://luaj.sourceforge.net/license.txt",
    },
    "GraalVM-Third-Party-Licenses": {
        "file": "GraalVM-THIRD-PARTY-LICENSES.txt",
        "sha256": "2894dad2ce3342888a1d224be01ed16fc1167f8b70910944e03e10c3889836b4",
        "source": "GraalVM Community 25.0.2 THIRD_PARTY_LICENSE.txt",
    },
}

def sha256(path):
    import hashlib

    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_license_report(command):
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if completed.returncode != 0:
        raise SystemExit(
            "license report command failed with exit "
            + str(completed.returncode)
            + "\nstderr:\n"
            + completed.stderr
        )
    return json.loads(completed.stdout)


def package_scope(package):
    comment = package["annotations"][0]["comment"]
    for part in comment.split(";"):
        part = part.strip()
        if part.startswith("ecritum-scope="):
            return part.split("=", 1)[1]
    return "unknown"


def required_license_ids(report):
    ids = set()
    for package in report["packages"]:
        if package_scope(package) != "shipped":
            continue
        expression = package["licenseConcluded"]
        if expression == "NOASSERTION":
            continue
        if expression == "GPL-2.0-only WITH Classpath-exception-2.0":
            ids.add(expression)
            continue
        for token in expression.replace(" OR ", " AND ").split(" AND "):
            token = token.strip()
            if token:
                ids.add(token)
    return sorted(ids)


def expected_manifest():
    entries = []
    for license_id, data in {**EXPECTED_TEXTS, **EXTRA_TEXTS}.items():
        entries.append({
            "id": license_id,
            "file": data["file"],
            "sha256": data["sha256"],
            "source": data["source"],
        })
    return {
        "formatVersion": 1,
        "licenseTexts": sorted(entries, key=lambda item: item["id"]),
    }


def validate_bundle(bundle, required_ids):
    errors = []
    manifest_path = bundle / "manifest.json"
    if not manifest_path.is_file():
        errors.append(f"missing license text manifest: {manifest_path}")
        manifest = None
    else:
        manifest = json.loads(manifest_path.read_text())
        expected = expected_manifest()
        if manifest != expected:
            errors.append(f"license text manifest is stale: {manifest_path}")

    manifest_entries = manifest["licenseTexts"] if manifest else expected_manifest()["licenseTexts"]
    manifest_by_id = {item["id"]: item for item in manifest_entries}
    for item in manifest_entries:
        path = bundle / item["file"]
        if not path.is_file():
            errors.append(f"missing manifest license text {item['id']}: {path}")
            continue
        actual_sha = sha256(path)
        if actual_sha != item["sha256"]:
            errors.append(f"license text hash mismatch for {item['id']}: {path} sha256={actual_sha}, expected {item['sha256']}")

    for license_id in required_ids:
        if license_id not in manifest_by_id:
            errors.append(f"missing expected license-text policy for SPDX expression: {license_id}")
            continue
        if license_id not in EXPECTED_TEXTS:
            errors.append(f"missing expected license-text policy for SPDX expression: {license_id}")
            continue
        data = EXPECTED_TEXTS[license_id]
        if manifest_by_id[license_id] != {
            "id": license_id,
            "file": data["file"],
            "sha256": data["sha256"],
            "source": data["source"],
        }:
            errors.append(f"license text manifest rule is stale for {license_id}")
            continue

    return errors, manifest


def artifact_license_dir(artifact):
    candidates = sorted(artifact.glob("*/EcritumRuntime.framework/Resources/Licenses"))
    if len(candidates) != 1:
        raise SystemExit(f"expected one packaged Resources/Licenses directory in {artifact}, found {len(candidates)}")
    return candidates[0]


def validate_release_zip(release_zip, required_ids):
    errors = []
    expected = expected_manifest()
    with zipfile.ZipFile(release_zip) as archive:
        names = archive.namelist()
        manifest_names = [name for name in names if name.endswith("/EcritumRuntime.framework/Resources/Licenses/manifest.json")]
        if len(manifest_names) != 1:
            return [f"expected one packaged Resources/Licenses/manifest.json in {release_zip}, found {len(manifest_names)}"]
        manifest_name = manifest_names[0]
        prefix = manifest_name.rsplit("/", 1)[0] + "/"
        manifest = json.loads(archive.read(manifest_name).decode())
        if manifest != expected:
            errors.append(f"license text manifest is stale in release zip: {manifest_name}")
        manifest_by_id = {item["id"]: item for item in manifest["licenseTexts"]}
        for item in manifest["licenseTexts"]:
            entry_name = prefix + item["file"]
            if entry_name not in names:
                errors.append(f"missing manifest license text in release zip {item['id']}: {entry_name}")
                continue
            import hashlib

            actual_sha = hashlib.sha256(archive.read(entry_name)).hexdigest()
            if actual_sha != item["sha256"]:
                errors.append(
                    f"license text hash mismatch in release zip for {item['id']}: {entry_name} sha256={actual_sha}, expected {item['sha256']}"
                )
        for license_id in required_ids:
            if license_id not in manifest_by_id:
                errors.append(f"missing expected license-text policy in release zip for SPDX expression: {license_id}")
    return errors


parser = argparse.ArgumentParser(description="Verify checked-in and packaged third-party license texts.")
parser.add_argument("--bundle", default="THIRD_PARTY_LICENSES")
parser.add_argument("--artifact", default=None, help="Optional EcritumRuntime.xcframework artifact to verify.")
parser.add_argument("--release-zip", default=None, help="Optional packaged EcritumRuntime.xcframework.zip to verify.")
parser.add_argument("--write-manifest", action="store_true", help="Write the expected bundle manifest and exit.")
parser.add_argument("--license-report-command", nargs=argparse.REMAINDER, default=["python3", "scripts/license-report.py"])
args = parser.parse_args()
if not args.license_report_command:
    args.license_report_command = ["python3", "scripts/license-report.py"]

bundle = Path(args.bundle)
if args.write_manifest:
    bundle.mkdir(parents=True, exist_ok=True)
    (bundle / "manifest.json").write_text(json.dumps(expected_manifest(), indent=2, sort_keys=True) + "\n")
    raise SystemExit(0)

report = run_license_report(args.license_report_command)
required_ids = required_license_ids(report)
errors, manifest = validate_bundle(bundle, required_ids)
artifact_bundle = None
if args.artifact:
    artifact_bundle = artifact_license_dir(Path(args.artifact))
    artifact_errors, _ = validate_bundle(artifact_bundle, required_ids)
    errors.extend(f"artifact: {error}" for error in artifact_errors)
if args.release_zip:
    zip_errors = validate_release_zip(Path(args.release_zip), required_ids)
    errors.extend(f"release zip: {error}" for error in zip_errors)

payload = {
    "artifactBundle": str(artifact_bundle) if artifact_bundle else None,
    "bundle": str(bundle),
    "expectedLicenseIds": sorted(EXPECTED_TEXTS),
    "ok": not errors,
    "releaseZip": args.release_zip,
    "requiredLicenseIds": required_ids,
    "violations": errors,
}
print(json.dumps(payload, indent=2, sort_keys=True))
raise SystemExit(0 if not errors else 1)
