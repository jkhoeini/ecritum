package ecritum;

import java.nio.charset.StandardCharsets;
import org.graalvm.nativeimage.IsolateThread;
import org.graalvm.nativeimage.c.function.CEntryPoint;
import org.graalvm.nativeimage.c.type.CCharPointer;
import org.graalvm.nativeimage.c.type.CLongPointer;
import org.graalvm.nativeimage.c.type.CTypeConversion;
import org.graalvm.word.UnsignedWord;

public final class PythonProbeEntrypoints {
    private PythonProbeEntrypoints() {
    }

    public static void main(String[] args) {
        // Native Image shared-library probe builds need an application class root.
    }

    @CEntryPoint(name = "ecritum_graal_eval_python_probe")
    public static int ecritumGraalEvalPythonProbe(
        IsolateThread thread,
        CCharPointer source,
        UnsignedWord sourceSize,
        CCharPointer sourceName,
        UnsignedWord sourceNameSize,
        CCharPointer outBuffer,
        UnsignedWord outBufferSize,
        CLongPointer outBytesWritten
    ) {
        if (source.isNull() || outBuffer.isNull() || outBytesWritten.isNull()) {
            return EcritumStatus.INVALID_ARGUMENT;
        }

        try {
            String sourceText = CTypeConversion.toJavaString(source, sourceSize, StandardCharsets.UTF_8);
            String sourceNameText = sourceName.isNull()
                ? ""
                : CTypeConversion.toJavaString(sourceName, sourceNameSize, StandardCharsets.UTF_8);
            return writeResult(
                BackendResultCodec.encode(PythonEvaluator.evaluate(sourceText, sourceNameText)),
                outBuffer,
                outBufferSize,
                outBytesWritten
            );
        } catch (Throwable ex) {
            SciEvalResult failure = SciEvalResult.internalError("python", "", "python backend failed");
            return writeResult(BackendResultCodec.encode(failure), outBuffer, outBufferSize, outBytesWritten);
        }
    }

    private static int writeResult(
        byte[] encoded,
        CCharPointer outBuffer,
        UnsignedWord outBufferSize,
        CLongPointer outBytesWritten
    ) {
        outBytesWritten.write(encoded.length);
        if (encoded.length > outBufferSize.rawValue()) {
            return EcritumStatus.BUFFER_TOO_SMALL;
        }
        for (int index = 0; index < encoded.length; index++) {
            outBuffer.write(index, encoded[index]);
        }
        return EcritumStatus.OK;
    }
}
