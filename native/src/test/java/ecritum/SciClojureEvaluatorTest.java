package ecritum;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.LinkedHashMap;
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
    void installsEcritumJsonNamespaceWithDeterministicRoundTrip() {
        assertEquals("{\"a\":1,\"b\":2}", ok("(ecritum.json/write-string {\"b\" 2 \"a\" 1})"));

        SciEvalResult result = SciClojureEvaluator.evaluate(
            "(ecritum.json/read-string \"{\\\"items\\\":[true,false,\\\"x\\\"],\\\"n\\\":1}\")",
            "facade-json.clj"
        );

        LinkedHashMap<String, Object> expected = new LinkedHashMap<>();
        expected.put("items", List.of(true, false, "x"));
        expected.put("n", 1L);
        assertEquals(EcritumStatus.OK, result.status(), result.message());
        assertEquals(expected, result.value());
    }

    @Test
    void rejectsUnsupportedJsonShapesWithScriptErrors() {
        SciEvalResult duplicate = SciClojureEvaluator.evaluate(
            "(ecritum.json/read-string \"{\\\"a\\\":1,\\\"a\\\":2}\")",
            "facade-json-duplicate.clj"
        );
        assertEquals(EcritumStatus.SCRIPT, duplicate.status());
        assertEquals("runtime", duplicate.category());
        assertTrue(duplicate.message().contains("duplicate JSON object key"));

        SciEvalResult keywordKey = SciClojureEvaluator.evaluate(
            "(ecritum.json/write-string {:a 1})",
            "facade-json-key.clj"
        );
        assertEquals(EcritumStatus.SCRIPT, keywordKey.status());
        assertEquals("runtime", keywordKey.category());
        assertTrue(keywordKey.message().contains("string map keys"));

        SciEvalResult integerOverflow = SciClojureEvaluator.evaluate(
            "(ecritum.json/read-string \"9223372036854775808\")",
            "facade-json-overflow.clj"
        );
        assertEquals(EcritumStatus.SCRIPT, integerOverflow.status());
        assertEquals("runtime", integerOverflow.category());
        assertTrue(integerOverflow.message().contains("overflow"));

        SciEvalResult bigIntegerOverflow = SciClojureEvaluator.evaluate(
            "(ecritum.json/write-string 9223372036854775808N)",
            "facade-json-bigint.clj"
        );
        assertEquals(EcritumStatus.SCRIPT, bigIntegerOverflow.status());
        assertEquals("runtime", bigIntegerOverflow.category());
        assertTrue(bigIntegerOverflow.message().contains("integer"));

        SciEvalResult nonFinite = SciClojureEvaluator.evaluate(
            "(ecritum.json/write-string ##NaN)",
            "facade-json-nan.clj"
        );
        assertEquals(EcritumStatus.SCRIPT, nonFinite.status());
        assertEquals("runtime", nonFinite.category());
        assertTrue(nonFinite.message().contains("finite"));

        SciEvalResult loneSurrogate = SciClojureEvaluator.evaluate(
            "(ecritum.json/read-string \"\\\"\\\\uD800\\\"\")",
            "facade-json-surrogate.clj"
        );
        assertEquals(EcritumStatus.SCRIPT, loneSurrogate.status());
        assertEquals("runtime", loneSurrogate.category());
        assertTrue(loneSurrogate.message().contains("surrogate"));
    }

    @Test
    void rejectsDataValuesWhenWritingJson() {
        SciEvalResult data = SciClojureEvaluator.evaluate(
            "(ecritum.json/write-string (app/blob))",
            "facade-json-data.clj",
            List.of(new HostProjection("app", "blob")),
            (namespace, function, arguments) -> new byte[] {0, 1}
        );

        assertEquals(EcritumStatus.SCRIPT, data.status());
        assertEquals("runtime", data.category());
        assertTrue(data.message().contains("data"));
    }

    @Test
    void installsEcritumTimeNamespaceWithPureParseAndFormat() {
        assertEquals(
            "2026-06-05T00:00:00Z",
            ok("(ecritum.time/format-instant (ecritum.time/parse-instant \"2026-06-05T00:00:00Z\"))")
        );
    }

    @Test
    void deniesSideEffectFacadesByDefaultWithPermissionStatus() {
        assertPermissionDenied("(ecritum.time/now)", "facade-time.clj");
        assertPermissionDenied("(ecritum.fs/read-text \"/tmp/ecritum\")", "facade-fs.clj");
        assertPermissionDenied("(ecritum.fs/read-bytes \"/tmp/ecritum\")", "facade-fs.clj");
        assertPermissionDenied("(ecritum.fs/exists? \"/tmp/ecritum\")", "facade-fs.clj");
        assertPermissionDenied("(ecritum.http/request {\"url\" \"https://example.com\"})", "facade-http.clj");
    }

    @Test
    void mapsStandardLibraryBridgeResultsThroughFacades() {
        StandardLibraryPolicy policy = new StandardLibraryPolicy("read_only", List.of("/tmp/ecritum"), true, false, null);
        StandardLibraryBridge bridge = (operation, arguments) -> switch (operation) {
            case "time.now" -> StandardLibraryResult.success("2026-06-06T00:00:00Z");
            case "fs.read_text" -> {
                assertEquals(List.of("/tmp/ecritum/data.txt"), arguments);
                yield StandardLibraryResult.success("contents");
            }
            case "fs.read_bytes" -> StandardLibraryResult.success(new byte[] {0, 1, 2});
            case "fs.exists" -> StandardLibraryResult.success(true);
            default -> StandardLibraryResult.failure(EcritumStatus.INTERNAL, "internal", "unexpected operation");
        };

        SciEvalResult success = SciClojureEvaluator.evaluate(
            """
            {"now" (ecritum.time/now)
             "text" (ecritum.fs/read-text "/tmp/ecritum/data.txt")
             "bytes" (ecritum.fs/read-bytes "/tmp/ecritum/data.bin")
             "exists" (ecritum.fs/exists? "/tmp/ecritum/data.txt")}
            """,
            "bridge-success.clj",
            List.of(),
            (namespace, function, arguments) -> 0L,
            policy,
            bridge
        );

        assertEquals(EcritumStatus.OK, success.status(), success.message());
        Map<?, ?> value = assertInstanceOf(Map.class, success.value());
        assertEquals("2026-06-06T00:00:00Z", value.get("now"));
        assertEquals("contents", value.get("text"));
        assertArrayEquals(new byte[] {0, 1, 2}, (byte[]) value.get("bytes"));
        assertEquals(Boolean.TRUE, value.get("exists"));

        assertBridgeFailure(EcritumStatus.PERMISSION_DENIED, "permission");
        assertBridgeFailure(EcritumStatus.SCRIPT, "runtime");
        assertBridgeFailure(EcritumStatus.INTERNAL, "internal");
    }

    @Test
    void permitsOnlyLiteralEcritumRequireForms() {
        assertEquals(
            "{\"a\":1}",
            ok("(do (require 'ecritum.json) (ecritum.json/write-string {\"a\" 1}))")
        );
        assertEquals(
            "{\"a\":1}",
            ok("(do (require '[ecritum.json :as json]) (json/write-string {\"a\" 1}))")
        );
        assertEquals(
            "{\"a\":1}",
            ok("(do (require 'ecritum.time '[ecritum.json :as json] 'ecritum.fs 'ecritum.http) (json/write-string {\"a\" 1}))")
        );
        assertScriptError("(require 'clojure.java.io)", "permission");
        assertScriptError("(require '[ecritum.json :refer [write-string]])", "permission");
        assertScriptError("(require '[ecritum.json :refer :all])", "permission");
        assertScriptError("(require '[ecritum.json :as java.io])", "permission");
        assertScriptError("(require '[ecritum.json :as clojure.java.io])", "permission");
        assertScriptError("(require 'ecritum.json 'clojure.java.io)", "permission");
        assertScriptError("(require (symbol \"ecritum.json\"))", "permission");
        assertScriptError("(requiring-resolve 'ecritum.json/write-string)", "permission");
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

            assertEquals(EcritumStatus.PERMISSION_DENIED, result.status(), probe);
            assertEquals("permission", result.category(), probe);
            assertEquals(0, calls.get(), probe);
        }
    }

    @Test
    void backendWireRoundTripsSuccessAndErrorResults() {
        byte[] encodedValue = BackendResultCodec.encode(SciEvalResult.ok(List.of(1L, Map.of("nested", true))));
        SciEvalResult decodedValue = BackendResultCodec.decode(encodedValue);
        assertEquals(EcritumStatus.OK, decodedValue.status());
        assertEquals("", decodedValue.language());
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
        assertEquals("", decodedData.language());
        assertArrayEquals(new byte[] {0, 1, 2, -1}, (byte[]) decodedData.value());
    }

    private Object ok(String source) {
        SciEvalResult result = SciClojureEvaluator.evaluate(source, "value-source.clj");
        assertEquals(EcritumStatus.OK, result.status(), result.message());
        return result.value();
    }

    private void assertScriptError(String source, String category) {
        SciEvalResult result = SciClojureEvaluator.evaluate(source, "security-source.clj");
        int expectedStatus = category.equals("permission") ? EcritumStatus.PERMISSION_DENIED : EcritumStatus.SCRIPT;
        assertEquals(expectedStatus, result.status(), source);
        assertEquals(category, result.category(), source);
        assertInstanceOf(String.class, result.message());
    }

    private void assertPermissionDenied(String source, String sourceName) {
        SciEvalResult result = SciClojureEvaluator.evaluate(source, sourceName);
        assertEquals(EcritumStatus.PERMISSION_DENIED, result.status(), source);
        assertEquals("permission", result.category(), source);
        assertEquals("clojure", result.language());
        assertEquals(sourceName, result.sourceName());
        assertTrue(result.message().contains(sourceName));
    }

    private void assertBridgeFailure(int status, String category) {
        StandardLibraryPolicy policy = new StandardLibraryPolicy("denied", List.of(), true, false, null);
        SciEvalResult result = SciClojureEvaluator.evaluate(
            "(ecritum.time/now)",
            "bridge-failure.clj",
            List.of(),
            (namespace, function, arguments) -> 0L,
            policy,
            (operation, arguments) -> StandardLibraryResult.failure(status, category, "bridge failed")
        );

        assertEquals(status, result.status());
        assertEquals(category, result.category());
        assertEquals("clojure", result.language());
        assertEquals("bridge-failure.clj", result.sourceName());
        assertTrue(result.message().contains("bridge failed"));
    }
}
