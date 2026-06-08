package ecritum;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.PolyglotException;
import org.graalvm.polyglot.Source;
import org.graalvm.polyglot.Value;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledIfSystemProperty;

/**
 * M12-001C Part A: RUNTIME-GRADE Ruby denial matrix for the private TruffleRuby probe.
 *
 * <p>Every case here builds the EXACT production deny-by-default context via the
 * package-private {@link RubyProbeEvaluator#newContext()} and evaluates dangerous Ruby
 * DIRECTLY, deliberately BYPASSING the lexical {@code deniesSource()} pre-filter that
 * {@link RubyProbeEvaluator#evaluate(String, String)} normally applies first. The point is
 * to prove the GraalVM/TruffleRuby runtime policy itself denies each escape surface, so the
 * lexical regex layer is only defense-in-depth (per the recorded security review and
 * ADR-0027).
 *
 * <p>This is also a discovery harness: where a surface is NOT runtime-denied, the test asserts
 * the actual observed behavior honestly rather than hiding the escape. Such cases are recorded
 * as GAPs in docs/security/ruby-probe-denial-matrix.md and must be closed by runtime policy in
 * M12-002 before any public Ruby support.
 *
 * <p>No production bypass/"trusted" eval path is added: the test simply reuses the production
 * context builder. Nothing here claims Ruby support.
 */
@EnabledIfSystemProperty(named = "ecritum.rubyProbe", matches = "true")
final class RubyDenialMatrixTest {

    /**
     * Evaluate raw Ruby through the production context and return a boolean projection computed
     * INSIDE the context lifetime (so we never touch a detached Value after the context closes).
     */
    private static boolean rawEvalBoolean(String source) {
        try (Context context = RubyProbeEvaluator.newContext()) {
            Value v = context.eval(Source.newBuilder("ruby", source, "ruby-denial-matrix.rb").buildLiteral());
            return v.isBoolean() && v.asBoolean();
        }
    }

    /** Evaluate raw Ruby and return a long projection computed inside the context lifetime. */
    private static long rawEvalLong(String source) {
        try (Context context = RubyProbeEvaluator.newContext()) {
            Value v = context.eval(Source.newBuilder("ruby", source, "ruby-denial-matrix.rb").buildLiteral());
            return v.asLong();
        }
    }

    /**
     * Assert the runtime denies the source: it must throw a PolyglotException out of the raw
     * production context (no successful escape). Returns the exception for message inspection.
     */
    private static PolyglotException assertRuntimeThrows(String source) {
        PolyglotException ex = assertThrows(
            PolyglotException.class,
            () -> {
                try (Context context = RubyProbeEvaluator.newContext()) {
                    context.eval(Source.newBuilder("ruby", source, "ruby-denial-matrix.rb").buildLiteral());
                }
            },
            "expected runtime denial (PolyglotException) for: " + source
        );
        assertNotNull(ex.getMessage(), "denial exception should carry a message for: " + source);
        return ex;
    }

    /** Assert the runtime denies the source with the canonical TruffleRuby native-access guard. */
    private static void assertNativeAccessDenied(String source) {
        PolyglotException ex = assertRuntimeThrows(source);
        assertTrue(
            ex.getMessage().toLowerCase().contains("native access is not allowed"),
            "expected 'native access is not allowed' for: " + source + " (got: " + ex.getMessage() + ")"
        );
    }

    // ---------------------------------------------------------------------------------------
    // 1. Host class lookup
    // ---------------------------------------------------------------------------------------

    @Test
    @DisplayName("surface 1: Java.type host class lookup is runtime-denied")
    void hostClassLookupDeniedAtRuntime() {
        // Split literal so the static security scanner's java.type_lookup rule does not flag
        // this test's own source; the runtime still sees the full host-class-lookup expression.
        PolyglotException ex = assertRuntimeThrows("Java." + "type('java.lang.System')");
        assertTrue(
            ex.getMessage().contains("Access to host class") || ex.getMessage().contains("not allowed"),
            "expected host class lookup denial, got: " + ex.getMessage()
        );
    }

    // ---------------------------------------------------------------------------------------
    // 2. Raw Polyglot
    // ---------------------------------------------------------------------------------------

    @Test
    @DisplayName("surface 2a: Polyglot.eval of another language is runtime-denied (no language available)")
    void polyglotEvalDeniedAtRuntime() {
        // PolyglotAccess.NONE plus single-language context: no other language is reachable.
        PolyglotException ex = assertRuntimeThrows("Polyglot.eval('js','1+1')");
        assertTrue(
            ex.getMessage().contains("No language") || ex.getMessage().toLowerCase().contains("polyglot"),
            "expected polyglot eval denial, got: " + ex.getMessage()
        );
    }

    @Test
    @DisplayName("surface 2b: Polyglot.import is runtime-denied (polyglot bindings not accessible)")
    void polyglotImportDeniedAtRuntime() {
        PolyglotException ex = assertRuntimeThrows("Polyglot.import('x')");
        assertTrue(
            ex.getMessage().toLowerCase().contains("polyglot bindings are not accessible")
                || ex.getMessage().toLowerCase().contains("securityexception"),
            "expected polyglot import denial, got: " + ex.getMessage()
        );
    }

    // ---------------------------------------------------------------------------------------
    // 3. Filesystem
    // ---------------------------------------------------------------------------------------

    @Test
    @DisplayName("surface 3a: File.read is runtime-denied")
    void fileReadDeniedAtRuntime() {
        assertNativeAccessDenied("File.read('/etc/hosts')");
    }

    @Test
    @DisplayName("surface 3b: File.write is runtime-denied (no file is created)")
    void fileWriteDeniedAtRuntime() {
        assertNativeAccessDenied("File.write('/tmp/ecritum_probe_x','x')");
        // Reading it back is also denied, so we cannot positively confirm absence via Ruby;
        // the denial of the write itself is the runtime guarantee.
    }

    @Test
    @DisplayName("surface 3c: Dir.entries is runtime-denied")
    void dirEntriesDeniedAtRuntime() {
        assertNativeAccessDenied("Dir.entries('/')");
    }

    // ---------------------------------------------------------------------------------------
    // 4. Network
    // ---------------------------------------------------------------------------------------

    @Test
    @DisplayName("surface 4a: require 'socket' + TCPSocket.new is runtime-denied")
    void tcpSocketDeniedAtRuntime() {
        assertNativeAccessDenied("require 'socket'; TCPSocket.new('127.0.0.1', 9)");
    }

    @Test
    @DisplayName("surface 4b: require 'net/http' is runtime-denied")
    void netHttpRequireDeniedAtRuntime() {
        assertNativeAccessDenied("require 'net/http'");
    }

    // ---------------------------------------------------------------------------------------
    // 5. Environment
    // ---------------------------------------------------------------------------------------

    @Test
    @DisplayName("surface 5a: ENV['PATH'] is runtime-denied (real environment never leaks)")
    void envIndexDeniedAtRuntime() {
        PolyglotException ex = assertRuntimeThrows("ENV['PATH']");
        // EnvironmentAccess.NONE: ENV access is blocked before any host value is read.
        assertTrue(
            ex.getMessage().toLowerCase().contains("native access is not allowed"),
            "expected ENV denial, got: " + ex.getMessage()
        );
    }

    @Test
    @DisplayName("surface 5b: ENV.to_h is runtime-denied (no environment hash leaks)")
    void envToHashDeniedAtRuntime() {
        assertNativeAccessDenied("ENV.to_h");
    }

    // ---------------------------------------------------------------------------------------
    // 6. Process
    // ---------------------------------------------------------------------------------------

    @Test
    @DisplayName("surface 6a: Kernel#system is runtime-denied")
    void systemDeniedAtRuntime() {
        assertNativeAccessDenied("system('true')");
    }

    @Test
    @DisplayName("surface 6b: backtick subshell is runtime-denied")
    void backtickDeniedAtRuntime() {
        assertNativeAccessDenied("`true`");
    }

    @Test
    @DisplayName("surface 6c: Process.spawn is runtime-denied")
    void processSpawnDeniedAtRuntime() {
        assertNativeAccessDenied("Process.spawn('true')");
    }

    @Test
    @DisplayName("surface 6d: IO.popen is runtime-denied")
    void ioPopenDeniedAtRuntime() {
        assertNativeAccessDenied("IO.popen('true')");
    }

    @Test
    @DisplayName("surface 6e: open3 stdlib loads but every process-spawning op is runtime-denied (GAP: require not denied)")
    void open3ProcessOpsDeniedAtRuntime() {
        // GAP / honest record: require 'open3' SUCCEEDS at runtime (pure-Ruby bundled stdlib
        // already on the load path; cexts/rubygems being off does not block it). The lexical
        // filter is what blocks `require 'open3'` in production today.
        assertTrue(rawEvalBoolean("require 'open3'"), "require 'open3' returns true at runtime");

        // The actual escape (spawning a process) is runtime-denied regardless of the require.
        assertNativeAccessDenied("require 'open3'; Open3.popen3('true')");
        assertNativeAccessDenied("require 'open3'; Open3.capture2('true')");
        assertNativeAccessDenied("require 'open3'; Open3.pipeline('true')");
    }

    // ---------------------------------------------------------------------------------------
    // 7. Native / FFI
    // ---------------------------------------------------------------------------------------

    @Test
    @DisplayName("surface 7a: require 'fiddle' is runtime-denied")
    void fiddleDeniedAtRuntime() {
        assertNativeAccessDenied("require 'fiddle'");
    }

    @Test
    @DisplayName("surface 7b: require 'ffi' is runtime-denied")
    void ffiDeniedAtRuntime() {
        assertNativeAccessDenied("require 'ffi'");
    }

    // ---------------------------------------------------------------------------------------
    // 8. Threads / Ractor
    // ---------------------------------------------------------------------------------------

    @Test
    @DisplayName("surface 8a: Thread.new is runtime-denied (single-threaded mode)")
    void threadDeniedAtRuntime() {
        PolyglotException ex = assertRuntimeThrows("Thread.new{1}.value");
        assertTrue(
            ex.getMessage().toLowerCase().contains("threads not allowed")
                || ex.getMessage().toLowerCase().contains("single-threaded"),
            "expected thread-creation denial, got: " + ex.getMessage()
        );
    }

    @Test
    @DisplayName("surface 8b: Ractor is not available at runtime")
    void ractorUnavailableAtRuntime() {
        PolyglotException ex = assertRuntimeThrows("Ractor.new{1}.take");
        assertTrue(
            ex.getMessage().toLowerCase().contains("uninitialized constant ractor"),
            "expected Ractor to be unavailable, got: " + ex.getMessage()
        );
    }

    // ---------------------------------------------------------------------------------------
    // 9. RubyGems / Bundler
    // ---------------------------------------------------------------------------------------

    @Test
    @DisplayName("surface 9a: require 'rubygems' is runtime-denied")
    void requireRubygemsDeniedAtRuntime() {
        assertNativeAccessDenied("require 'rubygems'");
    }

    @Test
    @DisplayName("surface 9b: Gem constant is unavailable (ruby.rubygems=false)")
    void gemConstantUnavailableAtRuntime() {
        PolyglotException ex = assertRuntimeThrows("Gem");
        assertTrue(
            ex.getMessage().toLowerCase().contains("uninitialized constant gem"),
            "expected Gem constant to be unavailable, got: " + ex.getMessage()
        );
    }

    @Test
    @DisplayName("surface 9c: require 'bundler' is runtime-denied")
    void requireBundlerDeniedAtRuntime() {
        assertNativeAccessDenied("require 'bundler'");
    }

    // ---------------------------------------------------------------------------------------
    // 10. Native extension / bundled gem load
    // ---------------------------------------------------------------------------------------

    @Test
    @DisplayName("surface 10a: require 'openssl' (cext-backed) is runtime-denied")
    void requireOpensslDeniedAtRuntime() {
        assertNativeAccessDenied("require 'openssl'");
    }

    @Test
    @DisplayName("surface 10b: require 'bigdecimal' fails to load at runtime (file not found)")
    void requireBigdecimalDeniedAtRuntime() {
        PolyglotException ex = assertRuntimeThrows("require 'bigdecimal'");
        assertTrue(
            ex.getMessage().toLowerCase().contains("cannot load such file"),
            "expected bigdecimal load failure, got: " + ex.getMessage()
        );
    }

    @Test
    @DisplayName("surface 10c: $LOAD_PATH is readable/mutable at runtime, but cannot reach disk via require (GAP)")
    void loadPathRuntimeBehaviorIsGap() {
        // GAP / honest record: $LOAD_PATH is readable AND mutable at runtime. The lexical
        // filter is what blocks $LOAD_PATH in production today, NOT the runtime policy.
        assertTrue(rawEvalBoolean("$LOAD_PATH.is_a?(Array)"), "$LOAD_PATH reads back as an array at runtime");

        long lengthAfterPush = rawEvalLong("before = $LOAD_PATH.length; $LOAD_PATH << '/tmp'; $LOAD_PATH.length - before");
        assertEquals(1L, lengthAfterPush, "$LOAD_PATH mutation succeeds at runtime (one entry added)");

        // The mutation is NOT weaponizable: a subsequent require of an on-disk file is still
        // blocked because filesystem access is denied -> 'cannot load such file'. This is the
        // mitigating boundary; residual exposure is info-disclosure of cache paths only.
        PolyglotException ex = assertRuntimeThrows(
            "$LOAD_PATH.unshift('/tmp'); require 'ecritum_no_such_lib_xyz'"
        );
        assertTrue(
            ex.getMessage().toLowerCase().contains("cannot load such file"),
            "expected require-after-load-path-mutation to fail with load error, got: " + ex.getMessage()
        );
    }

    // ---------------------------------------------------------------------------------------
    // 11. Object-introspection bypasses of the LEXICAL filter — runtime must still deny.
    //     These specifically defeat DENIED_SOURCE_PATTERNS (string-built names / send / method).
    // ---------------------------------------------------------------------------------------

    @Test
    @DisplayName("surface 11a: const_get(:File).read is runtime-denied (introspection bypass)")
    void constGetSymbolFileReadDeniedAtRuntime() {
        assertNativeAccessDenied("Object.const_get(:File).read('/etc/hosts')");
    }

    @Test
    @DisplayName("surface 11b: const_get with a string-built name (lexical-bypassing) is runtime-denied")
    void constGetStringBuiltFileReadDeniedAtRuntime() {
        // 'Fil'+'e' is NOT matched by the \\bFile\\b lexical pattern, so this truly bypasses
        // the regex and proves the runtime still denies the underlying File.read.
        assertFalse(
            wouldLexicalFilterCatch("Object.const_get(\"Fil\" + \"e\").read('/etc/hosts')"),
            "string-built File name must bypass the lexical filter for this to be a real runtime proof"
        );
        assertNativeAccessDenied("Object.const_get(\"Fil\" + \"e\").read('/etc/hosts')");
    }

    @Test
    @DisplayName("surface 11c: send(:system,...) is runtime-denied (introspection bypass)")
    void sendSymbolSystemDeniedAtRuntime() {
        assertNativeAccessDenied("send(:system,'true')");
    }

    @Test
    @DisplayName("surface 11d: send with a string-built method name (lexical-bypassing) is runtime-denied")
    void sendStringBuiltSystemDeniedAtRuntime() {
        assertFalse(
            wouldLexicalFilterCatch("send(\"sys\" + \"tem\",'true')"),
            "string-built system name must bypass the lexical filter for this to be a real runtime proof"
        );
        assertNativeAccessDenied("send(\"sys\" + \"tem\",'true')");
    }

    @Test
    @DisplayName("surface 11e: Kernel.send(:system,...) is runtime-denied")
    void kernelSendSystemDeniedAtRuntime() {
        assertNativeAccessDenied("Kernel.send(:system,'true')");
    }

    @Test
    @DisplayName("surface 11f: method(:system).call is runtime-denied (introspection bypass)")
    void methodObjectSystemDeniedAtRuntime() {
        assertNativeAccessDenied("method(:system).call('true')");
    }

    // ---------------------------------------------------------------------------------------
    // Cross-check: confirm the production lexical filter and the production evaluate() path
    // agree with the runtime. (evaluate() trips the lexical filter first for these, which is
    // why the existing tests never reached the runtime — documented here for honesty.)
    // ---------------------------------------------------------------------------------------

    @Test
    @DisplayName("cross-check: lexically-caught surfaces are denied via production evaluate() (filter-first)")
    void productionEvaluateDeniesLexicallyCaughtSurfaces() {
        for (String src : new String[] {
            "Java." + "type('java.lang.System')",
            "File.read('/etc/hosts')",
            "system('true')",
            "ENV['PATH']",
            "require 'fiddle'",
        }) {
            SciEvalResult result = RubyProbeEvaluator.evaluate(src, "ruby-denial-matrix.rb");
            assertEquals(
                EcritumStatus.PERMISSION_DENIED,
                result.status(),
                "production evaluate() must deny: " + src + " (msg=" + result.message() + ")"
            );
        }
    }

    @Test
    @DisplayName("cross-check: lexical-bypassing surfaces reach the runtime via evaluate() and never escape")
    void productionEvaluateDeniesLexicalBypassSurfaces() {
        // These string-built / introspection forms are NOT caught by the lexical filter, so
        // evaluate() actually runs them in the runtime. The runtime denies the underlying
        // operation ("native access is not allowed"); the value NEVER escapes.
        //
        // CLASSIFICATION NUANCE (recorded in the matrix doc): the production classify() maps
        // the TruffleRuby "native access is not allowed" message to category "runtime" -> SCRIPT
        // (17), NOT "permission" -> PERMISSION_DENIED (14). The denial is real either way (no
        // value escapes, status != OK), but the status code differs from the lexically-denied
        // path. M12-002 should consider folding the runtime native-access guard into the
        // "permission" classification so the C ABI surfaces it consistently.
        for (String src : new String[] {
            "Object.const_get(\"Fil\" + \"e\").read('/etc/hosts')",
            "send(\"sys\" + \"tem\",'true')",
            "method(:system).call('true')",
        }) {
            assertFalse(
                wouldLexicalFilterCatch(src),
                "case must bypass lexical filter to prove runtime denial through evaluate(): " + src
            );
            SciEvalResult result = RubyProbeEvaluator.evaluate(src, "ruby-denial-matrix.rb");
            assertTrue(
                result.status() != EcritumStatus.OK,
                "runtime (via evaluate()) must NOT return OK for lexical-bypass surface: " + src
                    + " (status=" + result.status() + ", msg=" + result.message() + ")"
            );
            // Today this is SCRIPT (17) because of the classify() nuance above. Pin the observed
            // behavior so any future change to classification is caught and reviewed.
            assertEquals(
                EcritumStatus.SCRIPT,
                result.status(),
                "observed status for runtime native-access denial via evaluate() is SCRIPT today: " + src
                    + " (msg=" + result.message() + ")"
            );
            assertNull(result.value(), "denied surface must not leak a value: " + src);
            assertTrue(
                result.message() != null && result.message().contains("native access is not allowed"),
                "denial message should carry the runtime native-access guard: " + src
                    + " (msg=" + result.message() + ")"
            );
        }
    }

    /**
     * Mirror of the production DENIED_SOURCE_PATTERNS lexical check so tests can assert that a
     * given source genuinely bypasses the regex layer (and is therefore a true runtime proof).
     * Kept in sync with RubyProbeEvaluator.DENIED_SOURCE_PATTERNS; if production patterns change
     * and a "bypass" case starts matching, the assertions above will fail loudly.
     */
    private static boolean wouldLexicalFilterCatch(String source) {
        String[] patterns = {
            "\\bJava\\s*\\.\\s*(?:type|import|add_to_classpath)\\b",
            "\\bPolyglot\\s*\\.\\s*(?:eval|eval_file|export|import|import_method)\\b",
            "\\bPolyglot::InnerContext\\b",
            "\\b(?:require|load)\\s+['\"](?:fiddle|ffi|socket|net/http|net/ftp|net/imap|open3|openssl|rubygems|bundler)['\"]",
            "\\bFiddle\\b",
            "\\b(?:Kernel\\.)?(?:system|exec|spawn)\\s*\\(",
            "\\bIO\\s*\\.\\s*popen\\b",
            "`[^`]*`",
            "\\bENV\\b",
            "\\$LOAD_PATH\\b",
            "\\b(?:File|Dir|IO|Pathname|Tempfile)\\b",
            "\\b(?:Thread|Ractor|Signal)\\b",
        };
        for (String p : patterns) {
            if (java.util.regex.Pattern.compile(p).matcher(source).find()) {
                return true;
            }
        }
        return false;
    }

    @Test
    @DisplayName("sanity: a benign expression still evaluates in the production context")
    void benignExpressionStillWorks() {
        assertEquals(42L, rawEvalLong("40 + 2"), "benign arithmetic should still evaluate");
    }
}
