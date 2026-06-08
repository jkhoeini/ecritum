package ecritum;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;

final class PythonEvaluatorTest {
    @Test
    void evaluatesScalarCollectionAndBytesValues() {
        assertEquals(42L, ok("40 + 2"));
        assertEquals(null, ok("None"));
        assertEquals(Boolean.TRUE, ok("True"));
        assertEquals("hello", ok("'hello'"));
        assertEquals(3.5d, ok("3.5"));
        assertEquals(List.of(1L, "two", true), ok("[1, 'two', True]"));
        assertEquals(Map.of("answer", 42L), ok("{'answer': 42}"));
        assertArrayEquals(new byte[] {0, 1, -1}, (byte[]) ok("bytes([0, 1, 255])"));
    }

    @Test
    void reportsStructuredErrorsWithSourceName() {
        SciEvalResult runtime = PythonEvaluator.evaluate("raise RuntimeError('boom')", "python-error.py");
        assertEquals(EcritumStatus.SCRIPT, runtime.status(), runtime.message());
        assertEquals("python", runtime.language());
        assertEquals("python-error.py", runtime.sourceName());
        assertEquals("runtime", runtime.category());
        assertTrue(runtime.message().contains("python-error.py"));

        SciEvalResult syntax = PythonEvaluator.evaluate("def", "python-syntax.py");
        assertEquals(EcritumStatus.SCRIPT, syntax.status(), syntax.message());
        assertEquals("python", syntax.language());
        assertEquals("python-syntax.py", syntax.sourceName());
        assertEquals("syntax", syntax.category());
    }

    @Test
    void deniesRawPythonImportsInsteadOfReturningModuleValues() {
        SciEvalResult result = PythonEvaluator.evaluate("import json\njson", "python-module.py");

        assertEquals(EcritumStatus.PERMISSION_DENIED, result.status(), result.message());
        assertEquals("python", result.language());
        assertEquals("permission", result.category());
    }

    @Test
    void projectsExplicitHostFunctionsUnderEcritumGlobal() {
        SciEvalResult result = PythonEvaluator.evaluate(
            "ecritum.app.combine(41, 'done', {'ok': True})",
            "python-host.py",
            List.of(new HostProjection("app", "combine")),
            (namespace, function, arguments) -> {
                assertEquals("app", namespace);
                assertEquals("combine", function);
                assertEquals(List.of(41L, "done", Map.of("ok", true)), arguments);
                return List.of(arguments.get(0), arguments.get(1));
            }
        );

        assertEquals(EcritumStatus.OK, result.status(), result.message());
        assertEquals("python", result.language());
        assertEquals(List.of(41L, "done"), result.value());
    }

    @Test
    void deniesRawPythonImportsBeforeHostArgumentsAreExposed() {
        SciEvalResult result = PythonEvaluator.evaluate(
            "import json\necritum.app.accept(json)",
            "python-host-module.py",
            List.of(new HostProjection("app", "accept")),
            (namespace, function, arguments) -> arguments
        );

        assertEquals(EcritumStatus.PERMISSION_DENIED, result.status(), result.message());
        assertEquals("python", result.language());
        assertEquals("permission", result.category());
    }

    @Test
    void normalizesHostByteResults() {
        SciEvalResult result = PythonEvaluator.evaluate(
            "ecritum.app.blob()",
            "python-host-bytes.py",
            List.of(new HostProjection("app", "blob")),
            (namespace, function, arguments) -> new byte[] {0, 1, 2, -1}
        );

        assertEquals(EcritumStatus.OK, result.status(), result.message());
        assertArrayEquals(new byte[] {0, 1, 2, -1}, (byte[]) result.value());
    }

    @Test
    void mapsHostCallbackFailuresToCallbackStatus() {
        SciEvalResult result = PythonEvaluator.evaluate(
            "ecritum.app.fail()",
            "python-host-fail.py",
            List.of(new HostProjection("app", "fail")),
            (namespace, function, arguments) -> {
                throw new HostFunctionException(EcritumStatus.CALLBACK, "callback", "token=SECRET");
            }
        );

        assertEquals(EcritumStatus.CALLBACK, result.status(), result.message());
        assertEquals("callback", result.category());
        assertEquals("python", result.language());
        assertEquals("python-host-fail.py", result.sourceName());
        assertTrue(result.message().contains("token=<redacted>"));
        assertTrue(!result.message().contains("SECRET"));
    }

    @Test
    void installsPureStandardLibraryFacadesAndDefaultDenials() {
        assertEquals("{\"a\":1,\"b\":2}", ok("ecritum.json.writeString({'b': 2, 'a': 1})"));

        SciEvalResult jsonRead = PythonEvaluator.evaluate(
            "ecritum.json.readString('{\"items\":[true,false,\"x\"],\"n\":1}')",
            "python-facade-json.py"
        );
        assertEquals(EcritumStatus.OK, jsonRead.status(), jsonRead.message());
        Map<?, ?> object = assertInstanceOf(Map.class, jsonRead.value());
        assertEquals(List.of(true, false, "x"), object.get("items"));
        assertEquals(1L, object.get("n"));

        assertEquals(
            "2026-06-05T00:00:00Z",
            ok("ecritum.time.formatInstant(ecritum.time.parseInstant('2026-06-05T00:00:00Z'))")
        );
        assertPermissionDenied("ecritum.time.now()");
        assertPermissionDenied("ecritum.fs.readText('/tmp/ecritum')");
        assertPermissionDenied("ecritum.http.request({'url': 'https://example.com'})");
    }

    @Test
    void deniesAmbientEscapeHatches() {
        assertPermissionDenied("import java");
        assertPermissionDenied("exec(\"import java\")");
        assertPermissionDenied("__builtins__.__import__('java')");
        assertPermissionDenied("getattr(__builtins__, '__import__')('os').environ");
        assertPermissionDenied("import polyglot");
        assertPermissionDenied("import ctypes");
        assertPermissionDenied("import cffi");
        assertPermissionDenied("import posix");
        assertPermissionDenied("import signal");
        assertPermissionDenied("import platform");
        assertPermissionDenied("import ssl");
        assertPermissionDenied("import urllib.request");
        assertPermissionDenied("import pathlib");
        assertPermissionDenied("import tempfile");
        assertPermissionDenied("import getpass");
        assertPermissionDenied("open('/tmp/ecritum-python-probe', 'w')");
        assertPermissionDenied("import subprocess\nsubprocess.run(['true'])");
        assertPermissionDenied("import os\nos.popen('true')");
        assertPermissionDenied("import socket\nsocket.socket()");
        assertPermissionDenied("import os\nos.environ");
        assertPermissionDenied("import sys\nsys.path.append('/tmp/ecritum')");
    }

    @Test
    void timesOutLongRunningScripts() {
        SciEvalResult result = PythonEvaluator.evaluate(
            "while True:\n    pass",
            "python-timeout.py",
            List.of(),
            (namespace, function, arguments) -> {
                throw new HostFunctionException(EcritumStatus.PERMISSION_DENIED, "permission", "host function is not projected");
            },
            new StandardLibraryPolicy("denied", List.of(), false, false, 1_000_000L),
            StandardLibraryBridge.denying()
        );

        assertEquals(EcritumStatus.TIMEOUT, result.status(), result.message());
        assertEquals("python", result.language());
        assertEquals("timeout", result.category());
    }

    private Object ok(String source) {
        SciEvalResult result = PythonEvaluator.evaluate(source, "python-value.py");
        assertEquals(EcritumStatus.OK, result.status(), result.message());
        assertEquals("python", result.language());
        return result.value();
    }

    private void assertPermissionDenied(String source) {
        SciEvalResult result = PythonEvaluator.evaluate(source, "python-security.py");
        assertEquals(EcritumStatus.PERMISSION_DENIED, result.status(), result.message());
        assertEquals("permission", result.category(), result.message());
        assertEquals("python", result.language());
        assertTrue(result.message().contains("python-security.py"));
    }
}
