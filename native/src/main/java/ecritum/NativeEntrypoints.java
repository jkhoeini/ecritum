package ecritum;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import org.graalvm.nativeimage.IsolateThread;
import org.graalvm.nativeimage.PinnedObject;
import org.graalvm.nativeimage.StackValue;
import org.graalvm.nativeimage.c.function.CEntryPoint;
import org.graalvm.nativeimage.c.function.CFunctionPointer;
import org.graalvm.nativeimage.c.function.InvokeCFunctionPointer;
import org.graalvm.nativeimage.c.type.CCharPointer;
import org.graalvm.nativeimage.c.type.CLongPointer;
import org.graalvm.nativeimage.c.type.CTypeConversion;
import org.graalvm.word.UnsignedWord;
import org.graalvm.word.WordFactory;

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

    @CEntryPoint(name = "ecritum_graal_eval_clojure")
    public static int ecritumGraalEvalClojure(
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
            byte[] encoded = BackendResultCodec.encode(SciClojureEvaluator.evaluate(sourceText, sourceNameText));
            outBytesWritten.write(encoded.length);
            if (encoded.length > outBufferSize.rawValue()) {
                return EcritumStatus.BUFFER_TOO_SMALL;
            }
            for (int index = 0; index < encoded.length; index++) {
                outBuffer.write(index, encoded[index]);
            }
            return EcritumStatus.OK;
        } catch (Throwable ex) {
            SciEvalResult failure = SciEvalResult.internalError("clojure", "", "clojure backend failed");
            byte[] encoded = BackendResultCodec.encode(failure);
            outBytesWritten.write(encoded.length);
            if (encoded.length <= outBufferSize.rawValue()) {
                for (int index = 0; index < encoded.length; index++) {
                    outBuffer.write(index, encoded[index]);
                }
            }
            return EcritumStatus.OK;
        }
    }

    @CEntryPoint(name = "ecritum_graal_eval_clojure_with_host")
    public static int ecritumGraalEvalClojureWithHost(
        IsolateThread thread,
        CCharPointer source,
        UnsignedWord sourceSize,
        CCharPointer sourceName,
        UnsignedWord sourceNameSize,
        CCharPointer hostManifest,
        UnsignedWord hostManifestSize,
        HostCallCallback hostCallback,
        UnsignedWord hostRuntime,
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
            List<HostProjection> projections = hostManifest.isNull()
                ? List.of()
                : parseHostManifest(CTypeConversion.toJavaString(hostManifest, hostManifestSize, StandardCharsets.UTF_8));
            HostFunctionInvoker invoker = hostCallback.isNull()
                ? (namespace, function, arguments) -> {
                    throw new HostFunctionException(EcritumStatus.PERMISSION_DENIED, "permission", "host bridge is unavailable");
                }
                : new NativeHostFunctionInvoker(hostCallback, hostRuntime);
            byte[] encoded = BackendResultCodec.encode(SciClojureEvaluator.evaluate(sourceText, sourceNameText, projections, invoker));
            outBytesWritten.write(encoded.length);
            if (encoded.length > outBufferSize.rawValue()) {
                return EcritumStatus.BUFFER_TOO_SMALL;
            }
            for (int index = 0; index < encoded.length; index++) {
                outBuffer.write(index, encoded[index]);
            }
            return EcritumStatus.OK;
        } catch (Throwable ex) {
            SciEvalResult failure = SciEvalResult.internalError("clojure", "", "clojure backend failed");
            byte[] encoded = BackendResultCodec.encode(failure);
            outBytesWritten.write(encoded.length);
            if (encoded.length <= outBufferSize.rawValue()) {
                for (int index = 0; index < encoded.length; index++) {
                    outBuffer.write(index, encoded[index]);
                }
            }
            return EcritumStatus.OK;
        }
    }

    @CEntryPoint(name = "ecritum_graal_eval_clojure_with_stdlib")
    public static int ecritumGraalEvalClojureWithStdlib(
        IsolateThread thread,
        CCharPointer source,
        UnsignedWord sourceSize,
        CCharPointer sourceName,
        UnsignedWord sourceNameSize,
        CCharPointer hostManifest,
        UnsignedWord hostManifestSize,
        HostCallCallback hostCallback,
        UnsignedWord hostRuntime,
        CCharPointer standardLibraryManifest,
        UnsignedWord standardLibraryManifestSize,
        StandardLibraryCallback standardLibraryCallback,
        UnsignedWord standardLibraryContext,
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
            List<HostProjection> projections = hostManifest.isNull()
                ? List.of()
                : parseHostManifest(CTypeConversion.toJavaString(hostManifest, hostManifestSize, StandardCharsets.UTF_8));
            HostFunctionInvoker hostInvoker = hostCallback.isNull()
                ? (namespace, function, arguments) -> {
                    throw new HostFunctionException(EcritumStatus.PERMISSION_DENIED, "permission", "host bridge is unavailable");
                }
                : new NativeHostFunctionInvoker(hostCallback, hostRuntime);
            StandardLibraryPolicy policy = standardLibraryManifest.isNull()
                ? StandardLibraryPolicy.denied()
                : StandardLibraryPolicy.fromManifest(CTypeConversion.toJavaString(
                    standardLibraryManifest,
                    standardLibraryManifestSize,
                    StandardCharsets.UTF_8
                ));
            StandardLibraryBridge bridge = standardLibraryCallback.isNull()
                ? StandardLibraryBridge.denying()
                : new NativeStandardLibraryBridge(standardLibraryCallback, standardLibraryContext, sourceNameText);
            byte[] encoded = BackendResultCodec.encode(SciClojureEvaluator.evaluate(
                sourceText,
                sourceNameText,
                projections,
                hostInvoker,
                policy,
                bridge
            ));
            outBytesWritten.write(encoded.length);
            if (encoded.length > outBufferSize.rawValue()) {
                return EcritumStatus.BUFFER_TOO_SMALL;
            }
            for (int index = 0; index < encoded.length; index++) {
                outBuffer.write(index, encoded[index]);
            }
            return EcritumStatus.OK;
        } catch (Throwable ex) {
            SciEvalResult failure = SciEvalResult.internalError("clojure", "", "clojure backend failed");
            byte[] encoded = BackendResultCodec.encode(failure);
            outBytesWritten.write(encoded.length);
            if (encoded.length <= outBufferSize.rawValue()) {
                for (int index = 0; index < encoded.length; index++) {
                    outBuffer.write(index, encoded[index]);
                }
            }
            return EcritumStatus.OK;
        }
    }

    @CEntryPoint(name = "ecritum_graal_eval_javascript_with_stdlib")
    public static int ecritumGraalEvalJavaScriptWithStdlib(
        IsolateThread thread,
        CCharPointer source,
        UnsignedWord sourceSize,
        CCharPointer sourceName,
        UnsignedWord sourceNameSize,
        CCharPointer hostManifest,
        UnsignedWord hostManifestSize,
        HostCallCallback hostCallback,
        UnsignedWord hostRuntime,
        CCharPointer standardLibraryManifest,
        UnsignedWord standardLibraryManifestSize,
        StandardLibraryCallback standardLibraryCallback,
        UnsignedWord standardLibraryContext,
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
            List<HostProjection> projections = hostManifest.isNull()
                ? List.of()
                : parseHostManifest(CTypeConversion.toJavaString(hostManifest, hostManifestSize, StandardCharsets.UTF_8));
            HostFunctionInvoker hostInvoker = hostCallback.isNull()
                ? (namespace, function, arguments) -> {
                    throw new HostFunctionException(EcritumStatus.PERMISSION_DENIED, "permission", "host bridge is unavailable");
                }
                : new NativeHostFunctionInvoker(hostCallback, hostRuntime);
            StandardLibraryPolicy policy = standardLibraryManifest.isNull()
                ? StandardLibraryPolicy.denied()
                : StandardLibraryPolicy.fromManifest(CTypeConversion.toJavaString(
                    standardLibraryManifest,
                    standardLibraryManifestSize,
                    StandardCharsets.UTF_8
                ));
            StandardLibraryBridge bridge = standardLibraryCallback.isNull()
                ? StandardLibraryBridge.denying()
                : new NativeStandardLibraryBridge(standardLibraryCallback, standardLibraryContext, sourceNameText);
            byte[] encoded = BackendResultCodec.encode(JavaScriptEvaluator.evaluate(
                sourceText,
                sourceNameText,
                projections,
                hostInvoker,
                policy,
                bridge
            ));
            outBytesWritten.write(encoded.length);
            if (encoded.length > outBufferSize.rawValue()) {
                return EcritumStatus.BUFFER_TOO_SMALL;
            }
            for (int index = 0; index < encoded.length; index++) {
                outBuffer.write(index, encoded[index]);
            }
            return EcritumStatus.OK;
        } catch (Throwable ex) {
            SciEvalResult failure = SciEvalResult.internalError("javascript", "", "javascript backend failed");
            byte[] encoded = BackendResultCodec.encode(failure);
            outBytesWritten.write(encoded.length);
            if (encoded.length <= outBufferSize.rawValue()) {
                for (int index = 0; index < encoded.length; index++) {
                    outBuffer.write(index, encoded[index]);
                }
            }
            return EcritumStatus.OK;
        }
    }

    @CEntryPoint(name = "ecritum_graal_eval_lua_with_stdlib")
    public static int ecritumGraalEvalLuaWithStdlib(
        IsolateThread thread,
        CCharPointer source,
        UnsignedWord sourceSize,
        CCharPointer sourceName,
        UnsignedWord sourceNameSize,
        CCharPointer hostManifest,
        UnsignedWord hostManifestSize,
        HostCallCallback hostCallback,
        UnsignedWord hostRuntime,
        CCharPointer standardLibraryManifest,
        UnsignedWord standardLibraryManifestSize,
        StandardLibraryCallback standardLibraryCallback,
        UnsignedWord standardLibraryContext,
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
            List<HostProjection> projections = hostManifest.isNull()
                ? List.of()
                : parseHostManifest(CTypeConversion.toJavaString(hostManifest, hostManifestSize, StandardCharsets.UTF_8));
            HostFunctionInvoker hostInvoker = hostCallback.isNull()
                ? (namespace, function, arguments) -> {
                    throw new HostFunctionException(EcritumStatus.PERMISSION_DENIED, "permission", "host bridge is unavailable");
                }
                : new NativeHostFunctionInvoker(hostCallback, hostRuntime);
            StandardLibraryPolicy policy = standardLibraryManifest.isNull()
                ? StandardLibraryPolicy.denied()
                : StandardLibraryPolicy.fromManifest(CTypeConversion.toJavaString(
                    standardLibraryManifest,
                    standardLibraryManifestSize,
                    StandardCharsets.UTF_8
                ));
            StandardLibraryBridge bridge = standardLibraryCallback.isNull()
                ? StandardLibraryBridge.denying()
                : new NativeStandardLibraryBridge(standardLibraryCallback, standardLibraryContext, sourceNameText);
            byte[] encoded = BackendResultCodec.encode(LuaEvaluator.evaluate(
                sourceText,
                sourceNameText,
                projections,
                hostInvoker,
                policy,
                bridge
            ));
            outBytesWritten.write(encoded.length);
            if (encoded.length > outBufferSize.rawValue()) {
                return EcritumStatus.BUFFER_TOO_SMALL;
            }
            for (int index = 0; index < encoded.length; index++) {
                outBuffer.write(index, encoded[index]);
            }
            return EcritumStatus.OK;
        } catch (Throwable ex) {
            SciEvalResult failure = SciEvalResult.internalError("lua", "", "lua backend failed");
            byte[] encoded = BackendResultCodec.encode(failure);
            outBytesWritten.write(encoded.length);
            if (encoded.length <= outBufferSize.rawValue()) {
                for (int index = 0; index < encoded.length; index++) {
                    outBuffer.write(index, encoded[index]);
                }
            }
            return EcritumStatus.OK;
        }
    }

    static List<HostProjection> parseHostManifest(String manifest) {
        if (manifest == null || manifest.isBlank()) {
            return List.of();
        }
        ArrayList<HostProjection> projections = new ArrayList<>();
        for (String line : manifest.split("\\n")) {
            if (line.isBlank()) {
                continue;
            }
            int separator = line.indexOf('/');
            if (separator <= 0 || separator == line.length() - 1) {
                throw new IllegalArgumentException("invalid host projection manifest entry");
            }
            projections.add(new HostProjection(line.substring(0, separator), line.substring(separator + 1)));
        }
        return List.copyOf(projections);
    }

    interface HostCallCallback extends CFunctionPointer {
        @InvokeCFunctionPointer
        int invoke(
            UnsignedWord runtime,
            CCharPointer namespaceName,
            UnsignedWord namespaceNameSize,
            CCharPointer functionName,
            UnsignedWord functionNameSize,
            CCharPointer arguments,
            UnsignedWord argumentsSize,
            CCharPointer outBuffer,
            UnsignedWord outBufferSize,
            CLongPointer outBytesWritten
        );
    }

    interface StandardLibraryCallback extends CFunctionPointer {
        @InvokeCFunctionPointer
        int invoke(
            UnsignedWord context,
            CCharPointer operationName,
            UnsignedWord operationNameSize,
            CCharPointer sourceName,
            UnsignedWord sourceNameSize,
            CCharPointer arguments,
            UnsignedWord argumentsSize,
            CCharPointer outBuffer,
            UnsignedWord outBufferSize,
            CLongPointer outBytesWritten
        );
    }

    private static final class NativeHostFunctionInvoker implements HostFunctionInvoker {
        private static final int HOST_RESULT_BUFFER_BYTES = 65536;
        private final HostCallCallback callback;
        private final UnsignedWord runtime;

        NativeHostFunctionInvoker(HostCallCallback callback, UnsignedWord runtime) {
            this.callback = callback;
            this.runtime = runtime;
        }

        @Override
        public Object invoke(String namespace, String function, List<Object> arguments) {
            byte[] argumentPayload = BackendResultCodec.encode(SciEvalResult.ok(List.copyOf(arguments)));
            byte[] resultPayload = new byte[HOST_RESULT_BUFFER_BYTES];
            CLongPointer bytesWritten = StackValue.get(CLongPointer.class);
            bytesWritten.write(0L);
            try (CTypeConversion.CCharPointerHolder namespaceHolder = CTypeConversion.toCString(namespace);
                 CTypeConversion.CCharPointerHolder functionHolder = CTypeConversion.toCString(function);
                 PinnedObject argumentPins = PinnedObject.create(argumentPayload);
                 PinnedObject resultPins = PinnedObject.create(resultPayload)) {
                int status = callback.invoke(
                    runtime,
                    namespaceHolder.get(),
                    WordFactory.unsigned(namespace.getBytes(StandardCharsets.UTF_8).length),
                    functionHolder.get(),
                    WordFactory.unsigned(function.getBytes(StandardCharsets.UTF_8).length),
                    argumentPins.addressOfArrayElement(0),
                    WordFactory.unsigned(argumentPayload.length),
                    resultPins.addressOfArrayElement(0),
                    WordFactory.unsigned(resultPayload.length),
                    bytesWritten
                );
                if (status != EcritumStatus.OK) {
                    throw new HostFunctionException(status, "callback", "host callback bridge failed");
                }
                long rawLength = bytesWritten.read();
                if (rawLength <= 0 || rawLength > resultPayload.length) {
                    throw new HostFunctionException(EcritumStatus.CALLBACK, "callback", "host callback returned invalid result");
                }
                SciEvalResult result = BackendResultCodec.decode(Arrays.copyOf(resultPayload, (int)rawLength));
                if (result.status() != EcritumStatus.OK) {
                    throw new HostFunctionException(result.status(), result.category(), result.message());
                }
                return result.value();
            }
        }
    }

    private static final class NativeStandardLibraryBridge implements StandardLibraryBridge {
        private static final int RESULT_BUFFER_BYTES = 65536;
        private final StandardLibraryCallback callback;
        private final UnsignedWord context;
        private final String sourceName;

        NativeStandardLibraryBridge(StandardLibraryCallback callback, UnsignedWord context, String sourceName) {
            this.callback = callback;
            this.context = context;
            this.sourceName = sourceName == null ? "" : sourceName;
        }

        @Override
        public Object invoke(String operation, List<Object> arguments) {
            byte[] argumentPayload = BackendResultCodec.encode(SciEvalResult.ok(List.copyOf(arguments)));
            byte[] resultPayload = new byte[RESULT_BUFFER_BYTES];
            CLongPointer bytesWritten = StackValue.get(CLongPointer.class);
            bytesWritten.write(0L);
            try (CTypeConversion.CCharPointerHolder operationHolder = CTypeConversion.toCString(operation);
                 CTypeConversion.CCharPointerHolder sourceNameHolder = CTypeConversion.toCString(sourceName);
                 PinnedObject argumentPins = PinnedObject.create(argumentPayload);
                 PinnedObject resultPins = PinnedObject.create(resultPayload)) {
                int status = callback.invoke(
                    context,
                    operationHolder.get(),
                    WordFactory.unsigned(operation.getBytes(StandardCharsets.UTF_8).length),
                    sourceNameHolder.get(),
                    WordFactory.unsigned(sourceName.getBytes(StandardCharsets.UTF_8).length),
                    argumentPins.addressOfArrayElement(0),
                    WordFactory.unsigned(argumentPayload.length),
                    resultPins.addressOfArrayElement(0),
                    WordFactory.unsigned(resultPayload.length),
                    bytesWritten
                );
                if (status != EcritumStatus.OK) {
                    throw StandardLibraryException.internalError("standard-library bridge failed");
                }
                long rawLength = bytesWritten.read();
                if (rawLength <= 0 || rawLength > resultPayload.length) {
                    throw StandardLibraryException.internalError("standard-library bridge returned invalid result");
                }
                SciEvalResult result = BackendResultCodec.decode(Arrays.copyOf(resultPayload, (int)rawLength));
                if (result.status() != EcritumStatus.OK) {
                    throw StandardLibraryException.bridgeResult(result.status(), result.category(), result.message());
                }
                return result.value();
            }
        }
    }
}
