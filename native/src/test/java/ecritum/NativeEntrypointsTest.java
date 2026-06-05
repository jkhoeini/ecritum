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

    @Test
    void statusConstantsCoverM2ValueAndErrorModel() {
        assertEquals(0, EcritumStatus.OK);
        assertEquals(1, EcritumStatus.INVALID_ARGUMENT);
        assertEquals(2, EcritumStatus.BUFFER_TOO_SMALL);
        assertEquals(3, EcritumStatus.RUNTIME_UNAVAILABLE);
        assertEquals(4, EcritumStatus.INVALID_HANDLE);
        assertEquals(5, EcritumStatus.OUT_OF_MEMORY);
        assertEquals(6, EcritumStatus.INVALID_UTF8);
        assertEquals(7, EcritumStatus.INPUT_TOO_LARGE);
        assertEquals(8, EcritumStatus.INVALID_CONFIG);
        assertEquals(9, EcritumStatus.UNSUPPORTED_CONFIG_VERSION);
        assertEquals(10, EcritumStatus.CONTEXTS_ALIVE);
        assertEquals(11, EcritumStatus.CLOSED);
        assertEquals(12, EcritumStatus.BUSY);
        assertEquals(13, EcritumStatus.REENTRANT_CALL);
        assertEquals(14, EcritumStatus.PERMISSION_DENIED);
        assertEquals(15, EcritumStatus.TIMEOUT);
        assertEquals(16, EcritumStatus.CANCELLED);
        assertEquals(17, EcritumStatus.SCRIPT);
        assertEquals(18, EcritumStatus.CALLBACK);
        assertEquals(19, EcritumStatus.TEARDOWN_FAILED);
        assertEquals(20, EcritumStatus.INTERNAL);
    }
}
