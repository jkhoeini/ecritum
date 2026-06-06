package ecritum;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import clojure.lang.Keyword;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;

final class ClojureValueCodecTest {
    @Test
    void normalizesScalarsCollectionsAndData() {
        byte[] data = new byte[] {0, 1, 2, -1};
        Map<Object, Object> source = new LinkedHashMap<>();
        source.put("items", List.of(1, Keyword.intern(null, "done"), data));

        Object normalized = ClojureValueCodec.normalize(source);

        Map<?, ?> object = (Map<?, ?>) normalized;
        List<?> items = (List<?>) object.get("items");
        assertEquals(1, items.get(0));
        assertEquals("done", items.get(1));
        byte[] normalizedData = (byte[]) items.get(2);
        assertArrayEquals(new byte[] {0, 1, 2, -1}, normalizedData);
        data[0] = 99;
        assertArrayEquals(new byte[] {0, 1, 2, -1}, normalizedData);
    }

    @Test
    void rejectsDuplicateNormalizedKeysAndUnsupportedValues() {
        Map<Object, Object> duplicate = new LinkedHashMap<>();
        duplicate.put("answer", 1);
        duplicate.put(Keyword.intern(null, "answer"), 2);

        assertThrows(IllegalArgumentException.class, () -> ClojureValueCodec.normalize(duplicate));
        assertThrows(IllegalArgumentException.class, () -> ClojureValueCodec.normalize(new Object()));
    }
}
