package ecritum;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

final class SciRequirePreprocessor {
    private static final List<String> ALLOWED_NAMESPACES = List.of(
        "ecritum.json",
        "ecritum.time",
        "ecritum.fs",
        "ecritum.http"
    );

    private SciRequirePreprocessor() {
    }

    static SciRequireRewrite rewrite(String source) {
        if (!source.contains("(require")) {
            return new SciRequireRewrite(source, Map.of(), true);
        }

        StringBuilder rewritten = new StringBuilder(source.length());
        LinkedHashMap<String, String> aliases = new LinkedHashMap<>();
        int index = 0;
        boolean inString = false;
        boolean escaped = false;
        boolean inComment = false;
        boolean foundRequire = false;
        while (index < source.length()) {
            char ch = source.charAt(index);
            if (inString) {
                rewritten.append(ch);
                if (escaped) {
                    escaped = false;
                } else if (ch == '\\') {
                    escaped = true;
                } else if (ch == '"') {
                    inString = false;
                }
                index++;
            } else if (inComment) {
                rewritten.append(ch);
                if (ch == '\n' || ch == '\r') {
                    inComment = false;
                }
                index++;
            } else if (ch == '"') {
                rewritten.append(ch);
                inString = true;
                index++;
            } else if (ch == ';') {
                rewritten.append(ch);
                inComment = true;
                index++;
            } else if (startsRequireForm(source, index)) {
                int end = findFormEnd(source, index);
                if (end < 0) {
                    return SciRequireRewrite.denied();
                }
                if (!parseRequireForm(source.substring(index, end + 1), aliases)) {
                    return SciRequireRewrite.denied();
                }
                rewritten.append("nil");
                index = end + 1;
                foundRequire = true;
            } else {
                rewritten.append(ch);
                index++;
            }
        }

        if (!foundRequire) {
            return new SciRequireRewrite(source, Map.of(), true);
        }
        return new SciRequireRewrite(rewritten.toString(), Map.copyOf(aliases), true);
    }

    private static boolean startsRequireForm(String source, int index) {
        String marker = "(require";
        if (!source.startsWith(marker, index)) {
            return false;
        }
        int next = index + marker.length();
        return next < source.length() && (Character.isWhitespace(source.charAt(next)) || source.charAt(next) == ')');
    }

    private static int findFormEnd(String source, int start) {
        int depth = 0;
        boolean inString = false;
        boolean escaped = false;
        for (int index = start; index < source.length(); index++) {
            char ch = source.charAt(index);
            if (inString) {
                if (escaped) {
                    escaped = false;
                } else if (ch == '\\') {
                    escaped = true;
                } else if (ch == '"') {
                    inString = false;
                }
                continue;
            }
            if (ch == '"') {
                inString = true;
            } else if (ch == '(') {
                depth++;
            } else if (ch == ')') {
                depth--;
                if (depth == 0) {
                    return index;
                }
                if (depth < 0) {
                    return -1;
                }
            }
        }
        return -1;
    }

    private static boolean parseRequireForm(String form, Map<String, String> aliases) {
        String body = form.substring("(require".length(), form.length() - 1).trim();
        List<String> clauses = splitClauses(body);
        if (clauses.isEmpty()) {
            return false;
        }
        for (String clause : clauses) {
            if (!parseRequireClause(clause, aliases)) {
                return false;
            }
        }
        return true;
    }

    private static List<String> splitClauses(String body) {
        ArrayList<String> clauses = new ArrayList<>();
        StringBuilder current = new StringBuilder();
        int bracketDepth = 0;
        for (int index = 0; index < body.length(); index++) {
            char ch = body.charAt(index);
            if (ch == '"' || ch == '(' || ch == ')' || ch == ';') {
                return List.of();
            }
            if (ch == '[') {
                bracketDepth++;
            } else if (ch == ']') {
                bracketDepth--;
                if (bracketDepth < 0) {
                    return List.of();
                }
            }
            if (Character.isWhitespace(ch) && bracketDepth == 0) {
                if (!current.isEmpty()) {
                    clauses.add(current.toString());
                    current.setLength(0);
                }
            } else {
                current.append(ch);
            }
        }
        if (bracketDepth != 0) {
            return List.of();
        }
        if (!current.isEmpty()) {
            clauses.add(current.toString());
        }
        return clauses;
    }

    private static boolean parseRequireClause(String clause, Map<String, String> aliases) {
        if (clause.startsWith("'[") && clause.endsWith("]")) {
            String[] tokens = clause.substring(2, clause.length() - 1).trim().split("\\s+");
            if (tokens.length != 3 || !":as".equals(tokens[1])) {
                return false;
            }
            if (!ALLOWED_NAMESPACES.contains(tokens[0]) || !isAlias(tokens[2])) {
                return false;
            }
            aliases.put(tokens[2], tokens[0]);
            return true;
        }
        if (!clause.startsWith("'")) {
            return false;
        }
        return ALLOWED_NAMESPACES.contains(clause.substring(1));
    }

    private static boolean isAlias(String alias) {
        if (!alias.matches("[A-Za-z][A-Za-z0-9_-]*")) {
            return false;
        }
        String lower = alias.toLowerCase();
        return !List.of("ecritum", "java", "javax", "sun", "clojure", "graal", "truffle", "sci").contains(lower);
    }
}
