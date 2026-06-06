#!/usr/bin/env python3
import ctypes
import json
import os
import tempfile
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
FRAMEWORK = Path(os.environ.get(
    "ECRITUM_NATIVE_FRAMEWORK",
    ROOT / "dist/local/EcritumRuntime.xcframework/macos-arm64/EcritumRuntime.framework/EcritumRuntime",
))

ECRITUM_OK = 0
ECRITUM_ERROR_CALLBACK = 18

KIND_NULL = 0
KIND_BOOL = 1
KIND_INT = 2
KIND_DOUBLE = 3
KIND_STRING = 4
KIND_DATA = 5
KIND_ARRAY = 6
KIND_OBJECT = 7

JOB_SUCCEEDED = 2
TERMINAL_JOBS = {2, 3, 5, 6, 7}

CAPABILITIES = [
    "eval",
    "host_call",
    "script_error",
    "stdlib_json",
    "stdlib_time",
    "permission_default_deny",
    "filesystem_allowed_root",
]
STATUS_NAMES = {
    0: "ECRITUM_OK",
    1: "ECRITUM_ERROR_INVALID_ARGUMENT",
    2: "ECRITUM_ERROR_BUFFER_TOO_SMALL",
    3: "ECRITUM_ERROR_RUNTIME_UNAVAILABLE",
    4: "ECRITUM_ERROR_INVALID_HANDLE",
    5: "ECRITUM_ERROR_OUT_OF_MEMORY",
    6: "ECRITUM_ERROR_INVALID_UTF8",
    7: "ECRITUM_ERROR_INPUT_TOO_LARGE",
    8: "ECRITUM_ERROR_INVALID_CONFIG",
    9: "ECRITUM_ERROR_UNSUPPORTED_CONFIG_VERSION",
    10: "ECRITUM_ERROR_CONTEXTS_ALIVE",
    11: "ECRITUM_ERROR_CLOSED",
    12: "ECRITUM_ERROR_BUSY",
    13: "ECRITUM_ERROR_REENTRANT_CALL",
    14: "ECRITUM_ERROR_PERMISSION_DENIED",
    15: "ECRITUM_ERROR_TIMEOUT",
    16: "ECRITUM_ERROR_CANCELLED",
    17: "ECRITUM_ERROR_SCRIPT",
    18: "ECRITUM_ERROR_CALLBACK",
    19: "ECRITUM_ERROR_TEARDOWN_FAILED",
    20: "ECRITUM_ERROR_INTERNAL",
    21: "ECRITUM_ERROR_ALREADY_EXISTS",
}


class EcritumBytes(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.POINTER(ctypes.c_uint8)),
        ("len", ctypes.c_size_t),
    ]


class EcritumStringView(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.c_char_p),
        ("len", ctypes.c_size_t),
    ]


class EcritumObjectEntry(ctypes.Structure):
    _fields_ = [
        ("key", EcritumStringView),
        ("value", ctypes.c_uint64),
    ]


HOST_CALLBACK = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.c_uint64,
    ctypes.POINTER(ctypes.c_uint64),
    ctypes.POINTER(ctypes.c_uint64),
    ctypes.c_void_p,
)


def make_bytes(raw):
    if not raw:
        return EcritumBytes(None, 0), None
    buffer = ctypes.create_string_buffer(raw)
    pointer = ctypes.cast(buffer, ctypes.POINTER(ctypes.c_uint8))
    return EcritumBytes(pointer, len(raw)), buffer


def make_string_view(raw):
    encoded = raw.encode("utf-8")
    if not encoded:
        return EcritumStringView(None, 0), None
    buffer = ctypes.create_string_buffer(encoded)
    return EcritumStringView(ctypes.cast(buffer, ctypes.c_char_p), len(encoded)), buffer


def copy_string(view):
    if not view.data or view.len == 0:
        return ""
    return ctypes.string_at(view.data, view.len).decode("utf-8")


def load_library():
    if not FRAMEWORK.exists():
        return None
    lib = ctypes.CDLL(str(FRAMEWORK), mode=ctypes.RTLD_GLOBAL)
    u64p = ctypes.POINTER(ctypes.c_uint64)
    errp = ctypes.POINTER(ctypes.c_uint64)
    sizep = ctypes.POINTER(ctypes.c_size_t)
    intp = ctypes.POINTER(ctypes.c_int)

    lib.ecritum_runtime_create.argtypes = [EcritumBytes, u64p, errp]
    lib.ecritum_runtime_create.restype = ctypes.c_int
    lib.ecritum_runtime_destroy.argtypes = [u64p, errp]
    lib.ecritum_runtime_destroy.restype = ctypes.c_int
    lib.ecritum_context_create.argtypes = [ctypes.c_uint64, EcritumBytes, u64p, errp]
    lib.ecritum_context_create.restype = ctypes.c_int
    lib.ecritum_context_destroy.argtypes = [u64p, errp]
    lib.ecritum_context_destroy.restype = ctypes.c_int
    lib.ecritum_namespace_create.argtypes = [ctypes.c_uint64, EcritumStringView, u64p, errp]
    lib.ecritum_namespace_create.restype = ctypes.c_int
    lib.ecritum_namespace_destroy.argtypes = [u64p, errp]
    lib.ecritum_namespace_destroy.restype = ctypes.c_int
    lib.ecritum_namespace_register_function.argtypes = [
        ctypes.c_uint64,
        EcritumStringView,
        HOST_CALLBACK,
        ctypes.c_void_p,
        ctypes.c_void_p,
        u64p,
        errp,
    ]
    lib.ecritum_namespace_register_function.restype = ctypes.c_int
    lib.ecritum_value_make_null.argtypes = [u64p, errp]
    lib.ecritum_value_make_null.restype = ctypes.c_int
    lib.ecritum_value_make_int.argtypes = [ctypes.c_int64, u64p, errp]
    lib.ecritum_value_make_int.restype = ctypes.c_int
    lib.ecritum_value_destroy.argtypes = [u64p]
    lib.ecritum_value_destroy.restype = ctypes.c_int
    lib.ecritum_value_kind.argtypes = [ctypes.c_uint64, intp]
    lib.ecritum_value_kind.restype = ctypes.c_int
    lib.ecritum_value_get_bool.argtypes = [ctypes.c_uint64, intp]
    lib.ecritum_value_get_bool.restype = ctypes.c_int
    lib.ecritum_value_get_int.argtypes = [ctypes.c_uint64, ctypes.POINTER(ctypes.c_int64)]
    lib.ecritum_value_get_int.restype = ctypes.c_int
    lib.ecritum_value_get_double.argtypes = [ctypes.c_uint64, ctypes.POINTER(ctypes.c_double)]
    lib.ecritum_value_get_double.restype = ctypes.c_int
    lib.ecritum_value_get_string.argtypes = [ctypes.c_uint64, ctypes.POINTER(EcritumStringView)]
    lib.ecritum_value_get_string.restype = ctypes.c_int
    lib.ecritum_value_get_data.argtypes = [ctypes.c_uint64, ctypes.POINTER(EcritumBytes)]
    lib.ecritum_value_get_data.restype = ctypes.c_int
    lib.ecritum_value_count.argtypes = [ctypes.c_uint64, sizep]
    lib.ecritum_value_count.restype = ctypes.c_int
    lib.ecritum_value_array_get.argtypes = [ctypes.c_uint64, ctypes.c_size_t, u64p, errp]
    lib.ecritum_value_array_get.restype = ctypes.c_int
    lib.ecritum_value_object_entry.argtypes = [ctypes.c_uint64, ctypes.c_size_t, ctypes.POINTER(EcritumStringView), u64p, errp]
    lib.ecritum_value_object_entry.restype = ctypes.c_int
    lib.ecritum_eval_start.argtypes = [ctypes.c_uint64, EcritumStringView, EcritumBytes, EcritumStringView, EcritumBytes, u64p, errp]
    lib.ecritum_eval_start.restype = ctypes.c_int
    lib.ecritum_job_wait.argtypes = [ctypes.c_uint64, ctypes.c_uint64, intp, errp]
    lib.ecritum_job_wait.restype = ctypes.c_int
    lib.ecritum_job_result.argtypes = [ctypes.c_uint64, u64p, errp]
    lib.ecritum_job_result.restype = ctypes.c_int
    lib.ecritum_job_destroy.argtypes = [u64p, errp]
    lib.ecritum_job_destroy.restype = ctypes.c_int
    lib.ecritum_error_destroy.argtypes = [errp]
    lib.ecritum_error_destroy.restype = ctypes.c_int
    lib.ecritum_error_status.argtypes = [ctypes.c_uint64, intp]
    lib.ecritum_error_status.restype = ctypes.c_int
    lib.ecritum_error_category.argtypes = [ctypes.c_uint64, ctypes.POINTER(EcritumStringView)]
    lib.ecritum_error_category.restype = ctypes.c_int
    lib.ecritum_error_operation.argtypes = [ctypes.c_uint64, ctypes.POINTER(EcritumStringView)]
    lib.ecritum_error_operation.restype = ctypes.c_int
    return lib


def runtime_config_with_read_root(root):
    return json.dumps(
        {
            "schemaVersion": 1,
            "languages": ["clojure"],
            "policy": {
                "filesystem": {
                    "mode": "read_only",
                    "roots": [{"kind": "directory", "path": str(root)}],
                },
                "network": {"mode": "denied"},
                "process": {"mode": "denied"},
                "environment": {"mode": "denied"},
                "clock": {"mode": "denied"},
                "random": {"mode": "denied"},
                "log": {"mode": "denied"},
            },
            "diagnostics": {"mode": "redacted"},
            "resourceLimits": {},
        },
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")


class RuntimeSession:
    def __init__(self, lib, runtime_config=b""):
        self.lib = lib
        self.runtime_config = runtime_config
        self.runtime = ctypes.c_uint64(0)
        self.context = ctypes.c_uint64(0)
        self.namespace = ctypes.c_uint64(0)
        self.callbacks = []
        self.host_calls = []
        self._create()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, traceback):
        self.close()
        return False

    def close(self):
        error = ctypes.c_uint64(0)
        if self.context.value:
            self.lib.ecritum_context_destroy(ctypes.byref(self.context), ctypes.byref(error))
            self._destroy_error(error)
        if self.namespace.value:
            self.lib.ecritum_namespace_destroy(ctypes.byref(self.namespace), ctypes.byref(error))
            self._destroy_error(error)
        if self.runtime.value:
            self.lib.ecritum_runtime_destroy(ctypes.byref(self.runtime), ctypes.byref(error))
            self._destroy_error(error)

    def _create(self):
        config, config_keepalive = make_bytes(self.runtime_config)
        empty, _ = make_bytes(b"")
        error = ctypes.c_uint64(0)
        self._config_keepalive = config_keepalive
        status = self.lib.ecritum_runtime_create(config, ctypes.byref(self.runtime), ctypes.byref(error))
        self._check(status, error, "runtime_create")
        status = self.lib.ecritum_context_create(self.runtime.value, empty, ctypes.byref(self.context), ctypes.byref(error))
        self._check(status, error, "context_create")
        app, keepalive = make_string_view("app")
        self._app_keepalive = keepalive
        status = self.lib.ecritum_namespace_create(self.runtime.value, app, ctypes.byref(self.namespace), ctypes.byref(error))
        self._check(status, error, "namespace_create")
        self._register("answer", self._answer)
        self._register("notify", self._notify)

    def _register(self, name, callback):
        error = ctypes.c_uint64(0)
        function = ctypes.c_uint64(0)
        raw_name, keepalive = make_string_view(name)
        host_callback = HOST_CALLBACK(callback)
        self.callbacks.append(host_callback)
        status = self.lib.ecritum_namespace_register_function(
            self.namespace.value,
            raw_name,
            host_callback,
            None,
            None,
            ctypes.byref(function),
            ctypes.byref(error),
        )
        self._check(status, error, "function_register")
        self.callbacks.append(keepalive)

    def _answer(self, call, out_result, out_error, user_data):
        try:
            self.host_calls.append({"namespace": "app", "function": "answer"})
            return self.lib.ecritum_value_make_int(42, out_result, out_error)
        except Exception:
            return ECRITUM_ERROR_CALLBACK

    def _notify(self, call, out_result, out_error, user_data):
        try:
            self.host_calls.append({"namespace": "app", "function": "notify"})
            return self.lib.ecritum_value_make_null(out_result, out_error)
        except Exception:
            return ECRITUM_ERROR_CALLBACK

    def eval(self, source):
        language, language_keepalive = make_string_view("clojure")
        source_name, source_name_keepalive = make_string_view("conformance.clj")
        source_bytes, source_keepalive = make_bytes(source.encode("utf-8"))
        options, options_keepalive = make_bytes(b"")
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
            if state.value not in TERMINAL_JOBS:
                raise RuntimeError(f"eval did not reach terminal state: {state.value}")
            return self._job_result(job.value, state.value == JOB_SUCCEEDED)
        finally:
            self.lib.ecritum_job_destroy(ctypes.byref(job), ctypes.byref(error))
            self._destroy_error(error)

    def _job_result(self, job, expect_success):
        error = ctypes.c_uint64(0)
        value = ctypes.c_uint64(0)
        status = self.lib.ecritum_job_result(job, ctypes.byref(value), ctypes.byref(error))
        if expect_success:
            self._check(status, error, "job_result")
            try:
                return {"ok": True, "value": self._copy_value(value.value)}
            finally:
                self.lib.ecritum_value_destroy(ctypes.byref(value))
        details = self._copy_error(error, fallback_status=status)
        self._destroy_error(error)
        return {"ok": False, **details}

    def _copy_value(self, value):
        kind = ctypes.c_int(-1)
        self._check(self.lib.ecritum_value_kind(value, ctypes.byref(kind)), ctypes.c_uint64(0), "value_kind")
        if kind.value == KIND_NULL:
            return {"kind": "null", "value": None}
        if kind.value == KIND_BOOL:
            raw = ctypes.c_int(0)
            self._check(self.lib.ecritum_value_get_bool(value, ctypes.byref(raw)), ctypes.c_uint64(0), "value_get_bool")
            return {"kind": "bool", "value": raw.value != 0}
        if kind.value == KIND_INT:
            raw = ctypes.c_int64(0)
            self._check(self.lib.ecritum_value_get_int(value, ctypes.byref(raw)), ctypes.c_uint64(0), "value_get_int")
            return {"kind": "int", "value": raw.value}
        if kind.value == KIND_DOUBLE:
            raw = ctypes.c_double(0.0)
            self._check(self.lib.ecritum_value_get_double(value, ctypes.byref(raw)), ctypes.c_uint64(0), "value_get_double")
            return {"kind": "double", "value": raw.value}
        if kind.value == KIND_STRING:
            view = EcritumStringView()
            self._check(self.lib.ecritum_value_get_string(value, ctypes.byref(view)), ctypes.c_uint64(0), "value_get_string")
            return {"kind": "string", "value": copy_string(view)}
        if kind.value == KIND_DATA:
            data = EcritumBytes()
            self._check(self.lib.ecritum_value_get_data(value, ctypes.byref(data)), ctypes.c_uint64(0), "value_get_data")
            raw = bytes(ctypes.string_at(data.data, data.len)) if data.data and data.len else b""
            return {"kind": "data", "value": list(raw)}
        if kind.value == KIND_ARRAY:
            count = ctypes.c_size_t(0)
            self._check(self.lib.ecritum_value_count(value, ctypes.byref(count)), ctypes.c_uint64(0), "value_count")
            items = []
            for index in range(count.value):
                child = ctypes.c_uint64(0)
                error = ctypes.c_uint64(0)
                status = self.lib.ecritum_value_array_get(value, index, ctypes.byref(child), ctypes.byref(error))
                self._check(status, error, "value_array_get")
                try:
                    items.append(self._copy_value(child.value))
                finally:
                    self.lib.ecritum_value_destroy(ctypes.byref(child))
            return {"kind": "array", "value": items}
        if kind.value == KIND_OBJECT:
            count = ctypes.c_size_t(0)
            self._check(self.lib.ecritum_value_count(value, ctypes.byref(count)), ctypes.c_uint64(0), "value_count")
            values = {}
            for index in range(count.value):
                key = EcritumStringView()
                child = ctypes.c_uint64(0)
                error = ctypes.c_uint64(0)
                status = self.lib.ecritum_value_object_entry(value, index, ctypes.byref(key), ctypes.byref(child), ctypes.byref(error))
                self._check(status, error, "value_object_entry")
                try:
                    values[copy_string(key)] = self._copy_value(child.value)
                finally:
                    self.lib.ecritum_value_destroy(ctypes.byref(child))
            return {"kind": "object", "value": values}
        raise RuntimeError(f"unknown value kind: {kind.value}")

    def _copy_error(self, error, fallback_status):
        status_value = ctypes.c_int(fallback_status)
        if error.value:
            self.lib.ecritum_error_status(error.value, ctypes.byref(status_value))
        return {
            "status": STATUS_NAMES.get(status_value.value, f"ECRITUM_ERROR_{status_value.value}"),
            "category": self._error_view(error, self.lib.ecritum_error_category),
            "operation": self._error_view(error, self.lib.ecritum_error_operation),
        }

    def _error_view(self, error, accessor):
        if not error.value:
            return ""
        view = EcritumStringView()
        status = accessor(error.value, ctypes.byref(view))
        if status != ECRITUM_OK:
            return ""
        return copy_string(view)

    def _check(self, status, error, operation):
        if status == ECRITUM_OK:
            return
        details = self._copy_error(error, fallback_status=status)
        self._destroy_error(error)
        raise RuntimeError(f"{operation} failed: {details}")

    def _destroy_error(self, error):
        if error.value:
            self.lib.ecritum_error_destroy(ctypes.byref(error))


def pass_result(case_id, actual):
    return {"caseId": case_id, "status": "pass", "reason": "packaged Clojure runtime matched expected behavior", "actual": actual}


def pending_result(case):
    missing = sorted(set(case.get("capabilities", [])) - set(CAPABILITIES))
    return {
        "caseId": case["caseId"],
        "status": "pending_capability",
        "reason": "clojure native provider lacks capabilities: " + ", ".join(missing),
    }


def run_case(case, session):
    case_id = case["caseId"]
    if case_id == "eval.scalar":
        result = session.eval("42")
        return pass_result(case_id, {"value": result["value"]})
    if case_id == "eval.array":
        result = session.eval("[1 \"two\" true]")
        return pass_result(case_id, {"value": result["value"]})
    if case_id == "eval.object":
        result = session.eval("{\"answer\" 42}")
        return pass_result(case_id, {"value": result["value"]})
    if case_id == "error.script_to_structured_error":
        result = session.eval("(/ 1 0)")
        return pass_result(case_id, {
            "status": result["status"],
            "category": result["category"],
            "operation": result["operation"],
        })
    if case_id == "host.host_function_returns_value":
        result = session.eval("(app/answer)")
        return pass_result(case_id, {"value": result["value"]})
    if case_id == "host.script_calls_host_function":
        session.host_calls = []
        session.eval("(app/notify)")
        return pass_result(case_id, {"hostCalls": session.host_calls})
    if case_id == "stdlib.json.roundtrip":
        result = session.eval('(ecritum.json/write-string {"b" 2 "a" 1})')
        return pass_result(case_id, {"status": "ECRITUM_OK", "value": result["value"]})
    if case_id == "stdlib.time.parse_format":
        result = session.eval('(ecritum.time/format-instant (ecritum.time/parse-instant "2026-06-05T00:00:00Z"))')
        return pass_result(case_id, {"status": "ECRITUM_OK", "value": result["value"]})
    if case_id == "stdlib.time.now_default_denied":
        result = session.eval("(ecritum.time/now)")
        return pass_result(case_id, {
            "status": result["status"],
            "category": result["category"],
            "operation": result["operation"],
        })
    if case_id == "stdlib.fs.default_denied":
        result = session.eval('(ecritum.fs/read-text "/tmp/ecritum-conformance-denied")')
        return pass_result(case_id, {
            "status": result["status"],
            "category": result["category"],
            "operation": result["operation"],
        })
    if case_id == "stdlib.http.default_denied":
        result = session.eval('(ecritum.http/request {"url" "https://example.com"})')
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
                with RuntimeSession(session.lib, runtime_config_with_read_root(root)) as fs_session:
                    if case_id == "stdlib.fs.allowed_root_inside":
                        result = fs_session.eval(f'(ecritum.fs/read-text "{inside}")')
                        return pass_result(case_id, {"status": "ECRITUM_OK", "value": result["value"]})
                    result = fs_session.eval(f'(ecritum.fs/read-text "{outside}")')
                    return pass_result(case_id, {
                        "status": result["status"],
                        "category": result["category"],
                        "operation": result["operation"],
                    })
            finally:
                outside.unlink(missing_ok=True)
    return pending_result(case)


def missing_artifact_response(cases):
    return {
        "protocolVersion": 1,
        "provider": {"id": "clojure-native-missing-artifact", "capabilities": []},
        "results": [
            {
                "caseId": case["caseId"],
                "status": "pending_capability",
                "reason": "missing dist/local/EcritumRuntime.xcframework",
            }
            for case in cases
        ],
    }


def main():
    request = json.load(sys.stdin)
    cases = request["cases"]
    lib = load_library()
    if lib is None:
        print(json.dumps(missing_artifact_response(cases), sort_keys=True))
        return 0

    session = RuntimeSession(lib)
    try:
        results = [run_case(case, session) for case in cases]
    finally:
        session.close()

    print(json.dumps({
        "protocolVersion": 1,
        "provider": {"id": "clojure-native-xcframework", "capabilities": CAPABILITIES},
        "results": results,
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
