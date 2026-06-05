package ecritum;

import java.util.List;

record HostProjection(String namespace, String function) {
}

interface HostFunctionInvoker {
    Object invoke(String namespace, String function, List<Object> arguments);
}

final class HostFunctionException extends RuntimeException {
    private final int status;
    private final String category;

    HostFunctionException(int status, String category, String message) {
        super(message);
        this.status = status;
        this.category = category;
    }

    int status() {
        return status;
    }

    String category() {
        return category;
    }
}
