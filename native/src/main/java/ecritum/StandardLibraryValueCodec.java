package ecritum;

import clojure.lang.Keyword;
import clojure.lang.Symbol;
import java.util.AbstractMap;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

final class StandardLibraryValueCodec {
    private StandardLibraryValueCodec() {
    }

    static String writeJson(Object value) {
        StringBuilder out = new StringBuilder();
        writeValue(out, value);
        return out.toString();
    }

    static Object readJson(String json) {
        JsonParser parser = new JsonParser(json);
        Object value = parser.parseValue();
        parser.expectEnd();
        return value;
    }

    private static void writeValue(StringBuilder out, Object value) {
        if (value == null) {
            out.append("null");
        } else if (value instanceof Boolean bool) {
            out.append(bool ? "true" : "false");
        } else if (isInteger(value)) {
            try {
                out.append(asLong(value));
            } catch (ArithmeticException ex) {
                throw StandardLibraryException.scriptError("JSON integer overflow");
            }
        } else if (value instanceof Float || value instanceof Double || value instanceof BigDecimal) {
            writeDouble(out, ((Number) value).doubleValue());
        } else if (value instanceof CharSequence || value instanceof Character) {
            writeString(out, value.toString());
        } else if (value instanceof byte[]) {
            throw StandardLibraryException.scriptError("JSON does not support data values");
        } else if (value instanceof Map<?, ?> map) {
            writeObject(out, map);
        } else if (value instanceof Iterable<?> iterable) {
            writeArray(out, iterable);
        } else {
            throw StandardLibraryException.scriptError("unsupported JSON value");
        }
    }

    private static void writeArray(StringBuilder out, Iterable<?> iterable) {
        out.append('[');
        boolean first = true;
        for (Object item : iterable) {
            if (!first) {
                out.append(',');
            }
            writeValue(out, item);
            first = false;
        }
        out.append(']');
    }

    private static void writeObject(StringBuilder out, Map<?, ?> map) {
        ArrayList<Map.Entry<String, Object>> entries = new ArrayList<>();
        for (Map.Entry<?, ?> entry : map.entrySet()) {
            Object key = entry.getKey();
            if (!(key instanceof String)) {
                if (key instanceof Keyword || key instanceof Symbol) {
                    throw StandardLibraryException.scriptError("JSON objects require string map keys");
                }
                throw StandardLibraryException.scriptError("JSON objects require string map keys");
            }
            entries.add(new AbstractMap.SimpleImmutableEntry<>((String) key, entry.getValue()));
        }
        entries.sort(Comparator.comparing(Map.Entry::getKey));

        out.append('{');
        for (int index = 0; index < entries.size(); index++) {
            if (index > 0) {
                out.append(',');
            }
            writeString(out, entries.get(index).getKey());
            out.append(':');
            writeValue(out, entries.get(index).getValue());
        }
        out.append('}');
    }

    private static void writeString(StringBuilder out, String value) {
        validateStringSurrogates(value);
        out.append('"');
        for (int index = 0; index < value.length(); index++) {
            char ch = value.charAt(index);
            switch (ch) {
                case '"' -> out.append("\\\"");
                case '\\' -> out.append("\\\\");
                case '\b' -> out.append("\\b");
                case '\f' -> out.append("\\f");
                case '\n' -> out.append("\\n");
                case '\r' -> out.append("\\r");
                case '\t' -> out.append("\\t");
                default -> {
                    if (ch < 0x20) {
                        out.append(String.format("\\u%04x", (int) ch));
                    } else {
                        out.append(ch);
                    }
                }
            }
        }
        out.append('"');
    }

    private static void validateStringSurrogates(String value) {
        for (int index = 0; index < value.length(); index++) {
            char ch = value.charAt(index);
            if (Character.isHighSurrogate(ch)) {
                if (index + 1 >= value.length() || !Character.isLowSurrogate(value.charAt(index + 1))) {
                    throw StandardLibraryException.scriptError("invalid JSON string surrogate");
                }
                index++;
            } else if (Character.isLowSurrogate(ch)) {
                throw StandardLibraryException.scriptError("invalid JSON string surrogate");
            }
        }
    }

    private static void writeDouble(StringBuilder out, double value) {
        if (!Double.isFinite(value)) {
            throw StandardLibraryException.scriptError("JSON numbers must be finite");
        }
        out.append(BigDecimal.valueOf(value).stripTrailingZeros().toPlainString());
    }

    private static boolean isInteger(Object value) {
        return value instanceof Byte
            || value instanceof Short
            || value instanceof Integer
            || value instanceof Long
            || value instanceof BigInteger
            || value instanceof clojure.lang.BigInt;
    }

    private static long asLong(Object value) {
        if (value instanceof BigInteger bigInteger) {
            return bigInteger.longValueExact();
        }
        if (value instanceof clojure.lang.BigInt bigInt) {
            return bigInt.toBigInteger().longValueExact();
        }
        return ((Number) value).longValue();
    }

    private static final class JsonParser {
        private final String json;
        private int index;

        JsonParser(String json) {
            this.json = json == null ? "" : json;
        }

        Object parseValue() {
            skipWhitespace();
            if (index >= json.length()) {
                throw StandardLibraryException.scriptError("empty JSON input");
            }
            char ch = json.charAt(index);
            return switch (ch) {
                case 'n' -> readLiteral("null", null);
                case 't' -> readLiteral("true", Boolean.TRUE);
                case 'f' -> readLiteral("false", Boolean.FALSE);
                case '"' -> readString();
                case '[' -> readArray();
                case '{' -> readObject();
                default -> {
                    if (ch == '-' || Character.isDigit(ch)) {
                        yield readNumber();
                    }
                    throw StandardLibraryException.scriptError("invalid JSON value");
                }
            };
        }

        void expectEnd() {
            skipWhitespace();
            if (index != json.length()) {
                throw StandardLibraryException.scriptError("trailing JSON input");
            }
        }

        private Object readLiteral(String literal, Object value) {
            if (!json.startsWith(literal, index)) {
                throw StandardLibraryException.scriptError("invalid JSON literal");
            }
            index += literal.length();
            return value;
        }

        private List<Object> readArray() {
            index++;
            ArrayList<Object> values = new ArrayList<>();
            skipWhitespace();
            if (consume(']')) {
                return values;
            }
            while (true) {
                values.add(parseValue());
                skipWhitespace();
                if (consume(']')) {
                    return values;
                }
                expect(',');
            }
        }

        private Map<String, Object> readObject() {
            index++;
            LinkedHashMap<String, Object> values = new LinkedHashMap<>();
            skipWhitespace();
            if (consume('}')) {
                return values;
            }
            while (true) {
                skipWhitespace();
                if (index >= json.length() || json.charAt(index) != '"') {
                    throw StandardLibraryException.scriptError("JSON object keys must be strings");
                }
                String key = readString();
                if (values.containsKey(key)) {
                    throw StandardLibraryException.scriptError("duplicate JSON object key");
                }
                skipWhitespace();
                expect(':');
                values.put(key, parseValue());
                skipWhitespace();
                if (consume('}')) {
                    return values;
                }
                expect(',');
            }
        }

        private String readString() {
            expect('"');
            StringBuilder value = new StringBuilder();
            while (index < json.length()) {
                char ch = json.charAt(index++);
                if (ch == '"') {
                    String decoded = value.toString();
                    validateStringSurrogates(decoded);
                    return decoded;
                }
                if (ch == '\\') {
                    value.append(readEscape());
                } else if (ch < 0x20) {
                    throw StandardLibraryException.scriptError("invalid control character in JSON string");
                } else {
                    value.append(ch);
                }
            }
            throw StandardLibraryException.scriptError("unterminated JSON string");
        }

        private char readEscape() {
            if (index >= json.length()) {
                throw StandardLibraryException.scriptError("unterminated JSON escape");
            }
            char escaped = json.charAt(index++);
            return switch (escaped) {
                case '"', '\\', '/' -> escaped;
                case 'b' -> '\b';
                case 'f' -> '\f';
                case 'n' -> '\n';
                case 'r' -> '\r';
                case 't' -> '\t';
                case 'u' -> readUnicodeEscape();
                default -> throw StandardLibraryException.scriptError("invalid JSON escape");
            };
        }

        private char readUnicodeEscape() {
            if (index + 4 > json.length()) {
                throw StandardLibraryException.scriptError("truncated JSON unicode escape");
            }
            int value = 0;
            for (int count = 0; count < 4; count++) {
                int digit = Character.digit(json.charAt(index++), 16);
                if (digit < 0) {
                    throw StandardLibraryException.scriptError("invalid JSON unicode escape");
                }
                value = (value << 4) + digit;
            }
            return (char) value;
        }

        private Object readNumber() {
            int start = index;
            if (consume('-')) {
                if (index >= json.length()) {
                    throw StandardLibraryException.scriptError("invalid JSON number");
                }
            }
            readIntegerPart();
            boolean fractional = false;
            if (consume('.')) {
                fractional = true;
                readDigits("invalid JSON fraction");
            }
            if (index < json.length() && (json.charAt(index) == 'e' || json.charAt(index) == 'E')) {
                fractional = true;
                index++;
                if (index < json.length() && (json.charAt(index) == '+' || json.charAt(index) == '-')) {
                    index++;
                }
                readDigits("invalid JSON exponent");
            }
            String number = json.substring(start, index);
            try {
                if (fractional) {
                    double parsed = Double.parseDouble(number);
                    if (!Double.isFinite(parsed)) {
                        throw StandardLibraryException.scriptError("JSON number is not finite");
                    }
                    return parsed;
                }
                return Long.parseLong(number);
            } catch (NumberFormatException ex) {
                throw StandardLibraryException.scriptError("JSON integer overflow");
            }
        }

        private void readIntegerPart() {
            if (index >= json.length()) {
                throw StandardLibraryException.scriptError("invalid JSON number");
            }
            if (consume('0')) {
                if (index < json.length() && Character.isDigit(json.charAt(index))) {
                    throw StandardLibraryException.scriptError("invalid JSON leading zero");
                }
                return;
            }
            readDigits("invalid JSON number");
        }

        private void readDigits(String message) {
            int start = index;
            while (index < json.length() && Character.isDigit(json.charAt(index))) {
                index++;
            }
            if (index == start) {
                throw StandardLibraryException.scriptError(message);
            }
        }

        private void skipWhitespace() {
            while (index < json.length()) {
                char ch = json.charAt(index);
                if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
                    index++;
                } else {
                    return;
                }
            }
        }

        private void expect(char expected) {
            if (!consume(expected)) {
                throw StandardLibraryException.scriptError("expected '" + expected + "' in JSON");
            }
        }

        private boolean consume(char expected) {
            if (index < json.length() && json.charAt(index) == expected) {
                index++;
                return true;
            }
            return false;
        }
    }
}
