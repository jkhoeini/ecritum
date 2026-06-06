package ecritum;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import org.junit.jupiter.api.Test;

final class StandardLibraryResultTest {
    @Test
    void successCarriesValue() {
        StandardLibraryResult result = StandardLibraryResult.success(42L);

        assertEquals(EcritumStatus.OK, result.status());
        assertEquals(42L, result.valueOrThrow());
    }

    @Test
    void failureMapsToStandardLibraryException() {
        StandardLibraryResult result = StandardLibraryResult.failure(
            EcritumStatus.PERMISSION_DENIED,
            "permission",
            "fs denied"
        );

        StandardLibraryException error = assertThrows(StandardLibraryException.class, result::valueOrThrow);
        assertEquals(EcritumStatus.PERMISSION_DENIED, error.status());
        assertEquals("permission", error.category());
        assertEquals("fs denied", error.getMessage());
    }

    @Test
    void okFailureIsInvalid() {
        assertThrows(IllegalArgumentException.class, () -> StandardLibraryResult.failure(EcritumStatus.OK, "", ""));
    }
}
