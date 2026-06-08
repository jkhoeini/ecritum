package ecritum;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;

final class RubyEvaluatorTest {
    @Test
    void evaluatesScalarCollectionHashAndBytesValues() {
        assertEquals(42L, ok("40 + 2"));
        assertEquals(null, ok("nil"));
        assertEquals(Boolean.TRUE, ok("true"));
        assertEquals("hello", ok("'hello'"));
        assertEquals(3.5d, ok("3.5"));
        // BigInteger range like Python (2**63 fits a long via BigInteger path).
        assertEquals(1_000_000_000_000L, ok("1_000_000_000_000"));
        assertEquals(List.of(1L, "two", true), ok("[1, 'two', true]"));
        assertEquals(Map.of("answer", 42L), ok("{'answer' => 42}"));
        // ASCII-8BIT (binary) Ruby string -> bytes.
        assertArrayEquals(new byte[] {0, 1, -1}, (byte[]) ok("[0, 1, 255].pack('C*')"));
    }

    @Test
    void reportsStructuredErrorsWithSourceName() {
        SciEvalResult runtime = RubyEvaluator.evaluate("raise 'boom'", "ruby-error.rb");
        assertEquals(EcritumStatus.SCRIPT, runtime.status(), runtime.message());
        assertEquals("ruby", runtime.language());
        assertEquals("ruby-error.rb", runtime.sourceName());
        assertEquals("runtime", runtime.category());
        assertTrue(runtime.message().contains("ruby-error.rb"));

        SciEvalResult syntax = RubyEvaluator.evaluate("def", "ruby-syntax.rb");
        assertEquals(EcritumStatus.SCRIPT, syntax.status(), syntax.message());
        assertEquals("ruby", syntax.language());
        assertEquals("ruby-syntax.rb", syntax.sourceName());
        assertEquals("syntax", syntax.category());
    }

    @Test
    void projectsExplicitHostFunctionsUnderEcritumGlobal() {
        SciEvalResult result = RubyEvaluator.evaluate(
            "ecritum.app.combine(41, 'done', {'ok' => true})",
            "ruby-host.rb",
            List.of(new HostProjection("app", "combine")),
            (namespace, function, arguments) -> {
                assertEquals("app", namespace);
                assertEquals("combine", function);
                assertEquals(List.of(41L, "done", Map.of("ok", true)), arguments);
                return List.of(arguments.get(0), arguments.get(1));
            }
        );

        assertEquals(EcritumStatus.OK, result.status(), result.message());
        assertEquals("ruby", result.language());
        assertEquals(List.of(41L, "done"), result.value());
    }

    @Test
    void normalizesHostByteResults() {
        SciEvalResult result = RubyEvaluator.evaluate(
            "ecritum.app.blob()",
            "ruby-host-bytes.rb",
            List.of(new HostProjection("app", "blob")),
            (namespace, function, arguments) -> new byte[] {0, 1, 2, -1}
        );

        assertEquals(EcritumStatus.OK, result.status(), result.message());
        assertArrayEquals(new byte[] {0, 1, 2, -1}, (byte[]) result.value());
    }

    @Test
    void mapsHostCallbackFailuresToCallbackStatus() {
        SciEvalResult result = RubyEvaluator.evaluate(
            "ecritum.app.fail()",
            "ruby-host-fail.rb",
            List.of(new HostProjection("app", "fail")),
            (namespace, function, arguments) -> {
                throw new HostFunctionException(EcritumStatus.CALLBACK, "callback", "token=SECRET");
            }
        );

        assertEquals(EcritumStatus.CALLBACK, result.status(), result.message());
        assertEquals("callback", result.category());
        assertEquals("ruby", result.language());
        assertEquals("ruby-host-fail.rb", result.sourceName());
        assertTrue(result.message().contains("token=<redacted>"));
        assertTrue(!result.message().contains("SECRET"));
    }

    @Test
    void installsPureStandardLibraryFacadesAndDefaultDenials() {
        assertEquals("{\"a\":1,\"b\":2}", ok("ecritum.json.writeString({'b' => 2, 'a' => 1})"));

        SciEvalResult jsonRead = RubyEvaluator.evaluate(
            "ecritum.json.readString('{\"items\":[true,false,\"x\"],\"n\":1}')",
            "ruby-facade-json.rb"
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
        assertPermissionDenied("ecritum.http.request({'url' => 'https://example.com'})");
    }

    @Test
    void deniesAmbientEscapeHatches() {
        // Java / Polyglot / inner-context
        assertPermissionDenied("Java.type('java.lang.System')");
        assertPermissionDenied("Polyglot.eval('js', '1+1')");
        assertPermissionDenied("Polyglot.import('x')");
        // Filesystem / directory
        assertPermissionDenied("File.read('/etc/hosts')");
        assertPermissionDenied("File.write('/tmp/ecritum_ruby_probe', 'x')");
        assertPermissionDenied("Dir.entries('/')");
        // Network
        assertPermissionDenied("require 'socket'\nTCPSocket.new('127.0.0.1', 9)");
        assertPermissionDenied("require 'net/http'");
        // Environment
        assertPermissionDenied("ENV['PATH']");
        assertPermissionDenied("ENV.to_h");
        // Process
        assertPermissionDenied("system('true')");
        assertPermissionDenied("`true`");
        assertPermissionDenied("Process.spawn('true')");
        assertPermissionDenied("IO.popen('true')");
        // Native / FFI
        assertPermissionDenied("require 'fiddle'");
        assertPermissionDenied("Fiddle");
        // Threads
        assertPermissionDenied("Thread.new { 1 }.value");
        // RubyGems / Bundler
        assertPermissionDenied("require 'rubygems'");
        assertPermissionDenied("require 'bundler'");

        // Lexical-bypass introspection: string-built constant / method names that
        // defeat DENIED_SOURCE_PATTERNS and reach the real runtime. These MUST
        // still be PERMISSION_DENIED via the runtime guard (classification fix).
        assertPermissionDenied("Object.const_get(\"Fil\" + \"e\").read('/etc/hosts')");
        assertPermissionDenied("send(\"sys\" + \"tem\", 'true')");
        assertPermissionDenied("send(:system, 'true')");
        assertPermissionDenied("Kernel.send(:system, 'true')");
        assertPermissionDenied("method(:system).call('true')");
    }

    @Test
    void deniesOpen3RequireAtRuntimeBypassingLexicalFilter() {
        // GAP-1: require 'open3' must be denied by the RUNTIME prelude even when
        // the lexical filter is bypassed by a string-built feature name. The
        // prelude undef's :require, so this raises NoMethodError on a sealed
        // name -> classified as permission.
        SciEvalResult bypass = RubyEvaluator.evaluate("require('open' + '3')", "ruby-gap1-bypass.rb");
        assertEquals(EcritumStatus.PERMISSION_DENIED, bypass.status(), bypass.message());
        assertEquals("permission", bypass.category(), bypass.message());
        assertEquals("ruby", bypass.language());

        // require_relative and load are denied at runtime too.
        SciEvalResult relative = RubyEvaluator.evaluate("require_relative('x')", "ruby-gap1-relative.rb");
        assertEquals(EcritumStatus.PERMISSION_DENIED, relative.status(), relative.message());
        assertEquals("permission", relative.category(), relative.message());
    }

    @Test
    void resealsRequireAgainstReflectionAndReopenBypasses() {
        // BLOCKER-1: the prelude undef's the loaders, so the original working
        // loader cannot be recovered through any reflection/dispatch path. Each
        // of these raises NoMethodError on a sealed name -> PERMISSION_DENIED.
        // (Before hardening, the loaders were merely REDEFINED to raise, so the
        // method object was still reachable via instance_method/send/bind_call.)
        assertPermissionDenied("Kernel.instance_method(:require).bind_call(self, 'open' + '3')");
        assertPermissionDenied("method(:require).unbind.bind_call(self, 'open' + '3')");
        assertPermissionDenied("method(:require).call('open' + '3')");
        assertPermissionDenied("Kernel.send(:require, 'open' + '3')");
        assertPermissionDenied("send(:require, 'open' + '3')");
        assertPermissionDenied("__send__(:require, 'open' + '3')");
        assertPermissionDenied("public_send(:require, 'open' + '3')");
        assertPermissionDenied("Kernel.singleton_class.instance_method(:require).bind_call(Kernel, 'open' + '3')");
        // A guest re-open that calls super cannot reach the removed original:
        // "super: no superclass method 'require'" -> sealed-name denial.
        assertPermissionDenied("module Kernel; def require(*a); super; end; end\nrequire('open' + '3')");

        // A guest may re-define its OWN inert require (it never reaches the real
        // loader). This is harmless: the value is whatever the guest returns and
        // no feature is loaded. Documented as the accepted residual behavior.
        SciEvalResult inert = RubyEvaluator.evaluate(
            "module Kernel; def require(*a); 99; end; end\nrequire('open3')",
            "ruby-reopen-inert.rb"
        );
        assertEquals(EcritumStatus.OK, inert.status(), inert.message());
        assertEquals(99L, inert.value());
    }

    @Test
    void deniesEvalFamilyForPythonParity() {
        // BLOCKER-2: deny guest dynamic evaluation, matching PythonEvaluator's
        // eval/exec/compile denial. The prelude undef's the whole eval family,
        // so each raises NoMethodError on a sealed name -> PERMISSION_DENIED.
        // This is distinct from the HOST context.eval used by guestBytes and
        // installEcritumGlobal (the polyglot API, not guest Kernel#eval).
        assertPermissionDenied("eval('1 + 1')");
        assertPermissionDenied("eval(\"require 'open3'\")");
        assertPermissionDenied("instance_eval('1 + 1')");
        assertPermissionDenied("String.class_eval('1 + 1')");
        assertPermissionDenied("Kernel.module_eval('1 + 1')");
        assertPermissionDenied("binding.eval('1 + 1')");
        assertPermissionDenied("instance_exec(1) { |x| x }");
        assertPermissionDenied("String.class_exec { 1 }");
        // Eval denial does NOT break legitimate metaprogramming the facades rely
        // on: define_method (used by installEcritumGlobal) still works.
        assertEquals(7L, ok("Object.send(:define_method, :__t) { 7 }; __t"));
    }

    @Test
    void deniesLoadPathMutationAtRuntimeBypassingLexicalFilter() {
        // GAP-2: $LOAD_PATH mutation must be denied by the RUNTIME prelude (frozen
        // array) even when the lexical filter is bypassed. We bypass it via the
        // $: / $" aliases for $LOAD_PATH / $LOADED_FEATURES, which are NOT matched
        // by DENIED_SOURCE_PATTERNS and so reach the runtime (eval is now denied,
        // so the prior eval-built bypass is no longer the vehicle).
        SciEvalResult mutate = RubyEvaluator.evaluate("$: << '/tmp'", "ruby-gap2-mutate.rb");
        assertEquals(EcritumStatus.SCRIPT, mutate.status(), mutate.message());
        assertEquals("runtime", mutate.category(), mutate.message());
        assertTrue(
            mutate.message().toLowerCase().contains("frozen") || mutate.message().toLowerCase().contains("can't modify"),
            mutate.message()
        );

        // Reading the load path (via alias) discloses nothing: frozen empty array.
        SciEvalResult read = RubyEvaluator.evaluate("$:", "ruby-gap2-read.rb");
        assertEquals(EcritumStatus.OK, read.status(), read.message());
        assertEquals(List.of(), read.value());

        SciEvalResult unshift = RubyEvaluator.evaluate("$:.unshift('/tmp')", "ruby-gap2-unshift.rb");
        assertEquals(EcritumStatus.SCRIPT, unshift.status(), unshift.message());
        assertEquals("runtime", unshift.category(), unshift.message());

        // $LOADED_FEATURES (via $") is frozen too.
        SciEvalResult features = RubyEvaluator.evaluate("$\" << 'x'", "ruby-gap2-features.rb");
        assertEquals(EcritumStatus.SCRIPT, features.status(), features.message());
        assertEquals("runtime", features.category(), features.message());
    }

    @Test
    void deniesGuestConcurrencyButAllowsCooperativeFibers() {
        // RISK-1: OS-thread / process / Ractor concurrency stays denied.
        assertPermissionDenied("Thread.start { 1 }.value");
        assertPermissionDenied("Thread.new { 1 }.value");
        // Process.fork / Thread.fork are not available in this build; probe that
        // they cannot spawn. Process.fork raises NotImplementedError; assert no
        // OK value escapes (it is never status OK with a forked child).
        SciEvalResult procFork = RubyEvaluator.evaluate(
            "Process.respond_to?(:fork) ? (Process.fork || 'parent') : 'no-fork'",
            "ruby-procfork.rb"
        );
        assertTrue(
            procFork.value() == null || "no-fork".equals(procFork.value()) || "parent".equals(procFork.value()),
            "Process.fork must not spawn a child: " + procFork.message()
        );
        // Ractor is unavailable (ruby.single-threaded); reference is denied.
        SciEvalResult ractor = RubyEvaluator.evaluate(
            "defined?(Ractor) ? Ractor.new { 1 }.take : 'no-ractor'",
            "ruby-ractor.rb"
        );
        assertTrue(
            ractor.status() != EcritumStatus.OK || "no-ractor".equals(ractor.value()),
            "Ractor must not provide working concurrency: " + ractor.message()
        );

        // Cooperative fibers ARE allowed (they back internal core ops) and are
        // single-threaded, so they cannot escape: a fiber that attempts a denied
        // op is still PERMISSION_DENIED.
        assertEquals(1L, ok("Fiber.new { 1 }.resume"));
        assertEquals(List.of(2L, 4L), ok("[1,2,3].lazy.map { |x| x * 2 }.first(2)"));
        assertEquals(1L, ok("e = Enumerator.new { |y| y << 1; y << 2 }; e.next"));
        assertPermissionDenied("Fiber.new { send(:system, 'true') }.resume");
        assertPermissionDenied("Fiber.new { send(:require, 'open' + '3') }.resume");
    }

    @Test
    void objectSpaceCannotReachWorkingLoaderOrHostInternals() {
        // RISK-3: ObjectSpace may still reify a Method/UnboundMethod named
        // :require (a stale object from boot), but it is INERT -- invoking it
        // cannot load anything because $LOADED_FEATURES is frozen, and the
        // underlying native ops are denied regardless. No capability escapes.
        SciEvalResult recovered = RubyEvaluator.evaluate(
            "m = nil; ObjectSpace.each_object(Method) { |x| m = x if x.name == :require }; "
                + "m.nil? ? 'no-method' : m.call('open' + '3')",
            "ruby-os-require.rb"
        );
        assertTrue(recovered.status() != EcritumStatus.OK, "ObjectSpace require must not succeed: " + recovered.message());

        SciEvalResult recoveredUnbound = RubyEvaluator.evaluate(
            "m = nil; ObjectSpace.each_object(UnboundMethod) { |x| m = x if x.name == :require }; "
                + "m.nil? ? 'no-method' : m.bind_call(Object.new, 'open' + '3')",
            "ruby-os-require-unbound.rb"
        );
        assertTrue(
            recoveredUnbound.status() != EcritumStatus.OK,
            "ObjectSpace unbound require must not succeed: " + recoveredUnbound.message()
        );

        // ObjectSpace can reach the File class object, but File.read is still
        // runtime-denied (native access) -> PERMISSION_DENIED.
        assertPermissionDenied(
            "k = nil; ObjectSpace.each_object(Class) { |c| k = c if c.name == 'File' }; "
                + "k.nil? ? raise('no-File') : k.read('/etc/hosts')"
        );
    }

    @Test
    void neutralizesRuntimeFingerprint() {
        // RISK-2: the TruffleRuby/GraalVM version banner is not disclosed.
        assertEquals("ruby", ok("RUBY_DESCRIPTION"));
        assertEquals("ecritum", ok("RUBY_PLATFORM"));
    }

    @Test
    void lexicallyDeniedAndRuntimeDeniedEscapesBothReportPermissionDenied() {
        // Lexically denied (trips DENIED_SOURCE_PATTERNS before eval):
        SciEvalResult lexical = RubyEvaluator.evaluate("system('true')", "ruby-lexical.rb");
        assertEquals(EcritumStatus.PERMISSION_DENIED, lexical.status(), lexical.message());
        assertEquals("permission", lexical.category());

        // Runtime denied (bypasses the regex, hits the native-access guard):
        SciEvalResult runtimeDenied = RubyEvaluator.evaluate("send(\"sys\" + \"tem\", 'true')", "ruby-runtime.rb");
        assertEquals(EcritumStatus.PERMISSION_DENIED, runtimeDenied.status(), runtimeDenied.message());
        assertEquals("permission", runtimeDenied.category());
    }

    @Test
    void timesOutLongRunningScripts() {
        SciEvalResult result = RubyEvaluator.evaluate(
            "loop { }",
            "ruby-timeout.rb",
            List.of(),
            (namespace, function, arguments) -> {
                throw new HostFunctionException(EcritumStatus.PERMISSION_DENIED, "permission", "host function is not projected");
            },
            new StandardLibraryPolicy("denied", List.of(), false, false, 1_000_000L),
            StandardLibraryBridge.denying()
        );

        assertEquals(EcritumStatus.TIMEOUT, result.status(), result.message());
        assertEquals("ruby", result.language());
        assertEquals("timeout", result.category());
    }

    private Object ok(String source) {
        SciEvalResult result = RubyEvaluator.evaluate(source, "ruby-value.rb");
        assertEquals(EcritumStatus.OK, result.status(), result.message());
        assertEquals("ruby", result.language());
        return result.value();
    }

    private void assertPermissionDenied(String source) {
        SciEvalResult result = RubyEvaluator.evaluate(source, "ruby-security.rb");
        assertEquals(EcritumStatus.PERMISSION_DENIED, result.status(), result.message());
        assertEquals("permission", result.category(), result.message());
        assertEquals("ruby", result.language());
        assertTrue(result.message().contains("ruby-security.rb"));
    }
}
