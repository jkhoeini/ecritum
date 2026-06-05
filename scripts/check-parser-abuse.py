#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
from pathlib import Path


REQUIRED_SURFACES = {
    "config",
    "eval_options",
    "source",
    "value",
    "error",
    "callback",
    "handle",
}

REQUIRED_VECTORS = {
    "invalid_utf8",
    "oversized_input",
    "duplicate_keys",
    "bad_paths",
    "deep_nesting",
    "large_arrays",
    "nul_bytes",
    "stale_handle",
    "wrong_handle_kind",
    "double_destroy",
    "use_after_destroy",
    "concurrent_calls",
}

VALID_STATUSES = {"covered", "blocked"}


class ParserAbuseError(Exception):
    pass


def emit(payload):
    print(json.dumps(payload, indent=2, sort_keys=True))


def fail(message):
    print(message, file=sys.stderr)
    raise SystemExit(2)


def require_type(value, expected, path):
    if not isinstance(value, expected):
        raise ParserAbuseError(f"{path} must be {expected.__name__}")


def require_non_empty_string(value, path):
    if not isinstance(value, str) or value == "":
        raise ParserAbuseError(f"{path} must be a non-empty string")


def require_command_list(value, path):
    require_type(value, list, path)
    for index, command in enumerate(value):
        require_non_empty_string(command, f"{path}[{index}]")


def load_manifest(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except OSError as exc:
        raise ParserAbuseError(f"cannot read manifest: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ParserAbuseError(f"invalid manifest JSON: {exc}") from exc


def validate_cases(manifest):
    require_type(manifest, dict, "manifest")
    if manifest.get("schemaVersion") != 1:
        raise ParserAbuseError("manifest.schemaVersion must be 1")
    if manifest.get("suite") != "ecritum-parser-abuse":
        raise ParserAbuseError("manifest.suite must be ecritum-parser-abuse")

    cases = manifest.get("cases")
    require_type(cases, list, "manifest.cases")
    if not cases:
        raise ParserAbuseError("manifest.cases must not be empty")

    seen = set()
    normalized = []
    for index, case in enumerate(cases):
        path = f"manifest.cases[{index}]"
        require_type(case, dict, path)
        for field in ("caseId", "surface", "vector", "status"):
            require_non_empty_string(case.get(field), f"{path}.{field}")
        if case["caseId"] in seen:
            raise ParserAbuseError(f"duplicate parser abuse case id: {case['caseId']}")
        seen.add(case["caseId"])
        if case["status"] not in VALID_STATUSES:
            raise ParserAbuseError(f"{path}.status must be one of {sorted(VALID_STATUSES)}")
        if case["status"] == "covered":
            evidence = case.get("evidenceCommands")
            if not evidence:
                raise ParserAbuseError(f"covered case requires evidenceCommands: {case['caseId']}")
            require_command_list(evidence, f"{path}.evidenceCommands")
        if case["status"] == "blocked":
            require_non_empty_string(case.get("blockedBy"), f"{path}.blockedBy")
        normalized.append(
            {
                "caseId": case["caseId"],
                "surface": case["surface"],
                "vector": case["vector"],
                "status": case["status"],
                "evidenceCommands": case.get("evidenceCommands", []),
                "blockedBy": case.get("blockedBy", ""),
            }
        )

    missing_surfaces = sorted(REQUIRED_SURFACES - {case["surface"] for case in normalized})
    if missing_surfaces:
        raise ParserAbuseError(f"missing parser abuse surface: {missing_surfaces[0]}")
    missing_vectors = sorted(REQUIRED_VECTORS - {case["vector"] for case in normalized})
    if missing_vectors:
        raise ParserAbuseError(f"missing parser abuse vector: {missing_vectors[0]}")
    return sorted(normalized, key=lambda case: case["caseId"])


def summarize(cases, strict):
    covered = sum(1 for case in cases if case["status"] == "covered")
    blocked = sum(1 for case in cases if case["status"] == "blocked")
    complete = blocked == 0
    ok = complete or not strict
    return {
        "ok": ok,
        "parserAbuseComplete": complete,
        "strict": strict,
        "summary": {
            "total": len(cases),
            "covered": covered,
            "blocked": blocked,
        },
        "cases": cases,
    }


def verify_evidence(cases, timeout_seconds):
    checked = 0
    failed = 0
    results = []
    for case in cases:
        if case["status"] != "covered":
            continue
        for command in case["evidenceCommands"]:
            checked += 1
            try:
                completed = subprocess.run(
                    command,
                    shell=True,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=timeout_seconds,
                    check=False,
                )
                exit_code = completed.returncode
                error = ""
            except subprocess.TimeoutExpired as exc:
                exit_code = 124
                error = f"timed out after {timeout_seconds} seconds"
                completed = exc
            if exit_code != 0:
                failed += 1
            results.append(
                {
                    "caseId": case["caseId"],
                    "command": command,
                    "exitCode": exit_code,
                    "ok": exit_code == 0,
                    "stderr": (getattr(completed, "stderr", "") or error or "").strip(),
                }
            )

    return {
        "ok": failed == 0,
        "summary": {
            "evidenceChecked": checked,
            "evidenceFailed": failed,
        },
        "evidence": results,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Check the Ecritum parser-abuse coverage manifest.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "stdout: deterministic JSON.\n"
            "stderr: manifest diagnostics.\n"
            "exit 0: baseline succeeded; exit 1: strict blocked cases; exit 2: checker error."
        ),
    )
    parser.add_argument("--manifest", default="Tests/Security/parser-abuse-manifest.json")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--validate-manifest", action="store_true")
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--verify-evidence", action="store_true")
    parser.add_argument("--evidence-timeout-seconds", type=float, default=30.0)
    args = parser.parse_args()

    try:
        cases = validate_cases(load_manifest(Path(args.manifest)))
        if args.validate_manifest:
            emit({"ok": True, "cases": len(cases)})
            return 0
        if args.list:
            emit({"schemaVersion": 1, "suite": "ecritum-parser-abuse", "cases": cases})
            return 0
        if args.verify_evidence:
            payload = verify_evidence(cases, args.evidence_timeout_seconds)
            emit(payload)
            return 0 if payload["ok"] else 1
        payload = summarize(cases, args.strict)
        emit(payload)
        return 0 if payload["ok"] else 1
    except ParserAbuseError as exc:
        fail(str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
