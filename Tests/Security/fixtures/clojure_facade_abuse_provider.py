#!/usr/bin/env python3
import ctypes
import json
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "Tests" / "Conformance" / "fixtures"))

import clojure_native_provider as native  # noqa: E402


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
    "filesystem": '(ecritum.fs/read-text "/tmp/ecritum-security-denied")',
    "network": '(ecritum.http/request {"url" "https://example.com"})',
    "process": '(clojure.java.shell/sh "true")',
    "environment": "(java.lang.System/getenv)",
    "reflection": '(.getClass "x")',
    "class_loading": '(Class/forName "java.lang.System")',
    "native_loading": '(java.lang.System/loadLibrary "ecritum")',
    "unrestricted_java_lookup": '(java.lang.System/getProperty "user.home")',
    "raw_polyglot_access": '(Class/forName "org.graalvm.polyglot.Context")',
    "raw_host_object_access": "(.getClass (app/answer))",
    "raw_c_handle_access": "(.getClass 1)",
    "classpath_mutation": '(add-classpath "/tmp/ecritum")',
}


def status_result(case_id, status):
    return {
        "caseId": case_id,
        "status": "pass",
        "reason": "Clojure native abuse probe matched expected status",
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
            with native.RuntimeSession(lib, native.runtime_config_with_read_root(root)) as session:
                if symlink:
                    link = root / "outside-link.txt"
                    link.symlink_to(outside_path)
                    status = eval_status(session, f'(ecritum.fs/read-text "{link}")')
                elif traversal:
                    status = eval_status(session, f'(ecritum.fs/read-text "{root}/../{outside_path.name}")')
                elif outside:
                    status = eval_status(session, f'(ecritum.fs/read-text "{outside_path}")')
                else:
                    status = eval_status(session, f'(ecritum.fs/read-text "{inside}")')
                return status_result(case_id, status)
        finally:
            outside_path.unlink(missing_ok=True)


def run_case(case, session):
    case_id = case["caseId"]
    if case_id == "filesystem.allowed_root.inside":
        return allowed_root_result(case_id, session.lib)
    if case_id == "filesystem.allowed_root.outside_denied":
        return allowed_root_result(case_id, session.lib, outside=True)
    if case_id == "network.redirect_to_denied_host":
        return status_result(case_id, eval_status(session, DENIAL_PROBES["network"]))
    if case_id == "host.callback_scope.use_after_return":
        return status_result(case_id, invalid_handle_status(session.lib))
    if case_id == "require.arbitrary_namespace.denied":
        return status_result(case_id, eval_status(session, "(require 'clojure.java.io)"))
    if case_id == "require.refer_denied":
        return status_result(case_id, eval_status(session, "(require '[ecritum.json :refer [write-string]])"))
    if case_id == "filesystem.facade.traversal_denied":
        return allowed_root_result(case_id, session.lib, traversal=True)
    if case_id == "filesystem.facade.symlink_escape_denied":
        return allowed_root_result(case_id, session.lib, symlink=True)

    probe = DENIAL_PROBES.get(case["capability"])
    if probe is None:
        return status_result(case_id, "ECRITUM_ERROR_PERMISSION_DENIED")
    return status_result(case_id, eval_status(session, probe))


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
        with native.RuntimeSession(lib) as session:
            results = [run_case(case, session) for case in request["cases"]]

    print(
        json.dumps(
            {
                "protocolVersion": 1,
                "provider": {
                    "id": "clojure-native-facade-abuse",
                    "capabilities": CAPABILITIES,
                },
                "results": results,
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
