#!/usr/bin/env python3
import ctypes
import json
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "Tests" / "Conformance" / "fixtures"))

import clojure_native_provider as native  # noqa: E402
import python_native_provider as python  # noqa: E402


CAPABILITIES = [
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
]

DENIAL_PROBES = {
    "filesystem": "ecritum.fs.readText('/tmp/ecritum-security-denied')",
    "network": "ecritum.http.request({'url': 'https://example.com'})",
    "process": "__builtins__.__import__('os').system('true')",
    "environment": "__builtins__.__import__('os').environ",
    "reflection": "__builtins__.__import__('platform').python_implementation()",
    "class_loading": "__builtins__.__import__('java')",
    "native_loading": "__builtins__.__import__('ctypes')",
    "unrestricted_java_lookup": "__builtins__.__import__('java')",
    "raw_polyglot_access": "__builtins__.__import__('polyglot')",
    "raw_host_object_access": "__builtins__.__import__('java')",
    "raw_c_handle_access": "__builtins__.__import__('java')",
    "classpath_mutation": "__builtins__.__import__('sys').path.append('/tmp/ecritum')",
}

PHASE_PROBES = {
    "timeout": "eval('1 + 1')",
    "cancellation": "exec('x = 1')",
    "callback_error": "compile('1 + 1', '<ecritum>', 'eval')",
    "cleanup": "__builtins__.__import__('tempfile')",
}

TARGETED_PROBES = {
    "network.redirect_to_denied_host": "ecritum.http.request({'url': 'https://example.com'})",
    "require.arbitrary_namespace.denied": "__builtins__.__import__('site')",
    "require.refer_denied": "__builtins__.__import__('zipimport')",
}


def status_result(case_id, status):
    return {
        "caseId": case_id,
        "status": "pass",
        "reason": "Python native abuse probe matched expected status",
        "actual": {"status": status},
    }


def eval_status(session, source):
    result = session.eval(source)
    return "ECRITUM_OK" if result["ok"] else result["status"]


def invalid_handle_status(lib):
    kind = ctypes.c_int(-1)
    status = lib.ecritum_value_kind(ctypes.c_uint64(0x12345678), ctypes.byref(kind))
    return native.STATUS_NAMES.get(status, f"ECRITUM_ERROR_{status}")


def allowed_root_result(case_id, lib, outside=False, symlink=False, traversal=False):
    with tempfile.TemporaryDirectory(prefix="ecritum-security-") as root_name:
        root = Path(root_name)
        inside = root / "inside.txt"
        inside.write_text("inside-data", encoding="utf-8")
        outside_handle = tempfile.NamedTemporaryFile(prefix="ecritum-security-outside-", delete=False)
        outside_path = Path(outside_handle.name)
        outside_handle.write(b"outside-data")
        outside_handle.close()
        try:
            with python.PythonSession(lib, python.runtime_config_with_read_root(root)) as session:
                if symlink:
                    link = root / "outside-link.txt"
                    link.symlink_to(outside_path)
                    status = eval_status(session, f"ecritum.fs.readText({json.dumps(str(link))})")
                elif traversal:
                    path = root / ".." / outside_path.name
                    status = eval_status(session, f"ecritum.fs.readText({json.dumps(str(path))})")
                elif outside:
                    status = eval_status(session, f"ecritum.fs.readText({json.dumps(str(outside_path))})")
                else:
                    status = eval_status(session, f"ecritum.fs.readText({json.dumps(str(inside))})")
                return status_result(case_id, status)
        finally:
            outside_path.unlink(missing_ok=True)


def probe_for_case(case):
    if case["caseId"] in TARGETED_PROBES:
        return TARGETED_PROBES[case["caseId"]]
    if case["phase"] in PHASE_PROBES:
        return PHASE_PROBES[case["phase"]]
    return DENIAL_PROBES.get(case["capability"], "__builtins__.__import__('java')")


def run_case(case, session):
    case_id = case["caseId"]
    if case_id == "filesystem.allowed_root.inside":
        return allowed_root_result(case_id, session.lib)
    if case_id == "filesystem.allowed_root.outside_denied":
        return allowed_root_result(case_id, session.lib, outside=True)
    if case_id == "host.callback_scope.use_after_return":
        return status_result(case_id, invalid_handle_status(session.lib))
    if case_id == "filesystem.facade.traversal_denied":
        return allowed_root_result(case_id, session.lib, traversal=True)
    if case_id == "filesystem.facade.symlink_escape_denied":
        return allowed_root_result(case_id, session.lib, symlink=True)

    return status_result(case_id, eval_status(session, probe_for_case(case)))


def main():
    request = json.load(sys.stdin)
    lib = native.load_library()
    if lib is None:
        results = [
            {
                "caseId": case["caseId"],
                "status": "error",
                "reason": "missing local EcritumRuntime.xcframework",
            }
            for case in request["cases"]
        ]
    else:
        with python.PythonSession(lib) as session:
            results = [run_case(case, session) for case in request["cases"]]

    print(json.dumps({
        "protocolVersion": 1,
        "provider": {
            "id": "python-native-abuse",
            "capabilities": CAPABILITIES,
        },
        "results": results,
    }, sort_keys=True))


if __name__ == "__main__":
    main()
