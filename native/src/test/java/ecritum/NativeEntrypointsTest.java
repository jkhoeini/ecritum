package ecritum;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;

import org.junit.jupiter.api.Test;

final class NativeEntrypointsTest {
    @Test
    void versionForTestsReturnsDevelopmentVersion() {
        assertEquals(NativeEntrypoints.VERSION, NativeEntrypoints.versionForTests());
        assertFalse(NativeEntrypoints.versionForTests().isBlank());
    }

    @Test
    void versionBufferStatusRejectsNonPositiveBufferSizes() {
        assertEquals(EcritumStatus.INVALID_ARGUMENT, NativeEntrypoints.versionBufferStatus(0));
        assertEquals(EcritumStatus.INVALID_ARGUMENT, NativeEntrypoints.versionBufferStatus(-1));
    }

    @Test
    void versionBufferStatusRequiresSpaceForTerminator() {
        int exactSize = NativeEntrypoints.VERSION.getBytes(java.nio.charset.StandardCharsets.UTF_8).length + 1;

        assertEquals(EcritumStatus.BUFFER_TOO_SMALL, NativeEntrypoints.versionBufferStatus(exactSize - 1));
        assertEquals(EcritumStatus.OK, NativeEntrypoints.versionBufferStatus(exactSize));
    }
}
