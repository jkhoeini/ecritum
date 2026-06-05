package ecritum;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;

final class JavaScriptEvaluatorTest {
    @Test
    void evaluatesScalarAndCollectionValues() {
        assertEquals(42L, ok("40 + 2"));
        assertEquals(null, ok("null"));
        assertEquals(null, ok("undefined"));
        assertEquals(Boolean.TRUE, ok("true"));
        assertEquals("hello", ok("'hello'"));
        assertEquals(3.5d, ok("3.5"));
        assertEquals(List.of(1L, "two", true), ok("[1, 'two', true]"));
        assertEquals(Map.of("answer", 42L), ok("({answer: 42})"));
    }

    @Test
    void convertsByteArraysToDataValues() {
        assertArrayEquals(new byte[] {0, 1, -1}, (byte[]) ok("new Uint8Array([0, 1, 255])"));
        assertArrayEquals(new byte[] {0, 2, -1}, (byte[]) ok("new Uint8Array([0, 2, 255]).buffer"));
    }

    @Test
    void reportsSyntaxAndRuntimeErrorsWithSourceName() {
        SciEvalResult syntax = JavaScriptEvaluator.evaluate("function", "syntax-source.js");
        assertEquals(EcritumStatus.SCRIPT, syntax.status());
        assertEquals("javascript", syntax.language());
        assertEquals("syntax-source.js", syntax.sourceName());
        assertEquals("syntax", syntax.category());
        assertTrue(syntax.message().contains("syntax-source.js"));

        SciEvalResult runtime = JavaScriptEvaluator.evaluate("throw new Error('boom')", "runtime-source.js");
        assertEquals(EcritumStatus.SCRIPT, runtime.status());
        assertEquals("javascript", runtime.language());
        assertEquals("runtime-source.js", runtime.sourceName());
        assertEquals("runtime", runtime.category());
        assertTrue(runtime.message().contains("runtime-source.js"));
    }

    @Test
    void deniesAmbientEscapeHatches() {
        assertPermissionDenied("Java.type('java.lang.System')", "js-security.js");
        assertPermissionDenied("Polyglot.import('x')", "js-security.js");
        assertPermissionDenied("require('fs')", "js-security.js");
        assertPermissionDenied("fetch('https://example.com')", "js-security.js");
        assertPermissionDenied("load('/tmp/ecritum.js')", "js-security.js");
        assertPermissionDenied("read('/tmp/ecritum.js')", "js-security.js");
        assertPermissionDenied("Function(\"return Java.type('java.lang.System')\")()", "js-security.js");
        assertPermissionDenied("Java.addToClasspath('/tmp/ecritum')", "js-security.js");
    }

    @Test
    void rejectsPromisesAndTopLevelAwait() {
        SciEvalResult promise = JavaScriptEvaluator.evaluate("(async function(){ return 42; })()", "promise-source.js");
        assertEquals(EcritumStatus.SCRIPT, promise.status());
        assertEquals("runtime", promise.category());
        assertTrue(promise.message().contains("promises"));

        SciEvalResult await = JavaScriptEvaluator.evaluate("await 42", "await-source.js");
        assertEquals(EcritumStatus.SCRIPT, await.status());
        assertEquals("syntax", await.category());
    }

    @Test
    void rejectsUnsupportedResultValuesWithScriptErrors() {
        assertScriptError("Symbol('x')");
        assertScriptError("function f() { return 1; }; f");
        assertScriptError("new Map([['a', 1]])");
        assertScriptError("const x = {}; x.self = x; x");
        assertScriptError("NaN");
        assertScriptError("Infinity");
        assertScriptError("9223372036854775808n");
    }

    @Test
    void projectsExplicitHostFunctionsUnderEcritumGlobal() {
        SciEvalResult result = JavaScriptEvaluator.evaluate(
            "ecritum.app.answer()",
            "host-source.js",
            List.of(new HostProjection("app", "answer")),
            (namespace, function, arguments) -> {
                assertEquals("app", namespace);
                assertEquals("answer", function);
                assertEquals(List.of(), arguments);
                return 42L;
            }
        );

        assertEquals(EcritumStatus.OK, result.status(), result.message());
        assertEquals(42L, result.value());
    }

    @Test
    void passesHostArgumentsAndResults() {
        SciEvalResult result = JavaScriptEvaluator.evaluate(
            "ecritum.app.combine(41, 'done', {ok: true})",
            "host-args.js",
            List.of(new HostProjection("app", "combine")),
            (namespace, function, arguments) -> {
                assertEquals("app", namespace);
                assertEquals("combine", function);
                assertEquals(List.of(41L, "done", Map.of("ok", true)), arguments);
                return List.of(arguments.get(0), arguments.get(1));
            }
        );

        assertEquals(EcritumStatus.OK, result.status(), result.message());
        assertEquals(List.of(41L, "done"), result.value());
    }

    @Test
    void mapsHostCallbackFailureToCallbackStatus() {
        SciEvalResult result = JavaScriptEvaluator.evaluate(
            "ecritum.app.fail()",
            "host-fail.js",
            List.of(new HostProjection("app", "fail")),
            (namespace, function, arguments) -> {
                throw new HostFunctionException(EcritumStatus.CALLBACK, "callback", "token=SECRET");
            }
        );

        assertEquals(EcritumStatus.CALLBACK, result.status());
        assertEquals("callback", result.category());
        assertEquals("javascript", result.language());
        assertEquals("host-fail.js", result.sourceName());
        assertTrue(result.message().contains("token=<redacted>"));
        assertTrue(!result.message().contains("SECRET"));
    }

    @Test
    void failsClosedForProjectionCollisions() {
        assertPermissionResult(JavaScriptEvaluator.evaluate(
            "1",
            "collision.js",
            List.of(new HostProjection("json", "custom")),
            (namespace, function, arguments) -> 1L
        ));
        assertPermissionResult(JavaScriptEvaluator.evaluate(
            "1",
            "collision.js",
            List.of(new HostProjection("app", "tools"), new HostProjection("app.tools", "notify")),
            (namespace, function, arguments) -> 1L
        ));
    }

    @Test
    void installsPureStandardLibraryFacadesAndDefaultDenials() {
        assertEquals("{\"a\":1,\"b\":2}", ok("ecritum.json.writeString({b: 2, a: 1})"));

        SciEvalResult jsonRead = JavaScriptEvaluator.evaluate(
            "ecritum.json.readString('{\"items\":[true,false,\"x\"],\"n\":1}')",
            "facade-json.js"
        );
        LinkedHashMap<String, Object> expected = new LinkedHashMap<>();
        expected.put("items", List.of(true, false, "x"));
        expected.put("n", 1L);
        assertEquals(EcritumStatus.OK, jsonRead.status(), jsonRead.message());
        assertEquals(expected, jsonRead.value());

        assertEquals(
            "2026-06-05T00:00:00Z",
            ok("ecritum.time.formatInstant(ecritum.time.parseInstant('2026-06-05T00:00:00Z'))")
        );
        assertPermissionDenied("ecritum.time.now()", "facade-time.js");
        assertPermissionDenied("ecritum.fs.readText('/tmp/ecritum')", "facade-fs.js");
        assertPermissionDenied("ecritum.http.request({url: 'https://example.com'})", "facade-http.js");
    }

    private Object ok(String source) {
        SciEvalResult result = JavaScriptEvaluator.evaluate(source, "value-source.js");
        assertEquals(EcritumStatus.OK, result.status(), result.message());
        return result.value();
    }

    private void assertScriptError(String source) {
        SciEvalResult result = JavaScriptEvaluator.evaluate(source, "script-error.js");
        assertEquals(EcritumStatus.SCRIPT, result.status(), source);
        assertEquals("runtime", result.category(), source);
        assertInstanceOf(String.class, result.message());
    }

    private void assertPermissionDenied(String source, String sourceName) {
        assertPermissionResult(JavaScriptEvaluator.evaluate(source, sourceName));
    }

    private void assertPermissionResult(SciEvalResult result) {
        assertEquals(EcritumStatus.PERMISSION_DENIED, result.status(), result.message());
        assertEquals("permission", result.category());
        assertEquals("javascript", result.language());
    }
}
