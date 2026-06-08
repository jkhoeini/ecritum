package ecritum;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;

final class LuaEvaluatorTest {
    @Test
    void evaluatesScalarAndCollectionValues() {
        assertEquals(42L, ok("return 40 + 2"));
        assertEquals(null, ok("return nil"));
        assertEquals(Boolean.TRUE, ok("return true"));
        assertEquals("hello", ok("return 'hello'"));
        assertEquals(3.5d, ok("return 3.5"));
        assertEquals(List.of(1L, "two", true), ok("return {1, 'two', true}"));
        assertEquals(Map.of("answer", 42L), ok("return {answer = 42}"));
    }

    @Test
    void reportsSyntaxRuntimePermissionAndTimeoutErrorsWithSourceName() {
        SciEvalResult syntax = LuaEvaluator.evaluate("function", "syntax-source.lua");
        assertEquals(EcritumStatus.SCRIPT, syntax.status());
        assertEquals("lua", syntax.language());
        assertEquals("syntax-source.lua", syntax.sourceName());
        assertEquals("syntax", syntax.category());
        assertTrue(syntax.message().contains("syntax-source.lua"));

        SciEvalResult runtime = LuaEvaluator.evaluate("error('boom')", "runtime-source.lua");
        assertEquals(EcritumStatus.SCRIPT, runtime.status());
        assertEquals("lua", runtime.language());
        assertEquals("runtime-source.lua", runtime.sourceName());
        assertEquals("runtime", runtime.category());
        assertTrue(runtime.message().contains("runtime-source.lua"));

        SciEvalResult permission = LuaEvaluator.evaluate("io.open('/tmp/ecritum')", "permission-source.lua");
        assertEquals(EcritumStatus.PERMISSION_DENIED, permission.status());
        assertEquals("permission", permission.category());
        assertEquals("permission-source.lua", permission.sourceName());

        SciEvalResult timeout = LuaEvaluator.evaluate("while true do end", "timeout-source.lua");
        assertEquals(EcritumStatus.TIMEOUT, timeout.status());
        assertEquals("timeout", timeout.category());
        assertEquals("timeout-source.lua", timeout.sourceName());
    }

    @Test
    void deniesAmbientLuaJEscapeHatches() {
        assertPermissionDenied("luajava.bindClass('java.lang.System')");
        assertPermissionDenied("Java.type('java.lang.System')");
        assertPermissionDenied("require('io')");
        assertPermissionDenied("package.loadlib('x', 'y')");
        assertPermissionDenied("load('return 1')");
        assertPermissionDenied("loadstring('return 1')");
        assertPermissionDenied("loadfile('/tmp/ecritum.lua')");
        assertPermissionDenied("dofile('/tmp/ecritum.lua')");
        assertPermissionDenied("debug.getregistry()");
        assertPermissionDenied("os.getenv('HOME')");
        assertPermissionDenied("io.open('/tmp/ecritum')");
        assertPermissionDenied("string.dump(function() end)");
        assertPermissionDenied("coroutine.resume(coroutine.create(function() while true do end end))");
    }

    @Test
    void removesLoaderDebuggerDumpAndCoroutineSurfaceFromGlobals() {
        assertEquals(Boolean.TRUE, ok("""
            local g = _G
            return rawget(g, 'de' .. 'bug') == nil
              and rawget(g, 'pa' .. 'ckage') == nil
              and rawget(g, 're' .. 'quire') == nil
              and rawget(g, 'lo' .. 'ad') == nil
              and rawget(g, 'load' .. 'string') == nil
              and rawget(g, 'corou' .. 'tine') == nil
              and rawget(string, 'du' .. 'mp') == nil
            """));
    }

    @Test
    void rejectsUnsupportedResultValuesWithScriptErrors() {
        assertScriptError("return function() end");
        assertScriptError("return 0/0");
        assertScriptError("return 9223372036854775808");
        assertScriptError("local t = {}; t.self = t; return t");
        assertScriptError("return {1, answer = 42}");
        assertScriptError("return {1, nil, 3}");
    }

    @Test
    void projectsExplicitHostFunctionsUnderEcritumGlobal() {
        SciEvalResult result = LuaEvaluator.evaluate(
            "return ecritum.app.answer()",
            "host-source.lua",
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
        SciEvalResult result = LuaEvaluator.evaluate(
            "return ecritum.app.combine(41, 'done', {ok = true})",
            "host-args.lua",
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
        SciEvalResult result = LuaEvaluator.evaluate(
            "return ecritum.app.fail()",
            "host-fail.lua",
            List.of(new HostProjection("app", "fail")),
            (namespace, function, arguments) -> {
                throw new HostFunctionException(EcritumStatus.CALLBACK, "callback", "token=SECRET");
            }
        );

        assertEquals(EcritumStatus.CALLBACK, result.status());
        assertEquals("callback", result.category());
        assertEquals("lua", result.language());
        assertEquals("host-fail.lua", result.sourceName());
        assertTrue(result.message().contains("token=<redacted>"));
        assertTrue(!result.message().contains("SECRET"));
    }

    @Test
    void failsClosedForProjectionCollisions() {
        assertPermissionResult(LuaEvaluator.evaluate(
            "return 1",
            "collision.lua",
            List.of(new HostProjection("json", "custom")),
            (namespace, function, arguments) -> 1L
        ));
        assertPermissionResult(LuaEvaluator.evaluate(
            "return 1",
            "collision.lua",
            List.of(new HostProjection("app", "tools"), new HostProjection("app.tools", "notify")),
            (namespace, function, arguments) -> 1L
        ));
    }

    @Test
    void installsPureStandardLibraryFacadesAndDefaultDenials() {
        assertEquals("{\"a\":1,\"b\":2}", ok("return ecritum.json.writeString({b = 2, a = 1})"));

        SciEvalResult jsonRead = LuaEvaluator.evaluate(
            "return ecritum.json.readString('{\"items\":[true,false,\"x\"],\"n\":1}')",
            "facade-json.lua"
        );
        LinkedHashMap<String, Object> expected = new LinkedHashMap<>();
        expected.put("items", List.of(true, false, "x"));
        expected.put("n", 1L);
        assertEquals(EcritumStatus.OK, jsonRead.status(), jsonRead.message());
        assertEquals(expected, jsonRead.value());

        assertEquals(
            "2026-06-05T00:00:00Z",
            ok("return ecritum.time.formatInstant(ecritum.time.parseInstant('2026-06-05T00:00:00Z'))")
        );
        assertPermissionDenied("return ecritum.time.now()");
        assertPermissionDenied("return ecritum.fs.readText('/tmp/ecritum')");
        assertPermissionDenied("return ecritum.http.request({url = 'https://example.com'})");
    }

    private Object ok(String source) {
        SciEvalResult result = LuaEvaluator.evaluate(source, "value-source.lua");
        assertEquals(EcritumStatus.OK, result.status(), result.message());
        assertEquals("lua", result.language());
        return result.value();
    }

    private void assertScriptError(String source) {
        SciEvalResult result = LuaEvaluator.evaluate(source, "script-error.lua");
        assertEquals(EcritumStatus.SCRIPT, result.status(), source);
        assertEquals("runtime", result.category(), source);
        assertInstanceOf(String.class, result.message());
    }

    private void assertPermissionDenied(String source) {
        assertPermissionResult(LuaEvaluator.evaluate(source, "security.lua"));
    }

    private void assertPermissionResult(SciEvalResult result) {
        assertEquals(EcritumStatus.PERMISSION_DENIED, result.status(), result.message());
        assertEquals("permission", result.category());
        assertEquals("lua", result.language());
    }
}
