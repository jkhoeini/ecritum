package ecritum;

import java.nio.charset.StandardCharsets;
import org.graalvm.nativeimage.IsolateThread;
import org.graalvm.nativeimage.c.function.CEntryPoint;
import org.graalvm.nativeimage.c.type.CCharPointer;
import org.graalvm.nativeimage.c.type.CTypeConversion;
import org.graalvm.word.UnsignedWord;

public final class NativeEntrypoints {
    public static final String VERSION = "0.1.0-dev";
    private static final byte[] VERSION_BYTES = VERSION.getBytes(StandardCharsets.UTF_8);

    private NativeEntrypoints() {
    }

    public static void main(String[] args) {
        // Native Image shared-library builds need an application class root.
    }

    public static String versionForTests() {
        return VERSION;
    }

    static int versionBufferStatus(long bufferSize) {
        if (bufferSize <= 0) {
            return EcritumStatus.INVALID_ARGUMENT;
        }

        long requiredSize = VERSION_BYTES.length + 1L;
        if (bufferSize < requiredSize) {
            return EcritumStatus.BUFFER_TOO_SMALL;
        }

        return EcritumStatus.OK;
    }

    @CEntryPoint(name = "ecritum_graal_version")
    public static int ecritumGraalVersion(IsolateThread thread, CCharPointer buffer, UnsignedWord bufferSize) {
        if (buffer.isNull()) {
            return EcritumStatus.INVALID_ARGUMENT;
        }

        int bufferStatus = versionBufferStatus(bufferSize.rawValue());
        if (bufferStatus != EcritumStatus.OK) {
            return bufferStatus;
        }

        try {
            CTypeConversion.toCString(VERSION, StandardCharsets.UTF_8, buffer, bufferSize);
            return EcritumStatus.OK;
        } catch (Throwable ex) {
            return EcritumStatus.RUNTIME_UNAVAILABLE;
        }
    }
}
