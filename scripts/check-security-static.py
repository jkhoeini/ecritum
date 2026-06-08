#!/usr/bin/env python3
import argparse
import json
import re
import sys
from pathlib import Path


DEFAULT_ROOTS = [
    "Package.swift",
    "justfile",
    ".mvn",
    "native/pom.xml",
    "native/src/core/java",
    "native/src/full/java",
    "native/src/main",
    "native/src/python-probe",
    "native/src/ruby-probe",
    "Sources",
    "scripts",
]

SCANNED_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".h",
    ".hpp",
    ".java",
    ".json",
    ".m",
    ".mm",
    ".properties",
    ".py",
    ".sh",
    ".swift",
    ".toml",
    ".xml",
    ".config",
}

SCANNED_NAMES = {"justfile", "pom.xml", "Package.swift", "jvm.config", "maven.config"}

EXCLUDED_PARTS = {
    ".build",
    ".git",
    ".jj",
    "build",
    "dist",
    "target",
    "__pycache__",
}

EXCLUDED_PATH_FRAGMENTS = {
    "Tests/Security/fixtures/negative-static",
    "scripts/check-security-static.py",
}

RULES = [
    ("polyglot.allow_all_access", r"\ballowAllAccess\s*\(\s*true\s*\)"),
    ("polyglot.host_access_all", r"\bHostAccess\s*\.\s*ALL\b"),
    ("polyglot.io_access_all", r"\bIOAccess\s*\.\s*ALL\b"),
    ("polyglot.allow_io_true", r"\ballowIO\s*\(\s*true\s*\)"),
    ("polyglot.native_access", r"\ballowNativeAccess\s*\(\s*true\s*\)"),
    ("polyglot.create_process", r"\ballowCreateProcess\s*\(\s*true\s*\)"),
    ("polyglot.create_thread", r"\ballowCreateThread\s*\(\s*true\s*\)"),
    ("polyglot.host_class_loading", r"\ballowHostClassLoading\s*\(\s*true\s*\)"),
    ("polyglot.host_class_lookup", r"\ballowHostClassLookup\s*\(\s*(?:[A-Za-z_$][\w$]*\s*->\s*true|\([^)]*\)\s*->\s*true|[A-Za-z_$][\w$]*::[A-Za-z_$][\w$]*)"),
    ("polyglot.polyglot_access_all", r"\bPolyglotAccess\s*\.\s*ALL\b"),
    ("polyglot.environment_inherit", r"\bEnvironmentAccess\s*\.\s*INHERIT\b"),
    ("polyglot.raw_option_passthrough", r"\.(?:option|options)\s*\("),
    ("java.type_lookup", r"\bJava\s*\.\s*(?:type|addToClasspath)\s*\("),
    ("java.class_for_name", r"\bClass\s*\.\s*forName\s*\("),
    ("java.class_loader", r"\bClassLoader\b"),
    ("native.system_load", r"\bSystem\s*\.\s*(?:load|loadLibrary)\s*\("),
    ("native.enable_native_access", r"--enable-native-access(?:=|\b)"),
    ("native.fallback_image", r"\b--fallback\b"),
    ("native.preserve_all", r"-H:Preserve=all\b"),
    ("native_image.reflect_all", r'"(?:allDeclaredMethods|allPublicMethods|allDeclaredFields|allPublicFields|allDeclaredConstructors|allPublicConstructors)"\s*:\s*true'),
    ("native_image.resource_wildcard", r'"pattern"\s*:\s*"\.\*"'),
    ("native_image.jni_all", r'"(?:allDeclaredMethods|allPublicMethods|allDeclaredFields|allPublicFields|allDeclaredConstructors|allPublicConstructors)"\s*:\s*true'),
    ("luaj.standard_globals", r"\b(?:JsePlatform|JmePlatform)\s*\.\s*standardGlobals\s*\("),
    ("luaj.jse_bridge", r"\borg\.luaj\.vm2\.lib\.jse\."),
    ("luaj.luajava_lib", r"\b(?:LuajavaLib|CoerceJavaToLua|CoerceLuaToJava)\b"),
    ("luaj.coroutine_lib", r"\bCoroutineLib\b"),
    ("luaj.luajc", r"\bLuaJC\b"),
]

FIXED_OPTION_ALLOWLIST = {
    '.option("ruby.platform-native", "false")',
    '.option("ruby.cexts", "false")',
    '.option("ruby.rubygems", "false")',
}

# Security-positive TruffleRuby options for the production Ruby evaluator
# (native/src/full/java/ecritum/RubyEvaluator.java). The first three are deny
# switches (no native platform, no C-extensions, no RubyGems). ruby.single-threaded
# enforces guest Thread.new denial ("threads not allowed in single-threaded mode");
# it is paired with allowCreateThread(true) (also allowlisted below, gated on
# single-threaded being present) purely so TruffleRuby can run its internal fibers
# for core operations (e.g. Array#pack). No guest concurrency is exposed. None of
# these options relax the sandbox; they all narrow the guest surface.
FULL_RUBY_OPTION_ALLOWLIST = {
    '.option("ruby.platform-native", "false")',
    '.option("ruby.cexts", "false")',
    '.option("ruby.rubygems", "false")',
    '.option("ruby.single-threaded", "true")',
}


class StaticCheckError(Exception):
    pass


def emit(payload):
    print(json.dumps(payload, indent=2, sort_keys=True))


def fail(message):
    print(message, file=sys.stderr)
    raise SystemExit(2)


def is_excluded(path):
    normalized = path.as_posix()
    if any(part in EXCLUDED_PARTS for part in path.parts):
        return True
    return any(fragment in normalized for fragment in EXCLUDED_PATH_FRAGMENTS)


def should_scan(path):
    return path.is_file() and (path.suffix in SCANNED_SUFFIXES or path.name in SCANNED_NAMES)


def iter_files(roots):
    for root in roots:
        path = Path(root)
        if not path.exists():
            continue
        if path.is_file():
            if should_scan(path) and not is_excluded(path):
                yield path
            continue
        for child in sorted(path.rglob("*")):
            if should_scan(child) and not is_excluded(child):
                yield child


def line_and_column(text, offset):
    line = text.count("\n", 0, offset) + 1
    line_start = text.rfind("\n", 0, offset)
    column = offset + 1 if line_start == -1 else offset - line_start
    return line, column


def is_ruby_probe_path(path):
    return "native/src/ruby-probe" in path.as_posix()


def is_full_ruby_evaluator_path(path):
    return path.as_posix().endswith("native/src/full/java/ecritum/RubyEvaluator.java")


def scan_file(path, compiled_rules):
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise StaticCheckError(f"cannot read {path}: {exc}") from exc

    violations = []
    for rule, pattern in compiled_rules:
        for match in pattern.finditer(text):
            # allowCreateThread(true) is permitted ONLY in the production Ruby
            # evaluator, and ONLY while ruby.single-threaded=true is also present
            # so the guest-visible thread surface stays denied (guest Thread.new
            # raises "threads not allowed in single-threaded mode"). It exists
            # solely so TruffleRuby can run its internal fibers; no concurrency is
            # exposed. If single-threaded is ever dropped, this exemption lapses.
            if (
                rule == "polyglot.create_thread"
                and is_full_ruby_evaluator_path(path)
                and '.option("ruby.single-threaded", "true")' in text
            ):
                continue
            if rule == "polyglot.raw_option_passthrough":
                line_start = text.rfind("\n", 0, match.start()) + 1
                line_end = text.find("\n", match.start())
                line = text[line_start:] if line_end == -1 else text[line_start:line_end]
                # Allow a trailing ';' so the last option in a builder chain
                # matches the same fixed allowlist entry as chained options.
                stripped_line = line.strip().rstrip(";")
                if is_ruby_probe_path(path) and stripped_line in FIXED_OPTION_ALLOWLIST:
                    continue
                if is_full_ruby_evaluator_path(path) and stripped_line in FULL_RUBY_OPTION_ALLOWLIST:
                    continue
            line, column = line_and_column(text, match.start())
            violations.append(
                {
                    "path": str(path),
                    "line": line,
                    "column": column,
                    "rule": rule,
                    "match": match.group(0),
                }
            )
    return violations


def main():
    parser = argparse.ArgumentParser(
        description="Check source and build metadata for forbidden Ecritum sandbox bypass settings.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "stdout: deterministic JSON summary.\n"
            "stderr: usage and read errors only.\n"
            "exit 0: no violations; exit 1: violations found; exit 2: checker error."
        ),
    )
    parser.add_argument("--root", action="append", dest="roots", help="Path to scan. May be repeated.")
    args = parser.parse_args()

    roots = args.roots if args.roots else DEFAULT_ROOTS
    compiled_rules = [(rule, re.compile(pattern)) for rule, pattern in RULES]

    try:
        files = list(dict.fromkeys(iter_files(roots)))
        violations = []
        for path in files:
            violations.extend(scan_file(path, compiled_rules))
    except StaticCheckError as exc:
        fail(str(exc))

    payload = {
        "ok": not violations,
        "violations": sorted(violations, key=lambda item: (item["path"], item["line"], item["column"], item["rule"])),
        "scanned": {
            "files": len(files),
            "roots": roots,
        },
    }
    emit(payload)
    return 0 if payload["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
