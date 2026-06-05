package ecritum;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

final class BackendResultCodec {
    private static final int MAGIC = 0x45435631; // ECV1
    private static final int KIND_NULL = 0;
    private static final int KIND_BOOL = 1;
    private static final int KIND_INT = 2;
    private static final int KIND_DOUBLE = 3;
    private static final int KIND_STRING = 4;
    private static final int KIND_DATA = 5;
    private static final int KIND_ARRAY = 6;
    private static final int KIND_OBJECT = 7;

    private BackendResultCodec() {
    }

    static byte[] encode(SciEvalResult result) {
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream out = new DataOutputStream(bytes);
            out.writeInt(MAGIC);
            out.writeInt(result.status());
            if (result.status() == EcritumStatus.OK) {
                writeValue(out, result.value());
            } else {
                writeString(out, result.language());
                writeString(out, result.sourceName());
                writeString(out, result.category());
                writeString(out, result.message());
            }
            out.flush();
            return bytes.toByteArray();
        } catch (IOException ex) {
            throw new IllegalStateException("backend result encoding failed", ex);
        }
    }

    static SciEvalResult decode(byte[] bytes) {
        try {
            DataInputStream in = new DataInputStream(new ByteArrayInputStream(bytes));
            int magic = in.readInt();
            if (magic != MAGIC) {
                throw new IllegalArgumentException("invalid backend result magic");
            }
            int status = in.readInt();
            if (status == EcritumStatus.OK) {
                return SciEvalResult.ok(readValue(in));
            }
            String language = readString(in);
            String sourceName = readString(in);
            String category = readString(in);
            String message = readString(in);
            return new SciEvalResult(status, null, language, sourceName, category, message);
        } catch (IOException ex) {
            throw new IllegalArgumentException("invalid backend result payload", ex);
        }
    }

    private static void writeValue(DataOutputStream out, Object value) throws IOException {
        if (value == null) {
            out.writeByte(KIND_NULL);
        } else if (value instanceof Boolean bool) {
            out.writeByte(KIND_BOOL);
            out.writeBoolean(bool);
        } else if (isInteger(value)) {
            out.writeByte(KIND_INT);
            out.writeLong(asLong(value));
        } else if (value instanceof Float || value instanceof Double || value instanceof BigDecimal) {
            out.writeByte(KIND_DOUBLE);
            out.writeDouble(((Number) value).doubleValue());
        } else if (value instanceof CharSequence || value instanceof Character) {
            out.writeByte(KIND_STRING);
            writeString(out, value.toString());
        } else if (value instanceof byte[] data) {
            out.writeByte(KIND_DATA);
            writeBytes(out, data);
        } else if (value instanceof List<?> list) {
            out.writeByte(KIND_ARRAY);
            out.writeInt(list.size());
            for (Object item : list) {
                writeValue(out, item);
            }
        } else if (value instanceof Map<?, ?> map) {
            out.writeByte(KIND_OBJECT);
            out.writeInt(map.size());
            for (Map.Entry<?, ?> entry : map.entrySet()) {
                writeString(out, String.valueOf(entry.getKey()));
                writeValue(out, entry.getValue());
            }
        } else {
            throw new IllegalArgumentException("unsupported backend value type");
        }
    }

    private static Object readValue(DataInputStream in) throws IOException {
        int kind = in.readUnsignedByte();
        return switch (kind) {
            case KIND_NULL -> null;
            case KIND_BOOL -> in.readBoolean();
            case KIND_INT -> in.readLong();
            case KIND_DOUBLE -> in.readDouble();
            case KIND_STRING -> readString(in);
            case KIND_DATA -> readBytes(in);
            case KIND_ARRAY -> readArray(in);
            case KIND_OBJECT -> readObject(in);
            default -> throw new IllegalArgumentException("unknown backend value kind");
        };
    }

    private static List<Object> readArray(DataInputStream in) throws IOException {
        int count = readCount(in);
        ArrayList<Object> values = new ArrayList<>(count);
        for (int index = 0; index < count; index++) {
            values.add(readValue(in));
        }
        return values;
    }

    private static Map<String, Object> readObject(DataInputStream in) throws IOException {
        int count = readCount(in);
        LinkedHashMap<String, Object> values = new LinkedHashMap<>();
        for (int index = 0; index < count; index++) {
            values.put(readString(in), readValue(in));
        }
        return values;
    }

    private static void writeString(DataOutputStream out, String value) throws IOException {
        byte[] bytes = (value == null ? "" : value).getBytes(StandardCharsets.UTF_8);
        writeBytes(out, bytes);
    }

    private static void writeBytes(DataOutputStream out, byte[] bytes) throws IOException {
        out.writeInt(bytes.length);
        out.write(bytes);
    }

    private static String readString(DataInputStream in) throws IOException {
        int len = readCount(in);
        byte[] bytes = in.readNBytes(len);
        if (bytes.length != len) {
            throw new IOException("truncated backend result string");
        }
        return new String(bytes, StandardCharsets.UTF_8);
    }

    private static byte[] readBytes(DataInputStream in) throws IOException {
        int len = readCount(in);
        byte[] bytes = in.readNBytes(len);
        if (bytes.length != len) {
            throw new IOException("truncated backend result bytes");
        }
        return bytes;
    }

    private static int readCount(DataInputStream in) throws IOException {
        int count = in.readInt();
        if (count < 0) {
            throw new IOException("negative backend result count");
        }
        return count;
    }

    private static boolean isInteger(Object value) {
        return value instanceof Byte
            || value instanceof Short
            || value instanceof Integer
            || value instanceof Long
            || value instanceof BigInteger;
    }

    private static long asLong(Object value) {
        if (value instanceof BigInteger bigInteger) {
            return bigInteger.longValueExact();
        }
        return ((Number) value).longValue();
    }
}
