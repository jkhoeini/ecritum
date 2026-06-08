#!/usr/bin/env python3
import argparse
import hashlib
import json
import plistlib
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


DEVELOPER_ID_APPLICATION = "Developer ID Application:"


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path, label, violations):
    try:
        return json.loads(path.read_text())
    except FileNotFoundError:
        violations.append(f"missing {label}: {path}")
    except json.JSONDecodeError as error:
        violations.append(f"invalid {label}: {path}: {error}")
    return {}


def command_text(completed):
    return (completed.stdout + "\n" + completed.stderr).strip()


def run_command(command):
    try:
        return subprocess.run(
            command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except FileNotFoundError as error:
        return subprocess.CompletedProcess(command, 127, "", str(error))


def discover_signed_code(artifact, violations):
    if not artifact.is_dir():
        violations.append(f"missing artifact directory: {artifact}")
        return []

    frameworks = sorted(artifact.glob("*/EcritumRuntime.framework"))
    if not frameworks:
        violations.append(f"artifact contains no EcritumRuntime.framework slices: {artifact}")
        return []

    paths = []
    seen = set()
    for framework in frameworks:
        executable = framework / "EcritumRuntime"
        if executable.is_file():
            paths.append(executable)
            seen.add(executable)
        else:
            violations.append(f"missing framework executable: {executable}")

        for dylib in sorted(framework.rglob("*.dylib")):
            if dylib not in seen:
                paths.append(dylib)
                seen.add(dylib)

    if not paths:
        violations.append(f"artifact contains no signed code: {artifact}")
    return sorted(paths, key=lambda item: str(item))


def detail_values(text, key):
    prefix = key + "="
    return [line[len(prefix):].strip() for line in text.splitlines() if line.startswith(prefix)]


def has_hardened_runtime(details):
    return (
        bool(detail_values(details, "Runtime Version"))
        or bool(re.search(r"flags=.*\bruntime\b", details, flags=re.IGNORECASE))
    )


def has_secure_timestamp(details):
    timestamps = detail_values(details, "Timestamp")
    return any(value and value.lower() != "none" for value in timestamps)


def get_task_allow_true(entitlements_text):
    if not entitlements_text.strip():
        return False
    try:
        payload = plistlib.loads(entitlements_text.encode())
        return payload.get("com.apple.security.get-task-allow") is True
    except Exception:
        return bool(re.search(
            r"<key>\s*com\.apple\.security\.get-task-allow\s*</key>\s*<true\s*/>",
            entitlements_text,
            flags=re.DOTALL,
        ))


def verify_code_signature(path, artifact):
    relative_path = str(path.relative_to(artifact))
    violations = []

    verify = run_command(["codesign", "--verify", "--deep", "--strict", "--verbose=2", str(path)])
    if verify.returncode != 0:
        violations.append(f"{relative_path}: codesign verification failed: {command_text(verify)}")

    details_result = run_command(["codesign", "-dv", "--verbose=4", str(path)])
    details = command_text(details_result)
    if details_result.returncode != 0:
        violations.append(f"{relative_path}: unable to read codesign details: {details}")

    entitlements_result = run_command(["codesign", "-d", "--entitlements", ":-", str(path)])
    entitlements = entitlements_result.stdout or command_text(entitlements_result)

    authorities = detail_values(details, "Authority")
    cd_hash = next(iter(detail_values(details, "CDHash")), "")
    team_identifier = next(iter(detail_values(details, "TeamIdentifier")), "")
    signature = next(iter(detail_values(details, "Signature")), "")
    timestamp_value = next(iter(detail_values(details, "Timestamp")), "")
    runtime = has_hardened_runtime(details)
    timestamp = has_secure_timestamp(details)
    developer_id = any(value.startswith(DEVELOPER_ID_APPLICATION) for value in authorities)

    if signature.lower() == "adhoc" or "Signature=adhoc" in details:
        violations.append(f"{relative_path}: public release cannot use an ad-hoc signature")
    if not developer_id:
        violations.append(f"{relative_path}: signature chain does not include Developer ID Application authority")
    if not team_identifier or team_identifier.lower() == "not set":
        violations.append(f"{relative_path}: missing Developer ID team identifier")
    if not runtime:
        violations.append(f"{relative_path}: missing hardened runtime signing option")
    if not timestamp:
        violations.append(f"{relative_path}: missing secure timestamp")
    if get_task_allow_true(entitlements):
        violations.append(f"{relative_path}: get-task-allow entitlement is enabled")

    return {
        "authorities": authorities,
        "cdHash": cd_hash,
        "developerIdApplication": developer_id,
        "detailsCommand": ["codesign", "-dv", "--verbose=4", str(path)],
        "hardenedRuntime": runtime,
        "ok": not violations,
        "path": str(path),
        "relativePath": relative_path,
        "secureTimestamp": timestamp,
        "teamIdentifier": team_identifier,
        "timestamp": timestamp_value,
        "verifyCommand": ["codesign", "--verify", "--deep", "--strict", "--verbose=2", str(path)],
        "violations": violations,
    }


def unwrap(payload, *keys):
    for key in keys:
        value = payload.get(key)
        if isinstance(value, dict):
            return value
    return payload


def first_value(payload, *keys):
    for key in keys:
        value = payload.get(key)
        if value not in (None, ""):
            return str(value)
    return ""


def matching_sha_values(payload):
    values = []
    for key in ["sha256", "releaseZipSha256", "archiveSha256"]:
        value = payload.get(key)
        if value:
            values.append((key, str(value).lower()))
    return values


def require_matching_sha(payloads, release_zip_sha, label, violations):
    values = []
    for payload in payloads:
        values.extend(matching_sha_values(payload))
    if not values:
        violations.append(f"{label} is missing release zip SHA-256 binding")
        return
    for key, value in values:
        if value != release_zip_sha:
            violations.append(f"{label} {key} does not match release zip SHA-256")


def validate_package_sidecars(release_zip, release_zip_sha, package_manifest, violations):
    checksum_sidecar = Path(str(release_zip) + ".checksum")
    if checksum_sidecar.exists():
        if checksum_sidecar.read_text().strip() != release_zip_sha:
            violations.append("release zip checksum sidecar does not match release zip SHA-256")
    else:
        violations.append(f"missing release zip checksum sidecar: {checksum_sidecar}")

    manifest_path = Path(package_manifest) if package_manifest else Path(str(release_zip) + ".json")
    manifest = load_json(manifest_path, "package manifest evidence", violations)
    if manifest:
        for key in ["sha256", "swiftPackageChecksum"]:
            if str(manifest.get(key, "")).lower() != release_zip_sha:
                violations.append(f"package manifest {key} does not match release zip SHA-256")
    return {
        "artifactKind": str(manifest.get("artifactKind", "")) if manifest else "",
        "checksumSidecar": str(checksum_sidecar),
        "manifest": str(manifest_path),
        "swiftPackageChecksum": str(manifest.get("swiftPackageChecksum", "")) if manifest else "",
    }


def validate_notarization(args, release_zip, release_zip_sha, violations):
    submit_payload = load_json(Path(args.notary_submit_json), "notarytool submit evidence", violations)
    log_payload = load_json(Path(args.notary_log_json), "notarytool log evidence", violations)
    submit = unwrap(submit_payload, "notarytoolSubmit", "submission", "submit")
    log = unwrap(log_payload, "notarytoolLog", "log")

    submission_id = first_value(submit, "id", "jobId", "submissionId")
    submit_status = first_value(submit, "status")
    if not submission_id:
        violations.append("notarytool submit evidence is missing submission id")
    if submit_status != "Accepted":
        violations.append(f"notarytool submit status is {submit_status!r}, expected 'Accepted'")

    submit_name = first_value(submit, "name")
    if submit_name and Path(submit_name).name != release_zip.name:
        violations.append(
            f"notarytool submit name {submit_name!r} does not match release zip {release_zip.name!r}"
        )
    require_matching_sha([submit_payload, submit], release_zip_sha, "notarytool submit evidence", violations)

    log_job_id = first_value(log, "jobId", "id", "submissionId")
    log_status = first_value(log, "status")
    if not log_job_id:
        violations.append("notarytool log evidence is missing job id")
    if submission_id and log_job_id and submission_id.lower() != log_job_id.lower():
        violations.append("notarytool submit id does not match log job id")
    if log_status != "Accepted":
        violations.append(f"notarytool log status is {log_status!r}, expected 'Accepted'")

    issues = log.get("issues", [])
    if issues is None:
        issues = []
    if not isinstance(issues, list):
        violations.append("notarytool log issues field must be a list")
        issues = []
    error_issues = [issue for issue in issues if str(issue.get("severity", "")).lower() == "error"]
    if error_issues:
        messages = ", ".join(str(issue.get("message", "unknown error")) for issue in error_issues)
        violations.append(f"notarytool log contains error issues: {messages}")
    require_matching_sha([log_payload, log], release_zip_sha, "notarytool log evidence", violations)

    return {
        "errorIssueCount": len(error_issues),
        "jobId": log_job_id,
        "status": submit_status,
        "submissionId": submission_id,
        "warningIssueCount": len([
            issue for issue in issues if str(issue.get("severity", "")).lower() == "warning"
        ]),
    }


def validate_zip_exception(path, release_zip, release_zip_sha, submission_id, violations):
    payload = load_json(Path(path), "stapling exception evidence", violations)
    archive_format = first_value(payload, "artifactFormat", "archiveFormat", "format").lower()
    if archive_format != "zip":
        violations.append("stapling exception artifactFormat must be 'zip'")

    reason = first_value(payload, "reason")
    if len(reason) < 20:
        violations.append("stapling exception must include a specific reason")
    if not first_value(payload, "acceptedBy", "owner"):
        violations.append("stapling exception must include acceptedBy")
    if not first_value(payload, "acceptedDate", "date"):
        violations.append("stapling exception must include acceptedDate")
    if not first_value(payload, "source", "adr"):
        violations.append("stapling exception must include source or adr")

    exception_submission_id = first_value(payload, "notarizationSubmissionId", "submissionId", "jobId")
    if submission_id and exception_submission_id and submission_id.lower() != exception_submission_id.lower():
        violations.append("stapling exception submission id does not match notarization submission id")
    archive_name = first_value(payload, "archive", "releaseZip")
    if archive_name and Path(archive_name).name != release_zip.name:
        violations.append("stapling exception archive name does not match release zip")
    require_matching_sha([payload], release_zip_sha, "stapling exception evidence", violations)

    return {
        "acceptedBy": first_value(payload, "acceptedBy", "owner"),
        "archiveFormat": archive_format,
        "mode": "zip-exception",
        "reason": reason,
        "source": first_value(payload, "source", "adr"),
    }


def validate_stapler_evidence(path, release_zip_sha, submission_id, violations):
    payload = load_json(Path(path), "stapler validation evidence", violations)
    status = first_value(payload, "status").lower()
    if status not in ("accepted", "ok", "validated"):
        violations.append(f"stapler validation status is {status!r}, expected accepted/ok/validated")
    if payload.get("exitCode") not in (0, "0"):
        violations.append("stapler validation exitCode must be 0")
    evidence_submission_id = first_value(payload, "notarizationSubmissionId", "submissionId", "jobId")
    if submission_id and evidence_submission_id and submission_id.lower() != evidence_submission_id.lower():
        violations.append("stapler validation submission id does not match notarization submission id")
    require_matching_sha([payload], release_zip_sha, "stapler validation evidence", violations)
    return {
        "mode": "stapler-validation",
        "status": status,
        "target": first_value(payload, "target"),
    }


def validate_stapling(args, release_zip, release_zip_sha, submission_id, violations):
    if release_zip.suffix.lower() == ".zip":
        if not args.stapling_exception_json:
            violations.append("SwiftPM zip artifacts require --stapling-exception-json")
            return {"mode": "missing-zip-exception"}
        return validate_zip_exception(
            args.stapling_exception_json,
            release_zip,
            release_zip_sha,
            submission_id,
            violations,
        )

    if not args.stapler_evidence_json:
        violations.append("non-zip public artifacts require --stapler-evidence-json")
        return {"mode": "missing-stapler-validation"}
    return validate_stapler_evidence(
        args.stapler_evidence_json,
        release_zip_sha,
        submission_id,
        violations,
    )


def safe_extract_release_zip(release_zip, violations):
    tmp = tempfile.TemporaryDirectory(prefix="ecritum-public-signing-unpack-")
    root = Path(tmp.name)
    try:
        with zipfile.ZipFile(release_zip) as archive:
            for info in archive.infolist():
                target = root / info.filename
                try:
                    target.resolve().relative_to(root.resolve())
                except ValueError:
                    violations.append(f"release zip contains unsafe path: {info.filename}")
                    continue
                archive.extract(info, root)
    except zipfile.BadZipFile as error:
        violations.append(f"invalid release zip: {release_zip}: {error}")
        return tmp, root / "EcritumRuntime.xcframework"

    artifact = root / "EcritumRuntime.xcframework"
    if not artifact.is_dir():
        violations.append("release zip does not contain EcritumRuntime.xcframework at archive root")
    return tmp, artifact


parser = argparse.ArgumentParser(
    description="Validate public Developer ID signing, notarization, and stapling evidence for an Ecritum release artifact."
)
parser.add_argument("--artifact", required=True, help="EcritumRuntime.xcframework directory.")
parser.add_argument("--release-zip", required=True, help="Deterministic SwiftPM release archive.")
parser.add_argument("--notary-submit-json", required=True, help="JSON from xcrun notarytool submit --wait --output-format json.")
parser.add_argument("--notary-log-json", required=True, help="JSON downloaded by xcrun notarytool log.")
parser.add_argument("--package-manifest", help="Package manifest JSON. Defaults to RELEASE_ZIP.json.")
parser.add_argument("--stapling-exception-json", help="Accepted exception JSON for archive formats that cannot be stapled directly.")
parser.add_argument("--stapler-evidence-json", help="JSON evidence from xcrun stapler validate for staple-capable artifacts.")
args = parser.parse_args()

artifact = Path(args.artifact)
release_zip = Path(args.release_zip)
violations = []
if not release_zip.is_file():
    violations.append(f"missing release zip: {release_zip}")
release_zip_sha = sha256(release_zip) if release_zip.is_file() else ""
package = validate_package_sidecars(release_zip, release_zip_sha, args.package_manifest, violations)

code_records = []
for code_path in discover_signed_code(artifact, violations):
    record = verify_code_signature(code_path, artifact)
    code_records.append(record)
    violations.extend(record["violations"])

post_unpack_records = []
extracted = None
if release_zip.is_file():
    extracted, unpacked_artifact = safe_extract_release_zip(release_zip, violations)
    for code_path in discover_signed_code(unpacked_artifact, violations):
        record = verify_code_signature(code_path, unpacked_artifact)
        post_unpack_records.append(record)
        violations.extend(record["violations"])

notarization = validate_notarization(args, release_zip, release_zip_sha, violations)
stapling = validate_stapling(
    args,
    release_zip,
    release_zip_sha,
    notarization.get("submissionId", ""),
    violations,
)

payload = {
    "artifact": str(artifact),
    "codeSignatures": code_records,
    "notarization": notarization,
    "ok": not violations,
    "package": package,
    "postUnpackCodeSignatures": post_unpack_records,
    "releaseZip": str(release_zip),
    "releaseZipSha256": release_zip_sha,
    "stapling": stapling,
    "violations": violations,
}
print(json.dumps(payload, indent=2, sort_keys=True))
if extracted:
    extracted.cleanup()
raise SystemExit(0 if payload["ok"] else 1)
