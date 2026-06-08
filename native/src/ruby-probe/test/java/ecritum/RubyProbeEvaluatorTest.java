package ecritum;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledIfSystemProperty;

@EnabledIfSystemProperty(named = "ecritum.rubyProbe", matches = "true")
final class RubyProbeEvaluatorTest {
    @Test
    void evaluatesBasicRubyValues() {
        assertEquals(42L, ok("40 + 2"));
        assertEquals("hello", ok("'hello'"));
        assertEquals(List.of(1L, "two", true), ok("[1, 'two', true]"));
    }

    @Test
    void reportsStructuredRubyErrors() {
        SciEvalResult result = RubyProbeEvaluator.evaluate("raise 'boom'", "ruby-error.rb");
        assertEquals(EcritumStatus.SCRIPT, result.status(), result.message());
        assertEquals("ruby", result.language());
        assertEquals("ruby-error.rb", result.sourceName());
        assertEquals("runtime", result.category());
        assertTrue(result.message().contains("ruby-error.rb"));
    }

    @Test
    void deniesAmbientEscapeHatches() {
        assertPermissionDenied("Java." + "type('java.lang.System')");
        assertPermissionDenied("Polyglot.eval('js', '40 + 2')");
        assertPermissionDenied("Polyglot::InnerContext.new { |context| context.eval('ruby', '1') }");
        assertPermissionDenied("require 'fiddle'");
        assertPermissionDenied("require 'socket'");
        assertPermissionDenied("require 'open3'");
        assertPermissionDenied("Kernel.system('true')");
        assertPermissionDenied("IO.popen('true')");
        assertPermissionDenied("`true`");
        assertPermissionDenied("ENV['HOME']");
        assertPermissionDenied("File.read('/etc/passwd')");
        assertPermissionDenied("Dir.entries('/tmp')");
        assertPermissionDenied("$LOAD_PATH << '/tmp'");
        assertPermissionDenied("Thread.new { 1 }");
        assertPermissionDenied("Ractor.new { 1 }");
    }

    @Test
    void cExtensionsAreDisabledOrDenied() {
        assertPermissionDenied("require 'openssl'");
        assertPermissionDenied("require 'rubygems'");
        assertPermissionDenied("require 'bundler'");
    }

    private Object ok(String source) {
        SciEvalResult result = RubyProbeEvaluator.evaluate(source, "ruby-probe.rb");
        assertEquals(EcritumStatus.OK, result.status(), result.message());
        assertEquals("ruby", result.language());
        return result.value();
    }

    private void assertPermissionDenied(String source) {
        SciEvalResult result = RubyProbeEvaluator.evaluate(source, "ruby-security.rb");
        assertEquals(EcritumStatus.PERMISSION_DENIED, result.status(), result.message());
        assertEquals("permission", result.category(), result.message());
        assertTrue(result.message().contains("ruby-security.rb"));
    }
}
