package ecritum;

import clojure.lang.Keyword;
import clojure.lang.Symbol;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.Map;

final class ClojureValueCodec {
    private ClojureValueCodec() {
    }

    static Object normalize(Object value) {
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
        if (value instanceof Keyword keyword) {
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
                normalized.put(key, normalize(entry.getValue()));
            }
            return normalized;
        }
        if (value instanceof Iterable<?> iterable) {
            ArrayList<Object> normalized = new ArrayList<>();
            for (Object item : iterable) {
                normalized.add(normalize(item));
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
}
