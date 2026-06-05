#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
from pathlib import Path


PROTOCOL_VERSION = 1
VALID_STATUSES = {"pass", "fail", "pending_capability", "error"}


class RunnerError(Exception):
    pass


def emit_json(payload):
    print(json.dumps(payload, indent=2, sort_keys=True))


def fail_runner(message):
    print(message, file=sys.stderr)
    raise SystemExit(2)


def load_manifest(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            manifest = json.load(handle)
    except OSError as exc:
        raise RunnerError(f"cannot read manifest: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise RunnerError(f"invalid manifest JSON: {exc}") from exc
    validate_manifest(manifest)
    return manifest


def require_type(value, expected_type, path):
    if not isinstance(value, expected_type):
        raise RunnerError(f"{path} must be {expected_type.__name__}")


def require_string_list(value, path):
    require_type(value, list, path)
    seen = set()
    for index, item in enumerate(value):
        if not isinstance(item, str) or item == "":
            raise RunnerError(f"{path}[{index}] must be a non-empty string")
        if item in seen:
            raise RunnerError(f"{path} contains duplicate value {item!r}")
        seen.add(item)


def validate_manifest(manifest):
    require_type(manifest, dict, "manifest")
    if manifest.get("schemaVersion") != 1:
        raise RunnerError("manifest.schemaVersion must be 1")
    if manifest.get("suite") != "ecritum-conformance":
        raise RunnerError("manifest.suite must be ecritum-conformance")
    require_type(manifest.get("cases"), list, "manifest.cases")
    if not manifest["cases"]:
        raise RunnerError("manifest.cases must not be empty")

    seen = set()
    for index, case in enumerate(manifest["cases"]):
        path = f"manifest.cases[{index}]"
        require_type(case, dict, path)
        for field in ("caseId", "title", "category", "description"):
            value = case.get(field)
            if not isinstance(value, str) or value == "":
                raise RunnerError(f"{path}.{field} must be a non-empty string")
        if case["caseId"] in seen:
            raise RunnerError(f"duplicate caseId {case['caseId']!r}")
        seen.add(case["caseId"])
        if not isinstance(case.get("required"), bool):
            raise RunnerError(f"{path}.required must be boolean")
        require_string_list(case.get("capabilities"), f"{path}.capabilities")


def selected_cases(manifest, categories, case_ids):
    cases = list(manifest["cases"])
    if categories:
        category_set = set(categories)
        cases = [case for case in cases if case["category"] in category_set]
    if case_ids:
        case_set = set(case_ids)
        known = {case["caseId"] for case in manifest["cases"]}
        missing = sorted(case_set - known)
        if missing:
            raise RunnerError("unknown case id(s): " + ", ".join(missing))
        cases = [case for case in cases if case["caseId"] in case_set]
    return sorted(cases, key=lambda case: case["caseId"])


def list_cases(manifest, cases):
    return {
        "schemaVersion": manifest["schemaVersion"],
        "suite": manifest["suite"],
        "cases": [
            {
                "caseId": case["caseId"],
                "title": case["title"],
                "category": case["category"],
                "required": case["required"],
                "capabilities": case["capabilities"],
            }
            for case in cases
        ],
    }


def run_provider(command, cases, timeout_seconds):
    if not command:
        raise RunnerError("--provider requires a command")
    request = {
        "protocolVersion": PROTOCOL_VERSION,
        "provider": {},
        "cases": cases,
    }
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
        stderr = (exc.stderr or "").strip()
        detail = f": {stderr}" if stderr else ""
        raise RunnerError(f"provider timed out after {timeout_seconds} seconds{detail}") from exc

    if completed.returncode != 0:
        stderr = completed.stderr.strip()
        detail = f": {stderr}" if stderr else ""
        raise RunnerError(f"provider exited with status {completed.returncode}{detail}")

    try:
        payload = json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise RunnerError(f"invalid provider JSON: {exc}") from exc

    return payload, completed.stderr.strip()


def validate_provider_payload(payload, selected):
    require_type(payload, dict, "provider output")
    if payload.get("protocolVersion") != PROTOCOL_VERSION:
        raise RunnerError("provider output protocolVersion must be 1")

    provider = payload.get("provider")
    require_type(provider, dict, "provider.provider")
    provider_id = provider.get("id")
    if not isinstance(provider_id, str) or provider_id == "":
        raise RunnerError("provider.id must be a non-empty string")
    require_string_list(provider.get("capabilities"), "provider.capabilities")

    results = payload.get("results")
    require_type(results, list, "provider.results")
    selected_ids = {case["caseId"] for case in selected}
    seen = set()
    by_id = {}
    for index, result in enumerate(results):
        path = f"provider.results[{index}]"
        require_type(result, dict, path)
        case_id = result.get("caseId")
        if not isinstance(case_id, str) or case_id == "":
            raise RunnerError(f"{path}.caseId must be a non-empty string")
        if case_id not in selected_ids:
            raise RunnerError(f"{path}.caseId is not selected: {case_id}")
        if case_id in seen:
            raise RunnerError(f"provider returned duplicate result for {case_id}")
        seen.add(case_id)
        status = result.get("status")
        if status not in VALID_STATUSES:
            raise RunnerError(f"{path}.status must be one of {sorted(VALID_STATUSES)}")
        reason = result.get("reason", "")
        if status in {"fail", "pending_capability", "error"} and (
            not isinstance(reason, str) or reason.strip() == ""
        ):
            raise RunnerError(f"{path}.{status} requires a non-empty reason")
        by_id[case_id] = result

    missing = sorted(selected_ids - seen)
    if missing:
        raise RunnerError("provider omitted result(s): " + ", ".join(missing))
    return provider, by_id


def missing_actual(reason):
    return "missing actual." + reason


def expected_mismatch(field):
    return f"actual.{field} does not match expected"


def ensure_expected_result(case, result):
    if result["status"] != "pass":
        return None

    expected = case.get("expected")
    if not isinstance(expected, dict):
        return None

    actual = result.get("actual")
    evidence = result.get("evidence")

    if "evidenceCommands" in expected:
        if not isinstance(evidence, dict):
            return "missing evidence.commands"
        commands = evidence.get("commands")
        if commands != expected["evidenceCommands"]:
            return "evidence.commands does not match expected"

    if "value" in expected:
        if not isinstance(actual, dict) or "value" not in actual:
            return missing_actual("value")
        if actual["value"] != expected["value"]:
            return expected_mismatch("value")

    if "status" in expected:
        if not isinstance(actual, dict) or "status" not in actual:
            return missing_actual("status")
        if actual["status"] != expected["status"]:
            return expected_mismatch("status")

    if "category" in expected:
        if not isinstance(actual, dict) or "category" not in actual:
            return missing_actual("category")
        if actual["category"] != expected["category"]:
            return expected_mismatch("category")

    if "operation" in expected:
        if not isinstance(actual, dict) or "operation" not in actual:
            return missing_actual("operation")
        if actual["operation"] != expected["operation"]:
            return expected_mismatch("operation")

    if "hostCalls" in expected:
        if not isinstance(actual, dict) or "hostCalls" not in actual:
            return missing_actual("hostCalls")
        if actual["hostCalls"] != expected["hostCalls"]:
            return expected_mismatch("hostCalls")

    return None


def normalize_result(case, provider_capabilities, result):
    status = result["status"]
    reason = result.get("reason", "")
    required_capabilities = set(case["capabilities"])
    provider_has_case_capabilities = required_capabilities.issubset(provider_capabilities)

    if status == "pass" and not provider_has_case_capabilities:
        status = "fail"
        missing = sorted(required_capabilities - provider_capabilities)
        reason = "provider passed case without declaring required capabilities: " + ", ".join(missing)
    elif status == "pending_capability" and provider_has_case_capabilities:
        status = "fail"
        reason = "provider declares required capabilities but returned pending_capability"
    else:
        expectation_error = ensure_expected_result(case, result)
        if expectation_error:
            status = "fail"
            reason = expectation_error

    normalized = {
        "caseId": case["caseId"],
        "title": case["title"],
        "category": case["category"],
        "required": case["required"],
        "capabilities": case["capabilities"],
        "status": status,
        "reason": reason,
    }
    if "evidence" in result:
        normalized["evidence"] = result["evidence"]
    if "actual" in result:
        normalized["actual"] = result["actual"]
    return normalized


def summarize(provider, cases, strict):
    counts = {"pass": 0, "fail": 0, "pending_capability": 0, "error": 0}
    for case in cases:
        counts[case["status"]] += 1

    conformant = all(case["status"] == "pass" for case in cases if case["required"])
    required_pending = any(
        case["required"] and case["status"] == "pending_capability" for case in cases
    )
    has_failure = counts["fail"] > 0 or counts["error"] > 0
    ok = not has_failure and not (strict and required_pending)

    payload = {
        "ok": ok,
        "conformant": conformant,
        "strict": strict,
        "provider": provider,
        "summary": {
            "total": len(cases),
            "passed": counts["pass"],
            "failed": counts["fail"],
            "pending": counts["pending_capability"],
            "errors": counts["error"],
        },
        "cases": cases,
    }
    return payload


def main():
    parser = argparse.ArgumentParser(
        description="Run the Ecritum conformance suite.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Streams:\n"
            "  stdout: deterministic JSON payloads only.\n"
            "  stderr: runner and provider diagnostics only.\n\n"
            "Exit codes:\n"
            "  0: harness success.\n"
            "  1: required failure or strict-mode pending required case.\n"
            "  2: runner or provider protocol error.\n\n"
            "Provider command: pass --provider last, followed by the provider argv."
        ),
    )
    parser.add_argument("--manifest", default="Tests/Conformance/manifest.json")
    parser.add_argument("--category", action="append", default=[])
    parser.add_argument("--case", dest="case_ids", action="append", default=[])
    parser.add_argument("--list", action="store_true", help="List selected cases as JSON.")
    parser.add_argument("--validate-manifest", action="store_true")
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--provider-timeout-seconds", type=float, default=10.0)
    parser.add_argument(
        "--provider",
        nargs=argparse.REMAINDER,
        help="Provider command. Must be the final runner argument.",
    )
    args = parser.parse_args()

    try:
        manifest = load_manifest(Path(args.manifest))
        cases = selected_cases(manifest, args.category, args.case_ids)
        if args.validate_manifest:
            emit_json({"ok": True, "cases": len(manifest["cases"])})
            return 0
        if args.list:
            emit_json(list_cases(manifest, cases))
            return 0
        if not cases:
            raise RunnerError("no conformance cases selected")

        provider_payload, provider_stderr = run_provider(
            args.provider or [], cases, args.provider_timeout_seconds
        )
        if provider_stderr:
            print(provider_stderr, file=sys.stderr)
        provider, provider_results = validate_provider_payload(provider_payload, cases)
        provider_capabilities = set(provider["capabilities"])
        normalized = [
            normalize_result(case, provider_capabilities, provider_results[case["caseId"]])
            for case in cases
        ]
        payload = summarize(provider, normalized, args.strict)
        emit_json(payload)
        return 0 if payload["ok"] else 1
    except RunnerError as exc:
        fail_runner(str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
