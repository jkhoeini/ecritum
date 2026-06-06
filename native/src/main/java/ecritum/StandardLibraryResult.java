package ecritum;

record StandardLibraryResult(int status, String category, String message, Object value) {
    static StandardLibraryResult success(Object value) {
        return new StandardLibraryResult(EcritumStatus.OK, "", "", value);
    }

    static StandardLibraryResult failure(int status, String category, String message) {
        if (status == EcritumStatus.OK) {
            throw new IllegalArgumentException("successful standard-library result must use success");
        }
        return new StandardLibraryResult(status, category, message, null);
    }

    Object valueOrThrow() {
        if (status == EcritumStatus.OK) {
            return value;
        }
        throw StandardLibraryException.bridgeResult(status, category, message);
    }
}
