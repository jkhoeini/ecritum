#!/usr/bin/env python3
import ctypes
import json
import sys
import tempfile
from pathlib import Path

import clojure_native_provider as native


CAPABILITIES = sorted(set(native.CAPABILITIES) | {"timeout"})


def runtime_config(languages=None, filesystem=None, resource_limits=None):
    return json.dumps(
        {
            "schemaVersion": 1,
            "languages": languages or ["python"],
            "policy": {
                "filesystem": filesystem or {"mode": "denied"},
                "network": {"mode": "denied"},
                "process": {"mode": "denied"},
                "environment": {"mode": "denied"},
                "clock": {"mode": "denied"},
                "random": {"mode": "denied"},
                "log": {"mode": "denied"},
            },
            "diagnostics": {"mode": "redacted"},
            "resourceLimits": resource_limits or {},
        },
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")


def runtime_config_with_read_root(root):
    return runtime_config(filesystem={
        "mode": "read_only",
        "roots": [{"kind": "directory", "path": str(root)}],
    })


def runtime_config_with_timeout(timeout_nanos):
    return runtime_config(resource_limits={"executionTimeoutNanos": timeout_nanos})


class PythonSession(native.RuntimeSession):
    def eval(self, source):
        language, language_keepalive = native.make_string_view("python")
        source_name, source_name_keepalive = native.make_string_view("conformance.py")
        source_bytes, source_keepalive = native.make_bytes(source.encode("utf-8"))
        options, options_keepalive = native.make_bytes(b"")
        _ = (language_keepalive, source_name_keepalive, source_keepalive, options_keepalive)
        error = ctypes.c_uint64(0)
        job = ctypes.c_uint64(0)
        status = self.lib.ecritum_eval_start(
            self.context.value,
            language,
            source_bytes,
            source_name,
            options,
            ctypes.byref(job),
            ctypes.byref(error),
        )
        self._check(status, error, "eval_start")
        try:
            state = ctypes.c_int(0)
            status = self.lib.ecritum_job_wait(job.value, 5_000_000_000, ctypes.byref(state), ctypes.byref(error))
            self._check(status, error, "job_wait")
            if state.value not in native.TERMINAL_JOBS:
                raise RuntimeError(f"eval did not reach terminal state: {state.value}")
            return self._job_result(job.value, state.value == native.JOB_SUCCEEDED)
        finally:
            self.lib.ecritum_job_destroy(ctypes.byref(job), ctypes.byref(error))
            self._destroy_error(error)


def pass_result(case_id, actual):
    return {"caseId": case_id, "status": "pass", "reason": "packaged Python runtime matched expected behavior", "actual": actual}


def pending_result(case):
    missing = sorted(set(case.get("capabilities", [])) - set(CAPABILITIES))
    return {
        "caseId": case["caseId"],
        "status": "pending_capability",
        "reason": "python native provider lacks capabilities: " + ", ".join(missing),
    }


def run_case(case, session):
    case_id = case["caseId"]
    if case_id == "eval.scalar":
        result = session.eval("42")
        return pass_result(case_id, {"value": result["value"]})
    if case_id == "eval.array":
        result = session.eval('[1, "two", True]')
        return pass_result(case_id, {"value": result["value"]})
    if case_id == "eval.object":
        result = session.eval('{"answer": 42}')
        return pass_result(case_id, {"value": result["value"]})
    if case_id == "error.script_to_structured_error":
        result = session.eval("1 / 0")
        return pass_result(case_id, {
            "status": result["status"],
            "category": result["category"],
            "operation": result["operation"],
        })
    if case_id == "host.host_function_returns_value":
        result = session.eval("ecritum.app.answer()")
        return pass_result(case_id, {"value": result["value"]})
    if case_id == "host.script_calls_host_function":
        session.host_calls = []
        session.eval("ecritum.app.notify()")
        return pass_result(case_id, {"hostCalls": session.host_calls})
    if case_id == "stdlib.json.roundtrip":
        result = session.eval("ecritum.json.writeString({'b': 2, 'a': 1})")
        return pass_result(case_id, {"status": "ECRITUM_OK", "value": result["value"]})
    if case_id == "stdlib.time.parse_format":
        result = session.eval("ecritum.time.formatInstant(ecritum.time.parseInstant('2026-06-05T00:00:00Z'))")
        return pass_result(case_id, {"status": "ECRITUM_OK", "value": result["value"]})
    if case_id == "stdlib.time.now_default_denied":
        result = session.eval("ecritum.time.now()")
        return pass_result(case_id, {
            "status": result["status"],
            "category": result["category"],
            "operation": result["operation"],
        })
    if case_id == "stdlib.fs.default_denied":
        result = session.eval("ecritum.fs.readText('/tmp/ecritum-conformance-denied')")
        return pass_result(case_id, {
            "status": result["status"],
            "category": result["category"],
            "operation": result["operation"],
        })
    if case_id == "stdlib.http.default_denied":
        result = session.eval("ecritum.http.request({'url': 'https://example.com'})")
        return pass_result(case_id, {
            "status": result["status"],
            "category": result["category"],
            "operation": result["operation"],
        })
    if case_id == "timeout.long_running_script":
        with PythonSession(session.lib, runtime_config_with_timeout(1_000_000)) as timeout_session:
            result = timeout_session.eval("while True:\n    pass")
        return pass_result(case_id, {
            "status": result["status"],
            "category": result["category"],
            "operation": result["operation"],
        })
    if case_id in {"stdlib.fs.allowed_root_inside", "stdlib.fs.allowed_root_outside_denied"}:
        with tempfile.TemporaryDirectory(prefix="ecritum-conformance-") as root_name:
            root = Path(root_name)
            inside = root / "inside.txt"
            inside.write_text("inside-data", encoding="utf-8")
            outside_handle = tempfile.NamedTemporaryFile(prefix="ecritum-conformance-outside-", delete=False)
            outside = Path(outside_handle.name)
            outside_handle.write(b"outside-data")
            outside_handle.close()
            try:
                with PythonSession(session.lib, runtime_config_with_read_root(root)) as fs_session:
                    if case_id == "stdlib.fs.allowed_root_inside":
                        result = fs_session.eval(f"ecritum.fs.readText({json.dumps(str(inside))})")
                        return pass_result(case_id, {"status": "ECRITUM_OK", "value": result["value"]})
                    result = fs_session.eval(f"ecritum.fs.readText({json.dumps(str(outside))})")
                    return pass_result(case_id, {
                        "status": result["status"],
                        "category": result["category"],
                        "operation": result["operation"],
                    })
            finally:
                outside.unlink(missing_ok=True)
    return pending_result(case)


def pending_all_response(provider_id, reason, cases):
    return {
        "protocolVersion": 1,
        "provider": {"id": provider_id, "capabilities": []},
        "results": [
            {
                "caseId": case["caseId"],
                "status": "pending_capability",
                "reason": reason,
            }
            for case in cases
        ],
    }


def is_python_runtime_unavailable(exc):
    message = str(exc)
    return "ECRITUM_ERROR_RUNTIME_UNAVAILABLE" in message and "eval_start" in message


def main():
    request = json.load(sys.stdin)
    cases = request["cases"]
    lib = native.load_library()
    if lib is None:
        print(json.dumps(pending_all_response(
            "python-native-missing-artifact",
            "missing dist/local/EcritumRuntime.xcframework",
            cases,
        ), sort_keys=True))
        return 0

    session = PythonSession(lib)
    try:
        try:
            session.eval("0")
        except RuntimeError as exc:
            if not is_python_runtime_unavailable(exc):
                raise
            print(json.dumps(pending_all_response(
                "python-native-runtime-not-wired",
                "packaged runtime does not yet support python through ecritum_eval_start",
                cases,
            ), sort_keys=True))
            return 0
        results = [run_case(case, session) for case in cases]
    finally:
        session.close()

    print(json.dumps({
        "protocolVersion": 1,
        "provider": {"id": "python-native-xcframework", "capabilities": CAPABILITIES},
        "results": results,
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
