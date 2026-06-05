package ecritum;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;

final class SciClojureEvaluatorTest {
    @Test
    void evaluatesScalarAndCollectionValues() {
        assertEquals(42L, ok("(+ 40 2)"));
        assertEquals(List.of(1L, 2L, 3L), ok("[1 2 3]"));
        assertEquals(Map.of("answer", 42L), ok("{\"answer\" 42}"));
        assertEquals(null, ok("nil"));
        assertEquals(Boolean.TRUE, ok("true"));
        assertEquals("hello", ok("\"hello\""));
        assertEquals(3.5d, ok("3.5"));
    }

    @Test
    void rejectsAmbientHostEscapeHatches() {
        assertScriptError("(Class/forName \"java.lang.System\")", "permission");
        assertScriptError("(java.lang.System/getenv)", "permission");
        assertScriptError("(load-file \"/etc/passwd\")", "permission");
        assertScriptError("(future 1)", "permission");
    }

    @Test
    void reportsSyntaxAndRuntimeErrorsWithSourceName() {
        SciEvalResult syntax = SciClojureEvaluator.evaluate("(defn", "syntax-source.clj");
        assertEquals(EcritumStatus.SCRIPT, syntax.status());
        assertEquals("clojure", syntax.language());
        assertEquals("syntax-source.clj", syntax.sourceName());
        assertEquals("syntax", syntax.category());
        assertTrue(syntax.message().contains("syntax-source.clj"));

        SciEvalResult runtime = SciClojureEvaluator.evaluate("(/ 1 0)", "runtime-source.clj");
        assertEquals(EcritumStatus.SCRIPT, runtime.status());
        assertEquals("clojure", runtime.language());
        assertEquals("runtime-source.clj", runtime.sourceName());
        assertEquals("runtime", runtime.category());
        assertTrue(runtime.message().contains("runtime-source.clj"));
    }

    @Test
    void rejectsDuplicateNormalizedMapKeys() {
        SciEvalResult duplicate = SciClojureEvaluator.evaluate("{:answer 1 \"answer\" 2}", "duplicate-source.clj");

        assertEquals(EcritumStatus.SCRIPT, duplicate.status());
        assertEquals("runtime", duplicate.category());
        assertTrue(duplicate.message().contains("duplicate normalized map key"));
    }

    @Test
    void redactsSecretBearingRuntimeDiagnostics() {
        SciEvalResult leaked = SciClojureEvaluator.evaluate(
            "(throw (ex-info \"token=SECRET&password=hunter2 https://example.com/callback?token=SECRET\" {}))",
            "secret-source.clj"
        );

        assertEquals(EcritumStatus.SCRIPT, leaked.status());
        assertEquals("runtime", leaked.category());
        assertTrue(leaked.message().contains("token=<redacted>"));
        assertTrue(leaked.message().contains("<url>"));
        assertTrue(!leaked.message().contains("SECRET"));
        assertTrue(!leaked.message().contains("hunter2"));
        assertTrue(!leaked.message().contains("example.com/callback"));
    }

    @Test
    void projectsExplicitHostFunctionsIntoSciNamespaces() {
        SciEvalResult result = SciClojureEvaluator.evaluate(
            "(app/answer)",
            "host-source.clj",
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
    void passesNormalizedHostArgumentsAndResults() {
        SciEvalResult result = SciClojureEvaluator.evaluate(
            "(app/combine 41 \"done\" {:ok true})",
            "host-args.clj",
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
    void hostFunctionsCanReturnDataValues() {
        SciEvalResult result = SciClojureEvaluator.evaluate(
            "(app/blob)",
            "host-data.clj",
            List.of(new HostProjection("app", "blob")),
            (namespace, function, arguments) -> new byte[] {0, 1, 2, -1}
        );

        assertEquals(EcritumStatus.OK, result.status(), result.message());
        assertArrayEquals(new byte[] {0, 1, 2, -1}, (byte[]) result.value());
    }

    @Test
    void mapsHostCallbackFailureToCallbackStatus() {
        SciEvalResult result = SciClojureEvaluator.evaluate(
            "(app/fail)",
            "host-fail.clj",
            List.of(new HostProjection("app", "fail")),
            (namespace, function, arguments) -> {
                throw new HostFunctionException(EcritumStatus.CALLBACK, "callback", "token=SECRET");
            }
        );

        assertEquals(EcritumStatus.CALLBACK, result.status());
        assertEquals("callback", result.category());
        assertEquals("clojure", result.language());
        assertEquals("host-fail.clj", result.sourceName());
        assertTrue(result.message().contains("callback error"));
        assertTrue(result.message().contains("token=<redacted>"));
        assertTrue(!result.message().contains("SECRET"));
    }

    @Test
    void unprojectedHostNamespacesAreNotCallable() {
        SciEvalResult result = SciClojureEvaluator.evaluate("(app/answer)", "missing-host.clj");

        assertEquals(EcritumStatus.SCRIPT, result.status());
        assertEquals("runtime", result.category());
    }

    @Test
    void deniesProjectionBypassProbesBeforeHostInvocation() {
        List<String> probes = List.of(
            "((resolve (symbol \"app\" \"answer\")))",
            "((ns-resolve 'app 'answer))",
            "(requiring-resolve 'app/answer)",
            "(load-string \"(app/answer)\")",
            "(require 'app)",
            "(import java.io.File)",
            "(new java.io.File \"/tmp/ecritum\")",
            "(.getClass (app/answer))"
        );

        for (String probe : probes) {
            AtomicInteger calls = new AtomicInteger();
            SciEvalResult result = SciClojureEvaluator.evaluate(
                probe,
                "projection-bypass.clj",
                List.of(new HostProjection("app", "answer")),
                (namespace, function, arguments) -> {
                    calls.incrementAndGet();
                    return 42L;
                }
            );

            assertEquals(EcritumStatus.SCRIPT, result.status(), probe);
            assertEquals("permission", result.category(), probe);
            assertEquals(0, calls.get(), probe);
        }
    }

    @Test
    void backendWireRoundTripsSuccessAndErrorResults() {
        byte[] encodedValue = BackendResultCodec.encode(SciEvalResult.ok(List.of(1L, Map.of("nested", true))));
        SciEvalResult decodedValue = BackendResultCodec.decode(encodedValue);
        assertEquals(EcritumStatus.OK, decodedValue.status());
        assertEquals(List.of(1L, Map.of("nested", true)), decodedValue.value());

        SciEvalResult scriptError = SciEvalResult.scriptError("clojure", "wire.clj", "runtime", "wire.clj: boom");
        byte[] encodedError = BackendResultCodec.encode(scriptError);
        SciEvalResult decodedError = BackendResultCodec.decode(encodedError);
        assertEquals(EcritumStatus.SCRIPT, decodedError.status());
        assertEquals("clojure", decodedError.language());
        assertEquals("wire.clj", decodedError.sourceName());
        assertEquals("runtime", decodedError.category());
        assertEquals("wire.clj: boom", decodedError.message());

        byte[] encodedData = BackendResultCodec.encode(SciEvalResult.ok(new byte[] {0, 1, 2, -1}));
        SciEvalResult decodedData = BackendResultCodec.decode(encodedData);
        assertEquals(EcritumStatus.OK, decodedData.status());
        assertArrayEquals(new byte[] {0, 1, 2, -1}, (byte[]) decodedData.value());
    }

    private Object ok(String source) {
        SciEvalResult result = SciClojureEvaluator.evaluate(source, "value-source.clj");
        assertEquals(EcritumStatus.OK, result.status(), result.message());
        return result.value();
    }

    private void assertScriptError(String source, String category) {
        SciEvalResult result = SciClojureEvaluator.evaluate(source, "security-source.clj");
        assertEquals(EcritumStatus.SCRIPT, result.status(), source);
        assertEquals(category, result.category(), source);
        assertInstanceOf(String.class, result.message());
    }
}
