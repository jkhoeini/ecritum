package ecritum;

final class StandardLibraryException extends RuntimeException {
    private final int status;
    private final String category;

    StandardLibraryException(int status, String category, String message) {
        super(message);
        this.status = status;
        this.category = category;
    }

    static StandardLibraryException permissionDenied(String message) {
        return new StandardLibraryException(EcritumStatus.PERMISSION_DENIED, "permission", message);
    }

    static StandardLibraryException scriptError(String message) {
        return new StandardLibraryException(EcritumStatus.SCRIPT, "runtime", message);
    }

    static StandardLibraryException internalError(String message) {
        return new StandardLibraryException(EcritumStatus.INTERNAL, "internal", message);
    }

    static StandardLibraryException bridgeResult(int status, String category, String message) {
        String safeCategory = category == null || category.isBlank() ? "internal" : category;
        return new StandardLibraryException(status, safeCategory, message);
    }

    int status() {
        return status;
    }

    String category() {
        return category;
    }
}
