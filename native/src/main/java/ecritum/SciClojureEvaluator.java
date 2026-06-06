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
import java.util.ArrayList;
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
        SciRequireRewrite requireRewrite = SciRequirePreprocessor.rewrite(source);
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
            return SciEvalResult.ok(ClojureValueCodec.normalize(rawValue));
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
                normalizedArgs.add(ClojureValueCodec.normalize(current.first()));
            }
            return ClojureValueCodec.normalize(invoker.invoke(namespace, function, normalizedArgs));
        }
    }
}
