package ecritum;

import clojure.lang.IPersistentMap;
import clojure.lang.ISeq;
import clojure.lang.RT;
import clojure.lang.RestFn;
import clojure.lang.Symbol;
import java.time.Instant;
import java.time.format.DateTimeParseException;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

final class StandardLibraryFacade {
    private StandardLibraryFacade() {
    }

    static Map<String, IPersistentMap> namespaces(StandardLibraryPolicy policy, StandardLibraryBridge bridge) {
        LinkedHashMap<String, IPersistentMap> namespaces = new LinkedHashMap<>();
        namespaces.put("ecritum.json", RT.map(
            Symbol.intern("read-string"), new FacadeFunction(args -> readJson(args)),
            Symbol.intern("write-string"), new FacadeFunction(args -> writeJson(args))
        ));
        namespaces.put("ecritum.time", RT.map(
            Symbol.intern("parse-instant"), new FacadeFunction(args -> parseInstant(args)),
            Symbol.intern("format-instant"), new FacadeFunction(args -> formatInstant(args)),
            Symbol.intern("now"), new FacadeFunction(args -> now(policy, bridge, args))
        ));
        namespaces.put("ecritum.fs", RT.map(
            Symbol.intern("read-text"), new FacadeFunction(args -> filesystemRead(policy, bridge, "fs.read_text", args)),
            Symbol.intern("read-bytes"), new FacadeFunction(args -> filesystemRead(policy, bridge, "fs.read_bytes", args)),
            Symbol.intern("exists?"), new FacadeFunction(args -> filesystemRead(policy, bridge, "fs.exists", args))
        ));
        namespaces.put("ecritum.http", RT.map(
            Symbol.intern("request"), new FacadeFunction(args -> httpRequest(policy, bridge, args))
        ));
        return namespaces;
    }

    private static Object readJson(List<Object> args) {
        expectArity(args, 1, "ecritum.json/read-string");
        Object json = args.getFirst();
        if (!(json instanceof CharSequence)) {
            throw StandardLibraryException.scriptError("ecritum.json/read-string expects a string");
        }
        return StandardLibraryValueCodec.readJson(json.toString());
    }

    private static Object writeJson(List<Object> args) {
        expectArity(args, 1, "ecritum.json/write-string");
        return StandardLibraryValueCodec.writeJson(args.getFirst());
    }

    private static Object parseInstant(List<Object> args) {
        expectArity(args, 1, "ecritum.time/parse-instant");
        return parseInstantString(args.getFirst(), "ecritum.time/parse-instant");
    }

    private static Object formatInstant(List<Object> args) {
        expectArity(args, 1, "ecritum.time/format-instant");
        return parseInstantString(args.getFirst(), "ecritum.time/format-instant");
    }

    private static Object now(StandardLibraryPolicy policy, StandardLibraryBridge bridge, List<Object> args) {
        expectArity(args, 0, "ecritum.time/now");
        if (!policy.clockReadable()) {
            throw StandardLibraryException.permissionDenied("clock access is not permitted");
        }
        return bridge.invoke("time.now", List.of());
    }

    private static Object filesystemRead(
        StandardLibraryPolicy policy,
        StandardLibraryBridge bridge,
        String operation,
        List<Object> args
    ) {
        expectArity(args, 1, operation);
        if (!policy.filesystemReadable()) {
            throw StandardLibraryException.permissionDenied("filesystem access is not permitted");
        }
        return bridge.invoke(operation, args);
    }

    private static Object httpRequest(StandardLibraryPolicy policy, StandardLibraryBridge bridge, List<Object> args) {
        expectArity(args, 1, "ecritum.http/request");
        throw StandardLibraryException.permissionDenied("http access is not permitted");
    }

    private static String parseInstantString(Object value, String operation) {
        if (!(value instanceof CharSequence)) {
            throw StandardLibraryException.scriptError(operation + " expects an ISO-8601 instant string");
        }
        try {
            return Instant.parse(value.toString()).toString();
        } catch (DateTimeParseException ex) {
            throw StandardLibraryException.scriptError(operation + " received an invalid ISO-8601 instant");
        }
    }

    private static void expectArity(List<Object> args, int expected, String operation) {
        if (args.size() != expected) {
            throw StandardLibraryException.scriptError(operation + " expects " + expected + " argument(s)");
        }
    }

    private interface FacadeInvoker {
        Object invoke(List<Object> args);
    }

    private static final class FacadeFunction extends RestFn {
        private final FacadeInvoker invoker;

        FacadeFunction(FacadeInvoker invoker) {
            this.invoker = invoker;
        }

        @Override
        public int getRequiredArity() {
            return 0;
        }

        @Override
        protected Object doInvoke(Object args) {
            ArrayList<Object> rawArgs = new ArrayList<>();
            for (ISeq current = (ISeq) args; current != null; current = current.next()) {
                rawArgs.add(current.first());
            }
            return SciClojureEvaluator.normalizeValue(invoker.invoke(List.copyOf(rawArgs)));
        }
    }
}
