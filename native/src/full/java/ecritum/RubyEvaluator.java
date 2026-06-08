package ecritum;

import java.lang.reflect.Array;
import java.math.BigInteger;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.locks.LockSupport;
import java.util.regex.Pattern;
import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.EnvironmentAccess;
import org.graalvm.polyglot.HostAccess;
import org.graalvm.polyglot.PolyglotAccess;
import org.graalvm.polyglot.PolyglotException;
import org.graalvm.polyglot.ResourceLimits;
import org.graalvm.polyglot.Source;
import org.graalvm.polyglot.Value;
import org.graalvm.polyglot.io.IOAccess;
import org.graalvm.polyglot.proxy.ProxyArray;
import org.graalvm.polyglot.proxy.ProxyExecutable;
import org.graalvm.polyglot.proxy.ProxyObject;

/**
 * Production Ruby evaluator built on the proven {@code PythonEvaluator} template.
 *
 * <p>Security is enforced primarily by a deny-by-default GraalVM/TruffleRuby
 * {@code Context} (the same options proven runtime-grade by the M12-001C denial
 * matrix), reinforced by a Ruby sandbox prelude that closes the two recorded
 * runtime gaps (GAP-1 {@code require 'open3'}, GAP-2 {@code $LOAD_PATH}
 * read/mutate) and a lexical {@code DENIED_SOURCE_PATTERNS} layer kept only as
 * defense-in-depth. Runtime-denied escape attempts are classified as
 * {@code permission} so the C ABI reports {@code PERMISSION_DENIED} (14)
 * consistently with lexically-denied attempts.
 */
final class RubyEvaluator {
    private static final String LANGUAGE = "ruby";
    private static final long STATEMENT_LIMIT = 100_000L;
    private static final double MAX_SAFE_INTEGER = 9_007_199_254_740_991d;

    /**
     * Ruby sandbox prelude. Host-controlled Ruby installed BEFORE any guest
     * code. Runtime mechanism (not lexical) that closes GAP-1 and GAP-2 and
     * brings the metaprogramming surface to parity with {@link PythonEvaluator}
     * (which denies {@code eval}/{@code exec}/{@code compile}).
     *
     * <p>Design note (M12-002 security review): the earlier prelude
     * <em>redefined</em> {@code require}/{@code load}/&hellip; to raise. That is
     * insufficient because (a) the redefined methods are still reachable through
     * {@code instance_method}/{@code send}/{@code bind_call} (only their bodies
     * raise) and (b) a guest can re-open {@code module Kernel} and define its own
     * {@code require}, and {@code super} would have reached the prior definition.
     * This prelude instead <em>removes</em> the loaders entirely with
     * {@code undef_method}, so:
     *
     * <ul>
     *   <li>GAP-1 (reseal): {@code require}/{@code require_relative}/{@code load}
     *       and the {@code autoload} hooks are {@code undef}'d on {@code Kernel}
     *       (instance + singleton) and {@code Module#autoload}. After undef,
     *       {@code require 'open3'}, {@code Kernel.instance_method(:require)},
     *       {@code method(:require)}, {@code send(:require, &hellip;)},
     *       {@code Kernel.send(:require, &hellip;)}, and a guest re-open that
     *       calls {@code super} all raise {@code NoMethodError} ("undefined
     *       method"). A guest may still define its OWN inert {@code require}
     *       (returning whatever it likes); it can never reach the real loader,
     *       which no longer exists in the method lookup chain. Ecritum stdlib
     *       facades are host-projected under the {@code ecritum} global, so
     *       guest code never needs Ruby's {@code require}.</li>
     *   <li>BLOCKER-2 (eval denial, Python parity): {@code Kernel#eval},
     *       {@code BasicObject#instance_eval}/{@code instance_exec},
     *       {@code Module#class_eval}/{@code module_eval}/{@code class_exec}/
     *       {@code module_exec}, and {@code Binding#eval} are {@code undef}'d so
     *       guest code cannot evaluate dynamically-built source. This does not
     *       affect the value model, host callbacks, or the stdlib facades, and
     *       it is distinct from the HOST {@code context.eval(&hellip;)} used by
     *       {@code guestBytes}/{@code installEcritumGlobal} (the polyglot API,
     *       not the guest {@code Kernel#eval} method). {@code define_method} is
     *       intentionally NOT undef'd — {@code installEcritumGlobal} needs it.</li>
     *   <li>GAP-2: {@code $LOAD_PATH} and {@code $LOADED_FEATURES} are read-only
     *       special globals in TruffleRuby (they cannot be reassigned), but
     *       their array contents are mutable, which is the recorded gap. The
     *       prelude clears the existing arrays in place (so reads disclose no
     *       absolute GraalVM cache paths) and freezes them, so
     *       {@code $LOAD_PATH << '...'} / {@code unshift} raise
     *       {@code FrozenError} at runtime. The frozen {@code $LOADED_FEATURES}
     *       is also the backstop that makes any {@code Method}/{@code UnboundMethod}
     *       object for {@code require} that a guest reifies through
     *       {@code ObjectSpace.each_object} inert: invoking it raises
     *       "$LOADED_FEATURES is frozen; cannot append feature" (RISK-3).</li>
     *   <li>RISK-2 (fingerprint): {@code RUBY_DESCRIPTION} and
     *       {@code RUBY_PLATFORM} are reset to neutral values so guest code
     *       cannot read the TruffleRuby/GraalVM version banner.</li>
     * </ul>
     */
    private static final String SANDBOX_PRELUDE = """
        module Kernel
          class << self
            undef_method(:require) if private_method_defined?(:require) || method_defined?(:require)
            undef_method(:require_relative) if private_method_defined?(:require_relative) || method_defined?(:require_relative)
            undef_method(:load) if private_method_defined?(:load) || method_defined?(:load)
            undef_method(:autoload) if private_method_defined?(:autoload) || method_defined?(:autoload)
          end
          undef_method(:require) if private_method_defined?(:require) || method_defined?(:require)
          undef_method(:require_relative) if private_method_defined?(:require_relative) || method_defined?(:require_relative)
          undef_method(:load) if private_method_defined?(:load) || method_defined?(:load)
          undef_method(:autoload) if private_method_defined?(:autoload) || method_defined?(:autoload)
          undef_method(:eval) if private_method_defined?(:eval) || method_defined?(:eval)
        end
        class Module
          undef_method(:autoload) if private_method_defined?(:autoload) || method_defined?(:autoload)
          undef_method(:class_eval) if private_method_defined?(:class_eval) || method_defined?(:class_eval)
          undef_method(:module_eval) if private_method_defined?(:module_eval) || method_defined?(:module_eval)
          undef_method(:class_exec) if private_method_defined?(:class_exec) || method_defined?(:class_exec)
          undef_method(:module_exec) if private_method_defined?(:module_exec) || method_defined?(:module_exec)
        end
        class BasicObject
          undef_method(:instance_eval) if private_method_defined?(:instance_eval) || method_defined?(:instance_eval)
          undef_method(:instance_exec) if private_method_defined?(:instance_exec) || method_defined?(:instance_exec)
        end
        class Binding
          undef_method(:eval) if private_method_defined?(:eval) || method_defined?(:eval)
        end

        $LOAD_PATH.clear.freeze
        $LOADED_FEATURES.clear.freeze

        Object.send(:remove_const, :RUBY_DESCRIPTION) if Object.const_defined?(:RUBY_DESCRIPTION)
        Object.const_set(:RUBY_DESCRIPTION, 'ruby')
        Object.send(:remove_const, :RUBY_PLATFORM) if Object.const_defined?(:RUBY_PLATFORM)
        Object.const_set(:RUBY_PLATFORM, 'ecritum')
        nil
        """;

    private static final Set<String> STANDARD_LIBRARY_NAMES = Set.of("json", "time", "fs", "http");

    private static final List<Pattern> DENIED_SOURCE_PATTERNS = List.of(
        Pattern.compile("\\bJava\\s*\\.\\s*(?:type|import|add_to_classpath)\\b"),
        Pattern.compile("\\bPolyglot\\s*\\.\\s*(?:eval|eval_file|export|import|import_method)\\b"),
        Pattern.compile("\\bPolyglot::InnerContext\\b"),
        Pattern.compile("\\b(?:require|load|require_relative)\\s+['\"](?:fiddle|ffi|socket|net/http|net/ftp|net/imap|open3|openssl|rubygems|bundler)['\"]"),
        Pattern.compile("\\bFiddle\\b"),
        Pattern.compile("\\b(?:Kernel\\.)?(?:system|exec|spawn)\\s*\\("),
        Pattern.compile("\\bIO\\s*\\.\\s*popen\\b"),
        Pattern.compile("`[^`]*`"),
        Pattern.compile("\\bENV\\b"),
        Pattern.compile("\\$LOAD_PATH\\b"),
        Pattern.compile("\\$LOADED_FEATURES\\b"),
        Pattern.compile("\\b(?:File|Dir|IO|Pathname|Tempfile)\\b"),
        Pattern.compile("\\b(?:Thread|Ractor|Signal)\\b")
    );

    private RubyEvaluator() {
    }

    static SciEvalResult evaluate(String source, String sourceName) {
        return evaluate(source, sourceName, List.of(), (namespace, function, arguments) -> {
            throw new HostFunctionException(EcritumStatus.PERMISSION_DENIED, "permission", "host function is not projected");
        });
    }

    static SciEvalResult evaluate(
        String source,
        String sourceName,
        List<HostProjection> projections,
        HostFunctionInvoker hostInvoker
    ) {
        return evaluate(source, sourceName, projections, hostInvoker, StandardLibraryPolicy.denied(), StandardLibraryBridge.denying());
    }

    static SciEvalResult evaluate(
        String source,
        String sourceName,
        List<HostProjection> projections,
        HostFunctionInvoker hostInvoker,
        StandardLibraryPolicy standardLibraryPolicy,
        StandardLibraryBridge standardLibraryBridge
    ) {
        String safeSourceName = sourceName == null ? "" : sourceName;
        if (source == null) {
            return SciEvalResult.scriptError(LANGUAGE, safeSourceName, "runtime", errorPrefix(safeSourceName) + "missing source");
        }
        if (deniesSource(source)) {
            return new SciEvalResult(
                EcritumStatus.PERMISSION_DENIED,
                null,
                LANGUAGE,
                safeSourceName,
                "permission",
                errorPrefix(safeSourceName) + "permission denied"
            );
        }

        AtomicBoolean timedOut = new AtomicBoolean(false);
        AtomicBoolean finished = new AtomicBoolean(false);
        try (Context context = newContext(standardLibraryPolicy.executionTimeoutNanos())) {
            installSandboxPrelude(context);
            Thread timeoutWatchdog = startTimeoutWatchdog(context, standardLibraryPolicy.executionTimeoutNanos(), timedOut, finished);
            try {
                installEcritumGlobal(
                    context,
                    ecritumGlobal(context, projections, hostInvoker, standardLibraryPolicy, standardLibraryBridge)
                );
                Value value = context.eval(Source.newBuilder("ruby", source, sourceFileName(safeSourceName)).buildLiteral());
                return SciEvalResult.ok(LANGUAGE, normalizeValue(value, newIdentitySet()));
            } finally {
                finished.set(true);
                if (timeoutWatchdog != null) {
                    timeoutWatchdog.interrupt();
                }
            }
        } catch (RubyAdapterException ex) {
            return new SciEvalResult(
                ex.status(),
                null,
                LANGUAGE,
                safeSourceName,
                ex.category(),
                errorPrefix(safeSourceName) + ex.category() + " error: " + sanitizeMessage(ex.getMessage())
            );
        } catch (PolyglotException ex) {
            HostFunctionException hostError = hostFunctionException(ex);
            if (hostError != null) {
                return new SciEvalResult(
                    hostError.status(),
                    null,
                    LANGUAGE,
                    safeSourceName,
                    hostError.category(),
                    errorPrefix(safeSourceName) + hostError.category() + " error: " + sanitizeMessage(hostError.getMessage())
                );
            }
            StandardLibraryException facadeError = standardLibraryException(ex);
            if (facadeError != null) {
                return new SciEvalResult(
                    facadeError.status(),
                    null,
                    LANGUAGE,
                    safeSourceName,
                    facadeError.category(),
                    errorPrefix(safeSourceName) + facadeError.category() + " error: " + sanitizeMessage(facadeError.getMessage())
                );
            }
            String category = classify(ex);
            if (timedOut.get()) {
                category = "timeout";
            }
            int status = switch (category) {
                case "permission" -> EcritumStatus.PERMISSION_DENIED;
                case "timeout" -> EcritumStatus.TIMEOUT;
                default -> EcritumStatus.SCRIPT;
            };
            return new SciEvalResult(
                status,
                null,
                LANGUAGE,
                safeSourceName,
                category,
                errorPrefix(safeSourceName) + category + " error: " + sanitizeMessage(ex.getMessage())
            );
        } catch (Throwable ex) {
            return SciEvalResult.internalError(LANGUAGE, safeSourceName, errorPrefix(safeSourceName) + "ruby backend failed");
        }
    }

    private static boolean deniesSource(String source) {
        for (Pattern pattern : DENIED_SOURCE_PATTERNS) {
            if (pattern.matcher(source).find()) {
                return true;
            }
        }
        return false;
    }

    private static Context newContext(Long executionTimeoutNanos) {
        Context.Builder builder = Context.newBuilder("ruby")
            .allowAllAccess(false)
            .allowHostAccess(HostAccess.NONE)
            .allowHostClassLookup(name -> false)
            .allowHostClassLoading(false)
            .allowPolyglotAccess(PolyglotAccess.NONE)
            .allowIO(IOAccess.NONE)
            .allowNativeAccess(false)
            .allowCreateProcess(false)
            // SECURITY: allowCreateThread(true) is paired with
            // ruby.single-threaded=true below and does NOT expose guest
            // concurrency. TruffleRuby maps internal fibers onto host threads,
            // and core operations lazily initialize via a fiber on first use
            // (e.g. Array#pack), which fails with "fibers not allowed" under
            // allowCreateThread(false). Permitting the internal thread machinery
            // while setting ruby.single-threaded=true makes guest Thread.new
            // raise "threads not allowed in single-threaded mode" -- the exact
            // denial the M12-001C matrix records -- so the guest-visible thread
            // surface stays fully denied. This is the only deviation from the
            // probe context and is enforced/justified at the Ruby level.
            .allowCreateThread(true)
            .allowEnvironmentAccess(EnvironmentAccess.NONE)
            .allowInnerContextOptions(false)
            .allowValueSharing(false)
            .allowExperimentalOptions(true)
            .option("ruby.platform-native", "false")
            .option("ruby.cexts", "false")
            .option("ruby.rubygems", "false")
            // Enforces the guest thread denial (see allowCreateThread note) and
            // lets TruffleRuby run internal fibers cooperatively.
            .option("ruby.single-threaded", "true");
        if (executionTimeoutNanos != null) {
            builder.resourceLimits(ResourceLimits.newBuilder()
                .statementLimit(statementLimitFor(executionTimeoutNanos), source -> true)
                .build());
        }
        return builder.build();
    }

    private static void installSandboxPrelude(Context context) {
        context.eval(Source.newBuilder("ruby", SANDBOX_PRELUDE, "ecritum-ruby-sandbox.rb").buildLiteral());
        context.resetLimits();
    }

    private static long statementLimitFor(long executionTimeoutNanos) {
        long scaled = Math.max(1L, executionTimeoutNanos / 1_000L);
        return Math.max(STATEMENT_LIMIT, Math.min(10_000_000L, scaled));
    }

    private static Thread startTimeoutWatchdog(
        Context context,
        Long executionTimeoutNanos,
        AtomicBoolean timedOut,
        AtomicBoolean finished
    ) {
        if (executionTimeoutNanos == null || executionTimeoutNanos == 0L) {
            return null;
        }
        Thread watchdog = new Thread(() -> {
            LockSupport.parkNanos(executionTimeoutNanos);
            if (Thread.currentThread().isInterrupted() || finished.get()) {
                return;
            }
            timedOut.set(true);
            try {
                context.interrupt(Duration.ofMillis(100));
            } catch (TimeoutException ignored) {
                // The statement limit remains the deterministic terminal guard.
            }
        }, "ecritum-ruby-timeout");
        watchdog.setDaemon(true);
        watchdog.start();
        return watchdog;
    }

    /**
     * Expose the {@code ecritum} host facade to guest Ruby. TruffleRuby does not
     * surface {@code getBindings("ruby")} members as accessible Ruby names (and
     * {@code Polyglot.import} is denied by {@code PolyglotAccess.NONE}), so we
     * define a private top-level method {@code ecritum} that returns the foreign
     * proxy. The installer is a Ruby lambda that captures the proxy passed as a
     * polyglot argument, run after the sandbox prelude so {@code define_method}
     * is available. The method is private, matching the sandbox surface, so it
     * is only callable as a bareword {@code ecritum}, never as {@code x.ecritum}.
     */
    private static void installEcritumGlobal(Context context, ProxyObject global) {
        Value installer = context.eval("ruby",
            "->(facade) { Object.send(:define_method, :ecritum) { facade }; Object.send(:private, :ecritum); nil }");
        installer.execute(global);
    }

    private static ProxyObject ecritumGlobal(
        Context context,
        List<HostProjection> projections,
        HostFunctionInvoker hostInvoker,
        StandardLibraryPolicy standardLibraryPolicy,
        StandardLibraryBridge standardLibraryBridge
    ) {
        ProjectionNode root = ProjectionNode.root(context);
        installStandardLibrary(context, root, standardLibraryPolicy, standardLibraryBridge);
        for (HostProjection projection : projections) {
            root.installHostProjection(projection, hostInvoker);
        }
        return root.toProxyObject();
    }

    private static void installStandardLibrary(
        Context context,
        ProjectionNode root,
        StandardLibraryPolicy policy,
        StandardLibraryBridge bridge
    ) {
        root.installReservedObject("json", Map.of(
            "readString", executable(args -> {
                expectArity(args, 1, "ecritum.json.readString");
                Value json = args[0];
                if (!json.isString()) {
                    throw scriptException("ecritum.json.readString expects a string");
                }
                return toGuestValue(context, StandardLibraryValueCodec.readJson(json.asString()));
            }),
            "writeString", executable(args -> {
                expectArity(args, 1, "ecritum.json.writeString");
                return StandardLibraryValueCodec.writeJson(normalizeValue(args[0], newIdentitySet()));
            })
        ));
        root.installReservedObject("time", Map.of(
            "parseInstant", executable(args -> {
                expectArity(args, 1, "ecritum.time.parseInstant");
                return parseInstant(args[0], "ecritum.time.parseInstant");
            }),
            "formatInstant", executable(args -> {
                expectArity(args, 1, "ecritum.time.formatInstant");
                return parseInstant(args[0], "ecritum.time.formatInstant");
            }),
            "now", executable(args -> {
                expectArity(args, 0, "ecritum.time.now");
                if (!policy.clockReadable()) {
                    throw StandardLibraryException.permissionDenied("clock access is not permitted");
                }
                return toGuestValue(context, bridge.invoke("time.now", List.of()).valueOrThrow());
            })
        ));
        root.installReservedObject("fs", Map.of(
            "readText", filesystemExecutable(context, policy, bridge, "fs.read_text"),
            "readBytes", filesystemExecutable(context, policy, bridge, "fs.read_bytes"),
            "exists", filesystemExecutable(context, policy, bridge, "fs.exists")
        ));
        root.installReservedObject("http", Map.of(
            "request", executable(args -> {
                expectArity(args, 1, "ecritum.http.request");
                throw StandardLibraryException.permissionDenied("http access is not permitted");
            })
        ));
    }

    private static ProxyExecutable filesystemExecutable(
        Context context,
        StandardLibraryPolicy policy,
        StandardLibraryBridge bridge,
        String operation
    ) {
        return executable(args -> {
            expectArity(args, 1, operation);
            if (!policy.filesystemReadable()) {
                throw StandardLibraryException.permissionDenied("filesystem access is not permitted");
            }
            return toGuestValue(
                context,
                bridge.invoke(operation, List.of(normalizeValue(args[0], newIdentitySet()))).valueOrThrow()
            );
        });
    }

    private static String parseInstant(Value value, String operation) {
        if (!value.isString()) {
            throw scriptException(operation + " expects an ISO-8601 instant string");
        }
        return java.time.Instant.parse(value.asString()).toString();
    }

    private static Object normalizeValue(Value value, Set<Value> seen) {
        if (value == null || value.isNull()) {
            return null;
        }
        if (value.canExecute()) {
            throw scriptException("unsupported Ruby result type");
        }
        if (value.isBoolean()) {
            return value.asBoolean();
        }
        if (value.isNumber()) {
            return normalizeNumber(value);
        }
        if (isRubyBinaryString(value)) {
            return rubyBinaryStringBytes(value);
        }
        if (value.isString()) {
            return value.asString();
        }
        if (value.hasArrayElements()) {
            if (!seen.add(value)) {
                throw scriptException("cyclic Ruby array");
            }
            try {
                long count = value.getArraySize();
                if (count > Integer.MAX_VALUE) {
                    throw scriptException("Ruby array is too large");
                }
                ArrayList<Object> items = new ArrayList<>((int) count);
                for (long index = 0; index < count; index++) {
                    items.add(normalizeValue(value.getArrayElement(index), seen));
                }
                return items;
            } finally {
                seen.remove(value);
            }
        }
        if (value.hasHashEntries()) {
            if (!seen.add(value)) {
                throw scriptException("cyclic Ruby object");
            }
            try {
                long count = value.getHashSize();
                if (count > Integer.MAX_VALUE) {
                    throw scriptException("Ruby object is too large");
                }
                LinkedHashMap<String, Object> entries = new LinkedHashMap<>();
                Value keys = value.getHashKeysIterator();
                while (keys.hasIteratorNextElement()) {
                    Value key = keys.getIteratorNextElement();
                    Object normalizedKey = normalizeValue(key, seen);
                    if (!(normalizedKey instanceof String stringKey)) {
                        throw scriptException("Ruby object keys must be strings");
                    }
                    entries.put(stringKey, normalizeValue(value.getHashValue(key), seen));
                }
                return entries;
            } finally {
                seen.remove(value);
            }
        }
        if (value.hasMembers() && isProxyValue(value)) {
            if (!seen.add(value)) {
                throw scriptException("cyclic Ruby object");
            }
            try {
                LinkedHashMap<String, Object> object = new LinkedHashMap<>();
                for (String key : value.getMemberKeys()) {
                    object.put(key, normalizeValue(value.getMember(key), seen));
                }
                return object;
            } finally {
                seen.remove(value);
            }
        }
        throw scriptException("unsupported Ruby result type");
    }

    private static Object normalizeNumber(Value value) {
        if (value.fitsInLong()) {
            return value.asLong();
        }
        if (value.fitsInBigInteger()) {
            BigInteger bigInteger = value.asBigInteger();
            try {
                return bigInteger.longValueExact();
            } catch (ArithmeticException ex) {
                throw scriptException("unsupported Ruby number");
            }
        }
        if (value.fitsInDouble()) {
            double number = value.asDouble();
            if (!Double.isFinite(number)) {
                throw scriptException("unsupported Ruby number");
            }
            if (Math.rint(number) == number && Math.abs(number) <= MAX_SAFE_INTEGER) {
                return (long) number;
            }
            return number;
        }
        throw scriptException("unsupported Ruby number");
    }

    private static Set<Value> newIdentitySet() {
        return Collections.newSetFromMap(new IdentityHashMap<>());
    }

    private static Set<Object> newHostIdentitySet() {
        return Collections.newSetFromMap(new IdentityHashMap<>());
    }

    private static Object toGuestValue(Context context, Object value) {
        return toGuestValue(context, value, newHostIdentitySet());
    }

    private static Object toGuestValue(Context context, Object value, Set<Object> seen) {
        if (value == null
            || value instanceof Boolean
            || value instanceof Number
            || value instanceof CharSequence
            || value instanceof Character) {
            return value;
        }
        if (value instanceof byte[] data) {
            return guestBytes(context, data);
        }
        if (value instanceof List<?> list) {
            return toGuestArray(context, list, seen);
        }
        if (value instanceof Map<?, ?> map) {
            return toGuestObject(context, map, seen);
        }
        if (value.getClass().isArray()) {
            return toGuestJavaArray(context, value, seen);
        }
        throw scriptException("unsupported host value type");
    }

    private static Value guestBytes(Context context, byte[] data) {
        // Build a binary (ASCII-8BIT) Ruby String from an integer array so host
        // byte results round-trip back to byte strings on the way out.
        Value packer = context.eval("ruby", "->(arr) { arr.pack('C*') }");
        Value array = context.eval("ruby", "[]");
        for (byte b : data) {
            array.invokeMember("push", Byte.toUnsignedInt(b));
        }
        return packer.execute(array);
    }

    private static Value toGuestArray(Context context, List<?> list, Set<Object> seen) {
        if (!seen.add(list)) {
            throw scriptException("cyclic host value");
        }
        try {
            Value array = context.eval("ruby", "[]");
            for (Object item : list) {
                array.invokeMember("push", toGuestValue(context, item, seen));
            }
            return array;
        } finally {
            seen.remove(list);
        }
    }

    private static Value toGuestObject(Context context, Map<?, ?> map, Set<Object> seen) {
        if (!seen.add(map)) {
            throw scriptException("cyclic host value");
        }
        try {
            Value object = context.eval("ruby", "{}");
            for (Map.Entry<?, ?> entry : map.entrySet()) {
                object.putHashEntry(String.valueOf(entry.getKey()), toGuestValue(context, entry.getValue(), seen));
            }
            return object;
        } finally {
            seen.remove(map);
        }
    }

    private static Value toGuestJavaArray(Context context, Object source, Set<Object> seen) {
        if (!seen.add(source)) {
            throw scriptException("cyclic host value");
        }
        try {
            Value array = context.eval("ruby", "[]");
            int length = Array.getLength(source);
            for (int index = 0; index < length; index++) {
                array.invokeMember("push", toGuestValue(context, Array.get(source, index), seen));
            }
            return array;
        } finally {
            seen.remove(source);
        }
    }

    private static boolean isRubyBinaryString(Value value) {
        if (!value.isString()) {
            return false;
        }
        try {
            Value encoding = value.invokeMember("encoding");
            if (encoding == null || encoding.isNull()) {
                return false;
            }
            Value name = encoding.invokeMember("name");
            if (name == null || !name.isString()) {
                return false;
            }
            String encodingName = name.asString().toUpperCase(Locale.ROOT);
            return encodingName.equals("ASCII-8BIT") || encodingName.equals("BINARY");
        } catch (UnsupportedOperationException | IllegalStateException ex) {
            return false;
        }
    }

    private static byte[] rubyBinaryStringBytes(Value value) {
        Value bytes = value.invokeMember("bytes");
        if (!bytes.hasArrayElements()) {
            throw scriptException("Ruby binary string did not expose bytes");
        }
        long count = bytes.getArraySize();
        if (count > Integer.MAX_VALUE) {
            throw scriptException("Ruby binary string is too large");
        }
        byte[] data = new byte[(int) count];
        for (int index = 0; index < data.length; index++) {
            Value element = bytes.getArrayElement(index);
            if (!element.fitsInInt()) {
                throw scriptException("Ruby binary string contains a non-byte element");
            }
            int raw = element.asInt();
            if (raw < 0 || raw > 255) {
                throw scriptException("Ruby binary string contains an out-of-range byte");
            }
            data[index] = (byte) raw;
        }
        return data;
    }

    private static RubyAdapterException scriptException(String message) {
        return new RubyAdapterException(EcritumStatus.SCRIPT, "runtime", message);
    }

    private static ProxyExecutable executable(ThrowingExecutable executable) {
        return executable::execute;
    }

    private static void expectArity(Value[] args, int expected, String operation) {
        if (args.length != expected) {
            throw scriptException(operation + " expects " + expected + " argument(s)");
        }
    }

    private static String classify(PolyglotException ex) {
        if (ex.isSyntaxError()) {
            return "syntax";
        }
        if (ex.isResourceExhausted() || ex.isInterrupted() || ex.isCancelled()) {
            return "timeout";
        }
        if (isPermissionHostException(ex)) {
            return "permission";
        }
        String message = ex.getMessage();
        String normalized = message == null ? "" : message.toLowerCase(Locale.ROOT);
        // The prelude undef's the loader and eval families (see SANDBOX_PRELUDE).
        // Reaching one of those sealed names -- directly, via send/__send__,
        // method(:name), Kernel.instance_method(:name).bind_call, or a guest
        // re-open that calls super -- raises NoMethodError ("undefined method
        // 'require'" / "...'eval'" etc.). That is a security denial, not a guest
        // bug, so fold it into the permission category (PERMISSION_DENIED, 14)
        // for support-grade GAP-1/BLOCKER-2 consistency. Only the SEALED names
        // are folded; an ordinary undefined-method (a guest typo) stays
        // classified as runtime/SCRIPT.
        if (isSealedMethodDenial(normalized)) {
            return "permission";
        }
        // Fold the TruffleRuby runtime denial guards (native access, host/Java
        // access, polyglot access, threads, process creation) into the
        // permission category so the C ABI returns PERMISSION_DENIED (14) for
        // runtime-denied escapes, matching the lexically-denied ones. This is
        // the M12-002 classification fix called out in the denial matrix.
        if (containsAny(
            normalized,
            "native access is not allowed",
            "native access",
            "access to host class",
            "host access",
            "access denied",
            "operation not permitted",
            "polyglot bindings are not accessible",
            "process creation",
            "threads not allowed",
            "thread creation",
            "security"
        )) {
            return "permission";
        }
        return "runtime";
    }

    // Loader/eval/exec method names removed by the sandbox prelude via
    // undef_method. A NoMethodError naming one of these is a sandbox denial.
    private static final Set<String> SEALED_METHOD_NAMES = Set.of(
        "require",
        "require_relative",
        "load",
        "autoload",
        "eval",
        "instance_eval",
        "instance_exec",
        "class_eval",
        "module_eval",
        "class_exec",
        "module_exec"
    );

    private static boolean isSealedMethodDenial(String normalizedMessage) {
        // TruffleRuby phrasing: "undefined method 'require' for ...",
        // "undefined method 'eval' for an instance of Binding",
        // "super: no superclass method 'require'".
        boolean undefinedMethodShape =
            normalizedMessage.contains("undefined method")
                || normalizedMessage.contains("no superclass method");
        if (!undefinedMethodShape) {
            return false;
        }
        for (String name : SEALED_METHOD_NAMES) {
            if (normalizedMessage.contains("'" + name + "'")
                || normalizedMessage.contains("`" + name + "'")) {
                return true;
            }
        }
        return false;
    }

    private static boolean isPermissionHostException(PolyglotException ex) {
        if (!ex.isHostException()) {
            return false;
        }
        try {
            Throwable current = ex.asHostException();
            while (current != null) {
                if (current instanceof SecurityException) {
                    return true;
                }
                current = current.getCause();
            }
        } catch (UnsupportedOperationException ignored) {
            return false;
        }
        return false;
    }

    private static boolean containsAny(String value, String... needles) {
        if (value == null) {
            return false;
        }
        for (String needle : needles) {
            if (value.contains(needle)) {
                return true;
            }
        }
        return false;
    }

    private static String sanitizeMessage(String message) {
        if (message == null || message.isBlank()) {
            return "script failed";
        }
        return message
            .replaceAll("https?://[^\\s)\\]}]+", "<url>")
            .replaceAll("(?i)\\b(token|password|secret|api[_-]?key)=([^\\s&]+)", "$1=<redacted>")
            .replaceAll("java\\.[A-Za-z0-9_.$]+", "host-class")
            .replaceAll("javax\\.[A-Za-z0-9_.$]+", "host-class")
            .replaceAll("org\\.graalvm\\.[A-Za-z0-9_.$]+", "polyglot")
            .replaceAll("/[A-Za-z0-9_./-]+", "<path>");
    }

    private static String sourceFileName(String sourceName) {
        if (sourceName == null || sourceName.isBlank()) {
            return "ecritum.rb";
        }
        return sourceName;
    }

    private static String errorPrefix(String sourceName) {
        if (sourceName == null || sourceName.isBlank()) {
            return LANGUAGE + ": ";
        }
        return LANGUAGE + " " + sourceName + ": ";
    }

    private static boolean isProxyValue(Value value) {
        return metaName(value).startsWith("org.graalvm.polyglot.proxy.Proxy");
    }

    private static String metaName(Value value) {
        try {
            Value meta = value.getMetaObject();
            if (meta == null || meta.isNull()) {
                return "";
            }
            return meta.getMetaQualifiedName();
        } catch (UnsupportedOperationException ex) {
            return "";
        }
    }

    private static HostFunctionException hostFunctionException(Throwable ex) {
        Throwable current = unwrapHostException(ex);
        while (current != null) {
            if (current instanceof HostFunctionException hostFunctionException) {
                return hostFunctionException;
            }
            current = current.getCause();
        }
        return null;
    }

    private static StandardLibraryException standardLibraryException(Throwable ex) {
        Throwable current = unwrapHostException(ex);
        while (current != null) {
            if (current instanceof StandardLibraryException standardLibraryException) {
                return standardLibraryException;
            }
            current = current.getCause();
        }
        return null;
    }

    private static Throwable unwrapHostException(Throwable ex) {
        if (ex instanceof PolyglotException polyglotException && polyglotException.isHostException()) {
            try {
                Throwable hostException = polyglotException.asHostException();
                if (hostException != null) {
                    return hostException;
                }
            } catch (UnsupportedOperationException ignored) {
                return ex;
            }
        }
        return ex;
    }

    private interface ThrowingExecutable {
        Object execute(Value... args);
    }

    private static final class RubyAdapterException extends RuntimeException {
        private final int status;
        private final String category;

        RubyAdapterException(int status, String category, String message) {
            super(message);
            this.status = status;
            this.category = category;
        }

        int status() {
            return status;
        }

        String category() {
            return category;
        }
    }

    private static final class ProjectionNode {
        private final Context context;
        private final boolean root;
        private final LinkedHashMap<String, ProjectionNode> children = new LinkedHashMap<>();
        private ProxyExecutable function;
        private Object fixedValue;

        private ProjectionNode(Context context, boolean root) {
            this.context = context;
            this.root = root;
        }

        static ProjectionNode root(Context context) {
            return new ProjectionNode(context, true);
        }

        void installReservedObject(String name, Map<String, Object> object) {
            ProjectionNode child = new ProjectionNode(context, false);
            child.fixedValue = ProxyObject.fromMap(object);
            children.put(name, child);
        }

        void installHostProjection(HostProjection projection, HostFunctionInvoker invoker) {
            if (projection == null) {
                return;
            }
            String namespace = projection.namespace();
            String functionName = projection.function();
            if (namespace == null || namespace.isBlank() || functionName == null || functionName.isBlank()) {
                throw permissionException("invalid Ruby host projection");
            }
            String[] segments = namespace.split("\\.");
            if (segments.length == 0 || STANDARD_LIBRARY_NAMES.contains(segments[0])) {
                throw permissionException("Ruby host projection collides with Ecritum namespace");
            }
            ProjectionNode current = this;
            for (String segment : segments) {
                if (segment.isBlank()) {
                    throw permissionException("invalid Ruby host projection");
                }
                current = current.child(segment);
            }
            ProjectionNode leaf = current.child(functionName);
            if (!leaf.children.isEmpty() || leaf.fixedValue != null || leaf.function != null) {
                throw permissionException("duplicate Ruby host projection");
            }
            leaf.function = args -> {
                ArrayList<Object> normalizedArgs = new ArrayList<>();
                for (Value arg : args) {
                    normalizedArgs.add(normalizeValue(arg, newIdentitySet()));
                }
                return toGuestValue(context, invoker.invoke(namespace, functionName, normalizedArgs));
            };
        }

        private ProjectionNode child(String key) {
            ProjectionNode existing = children.get(key);
            if (existing != null) {
                if (existing.function != null || existing.fixedValue != null) {
                    throw permissionException("Ruby host projection collides with existing function");
                }
                return existing;
            }
            ProjectionNode created = new ProjectionNode(context, false);
            children.put(key, created);
            return created;
        }

        ProxyObject toProxyObject() {
            LinkedHashMap<String, Object> object = new LinkedHashMap<>();
            for (Map.Entry<String, ProjectionNode> entry : children.entrySet()) {
                object.put(entry.getKey(), entry.getValue().toGuestMember());
            }
            return ProxyObject.fromMap(object);
        }

        private Object toGuestMember() {
            if (fixedValue != null) {
                return fixedValue;
            }
            if (function != null) {
                return function;
            }
            if (!root && children.isEmpty()) {
                throw permissionException("empty Ruby projection");
            }
            return toProxyObject();
        }
    }

    private static RubyAdapterException permissionException(String message) {
        return new RubyAdapterException(EcritumStatus.PERMISSION_DENIED, "permission", message);
    }
}
