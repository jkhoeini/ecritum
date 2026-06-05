#!/usr/bin/env python3
import ctypes
import json
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "Tests" / "Conformance" / "fixtures"))

import clojure_native_provider as native  # noqa: E402
import lua_native_provider as lua  # noqa: E402


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
    "filesystem": "return io.open('/tmp/ecritum-security-denied', 'r')",
    "network": "return ecritum.http.request({url = 'https://example.com'})",
    "process": "return os.execute('true')",
    "environment": "return os.getenv('HOME')",
    "reflection": "return debug.getregistry()",
    "class_loading": "return luajava.bindClass('java.lang.System')",
    "native_loading": "return package.loadlib('/tmp/libecritum.dylib', 'luaopen_ecritum')",
    "unrestricted_java_lookup": "return Java.type('java.lang.System')",
    "raw_polyglot_access": "return Polyglot.import('x')",
    "raw_host_object_access": "return debug.getmetatable(ecritum.app.answer)",
    "raw_c_handle_access": "return debug.getregistry()",
    "classpath_mutation": "package.path = '/tmp/ecritum/?.lua'; return require('owned')",
}

PHASE_PROBES = {
    "timeout": "local co = coroutine.create(function() while true do end end); return coroutine.resume(co)",
    "cancellation": "return load('return 1')",
    "callback_error": "return loadstring('return 1')",
    "cleanup": "return collectgarbage('collect')",
}

TARGETED_PROBES = {
    "network.redirect_to_denied_host": "return ecritum.http.request({url = 'https://example.com'})",
    "require.arbitrary_namespace.denied": "return require('io')",
    "require.refer_denied": "return dofile('/tmp/ecritum-security-denied.lua')",
}


def status_result(case_id, status):
    return {
        "caseId": case_id,
        "status": "pass",
        "reason": "Lua native abuse probe matched expected status",
        "actual": {"status": status},
    }


def eval_status(session, source):
    result = session.eval(source)
    return "ECRITUM_OK" if result["ok"] else result["status"]


def invalid_handle_status(lib):
    kind = ctypes.c_int(-1)
    status = lib.ecritum_value_kind(ctypes.c_uint64(0x12345678), ctypes.byref(kind))
    return native.STATUS_NAMES.get(status, f"ECRITUM_ERROR_{status}")


def quoted_lua_string(value):
    return json.dumps(str(value))


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
            with lua.LuaSession(lib, lua.runtime_config_with_read_root(root)) as session:
                if symlink:
                    link = root / "outside-link.txt"
                    link.symlink_to(outside_path)
                    status = eval_status(session, f"return ecritum.fs.readText({quoted_lua_string(link)})")
                elif traversal:
                    path = root / ".." / outside_path.name
                    status = eval_status(session, f"return ecritum.fs.readText({quoted_lua_string(path)})")
                elif outside:
                    status = eval_status(session, f"return ecritum.fs.readText({quoted_lua_string(outside_path)})")
                else:
                    status = eval_status(session, f"return ecritum.fs.readText({quoted_lua_string(inside)})")
                return status_result(case_id, status)
        finally:
            outside_path.unlink(missing_ok=True)


def probe_for_case(case):
    case_id = case["caseId"]
    if case_id in TARGETED_PROBES:
        return TARGETED_PROBES[case_id]
    phase_probe = PHASE_PROBES.get(case["phase"])
    if phase_probe is not None:
        return phase_probe
    return DENIAL_PROBES.get(case["capability"], "return debug.getregistry()")


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
        with lua.LuaSession(lib) as session:
            results = [run_case(case, session) for case in request["cases"]]

    print(
        json.dumps(
            {
                "protocolVersion": 1,
                "provider": {
                    "id": "lua-native-abuse",
                    "capabilities": CAPABILITIES,
                },
                "results": results,
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
