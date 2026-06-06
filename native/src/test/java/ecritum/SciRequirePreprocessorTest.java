package ecritum;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;

final class SciRequirePreprocessorTest {
    @Test
    void rewritesSupportedLiteralRequireFormsAndAliases() {
        SciRequireRewrite rewrite = SciRequirePreprocessor.rewrite(
            "(do (require 'ecritum.time '[ecritum.json :as json]) (json/write-string {\"a\" 1}))"
        );

        assertTrue(rewrite.allowed());
        assertEquals("(do nil (json/write-string {\"a\" 1}))", rewrite.source());
        assertEquals(Map.of("json", "ecritum.json"), rewrite.aliases());
    }

    @Test
    void ignoresRequireTextInsideStringsAndComments() {
        String source = "(do \"(require 'clojure.java.io)\" ; (require 'clojure.java.io)\n 42)";

        SciRequireRewrite rewrite = SciRequirePreprocessor.rewrite(source);

        assertTrue(rewrite.allowed());
        assertEquals(source, rewrite.source());
        assertEquals(Map.of(), rewrite.aliases());
    }

    @Test
    void deniesUnsupportedRequireForms() {
        List<String> sources = List.of(
            "(require 'clojure.java.io)",
            "(require '[ecritum.json :refer [write-string]])",
            "(require '[ecritum.json :refer :all])",
            "(require '[ecritum.json :as java])",
            "(require '[ecritum.json :as clojure])",
            "(require (symbol \"ecritum.json\"))",
            "(require 'ecritum.json",
            "(require)"
        );

        for (String source : sources) {
            SciRequireRewrite rewrite = SciRequirePreprocessor.rewrite(source);
            assertEquals(false, rewrite.allowed(), source);
            assertEquals("", rewrite.source(), source);
            assertEquals(Map.of(), rewrite.aliases(), source);
        }
    }
}
