#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
from pathlib import Path


PROTOCOL_VERSION = 1
REQUIRED_CAPABILITIES = {
    "filesystem",
    "network",
    "process",
    "environment",
    "reflection",
    "class_loading",
    "native_loading",
    "unrestricted_java_lookup",
    "raw_polyglot_access",
    "raw_host_object_access",
    "raw_c_handle_access",
    "classpath_mutation",
}
REQUIRED_PHASES = {"normal_eval", "timeout", "cancellation", "callback_error", "cleanup"}
REQUIRED_TARGETED_CASES = {
    "filesystem.allowed_root.inside",
    "filesystem.allowed_root.outside_denied",
    "network.redirect_to_denied_host",
    "host.callback_scope.use_after_return",
}
VALID_STATUSES = {"pass", "fail", "pending_capability", "error"}


class RunnerError(Exception):
    pass


def emit(payload):
    print(json.dumps(payload, indent=2, sort_keys=True))


def fail(message):
    print(message, file=sys.stderr)
    raise SystemExit(2)


def require_type(value, expected, path):
    if not isinstance(value, expected):
        raise RunnerError(f"{path} must be {expected.__name__}")


def require_string_list(value, path):
    require_type(value, list, path)
    seen = set()
    for index, item in enumerate(value):
        if not isinstance(item, str) or item == "":
            raise RunnerError(f"{path}[{index}] must be a non-empty string")
        if item in seen:
            raise RunnerError(f"{path} contains duplicate value {item!r}")
        seen.add(item)


def load_manifest(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            manifest = json.load(handle)
    except OSError as exc:
        raise RunnerError(f"cannot read manifest: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise RunnerError(f"invalid manifest JSON: {exc}") from exc
    return manifest


def expand_manifest(manifest):
    require_type(manifest, dict, "manifest")
    if manifest.get("schemaVersion") != 1:
        raise RunnerError("manifest.schemaVersion must be 1")
    if manifest.get("suite") != "ecritum-security-abuse":
        raise RunnerError("manifest.suite must be ecritum-security-abuse")

    matrix = manifest.get("matrix")
    if not isinstance(matrix, dict):
        raise RunnerError("missing required abuse case: filesystem.normal_eval")
    capabilities = matrix.get("capabilities")
    phases = matrix.get("phases")
    require_string_list(capabilities, "manifest.matrix.capabilities")
    require_string_list(phases, "manifest.matrix.phases")

    matrix_pairs = {(capability, phase) for capability in capabilities for phase in phases}
    required_pairs = {(capability, phase) for capability in REQUIRED_CAPABILITIES for phase in REQUIRED_PHASES}
    missing_pairs = sorted(required_pairs - matrix_pairs)
    if missing_pairs:
        capability, phase = missing_pairs[0]
        raise RunnerError(f"missing required abuse case: {capability}.{phase}")

    cases = []
    for capability, phase in sorted(matrix_pairs):
        cases.append(
            {
                "caseId": f"{capability}.{phase}.denied",
                "title": f"{capability} denied during {phase}",
                "capability": capability,
                "phase": phase,
                "required": True,
                "expected": {"status": "ECRITUM_ERROR_PERMISSION_DENIED"},
            }
        )

    targeted = manifest.get("targetedCases")
    require_type(targeted, list, "manifest.targetedCases")
    seen_targeted = set()
    for index, case in enumerate(targeted):
        path = f"manifest.targetedCases[{index}]"
        require_type(case, dict, path)
        for field in ("caseId", "title", "capability", "phase"):
            if not isinstance(case.get(field), str) or case[field] == "":
                raise RunnerError(f"{path}.{field} must be a non-empty string")
        if not isinstance(case.get("required"), bool):
            raise RunnerError(f"{path}.required must be boolean")
        if case["caseId"] in REQUIRED_TARGETED_CASES and not case["required"]:
            raise RunnerError(f"required targeted abuse case cannot be optional: {case['caseId']}")
        if not isinstance(case.get("expected"), dict):
            raise RunnerError(f"{path}.expected must be object")
        seen_targeted.add(case["caseId"])
        cases.append(case)

    missing_targeted = sorted(REQUIRED_TARGETED_CASES - seen_targeted)
    if missing_targeted:
        raise RunnerError(f"missing required abuse case: {missing_targeted[0]}")

    case_ids = [case["caseId"] for case in cases]
    if len(case_ids) != len(set(case_ids)):
        raise RunnerError("duplicate abuse case id")
    return sorted(cases, key=lambda case: case["caseId"])


def run_provider(command, cases, timeout_seconds):
    if not command:
        raise RunnerError("--provider requires a command")
    request = {"protocolVersion": PROTOCOL_VERSION, "cases": cases}
    try:
        completed = subprocess.run(
            command,
            input=json.dumps(request, sort_keys=True),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=timeout_seconds,
        )
    except OSError as exc:
        raise RunnerError(f"cannot start provider: {exc}") from exc
    except subprocess.TimeoutExpired as exc:
        raise RunnerError(f"provider timed out after {timeout_seconds} seconds") from exc

    if completed.returncode != 0:
        raise RunnerError(f"provider exited with status {completed.returncode}: {completed.stderr.strip()}")
    try:
        payload = json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise RunnerError(f"invalid provider JSON: {exc}") from exc
    if completed.stderr.strip():
        print(completed.stderr.strip(), file=sys.stderr)
    return payload


def validate_provider_payload(payload, cases):
    require_type(payload, dict, "provider output")
    if payload.get("protocolVersion") != PROTOCOL_VERSION:
        raise RunnerError("provider output protocolVersion must be 1")
    provider = payload.get("provider")
    require_type(provider, dict, "provider")
    if not isinstance(provider.get("id"), str) or provider["id"] == "":
        raise RunnerError("provider.id must be non-empty string")
    require_string_list(provider.get("capabilities"), "provider.capabilities")
    results = payload.get("results")
    require_type(results, list, "provider.results")
    selected = {case["caseId"] for case in cases}
    by_id = {}
    for index, result in enumerate(results):
        path = f"provider.results[{index}]"
        require_type(result, dict, path)
        case_id = result.get("caseId")
        if case_id not in selected:
            raise RunnerError(f"{path}.caseId is not selected: {case_id}")
        if case_id in by_id:
            raise RunnerError(f"provider returned duplicate result for {case_id}")
        status = result.get("status")
        if status not in VALID_STATUSES:
            raise RunnerError(f"{path}.status must be one of {sorted(VALID_STATUSES)}")
        reason = result.get("reason", "")
        if status != "pass" and (not isinstance(reason, str) or reason.strip() == ""):
            raise RunnerError(f"{path}.{status} requires non-empty reason")
        by_id[case_id] = result
    missing = sorted(selected - set(by_id))
    if missing:
        raise RunnerError("provider omitted result(s): " + ", ".join(missing))
    return provider, by_id


def expectation_error(case, result):
    if result["status"] != "pass":
        return None

    expected = case.get("expected")
    if not isinstance(expected, dict):
        return None

    actual = result.get("actual")
    if not isinstance(actual, dict):
        return "missing actual.status"
    if "status" in expected:
        if "status" not in actual:
            return "missing actual.status"
        if actual["status"] != expected["status"]:
            return "actual.status does not match expected"
    return None


def normalize_result(case, provider_capabilities, result):
    status = result["status"]
    reason = result.get("reason", "")
    required_capability = case["capability"]
    provider_has_capability = required_capability in provider_capabilities

    if status == "pass" and not provider_has_capability:
        status = "fail"
        reason = "provider passed case without declaring required capabilities: " + required_capability
    elif status == "pending_capability" and provider_has_capability:
        status = "fail"
        reason = "provider declares required capabilities but returned pending_capability"
    else:
        error = expectation_error(case, result)
        if error:
            status = "fail"
            reason = error

    normalized = {
        "caseId": case["caseId"],
        "title": case["title"],
        "capability": case["capability"],
        "phase": case["phase"],
        "required": case["required"],
        "status": status,
        "reason": reason,
    }
    if "actual" in result:
        normalized["actual"] = result["actual"]
    if "evidence" in result:
        normalized["evidence"] = result["evidence"]
    return normalized


def summarize(provider, cases, results, strict):
    normalized = []
    counts = {"pass": 0, "fail": 0, "pending_capability": 0, "error": 0}
    provider_capabilities = set(provider["capabilities"])
    for case in cases:
        normalized_result = normalize_result(case, provider_capabilities, results[case["caseId"]])
        counts[normalized_result["status"]] += 1
        normalized.append(normalized_result)

    security_conformant = all(case["status"] == "pass" for case in normalized if case["required"])
    required_pending = any(case["required"] and case["status"] == "pending_capability" for case in normalized)
    has_failure = counts["fail"] > 0 or counts["error"] > 0
    ok = not has_failure and not (strict and required_pending)
    return {
        "ok": ok,
        "securityConformant": security_conformant,
        "strict": strict,
        "provider": provider,
        "summary": {
            "total": len(normalized),
            "passed": counts["pass"],
            "pending": counts["pending_capability"],
            "failed": counts["fail"],
            "errors": counts["error"],
        },
        "cases": normalized,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Run the Ecritum security abuse baseline.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "stdout: deterministic JSON.\n"
            "stderr: runner/provider diagnostics.\n"
            "exit 0: baseline succeeded; exit 1: strict pending/failures; exit 2: runner/provider error."
        ),
    )
    parser.add_argument("--manifest", default="Tests/Security/abuse-manifest.json")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--validate-manifest", action="store_true")
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--provider-timeout-seconds", type=float, default=10.0)
    parser.add_argument("--provider", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    try:
        manifest = load_manifest(Path(args.manifest))
        cases = expand_manifest(manifest)
        if args.validate_manifest:
            emit({"ok": True, "cases": len(cases)})
            return 0
        if args.list:
            emit({"schemaVersion": 1, "suite": "ecritum-security-abuse", "cases": cases})
            return 0
        payload = run_provider(args.provider or [], cases, args.provider_timeout_seconds)
        provider, results = validate_provider_payload(payload, cases)
        summary = summarize(provider, cases, results, args.strict)
        emit(summary)
        return 0 if summary["ok"] else 1
    except RunnerError as exc:
        fail(str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
