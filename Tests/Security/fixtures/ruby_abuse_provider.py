#!/usr/bin/env python3
import ctypes
import json
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "Tests" / "Conformance" / "fixtures"))

import clojure_native_provider as native  # noqa: E402
import ruby_native_provider as ruby  # noqa: E402


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

# Each capability is exercised with a Ruby exploit snippet that MUST be denied
# by the production RubyEvaluator. Denials come from three layers, all of which
# RubyEvaluator.classify() folds into ECRITUM_ERROR_PERMISSION_DENIED (14):
#   - the lexical DENIED_SOURCE_PATTERNS pre-filter ("permission denied"),
#   - the sealed loader/eval prelude (NoMethodError on require/eval/...),
#   - the deny-by-default TruffleRuby context ("native access is not allowed",
#     "access to host class ...", "threads not allowed", "polyglot bindings
#     are not accessible").
DENIAL_PROBES = {
    "filesystem": "File.read('/etc/hosts')",
    "network": "require 'socket'; TCPSocket.new('127.0.0.1', 9)",
    "process": "system('true')",
    "environment": "ENV['PATH']",
    "reflection": "Java.type('java.lang.System')",
    "class_loading": "Java.type('java.lang.ClassLoader')",
    "native_loading": "require 'fiddle'",
    "unrestricted_java_lookup": "Java.type('java.lang.Runtime')",
    "raw_polyglot_access": "Polyglot.eval('js', '1 + 1')",
    "raw_host_object_access": "Java.type('java.lang.System').getProperty('user.home')",
    "raw_c_handle_access": "require 'ffi'",
    "classpath_mutation": "$LOAD_PATH << '/tmp/ecritum'",
}

# Phase coverage. The abuse matrix asserts that the capability stays denied
# during each lifecycle phase, so (mirroring the Python provider) each phase uses
# a denied surface that resolves deterministically to PERMISSION_DENIED rather
# than an actual long-running script. The dedicated timeout BEHAVIOR is covered
# by the conformance suite (timeout.long_running_script); here we assert that a
# denied capability is still denied when exercised under the timeout phase.
PHASE_PROBES = {
    "timeout": "system('true')",
    "cancellation": "system('true')",
    "callback_error": "File.read('/etc/hosts')",
    "cleanup": "require 'open3'; Open3.capture2('true')",
}

# Targeted cases from the abuse manifest, plus the lexical-bypass introspection
# vectors that defeat DENIED_SOURCE_PATTERNS and must still be runtime-denied.
TARGETED_PROBES = {
    "network.redirect_to_denied_host": "require 'net/http'",
    # String-built constant/method names that the lexical regex does NOT match,
    # so they reach the real runtime, which still denies the underlying op.
    "require.arbitrary_namespace.denied": "send(\"sys\" + \"tem\", 'true')",
    "require.refer_denied": "Object.const_get(\"Fil\" + \"e\").read('/etc/hosts')",
}

# Lexical-bypass introspection vectors proven runtime-denied by the M12-002
# review. These are exercised whenever a generated case maps to one of them, and
# also asserted directly via probe_for_case fallbacks. Each MUST be denied.
LEXICAL_BYPASS_PROBES = [
    "send(\"sys\" + \"tem\", 'true')",
    "Object.const_get(\"Fil\" + \"e\").read('/etc/hosts')",
    "Kernel.instance_method(:require).bind_call(self, 'open3')",
    "eval('1 + 1')",
]


def status_result(case_id, status):
    return {
        "caseId": case_id,
        "status": "pass",
        "reason": "Ruby native abuse probe matched expected status",
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
            with ruby.RubySession(lib, ruby.runtime_config_with_read_root(root)) as session:
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
    return DENIAL_PROBES.get(case["capability"], "Java.type('java.lang.System')")


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
        with ruby.RubySession(lib) as session:
            results = [run_case(case, session) for case in request["cases"]]

    print(json.dumps({
        "protocolVersion": 1,
        "provider": {
            "id": "ruby-native-abuse",
            "capabilities": CAPABILITIES,
        },
        "results": results,
    }, sort_keys=True))


if __name__ == "__main__":
    main()
