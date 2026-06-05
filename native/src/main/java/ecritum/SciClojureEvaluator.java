package ecritum;

import clojure.java.api.Clojure;
import clojure.lang.IFn;
import clojure.lang.ISeq;
import clojure.lang.IPersistentMap;
import clojure.lang.Keyword;
import clojure.lang.PersistentHashSet;
import clojure.lang.RT;
import clojure.lang.RestFn;
import clojure.lang.Symbol;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

final class SciClojureEvaluator {
    private static final String LANGUAGE = "clojure";
    private static final IFn REQUIRE = Clojure.var("clojure.core", "require");
    private static final IFn EVAL_STRING;
    private static final IPersistentMap EVAL_OPTIONS;

    static {
        REQUIRE.invoke(Symbol.intern("sci.core"));
        EVAL_STRING = Clojure.var("sci.core", "eval-string");
        EVAL_OPTIONS = RT.map(
            Keyword.intern(null, "deny"),
            PersistentHashSet.create(List.of(
                Symbol.intern("Class/forName"),
                Symbol.intern("java.lang.System/getenv"),
                Symbol.intern("java.lang.System/getProperty"),
                Symbol.intern("load-file"),
                Symbol.intern("future"),
                Symbol.intern("pmap"),
                Symbol.intern("resolve"),
                Symbol.intern("ns-resolve"),
                Symbol.intern("requiring-resolve"),
                Symbol.intern("load-string"),
                Symbol.intern("require"),
                Symbol.intern("import"),
                Symbol.intern("clojure.java.shell/sh"),
                Symbol.intern("babashka.process/process"),
                Symbol.intern("add-classpath"),
                Symbol.intern("add-deps")
            ))
        );
    }

    private SciClojureEvaluator() {
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
        String safeSourceName = sourceName == null ? "" : sourceName;
        if (source == null) {
            return SciEvalResult.scriptError(LANGUAGE, safeSourceName, "runtime", errorPrefix(safeSourceName) + "missing source");
        }
        if (ConservativeSourceDenyPolicy.denies(source)) {
            return SciEvalResult.scriptError(LANGUAGE, safeSourceName, "permission", errorPrefix(safeSourceName) + "permission denied");
        }

        try {
            Object rawValue = EVAL_STRING.invoke(source, evalOptions(projections, hostInvoker));
            return SciEvalResult.ok(normalizeValue(rawValue));
        } catch (Throwable ex) {
            HostFunctionException hostError = findHostFunctionException(ex);
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
            String category = classify(source, ex);
            return SciEvalResult.scriptError(
                LANGUAGE,
                safeSourceName,
                category,
                errorPrefix(safeSourceName) + category + " error: " + sanitizeMessage(ex.getMessage())
            );
        }
    }

    private static IPersistentMap evalOptions(List<HostProjection> projections, HostFunctionInvoker hostInvoker) {
        if (projections == null || projections.isEmpty()) {
            return EVAL_OPTIONS;
        }
        Map<String, IPersistentMap> namespaceFunctions = new LinkedHashMap<>();
        for (HostProjection projection : projections) {
            if (projection == null || projection.namespace() == null || projection.function() == null) {
                continue;
            }
            IPersistentMap functions = namespaceFunctions.getOrDefault(projection.namespace(), RT.map());
            functions = functions.assoc(
                Symbol.intern(projection.function()),
                new ProjectedHostFunction(projection.namespace(), projection.function(), hostInvoker)
            );
            namespaceFunctions.put(projection.namespace(), functions);
        }
        IPersistentMap namespaces = RT.map();
        for (Map.Entry<String, IPersistentMap> entry : namespaceFunctions.entrySet()) {
            namespaces = namespaces.assoc(Symbol.intern(entry.getKey()), entry.getValue());
        }
        return EVAL_OPTIONS.assoc(Keyword.intern(null, "namespaces"), namespaces);
    }

    private static String errorPrefix(String sourceName) {
        if (sourceName == null || sourceName.isBlank()) {
            return LANGUAGE + ": ";
        }
        return LANGUAGE + " " + sourceName + ": ";
    }

    private static Object normalizeValue(Object value) {
        if (value == null
            || value instanceof Boolean
            || value instanceof String
            || value instanceof Character
            || value instanceof Byte
            || value instanceof Short
            || value instanceof Integer
            || value instanceof Long
            || value instanceof Float
            || value instanceof Double
            || value instanceof BigDecimal) {
            return value;
        }
        if (value instanceof byte[] data) {
            return data.clone();
        }
        if (value instanceof BigInteger bigInteger) {
            return bigInteger.longValueExact();
        }
        if (value instanceof clojure.lang.BigInt bigInt) {
            return bigInt.toBigInteger().longValueExact();
        }
        if (value instanceof clojure.lang.Keyword keyword) {
            return keyword.getName();
        }
        if (value instanceof Symbol symbol) {
            return symbol.getName();
        }
        if (value instanceof Map<?, ?> map) {
            LinkedHashMap<String, Object> normalized = new LinkedHashMap<>();
            for (Map.Entry<?, ?> entry : map.entrySet()) {
                String key = normalizeKey(entry.getKey());
                if (normalized.containsKey(key)) {
                    throw new IllegalArgumentException("duplicate normalized map key");
                }
                normalized.put(key, normalizeValue(entry.getValue()));
            }
            return normalized;
        }
        if (value instanceof Iterable<?> iterable) {
            ArrayList<Object> normalized = new ArrayList<>();
            for (Object item : iterable) {
                normalized.add(normalizeValue(item));
            }
            return normalized;
        }
        throw new IllegalArgumentException("unsupported result type");
    }

    private static String normalizeKey(Object key) {
        if (key instanceof String string) {
            return string;
        }
        if (key instanceof Keyword keyword) {
            return keyword.getName();
        }
        if (key instanceof Symbol symbol) {
            return symbol.getName();
        }
        throw new IllegalArgumentException("unsupported map key type");
    }

    private static String classify(String source, Throwable ex) {
        if (ConservativeSourceDenyPolicy.denies(source)) {
            return "permission";
        }
        String message = ex.getMessage();
        String className = ex.getClass().getName();
        if (containsAny(message, "EOF while reading", "Unmatched delimiter", "Invalid token")
            || containsAny(className, "ReaderException", "ParseException")) {
            return "syntax";
        }
        return "runtime";
    }

    private static HostFunctionException findHostFunctionException(Throwable ex) {
        Throwable current = ex;
        while (current != null) {
            if (current instanceof HostFunctionException hostFunctionException) {
                return hostFunctionException;
            }
            current = current.getCause();
        }
        return null;
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
            .replaceAll("clojure\\.[A-Za-z0-9_.$]+", "clojure")
            .replaceAll("sci\\.[A-Za-z0-9_.$]+", "sci");
    }

    private static final class ConservativeSourceDenyPolicy {
        private static final List<String> DENIED_FRAGMENTS = List.of(
            "Class/forName",
            "java.lang.",
            "javax.",
            "load-file",
            "(load-string",
            "(resolve",
            "(ns-resolve",
            "(requiring-resolve",
            "(require",
            "(import",
            "(new ",
            ".getClass",
            "future",
            "pmap",
            "java.io.",
            "java.net.",
            "java.nio.",
            "clojure.java.shell",
            "babashka.process",
            "add-classpath",
            "add-deps"
        );

        private ConservativeSourceDenyPolicy() {
        }

        static boolean denies(String source) {
            for (String denied : DENIED_FRAGMENTS) {
                if (source.contains(denied)) {
                    return true;
                }
            }
            return false;
        }
    }

    private static final class ProjectedHostFunction extends RestFn {
        private final String namespace;
        private final String function;
        private final HostFunctionInvoker invoker;

        ProjectedHostFunction(String namespace, String function, HostFunctionInvoker invoker) {
            this.namespace = namespace;
            this.function = function;
            this.invoker = invoker;
        }

        @Override
        public int getRequiredArity() {
            return 0;
        }

        @Override
        protected Object doInvoke(Object args) {
            ArrayList<Object> normalizedArgs = new ArrayList<>();
            for (ISeq current = (ISeq) args; current != null; current = current.next()) {
                normalizedArgs.add(normalizeValue(current.first()));
            }
            return normalizeValue(invoker.invoke(namespace, function, normalizedArgs));
        }
    }
}
