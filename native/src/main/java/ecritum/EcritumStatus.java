package ecritum;

public final class EcritumStatus {
    public static final int OK = 0;
    public static final int INVALID_ARGUMENT = 1;
    public static final int BUFFER_TOO_SMALL = 2;
    public static final int RUNTIME_UNAVAILABLE = 3;
    public static final int INVALID_HANDLE = 4;
    public static final int OUT_OF_MEMORY = 5;
    public static final int INVALID_UTF8 = 6;
    public static final int INPUT_TOO_LARGE = 7;
    public static final int INVALID_CONFIG = 8;
    public static final int UNSUPPORTED_CONFIG_VERSION = 9;
    public static final int CONTEXTS_ALIVE = 10;
    public static final int CLOSED = 11;
    public static final int BUSY = 12;
    public static final int REENTRANT_CALL = 13;
    public static final int PERMISSION_DENIED = 14;
    public static final int TIMEOUT = 15;
    public static final int CANCELLED = 16;
    public static final int SCRIPT = 17;
    public static final int CALLBACK = 18;
    public static final int TEARDOWN_FAILED = 19;
    public static final int INTERNAL = 20;
    public static final int ALREADY_EXISTS = 21;

    private EcritumStatus() {
    }
}
