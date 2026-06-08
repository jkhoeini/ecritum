package ecritum;

import java.lang.reflect.Array;
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

final class PythonEvaluator {
    private static final String LANGUAGE = "python";
    private static final long STATEMENT_LIMIT = 100_000L;
    private static final String SANDBOX_PRELUDE = """
        import builtins

        def _ecritum_permission_denied(*args, **kwargs):
            raise PermissionError("operation not permitted")

        builtins.__import__ = _ecritum_permission_denied
        builtins.open = _ecritum_permission_denied
        builtins.eval = _ecritum_permission_denied
        builtins.exec = _ecritum_permission_denied
        builtins.compile = _ecritum_permission_denied
        builtins.input = _ecritum_permission_denied
        builtins.breakpoint = _ecritum_permission_denied
        """;
    private static final Set<String> STANDARD_LIBRARY_NAMES = Set.of("json", "time", "fs", "http");
    private static final List<Pattern> DENIED_SOURCE_PATTERNS = List.of(
        Pattern.compile("\\bimport\\s+java\\b"),
        Pattern.compile("\\bfrom\\s+java\\b"),
        Pattern.compile("__import__\\s*\\(\\s*['\"]java['\"]"),
        Pattern.compile("\\bimport\\s+polyglot\\b"),
        Pattern.compile("\\bfrom\\s+polyglot\\b"),
        Pattern.compile("\\bimport\\s+(?:ctypes|_ctypes|cffi|socket|subprocess|posix|signal|platform|ssl|urllib|pathlib|tempfile|getpass)\\b"),
        Pattern.compile("\\bfrom\\s+(?:ctypes|_ctypes|cffi|socket|subprocess|posix|signal|platform|ssl|urllib|pathlib|tempfile|getpass)\\b"),
        Pattern.compile("\\b(?:exec|eval|compile)\\s*\\("),
        Pattern.compile("\\bopen\\s*\\("),
        Pattern.compile("\\bos\\s*\\.\\s*(?:system|popen|environ)\\b"),
        Pattern.compile("\\bsys\\s*\\.\\s*path\\b"),
        Pattern.compile("\\b(?:importlib|zipimport|pickle|marshal)\\b")
    );

    private PythonEvaluator() {
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
                context.getBindings("python").putMember(
                    "ecritum",
                    ecritumGlobal(context, projections, hostInvoker, standardLibraryPolicy, standardLibraryBridge)
                );
                Value value = context.eval(Source.newBuilder("python", source, sourceFileName(safeSourceName)).buildLiteral());
                return SciEvalResult.ok(LANGUAGE, normalizeValue(value, newIdentitySet()));
            } finally {
                finished.set(true);
                if (timeoutWatchdog != null) {
                    timeoutWatchdog.interrupt();
                }
            }
        } catch (PythonAdapterException ex) {
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
            return SciEvalResult.internalError(LANGUAGE, safeSourceName, errorPrefix(safeSourceName) + "python backend failed");
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
        Context.Builder builder = Context.newBuilder("python")
            .allowAllAccess(false)
            .allowHostAccess(HostAccess.NONE)
            .allowHostClassLookup(name -> false)
            .allowHostClassLoading(false)
            .allowPolyglotAccess(PolyglotAccess.NONE)
            .allowIO(IOAccess.NONE)
            .allowNativeAccess(false)
            .allowCreateProcess(false)
            .allowCreateThread(false)
            .allowEnvironmentAccess(EnvironmentAccess.NONE)
            .allowInnerContextOptions(false)
            .allowValueSharing(false);
        if (executionTimeoutNanos != null) {
            builder.resourceLimits(ResourceLimits.newBuilder()
                .statementLimit(statementLimitFor(executionTimeoutNanos), source -> true)
                .build());
        }
        return builder.build();
    }

    private static void installSandboxPrelude(Context context) {
        context.eval(Source.newBuilder("python", SANDBOX_PRELUDE, "ecritum-python-sandbox.py").buildLiteral());
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
        }, "ecritum-python-timeout");
        watchdog.setDaemon(true);
        watchdog.start();
        return watchdog;
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
        if (isPythonNone(value)) {
            return null;
        }
        if (isPythonMainModule(value)) {
            return null;
        }
        if (value.canExecute()) {
            throw scriptException("unsupported Python result type");
        }
        if (value.isBoolean()) {
            return value.asBoolean();
        }
        if (value.isNumber()) {
            return normalizeNumber(value);
        }
        if (value.isString()) {
            return value.asString();
        }
        if (isPythonBytes(value)) {
            return pythonBytes(value);
        }
        if (value.hasArrayElements()) {
            if (!seen.add(value)) {
                throw scriptException("cyclic Python array");
            }
            try {
                long count = value.getArraySize();
                if (count > Integer.MAX_VALUE) {
                    throw scriptException("Python array is too large");
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
                throw scriptException("cyclic Python object");
            }
            try {
                long count = value.getHashSize();
                if (count > Integer.MAX_VALUE) {
                    throw scriptException("Python object is too large");
                }
                LinkedHashMap<String, Object> entries = new LinkedHashMap<>();
                Value keys = value.getHashKeysIterator();
                while (keys.hasIteratorNextElement()) {
                    Value key = keys.getIteratorNextElement();
                    Object normalizedKey = normalizeValue(key, seen);
                    if (!(normalizedKey instanceof String stringKey)) {
                        throw scriptException("Python object keys must be strings");
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
                throw scriptException("cyclic Python object");
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
        throw scriptException("unsupported Python result type");
    }

    private static Object normalizeNumber(Value value) {
        if (value.fitsInLong()) {
            return value.asLong();
        }
        if (value.fitsInDouble()) {
            double number = value.asDouble();
            if (!Double.isFinite(number)) {
                throw scriptException("unsupported Python number");
            }
            return number;
        }
        throw scriptException("unsupported Python number");
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
            return context.eval("python", "bytes").execute(ProxyArray.fromArray(boxBytes(data)));
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

    private static Value toGuestArray(Context context, List<?> list, Set<Object> seen) {
        if (!seen.add(list)) {
            throw scriptException("cyclic host value");
        }
        try {
            Value array = context.eval("python", "[]");
            for (Object item : list) {
                array.invokeMember("append", toGuestValue(context, item, seen));
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
            Value object = context.eval("python", "{}");
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
            Value array = context.eval("python", "[]");
            int length = Array.getLength(source);
            for (int index = 0; index < length; index++) {
                array.invokeMember("append", toGuestValue(context, Array.get(source, index), seen));
            }
            return array;
        } finally {
            seen.remove(source);
        }
    }

    private static Object[] boxBytes(byte[] data) {
        Object[] boxed = new Object[data.length];
        for (int index = 0; index < data.length; index++) {
            boxed[index] = Byte.toUnsignedInt(data[index]);
        }
        return boxed;
    }

    private static boolean isPythonBytes(Value value) {
        if (!value.hasArrayElements()) {
            return false;
        }
        String meta = metaName(value).toLowerCase(Locale.ROOT);
        return meta.equals("bytes") || meta.equals("bytearray");
    }

    private static boolean isPythonNone(Value value) {
        String meta = metaName(value).toLowerCase(Locale.ROOT);
        return meta.equals("nonetype") || "None".equals(value.toString());
    }

    private static boolean isPythonMainModule(Value value) {
        String meta = metaName(value).toLowerCase(Locale.ROOT);
        return meta.equals("module") && value.toString().contains("__main__");
    }

    private static byte[] pythonBytes(Value value) {
        long count = value.getArraySize();
        if (count > Integer.MAX_VALUE) {
            throw scriptException("Python bytes value is too large");
        }
        byte[] data = new byte[(int) count];
        for (int index = 0; index < data.length; index++) {
            Value element = value.getArrayElement(index);
            if (!element.fitsInInt()) {
                throw scriptException("Python bytes value contains a non-byte element");
            }
            int raw = element.asInt();
            if (raw < 0 || raw > 255) {
                throw scriptException("Python bytes value contains an out-of-range byte");
            }
            data[index] = (byte) raw;
        }
        return data;
    }

    private static PythonAdapterException scriptException(String message) {
        return new PythonAdapterException(EcritumStatus.SCRIPT, "runtime", message);
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
        String message = ex.getMessage();
        if (ex.isSyntaxError()) {
            return "syntax";
        }
        if (ex.isResourceExhausted() || ex.isInterrupted() || ex.isCancelled()) {
            return "timeout";
        }
        if (containsAny(
            message,
            "No module named 'java'",
            "No module named 'polyglot'",
            "No module named 'ctypes'",
            "No module named '_ctypes'",
            "No module named 'cffi'",
            "PermissionError",
            "operation not permitted",
            "Operation not permitted",
            "Operation is not allowed",
            "process creation",
            "native access",
            "host access",
            "socket",
            "subprocess"
        )) {
            return "permission";
        }
        return "runtime";
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
            return "ecritum.py";
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

    private static final class PythonAdapterException extends RuntimeException {
        private final int status;
        private final String category;

        PythonAdapterException(int status, String category, String message) {
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
                throw permissionException("invalid Python host projection");
            }
            String[] segments = namespace.split("\\.");
            if (segments.length == 0 || STANDARD_LIBRARY_NAMES.contains(segments[0])) {
                throw permissionException("Python host projection collides with Ecritum namespace");
            }
            ProjectionNode current = this;
            for (String segment : segments) {
                if (segment.isBlank()) {
                    throw permissionException("invalid Python host projection");
                }
                current = current.child(segment);
            }
            ProjectionNode leaf = current.child(functionName);
            if (!leaf.children.isEmpty() || leaf.fixedValue != null || leaf.function != null) {
                throw permissionException("duplicate Python host projection");
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
                    throw permissionException("Python host projection collides with existing function");
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
                throw permissionException("empty Python projection");
            }
            return toProxyObject();
        }
    }

    private static PythonAdapterException permissionException(String message) {
        return new PythonAdapterException(EcritumStatus.PERMISSION_DENIED, "permission", message);
    }
}
