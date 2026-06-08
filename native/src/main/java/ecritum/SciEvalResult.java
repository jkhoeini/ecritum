package ecritum;

record SciEvalResult(
    int status,
    Object value,
    String language,
    String sourceName,
    String category,
    String message
) {
    static SciEvalResult ok(Object value) {
        return new SciEvalResult(EcritumStatus.OK, value, "", "", "", "");
    }

    static SciEvalResult ok(String language, Object value) {
        return new SciEvalResult(EcritumStatus.OK, value, language, "", "", "");
    }

    static SciEvalResult scriptError(String language, String sourceName, String category, String message) {
        return new SciEvalResult(EcritumStatus.SCRIPT, null, language, sourceName, category, message);
    }

    static SciEvalResult internalError(String language, String sourceName, String message) {
        return new SciEvalResult(EcritumStatus.INTERNAL, null, language, sourceName, "internal", message);
    }
}
