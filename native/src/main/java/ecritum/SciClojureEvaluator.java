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
        return evaluate(
            source,
            sourceName,
            projections,
            hostInvoker,
            StandardLibraryPolicy.denied(),
            StandardLibraryBridge.denying()
        );
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
        RequireRewrite requireRewrite = SupportedRequireForms.rewrite(source);
        if (!requireRewrite.allowed() || ConservativeSourceDenyPolicy.denies(requireRewrite.source())) {
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
            Object rawValue = EVAL_STRING.invoke(
                requireRewrite.source(),
                evalOptions(
                    projections,
                    hostInvoker,
                    standardLibraryPolicy,
                    standardLibraryBridge,
                    requireRewrite.aliases()
                )
            );
            return SciEvalResult.ok(normalizeValue(rawValue));
        } catch (Throwable ex) {
            StandardLibraryException facadeError = findStandardLibraryException(ex);
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
        return evalOptions(
            projections,
            hostInvoker,
            StandardLibraryPolicy.denied(),
            StandardLibraryBridge.denying(),
            Map.of()
        );
    }

    private static IPersistentMap evalOptions(
        List<HostProjection> projections,
        HostFunctionInvoker hostInvoker,
        StandardLibraryPolicy standardLibraryPolicy,
        StandardLibraryBridge standardLibraryBridge,
        Map<String, String> aliases
    ) {
        return SciNamespaceInstaller.install(
            EVAL_OPTIONS,
            projections,
            hostInvoker,
            standardLibraryPolicy,
            standardLibraryBridge,
            aliases
        );
    }

    private static String errorPrefix(String sourceName) {
        if (sourceName == null || sourceName.isBlank()) {
            return LANGUAGE + ": ";
        }
        return LANGUAGE + " " + sourceName + ": ";
    }

    static Object normalizeValue(Object value) {
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

    private static StandardLibraryException findStandardLibraryException(Throwable ex) {
        Throwable current = ex;
        while (current != null) {
            if (current instanceof StandardLibraryException standardLibraryException) {
                return standardLibraryException;
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

    private record RequireRewrite(String source, Map<String, String> aliases, boolean allowed) {
        static RequireRewrite denied() {
            return new RequireRewrite("", Map.of(), false);
        }
    }

    private static final class SupportedRequireForms {
        private static final List<String> ALLOWED_NAMESPACES = List.of(
            "ecritum.json",
            "ecritum.time",
            "ecritum.fs",
            "ecritum.http"
        );

        private SupportedRequireForms() {
        }

        static RequireRewrite rewrite(String source) {
            if (!source.contains("(require")) {
                return new RequireRewrite(source, Map.of(), true);
            }

            StringBuilder rewritten = new StringBuilder(source.length());
            LinkedHashMap<String, String> aliases = new LinkedHashMap<>();
            int index = 0;
            boolean inString = false;
            boolean escaped = false;
            boolean inComment = false;
            boolean foundRequire = false;
            while (index < source.length()) {
                char ch = source.charAt(index);
                if (inString) {
                    rewritten.append(ch);
                    if (escaped) {
                        escaped = false;
                    } else if (ch == '\\') {
                        escaped = true;
                    } else if (ch == '"') {
                        inString = false;
                    }
                    index++;
                } else if (inComment) {
                    rewritten.append(ch);
                    if (ch == '\n' || ch == '\r') {
                        inComment = false;
                    }
                    index++;
                } else if (ch == '"') {
                    rewritten.append(ch);
                    inString = true;
                    index++;
                } else if (ch == ';') {
                    rewritten.append(ch);
                    inComment = true;
                    index++;
                } else if (startsRequireForm(source, index)) {
                    int end = findFormEnd(source, index);
                    if (end < 0) {
                        return RequireRewrite.denied();
                    }
                    if (!parseRequireForm(source.substring(index, end + 1), aliases)) {
                        return RequireRewrite.denied();
                    }
                    rewritten.append("nil");
                    index = end + 1;
                    foundRequire = true;
                } else {
                    rewritten.append(ch);
                    index++;
                }
            }

            if (!foundRequire) {
                return new RequireRewrite(source, Map.of(), true);
            }
            return new RequireRewrite(rewritten.toString(), Map.copyOf(aliases), true);
        }

        private static boolean startsRequireForm(String source, int index) {
            String marker = "(require";
            if (!source.startsWith(marker, index)) {
                return false;
            }
            int next = index + marker.length();
            return next < source.length() && Character.isWhitespace(source.charAt(next));
        }

        private static int findFormEnd(String source, int start) {
            int depth = 0;
            boolean inString = false;
            boolean escaped = false;
            for (int index = start; index < source.length(); index++) {
                char ch = source.charAt(index);
                if (inString) {
                    if (escaped) {
                        escaped = false;
                    } else if (ch == '\\') {
                        escaped = true;
                    } else if (ch == '"') {
                        inString = false;
                    }
                    continue;
                }
                if (ch == '"') {
                    inString = true;
                } else if (ch == '(') {
                    depth++;
                } else if (ch == ')') {
                    depth--;
                    if (depth == 0) {
                        return index;
                    }
                    if (depth < 0) {
                        return -1;
                    }
                }
            }
            return -1;
        }

        private static boolean parseRequireForm(String form, Map<String, String> aliases) {
            String body = form.substring("(require".length(), form.length() - 1).trim();
            List<String> clauses = splitClauses(body);
            if (clauses.isEmpty()) {
                return false;
            }
            for (String clause : clauses) {
                if (!parseRequireClause(clause, aliases)) {
                    return false;
                }
            }
            return true;
        }

        private static List<String> splitClauses(String body) {
            ArrayList<String> clauses = new ArrayList<>();
            StringBuilder current = new StringBuilder();
            int bracketDepth = 0;
            for (int index = 0; index < body.length(); index++) {
                char ch = body.charAt(index);
                if (ch == '"' || ch == '(' || ch == ')' || ch == ';') {
                    return List.of();
                }
                if (ch == '[') {
                    bracketDepth++;
                } else if (ch == ']') {
                    bracketDepth--;
                    if (bracketDepth < 0) {
                        return List.of();
                    }
                }
                if (Character.isWhitespace(ch) && bracketDepth == 0) {
                    if (!current.isEmpty()) {
                        clauses.add(current.toString());
                        current.setLength(0);
                    }
                } else {
                    current.append(ch);
                }
            }
            if (bracketDepth != 0) {
                return List.of();
            }
            if (!current.isEmpty()) {
                clauses.add(current.toString());
            }
            return clauses;
        }

        private static boolean parseRequireClause(String clause, Map<String, String> aliases) {
            if (clause.startsWith("'[") && clause.endsWith("]")) {
                String[] tokens = clause.substring(2, clause.length() - 1).trim().split("\\s+");
                if (tokens.length != 3 || !":as".equals(tokens[1])) {
                    return false;
                }
                if (!ALLOWED_NAMESPACES.contains(tokens[0]) || !isAlias(tokens[2])) {
                    return false;
                }
                aliases.put(tokens[2], tokens[0]);
                return true;
            }
            if (!clause.startsWith("'")) {
                return false;
            }
            return ALLOWED_NAMESPACES.contains(clause.substring(1));
        }

        private static boolean isAlias(String alias) {
            if (!alias.matches("[A-Za-z][A-Za-z0-9_-]*")) {
                return false;
            }
            String lower = alias.toLowerCase();
            return !List.of("ecritum", "java", "javax", "sun", "clojure", "graal", "truffle", "sci").contains(lower);
        }
    }

    static final class ProjectedHostFunction extends RestFn {
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
