package ecritum;

import java.lang.reflect.Array;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.EnvironmentAccess;
import org.graalvm.polyglot.HostAccess;
import org.graalvm.polyglot.PolyglotAccess;
import org.graalvm.polyglot.PolyglotException;
import org.graalvm.polyglot.Source;
import org.graalvm.polyglot.Value;
import org.graalvm.polyglot.io.IOAccess;
import org.graalvm.polyglot.proxy.ProxyArray;
import org.graalvm.polyglot.proxy.ProxyExecutable;
import org.graalvm.polyglot.proxy.ProxyObject;

final class JavaScriptEvaluator {
    private static final String LANGUAGE = "javascript";
    private static final Set<String> STANDARD_LIBRARY_NAMES = Set.of("json", "time", "fs", "http");

    private JavaScriptEvaluator() {
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

        try (Context context = newContext()) {
            context.getBindings("js").putMember(
                "ecritum",
                ecritumGlobal(context, projections, hostInvoker, standardLibraryPolicy, standardLibraryBridge)
            );
            Value value = context.eval(Source.newBuilder("js", source, sourceFileName(safeSourceName)).buildLiteral());
            return SciEvalResult.ok(normalizeValue(context, value, newIdentitySet()));
        } catch (JavaScriptAdapterException ex) {
            return new SciEvalResult(
                ex.status(),
                null,
                LANGUAGE,
                safeSourceName,
                ex.category(),
                errorPrefix(safeSourceName) + ex.category() + " error: " + sanitizeMessage(ex.getMessage())
            );
        } catch (PolyglotException ex) {
            JavaScriptAdapterException adapterError = javaScriptAdapterException(ex);
            if (adapterError != null) {
                return new SciEvalResult(
                    adapterError.status(),
                    null,
                    LANGUAGE,
                    safeSourceName,
                    adapterError.category(),
                    errorPrefix(safeSourceName) + adapterError.category() + " error: " + sanitizeMessage(adapterError.getMessage())
                );
            }
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
            int status = category.equals("permission") ? EcritumStatus.PERMISSION_DENIED : EcritumStatus.SCRIPT;
            return new SciEvalResult(
                status,
                null,
                LANGUAGE,
                safeSourceName,
                category,
                errorPrefix(safeSourceName) + category + " error: " + sanitizeMessage(ex.getMessage())
            );
        } catch (Throwable ex) {
            return SciEvalResult.internalError(LANGUAGE, safeSourceName, errorPrefix(safeSourceName) + "javascript backend failed");
        }
    }

    private static Context newContext() {
        return Context.newBuilder("js")
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
            .allowValueSharing(false)
            .build();
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
                return StandardLibraryValueCodec.writeJson(normalizeValue(context, args[0], newIdentitySet()));
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
                bridge.invoke(operation, List.of(normalizeValue(context, args[0], newIdentitySet()))).valueOrThrow()
            );
        });
    }

    private static String parseInstant(Value value, String operation) {
        if (!value.isString()) {
            throw scriptException(operation + " expects an ISO-8601 instant string");
        }
        return StandardLibraryFacadeInstant.format(value.asString(), operation);
    }

    private static Object normalizeValue(Context context, Value value, Set<Value> seen) {
        if (value == null || value.isNull()) {
            return null;
        }
        if (value.canExecute()) {
            throw scriptException("unsupported JavaScript result type");
        }
        if (isPromise(value)) {
            throw scriptException("JavaScript promises are not supported");
        }
        if (isArrayBuffer(value)) {
            return arrayBufferBytes(context, value);
        }
        if (isTypedByteArray(value)) {
            return typedArrayBytes(value);
        }
        if (value.isBoolean()) {
            return value.asBoolean();
        }
        if (metaName(value).equals("bigint")) {
            throw scriptException("unsupported JavaScript number");
        }
        if (value.isNumber()) {
            return normalizeNumber(value);
        }
        if (value.isString()) {
            return value.asString();
        }
        if (value.hasArrayElements()) {
            rejectCycles(context, value);
            if (!seen.add(value)) {
                throw scriptException("cyclic JavaScript array");
            }
            try {
                long count = value.getArraySize();
                if (count > Integer.MAX_VALUE) {
                    throw scriptException("JavaScript array is too large");
                }
                ArrayList<Object> items = new ArrayList<>((int) count);
                for (long index = 0; index < count; index++) {
                    items.add(normalizeValue(context, value.getArrayElement(index), seen));
                }
                return items;
            } finally {
                seen.remove(value);
            }
        }
        if (value.hasMembers()) {
            if (!isPlainObject(context, value)) {
                throw scriptException("unsupported JavaScript result type");
            }
            rejectCycles(context, value);
            if (!seen.add(value)) {
                throw scriptException("cyclic JavaScript object");
            }
            try {
                LinkedHashMap<String, Object> object = new LinkedHashMap<>();
                for (String key : value.getMemberKeys()) {
                    object.put(key, normalizeValue(context, value.getMember(key), seen));
                }
                return object;
            } finally {
                seen.remove(value);
            }
        }
        throw scriptException("unsupported JavaScript result type");
    }

    private static Object normalizeNumber(Value value) {
        if (value.fitsInLong()) {
            return value.asLong();
        }
        if (!value.fitsInDouble()) {
            throw scriptException("unsupported JavaScript number");
        }
        double result = value.asDouble();
        if (!Double.isFinite(result)) {
            throw scriptException("JavaScript numbers must be finite");
        }
        return result;
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
            return context.eval("js", "(bytes) => new Uint8Array(bytes)").execute(ProxyArray.fromArray(boxBytes(data)));
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
            Value array = context.eval("js", "[]");
            for (int index = 0; index < list.size(); index++) {
                array.setArrayElement(index, toGuestValue(context, list.get(index), seen));
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
            Value object = context.eval("js", "Object.create(null)");
            for (Map.Entry<?, ?> entry : map.entrySet()) {
                object.putMember(String.valueOf(entry.getKey()), toGuestValue(context, entry.getValue(), seen));
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
            Value array = context.eval("js", "[]");
            int length = Array.getLength(source);
            for (int index = 0; index < length; index++) {
                array.setArrayElement(index, toGuestValue(context, Array.get(source, index), seen));
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

    private static boolean isPromise(Value value) {
        return value.hasMember("then") && value.getMember("then").canExecute();
    }

    private static boolean isArrayBuffer(Value value) {
        return metaName(value).equals("ArrayBuffer");
    }

    private static byte[] arrayBufferBytes(Context context, Value value) {
        Value bytes = context.eval("js", "(buffer) => Array.from(new Uint8Array(buffer))").execute(value);
        return arrayElementsToBytes(bytes);
    }

    private static boolean isTypedByteArray(Value value) {
        String name = metaName(value);
        return name.equals("Uint8Array");
    }

    private static byte[] typedArrayBytes(Value value) {
        return arrayElementsToBytes(value);
    }

    private static byte[] arrayElementsToBytes(Value value) {
        if (!value.hasArrayElements()) {
            throw scriptException("expected byte array elements");
        }
        long count = value.getArraySize();
        if (count > Integer.MAX_VALUE) {
            throw scriptException("data value is too large");
        }
        ByteBuffer buffer = ByteBuffer.allocate((int) count);
        for (long index = 0; index < count; index++) {
            Value item = value.getArrayElement(index);
            if (!item.fitsInInt()) {
                throw scriptException("invalid byte value");
            }
            int byteValue = item.asInt();
            if (byteValue < 0 || byteValue > 255) {
                throw scriptException("invalid byte value");
            }
            buffer.put((byte) byteValue);
        }
        return buffer.array();
    }

    private static boolean isPlainObject(Context context, Value value) {
        String name = metaName(value);
        if (name.equals("Object") || name.startsWith("org.graalvm.polyglot.proxy.ProxyObject")) {
            return true;
        }
        try {
            return context.eval("js", """
                (value) => {
                  const prototype = Object.getPrototypeOf(value);
                  return prototype === Object.prototype || prototype === null;
                }
                """).execute(value).asBoolean();
        } catch (PolyglotException | UnsupportedOperationException | IllegalStateException ex) {
            return false;
        }
    }

    private static void rejectCycles(Context context, Value value) {
        if (!value.hasArrayElements() && !value.hasMembers()) {
            return;
        }
        if (isProxyValue(value)) {
            return;
        }
        try {
            Value checker = context.eval("js", """
                (value) => {
                  const active = new WeakSet();
                  const walk = (current) => {
                    if (current === null) {
                      return false;
                    }
                    const kind = typeof current;
                    if (kind !== "object" && kind !== "function") {
                      return false;
                    }
                    if (active.has(current)) {
                      return true;
                    }
                    active.add(current);
                    try {
                      if (ArrayBuffer.isView(current) || current instanceof ArrayBuffer) {
                        return false;
                      }
                      if (Array.isArray(current)) {
                        for (let index = 0; index < current.length; index += 1) {
                          if (walk(current[index])) {
                            return true;
                          }
                        }
                        return false;
                      }
                      const prototype = Object.getPrototypeOf(current);
                      if (prototype === Object.prototype || prototype === null) {
                        for (const key of Object.keys(current)) {
                          if (walk(current[key])) {
                            return true;
                          }
                        }
                      }
                      return false;
                    } finally {
                      active.delete(current);
                    }
                  };
                  return walk(value);
                }
                """);
            if (checker.execute(value).asBoolean()) {
                throw scriptException("cyclic JavaScript object");
            }
        } catch (JavaScriptAdapterException ex) {
            throw ex;
        } catch (PolyglotException | UnsupportedOperationException | IllegalStateException ex) {
            throw scriptException("unsupported JavaScript result type");
        }
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

    private static ProxyExecutable executable(ThrowingExecutable executable) {
        return executable::execute;
    }

    private static void expectArity(Value[] args, int expected, String operation) {
        if (args.length != expected) {
            throw scriptException(operation + " expects " + expected + " argument(s)");
        }
    }

    private static JavaScriptAdapterException scriptException(String message) {
        return new JavaScriptAdapterException(EcritumStatus.SCRIPT, "runtime", message);
    }

    private static JavaScriptAdapterException permissionException(String message) {
        return new JavaScriptAdapterException(EcritumStatus.PERMISSION_DENIED, "permission", message);
    }

    private static String errorPrefix(String sourceName) {
        if (sourceName == null || sourceName.isBlank()) {
            return LANGUAGE + ": ";
        }
        return LANGUAGE + " " + sourceName + ": ";
    }

    private static String sourceFileName(String sourceName) {
        if (sourceName == null || sourceName.isBlank()) {
            return "ecritum.js";
        }
        return sourceName;
    }

    private static String classify(PolyglotException ex) {
        String message = ex.getMessage();
        if (ex.isSyntaxError()) {
            return "syntax";
        }
        if (containsAny(
            message,
            "ReferenceError: Java",
            "ReferenceError: Polyglot",
            "ReferenceError: Packages",
            "ReferenceError: load",
            "ReferenceError: read",
            "ReferenceError: readbuffer",
            "ReferenceError: require",
            "ReferenceError: process",
            "ReferenceError: fetch",
            "TypeError: Cannot load",
            "Access to host class",
            "Host class loading is not allowed",
            "Operation is not allowed for",
            "Cannot load module"
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

    private static JavaScriptAdapterException javaScriptAdapterException(Throwable ex) {
        Throwable current = unwrapHostException(ex);
        while (current != null) {
            if (current instanceof JavaScriptAdapterException javaScriptAdapterException) {
                return javaScriptAdapterException;
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

    private static Set<Value> newIdentitySet() {
        return Collections.newSetFromMap(new IdentityHashMap<>());
    }

    private static Set<Object> newHostIdentitySet() {
        return Collections.newSetFromMap(new IdentityHashMap<>());
    }

    private interface ThrowingExecutable {
        Object execute(Value... args);
    }

    private static final class JavaScriptAdapterException extends RuntimeException {
        private final int status;
        private final String category;

        JavaScriptAdapterException(int status, String category, String message) {
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
                throw permissionException("invalid JavaScript host projection");
            }
            String[] segments = namespace.split("\\.");
            if (segments.length == 0 || STANDARD_LIBRARY_NAMES.contains(segments[0])) {
                throw permissionException("JavaScript host projection collides with Ecritum namespace");
            }
            ProjectionNode current = this;
            for (String segment : segments) {
                if (segment.isBlank()) {
                    throw permissionException("invalid JavaScript host projection");
                }
                current = current.child(segment);
            }
            ProjectionNode leaf = current.child(functionName);
            if (!leaf.children.isEmpty() || leaf.fixedValue != null || leaf.function != null) {
                throw permissionException("duplicate JavaScript host projection");
            }
            leaf.function = args -> {
                ArrayList<Object> normalizedArgs = new ArrayList<>();
                for (Value arg : args) {
                    normalizedArgs.add(normalizeValue(context, arg, newIdentitySet()));
                }
                return toGuestValue(context, invoker.invoke(namespace, functionName, normalizedArgs));
            };
        }

        private ProjectionNode child(String key) {
            ProjectionNode existing = children.get(key);
            if (existing != null) {
                if (existing.function != null || existing.fixedValue != null) {
                    throw permissionException("JavaScript host projection collides with existing function");
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
                throw permissionException("empty JavaScript projection");
            }
            return toProxyObject();
        }
    }

    private static final class StandardLibraryFacadeInstant {
        private StandardLibraryFacadeInstant() {
        }

        static String format(String value, String operation) {
            return java.time.Instant.parse(value).toString();
        }
    }
}
