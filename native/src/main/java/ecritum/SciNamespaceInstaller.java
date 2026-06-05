package ecritum;

import clojure.lang.IPersistentMap;
import clojure.lang.Keyword;
import clojure.lang.RT;
import clojure.lang.Symbol;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

final class SciNamespaceInstaller {
    private SciNamespaceInstaller() {
    }

    static IPersistentMap install(
        IPersistentMap baseOptions,
        List<HostProjection> projections,
        HostFunctionInvoker hostInvoker,
        StandardLibraryPolicy policy,
        StandardLibraryBridge bridge,
        Map<String, String> aliases
    ) {
        LinkedHashMap<String, IPersistentMap> namespaceFunctions = new LinkedHashMap<>();
        namespaceFunctions.putAll(StandardLibraryFacade.namespaces(policy, bridge));

        for (Map.Entry<String, String> alias : aliases.entrySet()) {
            IPersistentMap target = namespaceFunctions.get(alias.getValue());
            if (target != null) {
                namespaceFunctions.put(alias.getKey(), target);
            }
        }

        if (projections != null) {
            for (HostProjection projection : projections) {
                if (projection == null || projection.namespace() == null || projection.function() == null) {
                    continue;
                }
                if (projection.namespace().startsWith("ecritum.")) {
                    continue;
                }
                IPersistentMap functions = namespaceFunctions.getOrDefault(projection.namespace(), RT.map());
                functions = functions.assoc(
                    Symbol.intern(projection.function()),
                    new SciClojureEvaluator.ProjectedHostFunction(projection.namespace(), projection.function(), hostInvoker)
                );
                namespaceFunctions.put(projection.namespace(), functions);
            }
        }

        IPersistentMap namespaces = RT.map();
        for (Map.Entry<String, IPersistentMap> entry : namespaceFunctions.entrySet()) {
            namespaces = namespaces.assoc(Symbol.intern(entry.getKey()), entry.getValue());
        }
        return baseOptions.assoc(Keyword.intern(null, "namespaces"), namespaces);
    }
}
