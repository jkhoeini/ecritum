package ecritum;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledIfSystemProperty;

@EnabledIfSystemProperty(named = "ecritum.pythonProbe", matches = "true")
final class PythonEvaluatorProbeTest {
    @Test
    void evaluatesScalarAndStandardLibraryJsonValues() {
        assertEquals(42L, ok("40 + 2"));
        assertEquals(42L, ok("import json\njson.loads('{\"answer\": 42}')['answer']"));
    }

    @Test
    void evaluatesCollectionValuesAndReportsStructuredErrors() {
        assertEquals(List.of(1L, "two", true), ok("[1, 'two', True]"));
        assertEquals(Map.of("answer", 42L), ok("{'answer': 42}"));

        SciEvalResult result = PythonEvaluator.evaluate("raise RuntimeError('boom')", "python-error.py");
        assertEquals(EcritumStatus.SCRIPT, result.status(), result.message());
        assertEquals("python", result.language());
        assertEquals("python-error.py", result.sourceName());
        assertEquals("runtime", result.category());
        assertTrue(result.message().contains("python-error.py"));
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
        assertPermissionDenied("import polyglot");
        assertPermissionDenied("import ctypes");
        assertPermissionDenied("open('/tmp/ecritum-python-probe', 'w')");
        assertPermissionDenied("import subprocess\nsubprocess.run(['true'])");
        assertPermissionDenied("import socket\nsocket.socket()");
    }

    private Object ok(String source) {
        SciEvalResult result = PythonEvaluator.evaluate(source, "python-probe.py");
        assertEquals(EcritumStatus.OK, result.status(), result.message());
        assertEquals("python", result.language());
        return result.value();
    }

    private void assertPermissionDenied(String source) {
        SciEvalResult result = PythonEvaluator.evaluate(source, "python-security.py");
        assertEquals(EcritumStatus.PERMISSION_DENIED, result.status(), result.message());
        assertEquals("permission", result.category(), result.message());
        assertTrue(result.message().contains("python-security.py"));
    }
}
