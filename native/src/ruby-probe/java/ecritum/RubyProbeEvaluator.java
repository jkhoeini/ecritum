package ecritum;

import java.math.BigInteger;
import java.util.ArrayList;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Set;
import java.util.regex.Pattern;
import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.EnvironmentAccess;
import org.graalvm.polyglot.HostAccess;
import org.graalvm.polyglot.PolyglotAccess;
import org.graalvm.polyglot.PolyglotException;
import org.graalvm.polyglot.Source;
import org.graalvm.polyglot.Value;
import org.graalvm.polyglot.io.IOAccess;

final class RubyProbeEvaluator {
    private static final String LANGUAGE = "ruby";
    private static final double MAX_SAFE_INTEGER = 9_007_199_254_740_991d;
    private static final List<Pattern> DENIED_SOURCE_PATTERNS = List.of(
        Pattern.compile("\\bJava\\s*\\.\\s*(?:type|import|add_to_classpath)\\b"),
        Pattern.compile("\\bPolyglot\\s*\\.\\s*(?:eval|eval_file|export|import|import_method)\\b"),
        Pattern.compile("\\bPolyglot::InnerContext\\b"),
        Pattern.compile("\\b(?:require|load)\\s+['\"](?:fiddle|ffi|socket|net/http|net/ftp|net/imap|open3|openssl|rubygems|bundler)['\"]"),
        Pattern.compile("\\bFiddle\\b"),
        Pattern.compile("\\b(?:Kernel\\.)?(?:system|exec|spawn)\\s*\\("),
        Pattern.compile("\\bIO\\s*\\.\\s*popen\\b"),
        Pattern.compile("`[^`]*`"),
        Pattern.compile("\\bENV\\b"),
        Pattern.compile("\\$LOAD_PATH\\b"),
        Pattern.compile("\\b(?:File|Dir|IO|Pathname|Tempfile)\\b"),
        Pattern.compile("\\b(?:Thread|Ractor|Signal)\\b")
    );

    private RubyProbeEvaluator() {
    }

    static SciEvalResult evaluate(String source, String sourceName) {
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

        try (Context context = newContext()) {
            Value value = context.eval(Source.newBuilder("ruby", source, sourceFileName(safeSourceName)).buildLiteral());
            return SciEvalResult.ok(LANGUAGE, normalizeValue(value, newIdentitySet()));
        } catch (RubyProbeException ex) {
            return new SciEvalResult(
                ex.status(),
                null,
                LANGUAGE,
                safeSourceName,
                ex.category(),
                errorPrefix(safeSourceName) + ex.category() + " error: " + sanitizeMessage(ex.getMessage())
            );
        } catch (PolyglotException ex) {
            String category = classify(ex);
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
            return SciEvalResult.internalError(LANGUAGE, safeSourceName, errorPrefix(safeSourceName) + "ruby probe backend failed");
        }
    }

    // Package-private (not private) so RubyDenialMatrixTest can build the EXACT
    // production deny-by-default context and exercise raw eval that bypasses the
    // lexical deniesSource() pre-filter, proving runtime-grade denial (M12-001C
    // Part A). This is read-only test exposure of the production context; no
    // trusted/bypass eval path is added to production code.
    static Context newContext() {
        return Context.newBuilder("ruby")
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
            .allowExperimentalOptions(true)
            .option("ruby.platform-native", "false")
            .option("ruby.cexts", "false")
            .option("ruby.rubygems", "false")
            .build();
    }

    private static boolean deniesSource(String source) {
        for (Pattern pattern : DENIED_SOURCE_PATTERNS) {
            if (pattern.matcher(source).find()) {
                return true;
            }
        }
        return false;
    }

    private static Object normalizeValue(Value value, Set<Value> activeValues) {
        if (value == null || value.isNull()) {
            return null;
        }
        if (value.isBoolean()) {
            return value.asBoolean();
        }
        if (value.isString()) {
            return value.asString();
        }
        if (value.isNumber()) {
            return normalizeNumber(value);
        }
        if (value.hasArrayElements()) {
            if (!activeValues.add(value)) {
                throw scriptException("cyclic Ruby array");
            }
            try {
                long size = value.getArraySize();
                if (size > Integer.MAX_VALUE) {
                    throw scriptException("Ruby array is too large");
                }
                ArrayList<Object> values = new ArrayList<>((int) size);
                for (long index = 0; index < size; index++) {
                    values.add(normalizeValue(value.getArrayElement(index), activeValues));
                }
                return List.copyOf(values);
            } finally {
                activeValues.remove(value);
            }
        }
        throw scriptException("unsupported Ruby result type");
    }

    private static Object normalizeNumber(Value value) {
        if (value.fitsInLong()) {
            return value.asLong();
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
        if (value.fitsInBigInteger()) {
            BigInteger bigInteger = value.asBigInteger();
            try {
                return bigInteger.longValueExact();
            } catch (ArithmeticException ex) {
                throw scriptException("unsupported Ruby number");
            }
        }
        throw scriptException("unsupported Ruby number");
    }

    private static Set<Value> newIdentitySet() {
        return Collections.newSetFromMap(new IdentityHashMap<>());
    }

    private static String classify(PolyglotException ex) {
        String message = ex.getMessage();
        String normalized = message == null ? "" : message.toLowerCase();
        if (ex.isResourceExhausted() || ex.isInterrupted() || ex.isCancelled()) {
            return "timeout";
        }
        if (isPermissionHostException(ex) || normalized.contains("access denied") || normalized.contains("operation not permitted")) {
            return "permission";
        }
        return "runtime";
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

    private static RubyProbeException scriptException(String message) {
        return new RubyProbeException(EcritumStatus.SCRIPT, "runtime", message);
    }

    private static String sourceFileName(String sourceName) {
        if (sourceName == null || sourceName.isBlank()) {
            return "ecritum-ruby-probe.rb";
        }
        return sourceName;
    }

    private static String errorPrefix(String sourceName) {
        if (sourceName == null || sourceName.isBlank()) {
            return "";
        }
        return sourceName + ": ";
    }

    private static String sanitizeMessage(String message) {
        if (message == null || message.isBlank()) {
            return "Ruby probe execution failed";
        }
        return message
            .replaceAll("(?i)(token|secret|password|key)=[^\\s,;]+", "$1=<redacted>")
            .replace('\n', ' ')
            .replace('\r', ' ');
    }

    private static final class RubyProbeException extends RuntimeException {
        private final int status;
        private final String category;

        RubyProbeException(int status, String category, String message) {
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
}
