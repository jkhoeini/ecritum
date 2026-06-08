package ecritum;

import java.util.List;
import java.util.Map;

record StandardLibraryPolicy(
    String filesystemMode,
    List<String> filesystemRoots,
    boolean clockReadable,
    boolean networkReadable,
    Long executionTimeoutNanos
) {
    static StandardLibraryPolicy denied() {
        return new StandardLibraryPolicy("denied", List.of(), false, false, null);
    }

    static StandardLibraryPolicy fromManifest(String manifest) {
        if (manifest == null || manifest.isBlank()) {
            return denied();
        }
        Object parsed = StandardLibraryValueCodec.readJson(manifest);
        if (!(parsed instanceof Map<?, ?> root)) {
            throw StandardLibraryException.internalError("standard-library policy manifest is invalid");
        }
        expectKeys(root, List.of("schemaVersion", "filesystem", "network", "clock", "resourceLimits"), "standard-library policy manifest has unknown keys");
        expectLong(root.get("schemaVersion"), 1L, "standard-library policy manifest version is unsupported");

        Map<?, ?> filesystem = expectObject(root.get("filesystem"), "standard-library filesystem policy is invalid");
        expectKeys(filesystem, List.of("mode", "roots"), "standard-library filesystem policy has unknown keys");
        String filesystemMode = expectString(filesystem.get("mode"), "standard-library filesystem mode is invalid");
        if (!filesystemMode.equals("denied") && !filesystemMode.equals("read_only") && !filesystemMode.equals("read_write")) {
            throw StandardLibraryException.internalError("standard-library filesystem mode is unsupported");
        }
        List<String> roots = expectStringList(filesystem.get("roots"), "standard-library filesystem roots are invalid");
        if (roots.size() != roots.stream().distinct().count()) {
            throw StandardLibraryException.internalError("standard-library filesystem roots contain duplicates");
        }

        Map<?, ?> network = expectObject(root.get("network"), "standard-library network policy is invalid");
        expectKeys(network, List.of("mode", "rules"), "standard-library network policy has unknown keys");
        String networkMode = expectString(network.get("mode"), "standard-library network mode is invalid");
        if (!networkMode.equals("denied") && !networkMode.equals("allowed")) {
            throw StandardLibraryException.internalError("standard-library network mode is unsupported");
        }
        expectArray(network.get("rules"), "standard-library network rules are invalid");

        Map<?, ?> clock = expectObject(root.get("clock"), "standard-library clock policy is invalid");
        expectKeys(clock, List.of("mode"), "standard-library clock policy has unknown keys");
        String clockMode = expectString(clock.get("mode"), "standard-library clock mode is invalid");
        if (!clockMode.equals("denied") && !clockMode.equals("allowed")) {
            throw StandardLibraryException.internalError("standard-library clock mode is unsupported");
        }
        Map<?, ?> resourceLimits = expectObject(root.get("resourceLimits"), "standard-library resource limits are invalid");
        expectKeys(resourceLimits, List.of("executionTimeoutNanos"), "standard-library resource limits have unknown keys");

        return new StandardLibraryPolicy(
            filesystemMode,
            List.copyOf(roots),
            clockMode.equals("allowed"),
            networkMode.equals("allowed"),
            optionalLong(resourceLimits.get("executionTimeoutNanos"), "standard-library execution timeout is invalid")
        );
    }

    private static void expectKeys(Map<?, ?> map, List<String> allowedKeys, String message) {
        for (Object key : map.keySet()) {
            if (!(key instanceof String string) || !allowedKeys.contains(string)) {
                throw StandardLibraryException.internalError(message);
            }
        }
    }

    boolean filesystemReadable() {
        return filesystemMode.equals("read_only") || filesystemMode.equals("read_write");
    }

    private static Map<?, ?> expectObject(Object value, String message) {
        if (value instanceof Map<?, ?> map) {
            return map;
        }
        throw StandardLibraryException.internalError(message);
    }

    private static List<?> expectArray(Object value, String message) {
        if (value instanceof List<?> list) {
            return list;
        }
        throw StandardLibraryException.internalError(message);
    }

    private static List<String> expectStringList(Object value, String message) {
        return expectArray(value, message).stream()
            .map(item -> expectString(item, message))
            .toList();
    }

    private static String expectString(Object value, String message) {
        if (value instanceof String string) {
            return string;
        }
        throw StandardLibraryException.internalError(message);
    }

    private static void expectLong(Object value, long expected, String message) {
        if (value instanceof Long raw && raw == expected) {
            return;
        }
        throw StandardLibraryException.internalError(message);
    }

    private static Long optionalLong(Object value, String message) {
        if (value == null) {
            return null;
        }
        if (value instanceof Long raw) {
            return raw;
        }
        throw StandardLibraryException.internalError(message);
    }
}
