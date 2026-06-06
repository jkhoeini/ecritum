package ecritum;

import java.util.Map;

record SciRequireRewrite(String source, Map<String, String> aliases, boolean allowed) {
    static SciRequireRewrite denied() {
        return new SciRequireRewrite("", Map.of(), false);
    }
}
