package ecritum;

import java.lang.reflect.Array;
import java.util.ArrayList;
import java.util.AbstractMap;
import java.util.Collections;
import java.util.Comparator;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Pattern;
import org.luaj.vm2.Globals;
import org.luaj.vm2.LuaError;
import org.luaj.vm2.LuaTable;
import org.luaj.vm2.LuaValue;
import org.luaj.vm2.Varargs;
import org.luaj.vm2.compiler.LuaC;
import org.luaj.vm2.lib.BaseLib;
import org.luaj.vm2.lib.DebugLib;
import org.luaj.vm2.lib.MathLib;
import org.luaj.vm2.lib.PackageLib;
import org.luaj.vm2.lib.StringLib;
import org.luaj.vm2.lib.TableLib;
import org.luaj.vm2.lib.VarArgFunction;
import org.luaj.vm2.lib.ZeroArgFunction;

final class LuaEvaluator {
    private static final String LANGUAGE = "lua";
    private static final int INSTRUCTION_BUDGET = 100_000;
    private static final double MAX_SAFE_INTEGER = 9_007_199_254_740_991d;
    private static final Set<String> STANDARD_LIBRARY_NAMES = Set.of("json", "time", "fs", "http");
    private static final List<Pattern> DENIED_SOURCE_PATTERNS = List.of(
        Pattern.compile("\\bluajava\\b"),
        Pattern.compile("\\b(?:Java|Packages|Polyglot|Class)\\b"),
        Pattern.compile("\\bio\\s*(?:[.\\[]|\\b)"),
        Pattern.compile("\\bos\\s*(?:[.\\[]|\\b)"),
        Pattern.compile("\\bdebug\\b"),
        Pattern.compile("\\bpackage\\b"),
        Pattern.compile("\\brequire\\b"),
        Pattern.compile("\\bdofile\\b"),
        Pattern.compile("\\bloadfile\\b"),
        Pattern.compile("\\bloadstring\\b"),
        Pattern.compile("\\bload\\s*\\("),
        Pattern.compile("\\bcollectgarbage\\b"),
        Pattern.compile("\\bcoroutine\\b"),
        Pattern.compile("\\bstring\\s*\\.\\s*dump\\b")
    );

    private LuaEvaluator() {
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

        try {
            Globals globals = newGlobals(projections, hostInvoker, standardLibraryPolicy, standardLibraryBridge);
            LuaValue value = globals.load(source, sourceFileName(safeSourceName), globals).call();
            return SciEvalResult.ok(normalizeValue(value, newTableSet()));
        } catch (LuaAdapterException ex) {
            return new SciEvalResult(
                ex.status(),
                null,
                LANGUAGE,
                safeSourceName,
                ex.category(),
                errorPrefix(safeSourceName) + ex.category() + " error: " + sanitizeMessage(ex.getMessage())
            );
        } catch (HostFunctionException ex) {
            return new SciEvalResult(
                ex.status(),
                null,
                LANGUAGE,
                safeSourceName,
                ex.category(),
                errorPrefix(safeSourceName) + ex.category() + " error: " + sanitizeMessage(ex.getMessage())
            );
        } catch (StandardLibraryException ex) {
            return new SciEvalResult(
                ex.status(),
                null,
                LANGUAGE,
                safeSourceName,
                ex.category(),
                errorPrefix(safeSourceName) + ex.category() + " error: " + sanitizeMessage(ex.getMessage())
            );
        } catch (LuaError ex) {
            String category = classify(ex);
            int status = category.equals("timeout") ? EcritumStatus.TIMEOUT : EcritumStatus.SCRIPT;
            return new SciEvalResult(
                status,
                null,
                LANGUAGE,
                safeSourceName,
                category,
                errorPrefix(safeSourceName) + category + " error: " + sanitizeMessage(ex.getMessage())
            );
        } catch (Throwable ex) {
            return SciEvalResult.internalError(LANGUAGE, safeSourceName, errorPrefix(safeSourceName) + "lua backend failed");
        }
    }

    private static Globals newGlobals(
        List<HostProjection> projections,
        HostFunctionInvoker hostInvoker,
        StandardLibraryPolicy standardLibraryPolicy,
        StandardLibraryBridge standardLibraryBridge
    ) {
        Globals globals = new Globals();
        globals.load(new BaseLib());
        globals.load(new PackageLib());
        globals.load(new TableLib());
        globals.load(new StringLib());
        globals.load(new MathLib());
        globals.load(new DebugLib());
        LuaC.install(globals);
        installInstructionBudget(globals);
        stripGuestSurface(globals);
        installEcritumGlobal(globals, projections, hostInvoker, standardLibraryPolicy, standardLibraryBridge);
        return globals;
    }

    private static void installInstructionBudget(Globals globals) {
        LuaValue setHook = globals.get("debug").get("sethook");
        LuaValue hook = new ZeroArgFunction() {
            @Override
            public LuaValue call() {
                throw new LuaError("instruction budget exceeded");
            }
        };
        setHook.call(hook, LuaValue.valueOf(""), LuaValue.valueOf(INSTRUCTION_BUDGET));
    }

    private static void stripGuestSurface(Globals globals) {
        for (String name : List.of(
            "debug",
            "package",
            "require",
            "dofile",
            "loadfile",
            "load",
            "loadstring",
            "collectgarbage",
            "print",
            "coroutine",
            "io",
            "os"
        )) {
            globals.set(name, LuaValue.NIL);
        }
        LuaValue string = globals.get("string");
        if (string.istable()) {
            string.set("dump", LuaValue.NIL);
        }
    }

    private static void installEcritumGlobal(
        Globals globals,
        List<HostProjection> projections,
        HostFunctionInvoker hostInvoker,
        StandardLibraryPolicy standardLibraryPolicy,
        StandardLibraryBridge standardLibraryBridge
    ) {
        ProjectionNode root = ProjectionNode.root();
        installStandardLibrary(root, standardLibraryPolicy, standardLibraryBridge);
        for (HostProjection projection : projections) {
            root.installHostProjection(projection, hostInvoker);
        }
        globals.set("ecritum", root.toLuaTable());
    }

    private static void installStandardLibrary(
        ProjectionNode root,
        StandardLibraryPolicy policy,
        StandardLibraryBridge bridge
    ) {
        root.installReservedObject("json", Map.of(
            "readString", function(args -> {
                expectArity(args, 1, "ecritum.json.readString");
                LuaValue json = args.arg1();
                if (json.type() != LuaValue.TSTRING) {
                    throw scriptException("ecritum.json.readString expects a string");
                }
                return toGuestValue(StandardLibraryValueCodec.readJson(json.tojstring()));
            }),
            "writeString", function(args -> {
                expectArity(args, 1, "ecritum.json.writeString");
                return LuaValue.valueOf(StandardLibraryValueCodec.writeJson(normalizeValue(args.arg1(), newTableSet())));
            })
        ));
        root.installReservedObject("time", Map.of(
            "parseInstant", function(args -> {
                expectArity(args, 1, "ecritum.time.parseInstant");
                return LuaValue.valueOf(parseInstant(args.arg1(), "ecritum.time.parseInstant"));
            }),
            "formatInstant", function(args -> {
                expectArity(args, 1, "ecritum.time.formatInstant");
                return LuaValue.valueOf(parseInstant(args.arg1(), "ecritum.time.formatInstant"));
            }),
            "now", function(args -> {
                expectArity(args, 0, "ecritum.time.now");
                if (!policy.clockReadable()) {
                    throw StandardLibraryException.permissionDenied("clock access is not permitted");
                }
                return toGuestValue(bridge.invoke("time.now", List.of()).valueOrThrow());
            })
        ));
        root.installReservedObject("fs", Map.of(
            "readText", filesystemFunction(policy, bridge, "fs.read_text"),
            "readBytes", filesystemFunction(policy, bridge, "fs.read_bytes"),
            "exists", filesystemFunction(policy, bridge, "fs.exists")
        ));
        root.installReservedObject("http", Map.of(
            "request", function(args -> {
                expectArity(args, 1, "ecritum.http.request");
                throw StandardLibraryException.permissionDenied("http access is not permitted");
            })
        ));
    }

    private static LuaValue filesystemFunction(StandardLibraryPolicy policy, StandardLibraryBridge bridge, String operation) {
        return function(args -> {
            expectArity(args, 1, operation);
            LuaValue path = args.arg1();
            if (path.type() != LuaValue.TSTRING) {
                throw scriptException(operation + " expects a path string");
            }
            if (!policy.filesystemReadable()) {
                throw StandardLibraryException.permissionDenied("filesystem access is not permitted");
            }
            return toGuestValue(bridge.invoke(operation, List.of(path.tojstring())).valueOrThrow());
        });
    }

    private static String parseInstant(LuaValue value, String operation) {
        if (value.type() != LuaValue.TSTRING) {
            throw scriptException(operation + " expects an ISO-8601 instant string");
        }
        return java.time.Instant.parse(value.tojstring()).toString();
    }

    private static Object normalizeValue(LuaValue value, Set<LuaTable> seen) {
        if (value == null || value.isnil()) {
            return null;
        }
        if (value.isboolean()) {
            return value.toboolean();
        }
        if (value.isnumber()) {
            return normalizeNumber(value);
        }
        if (value.type() == LuaValue.TSTRING) {
            return value.tojstring();
        }
        if (value.istable()) {
            return normalizeTable(value.checktable(), seen);
        }
        throw scriptException("unsupported Lua result type");
    }

    private static Object normalizeNumber(LuaValue value) {
        if (value.isinttype()) {
            return value.tolong();
        }
        double raw = value.todouble();
        if (!Double.isFinite(raw)) {
            throw scriptException("Lua numbers must be finite");
        }
        if (Math.rint(raw) == raw) {
            if (Math.abs(raw) <= MAX_SAFE_INTEGER && raw >= Long.MIN_VALUE && raw <= Long.MAX_VALUE) {
                return (long) raw;
            }
            throw scriptException("Lua integer exceeds safe range");
        }
        return raw;
    }

    private static Object normalizeTable(LuaTable table, Set<LuaTable> seen) {
        LuaValue metatable = table.getmetatable();
        if (metatable != null && !metatable.isnil()) {
            throw scriptException("Lua tables with metatables are not supported");
        }
        if (!seen.add(table)) {
            throw scriptException("cyclic Lua table");
        }
        try {
            LinkedHashMap<Integer, Object> arrayItems = new LinkedHashMap<>();
            ArrayList<Map.Entry<String, Object>> objectItems = new ArrayList<>();
            LuaValue key = LuaValue.NIL;
            while (true) {
                Varargs entry = table.next(key);
                key = entry.arg1();
                if (key.isnil()) {
                    break;
                }
                LuaValue item = entry.arg(2);
                if (isPositiveIntegerKey(key)) {
                    int index = (int) key.tolong();
                    if (arrayItems.put(index, normalizeValue(item, seen)) != null) {
                        throw scriptException("duplicate Lua array key");
                    }
                } else if (key.type() == LuaValue.TSTRING) {
                    objectItems.add(new AbstractMap.SimpleImmutableEntry<>(
                        key.tojstring(),
                        normalizeValue(item, seen)
                    ));
                } else {
                    throw scriptException("unsupported Lua table key");
                }
            }
            if (!arrayItems.isEmpty() && !objectItems.isEmpty()) {
                throw scriptException("mixed Lua tables are not supported");
            }
            if (!arrayItems.isEmpty()) {
                ArrayList<Object> values = new ArrayList<>(arrayItems.size());
                for (int index = 1; index <= arrayItems.size(); index++) {
                    if (!arrayItems.containsKey(index)) {
                        throw scriptException("sparse Lua arrays are not supported");
                    }
                    values.add(arrayItems.get(index));
                }
                return values;
            }
            objectItems.sort(Comparator.comparing(Map.Entry::getKey));
            LinkedHashMap<String, Object> object = new LinkedHashMap<>();
            for (Map.Entry<String, Object> entry : objectItems) {
                if (object.put(entry.getKey(), entry.getValue()) != null) {
                    throw scriptException("duplicate Lua object key");
                }
            }
            return object;
        } finally {
            seen.remove(table);
        }
    }

    private static boolean isPositiveIntegerKey(LuaValue key) {
        if (!(key.isinttype() || key.islong() || key.isnumber())) {
            return false;
        }
        double raw = key.todouble();
        return Double.isFinite(raw) && Math.rint(raw) == raw && raw >= 1 && raw <= Integer.MAX_VALUE;
    }

    private static LuaValue toGuestValue(Object value) {
        if (value == null) {
            return LuaValue.NIL;
        }
        if (value instanceof Boolean bool) {
            return LuaValue.valueOf(bool);
        }
        if (value instanceof Byte || value instanceof Short || value instanceof Integer || value instanceof Long) {
            long raw = ((Number) value).longValue();
            if (raw >= Integer.MIN_VALUE && raw <= Integer.MAX_VALUE) {
                return LuaValue.valueOf((int) raw);
            }
            if (Math.abs((double) raw) <= MAX_SAFE_INTEGER) {
                return LuaValue.valueOf((double) raw);
            }
            throw scriptException("host integer exceeds Lua safe range");
        }
        if (value instanceof Float || value instanceof Double) {
            double raw = ((Number) value).doubleValue();
            if (!Double.isFinite(raw)) {
                throw scriptException("host numbers must be finite");
            }
            return LuaValue.valueOf(raw);
        }
        if (value instanceof CharSequence || value instanceof Character) {
            return LuaValue.valueOf(value.toString());
        }
        if (value instanceof byte[]) {
            throw scriptException("Lua data values are not supported");
        }
        if (value instanceof List<?> list) {
            LuaTable table = new LuaTable();
            for (int index = 0; index < list.size(); index++) {
                table.set(index + 1, toGuestValue(list.get(index)));
            }
            return table;
        }
        if (value instanceof Map<?, ?> map) {
            LuaTable table = new LuaTable();
            for (Map.Entry<?, ?> entry : map.entrySet()) {
                table.set(String.valueOf(entry.getKey()), toGuestValue(entry.getValue()));
            }
            return table;
        }
        if (value.getClass().isArray()) {
            int length = Array.getLength(value);
            LuaTable table = new LuaTable();
            for (int index = 0; index < length; index++) {
                table.set(index + 1, toGuestValue(Array.get(value, index)));
            }
            return table;
        }
        throw scriptException("unsupported host value type");
    }

    private static LuaValue function(ThrowingLuaFunction function) {
        return new VarArgFunction() {
            @Override
            public Varargs invoke(Varargs args) {
                return function.invoke(args);
            }
        };
    }

    private static void expectArity(Varargs args, int expected, String operation) {
        if (args.narg() != expected) {
            throw scriptException(operation + " expects " + expected + " argument(s)");
        }
    }

    private static boolean deniesSource(String source) {
        if (source.indexOf('\u001B') >= 0) {
            return true;
        }
        for (Pattern pattern : DENIED_SOURCE_PATTERNS) {
            if (pattern.matcher(source).find()) {
                return true;
            }
        }
        return false;
    }

    private static String classify(LuaError ex) {
        String message = ex.getMessage();
        if (containsAny(message, "instruction budget exceeded")) {
            return "timeout";
        }
        if (containsAny(message, "syntax error", "unexpected symbol", "malformed number", "<eof>", "expected near", "' expected")) {
            return "syntax";
        }
        return "runtime";
    }

    private static LuaAdapterException scriptException(String message) {
        return new LuaAdapterException(EcritumStatus.SCRIPT, "runtime", message);
    }

    private static LuaAdapterException permissionException(String message) {
        return new LuaAdapterException(EcritumStatus.PERMISSION_DENIED, "permission", message);
    }

    private static String errorPrefix(String sourceName) {
        if (sourceName == null || sourceName.isBlank()) {
            return LANGUAGE + ": ";
        }
        return LANGUAGE + " " + sourceName + ": ";
    }

    private static String sourceFileName(String sourceName) {
        if (sourceName == null || sourceName.isBlank()) {
            return "ecritum.lua";
        }
        return sourceName;
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
            .replaceAll("org\\.luaj\\.[A-Za-z0-9_.$]+", "luaj")
            .replaceAll("java\\.[A-Za-z0-9_.$]+", "host-class")
            .replaceAll("/[A-Za-z0-9_./-]+", "<path>");
    }

    private static Set<LuaTable> newTableSet() {
        return Collections.newSetFromMap(new IdentityHashMap<>());
    }

    private interface ThrowingLuaFunction {
        LuaValue invoke(Varargs args);
    }

    private static final class LuaAdapterException extends RuntimeException {
        private final int status;
        private final String category;

        LuaAdapterException(int status, String category, String message) {
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
        private final boolean root;
        private final LinkedHashMap<String, ProjectionNode> children = new LinkedHashMap<>();
        private LuaValue function;
        private LuaValue fixedValue;

        private ProjectionNode(boolean root) {
            this.root = root;
        }

        static ProjectionNode root() {
            return new ProjectionNode(true);
        }

        void installReservedObject(String name, Map<String, LuaValue> object) {
            ProjectionNode child = new ProjectionNode(false);
            LuaTable table = new LuaTable();
            for (Map.Entry<String, LuaValue> entry : object.entrySet()) {
                table.set(entry.getKey(), entry.getValue());
            }
            child.fixedValue = table;
            children.put(name, child);
        }

        void installHostProjection(HostProjection projection, HostFunctionInvoker invoker) {
            if (projection == null) {
                return;
            }
            String namespace = projection.namespace();
            String functionName = projection.function();
            if (namespace == null || namespace.isBlank() || functionName == null || functionName.isBlank()) {
                throw permissionException("invalid Lua host projection");
            }
            String[] segments = namespace.split("\\.");
            if (segments.length == 0 || STANDARD_LIBRARY_NAMES.contains(segments[0])) {
                throw permissionException("Lua host projection collides with Ecritum namespace");
            }
            ProjectionNode current = this;
            for (String segment : segments) {
                if (segment.isBlank()) {
                    throw permissionException("invalid Lua host projection");
                }
                current = current.child(segment);
            }
            ProjectionNode leaf = current.child(functionName);
            if (!leaf.children.isEmpty() || leaf.fixedValue != null || leaf.function != null) {
                throw permissionException("duplicate Lua host projection");
            }
            leaf.function = function(args -> {
                ArrayList<Object> normalizedArgs = new ArrayList<>();
                for (int index = 1; index <= args.narg(); index++) {
                    normalizedArgs.add(normalizeValue(args.arg(index), newTableSet()));
                }
                return toGuestValue(invoker.invoke(namespace, functionName, normalizedArgs));
            });
        }

        private ProjectionNode child(String key) {
            ProjectionNode existing = children.get(key);
            if (existing != null) {
                if (existing.function != null || existing.fixedValue != null) {
                    throw permissionException("Lua host projection collides with existing function");
                }
                return existing;
            }
            ProjectionNode created = new ProjectionNode(false);
            children.put(key, created);
            return created;
        }

        LuaTable toLuaTable() {
            LuaTable table = new LuaTable();
            for (Map.Entry<String, ProjectionNode> entry : children.entrySet()) {
                table.set(entry.getKey(), entry.getValue().toGuestMember());
            }
            return table;
        }

        private LuaValue toGuestMember() {
            if (fixedValue != null) {
                return fixedValue;
            }
            if (function != null) {
                return function;
            }
            if (!root && children.isEmpty()) {
                throw permissionException("empty Lua projection");
            }
            return toLuaTable();
        }
    }
}
