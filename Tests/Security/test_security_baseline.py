import importlib.util
import json
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
STATIC_CHECK = ROOT / "scripts" / "check-security-static.py"
ABUSE_RUNNER = ROOT / "scripts" / "run-security-abuse.py"
PARSER_CHECK = ROOT / "scripts" / "check-parser-abuse.py"
ABUSE_MANIFEST = ROOT / "Tests" / "Security" / "abuse-manifest.json"
PARSER_MANIFEST = ROOT / "Tests" / "Security" / "parser-abuse-manifest.json"
ABUSE_PROVIDER = ROOT / "Tests" / "Security" / "fixtures" / "abuse_provider.py"
RUBY_ABUSE_PROVIDER = ROOT / "Tests" / "Security" / "fixtures" / "ruby_abuse_provider.py"


REQUIRED_ABUSE_CAPABILITIES = {
    "filesystem",
    "network",
    "process",
    "environment",
    "reflection",
    "class_loading",
    "native_loading",
    "unrestricted_java_lookup",
    "raw_polyglot_access",
    "raw_host_object_access",
    "raw_c_handle_access",
    "classpath_mutation",
}

REQUIRED_ABUSE_PHASES = {
    "normal_eval",
    "timeout",
    "cancellation",
    "callback_error",
    "cleanup",
}

REQUIRED_TARGETED_ABUSE_CASES = {
    "filesystem.allowed_root.inside",
    "filesystem.allowed_root.outside_denied",
    "network.redirect_to_denied_host",
    "host.callback_scope.use_after_return",
}

REQUIRED_PARSER_SURFACES = {
    "config",
    "eval_options",
    "source",
    "value",
    "error",
    "callback",
    "handle",
}

REQUIRED_PARSER_VECTORS = {
    "invalid_utf8",
    "oversized_input",
    "duplicate_keys",
    "bad_paths",
    "deep_nesting",
    "large_arrays",
    "nul_bytes",
    "stale_handle",
    "wrong_handle_kind",
    "double_destroy",
    "use_after_destroy",
    "concurrent_calls",
}


def run_script(script, *args):
    return subprocess.run(
        [sys.executable, str(script), *args],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


class SecurityBaselineTests(unittest.TestCase):
    def test_static_check_passes_current_source_roots(self):
        result = run_script(STATIC_CHECK)

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["violations"], [])
        self.assertGreater(payload["scanned"]["files"], 0)
        self.assertIn("native/src/core/java", payload["scanned"]["roots"])
        self.assertIn("native/src/full/java", payload["scanned"]["roots"])
        self.assertIn("native/src/python-probe", payload["scanned"]["roots"])
        self.assertIn("native/src/ruby-probe", payload["scanned"]["roots"])

    def test_static_check_detects_forbidden_polyglot_pattern(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Bad.java"
            path.write_text(
                "class Bad { void bad() { builder.allowAllAccess(true); } }\n",
                encoding="utf-8",
            )

            result = run_script(STATIC_CHECK, "--root", directory)

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["ok"])
        self.assertEqual(payload["violations"][0]["rule"], "polyglot.allow_all_access")

    def test_static_check_detects_host_lookup_and_native_image_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Bad.java").write_text(
                "class Bad { void bad() { builder.allowHostClassLookup(name -> true); } }\n",
                encoding="utf-8",
            )
            (root / "reflect-config.json").write_text(
                json.dumps([{"name": "java.lang.ProcessBuilder", "allDeclaredMethods": True}]),
                encoding="utf-8",
            )
            (root / "resource-config.json").write_text(
                json.dumps({"resources": {"includes": [{"pattern": ".*"}]}}),
                encoding="utf-8",
            )

            result = run_script(STATIC_CHECK, "--root", directory)

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        rules = {violation["rule"] for violation in payload["violations"]}
        self.assertIn("polyglot.host_class_lookup", rules)
        self.assertIn("native_image.reflect_all", rules)
        self.assertIn("native_image.resource_wildcard", rules)

    def test_static_check_allows_explicit_host_lookup_deny_predicate(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Good.java"
            path.write_text(
                "class Good { void good() { builder.allowHostClassLookup(name -> false); } }\n",
                encoding="utf-8",
            )

            result = run_script(STATIC_CHECK, "--root", directory)

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["ok"])

    def test_static_check_allows_fixed_ruby_probe_options_only_under_ruby_probe(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "native" / "src" / "ruby-probe" / "java" / "Good.java"
            path.parent.mkdir(parents=True)
            path.write_text(
                textwrap.dedent(
                    """
                    class Good {
                        void good() {
                            builder
                                .option("ruby.platform-native", "false")
                                .option("ruby.cexts", "false")
                                .option("ruby.rubygems", "false")
                                .build();
                        }
                    }
                    """
                ),
                encoding="utf-8",
            )

            result = run_script(STATIC_CHECK, "--root", directory)

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["ok"])

    def test_static_check_rejects_fixed_ruby_probe_options_outside_ruby_probe(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "native" / "src" / "full" / "java" / "Bad.java"
            path.parent.mkdir(parents=True)
            path.write_text(
                'class Bad { void bad() { builder.option("ruby.platform-native", "false"); } }\n',
                encoding="utf-8",
            )

            result = run_script(STATIC_CHECK, "--root", directory)

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertEqual(payload["violations"][0]["rule"], "polyglot.raw_option_passthrough")

    def test_static_check_rejects_unlisted_options_inside_ruby_probe(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "native" / "src" / "ruby-probe" / "java" / "Bad.java"
            path.parent.mkdir(parents=True)
            path.write_text(
                'class Bad { void bad() { builder.option("ruby.allow-all", "true"); } }\n',
                encoding="utf-8",
            )

            result = run_script(STATIC_CHECK, "--root", directory)

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertEqual(payload["violations"][0]["rule"], "polyglot.raw_option_passthrough")

    def test_static_check_scans_maven_config_files(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / ".mvn" / "maven.config"
            path.parent.mkdir()
            path.write_text("--enable-native-access=ALL-UNNAMED\n", encoding="utf-8")

            result = run_script(STATIC_CHECK, "--root", directory)

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertEqual(payload["violations"][0]["rule"], "native.enable_native_access")

    def test_static_check_ignores_named_negative_fixture_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Tests" / "Security" / "fixtures" / "negative-static" / "Bad.java"
            path.parent.mkdir(parents=True)
            path.write_text("HostAccess.ALL\n", encoding="utf-8")

            result = run_script(STATIC_CHECK, "--root", directory)

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["ok"])

    def test_abuse_manifest_covers_required_matrix(self):
        result = run_script(ABUSE_RUNNER, "--manifest", str(ABUSE_MANIFEST), "--list")

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        cases = payload["cases"]
        matrix = {(case["capability"], case["phase"]) for case in cases if case.get("phase")}
        for capability in REQUIRED_ABUSE_CAPABILITIES:
            for phase in REQUIRED_ABUSE_PHASES:
                self.assertIn((capability, phase), matrix)
        self.assertTrue(REQUIRED_TARGETED_ABUSE_CASES.issubset({case["caseId"] for case in cases}))

    def test_abuse_normal_mode_allows_pending_but_is_not_conformant(self):
        result = run_script(
            ABUSE_RUNNER,
            "--manifest",
            str(ABUSE_MANIFEST),
            "--provider",
            sys.executable,
            str(ABUSE_PROVIDER),
            "--mode",
            "baseline",
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["ok"])
        self.assertFalse(payload["securityConformant"])
        self.assertGreater(payload["summary"]["pending"], 0)

    def test_abuse_strict_mode_fails_with_pending_required_cases(self):
        result = run_script(
            ABUSE_RUNNER,
            "--manifest",
            str(ABUSE_MANIFEST),
            "--strict",
            "--provider",
            sys.executable,
            str(ABUSE_PROVIDER),
            "--mode",
            "baseline",
        )

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["ok"])
        self.assertFalse(payload["securityConformant"])

    def test_abuse_runner_rejects_pass_without_provider_capability(self):
        with tempfile.TemporaryDirectory() as directory:
            provider = Path(directory) / "lying_provider.py"
            provider.write_text(
                textwrap.dedent(
                    """
                    import json
                    import sys

                    request = json.load(sys.stdin)
                    print(json.dumps({
                        "protocolVersion": 1,
                        "provider": {"id": "lying", "capabilities": []},
                        "results": [
                            {"caseId": case["caseId"], "status": "pass", "actual": case["expected"]}
                            for case in request["cases"]
                        ],
                    }))
                    """
                ),
                encoding="utf-8",
            )

            result = run_script(
                ABUSE_RUNNER,
                "--manifest",
                str(ABUSE_MANIFEST),
                "--provider",
                sys.executable,
                str(provider),
            )

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["ok"])
        self.assertFalse(payload["securityConformant"])
        self.assertGreater(payload["summary"]["failed"], 0)
        self.assertIn("without declaring required capabilities", payload["cases"][0]["reason"])

    def test_abuse_runner_validates_pass_actual_status(self):
        with tempfile.TemporaryDirectory() as directory:
            provider = Path(directory) / "mismatch_provider.py"
            provider.write_text(
                textwrap.dedent(
                    f"""
                    import json
                    import sys

                    request = json.load(sys.stdin)
                    print(json.dumps({{
                        "protocolVersion": 1,
                        "provider": {{"id": "mismatch", "capabilities": {json.dumps(sorted(REQUIRED_ABUSE_CAPABILITIES))}}},
                        "results": [
                            {{"caseId": case["caseId"], "status": "pass", "actual": {{"status": "ECRITUM_OK"}}}}
                            for case in request["cases"]
                        ],
                    }}))
                    """
                ),
                encoding="utf-8",
            )

            result = run_script(
                ABUSE_RUNNER,
                "--manifest",
                str(ABUSE_MANIFEST),
                "--provider",
                sys.executable,
                str(provider),
            )

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["ok"])
        self.assertGreater(payload["summary"]["failed"], 0)
        self.assertIn("actual.status does not match expected", {case["reason"] for case in payload["cases"]})

    def test_abuse_runner_rejects_pass_without_actual_status(self):
        with tempfile.TemporaryDirectory() as directory:
            provider = Path(directory) / "missing_actual_provider.py"
            provider.write_text(
                textwrap.dedent(
                    f"""
                    import json
                    import sys

                    request = json.load(sys.stdin)
                    print(json.dumps({{
                        "protocolVersion": 1,
                        "provider": {{"id": "missing-actual", "capabilities": {json.dumps(sorted(REQUIRED_ABUSE_CAPABILITIES))}}},
                        "results": [
                            {{"caseId": case["caseId"], "status": "pass"}}
                            for case in request["cases"]
                        ],
                    }}))
                    """
                ),
                encoding="utf-8",
            )

            result = run_script(
                ABUSE_RUNNER,
                "--manifest",
                str(ABUSE_MANIFEST),
                "--provider",
                sys.executable,
                str(provider),
            )

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["ok"])
        self.assertIn("missing actual.status", {case["reason"] for case in payload["cases"]})

    def test_abuse_runner_rejects_pending_when_provider_declares_capability(self):
        with tempfile.TemporaryDirectory() as directory:
            provider = Path(directory) / "declared_pending_provider.py"
            provider.write_text(
                textwrap.dedent(
                    f"""
                    import json
                    import sys

                    request = json.load(sys.stdin)
                    print(json.dumps({{
                        "protocolVersion": 1,
                        "provider": {{"id": "declared-pending", "capabilities": {json.dumps(sorted(REQUIRED_ABUSE_CAPABILITIES))}}},
                        "results": [
                            {{"caseId": case["caseId"], "status": "pending_capability", "reason": "not yet"}}
                            for case in request["cases"]
                        ],
                    }}))
                    """
                ),
                encoding="utf-8",
            )

            result = run_script(
                ABUSE_RUNNER,
                "--manifest",
                str(ABUSE_MANIFEST),
                "--provider",
                sys.executable,
                str(provider),
            )

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["ok"])
        self.assertIn(
            "provider declares required capabilities but returned pending_capability",
            {case["reason"] for case in payload["cases"]},
        )

    def test_abuse_manifest_requires_required_matrix(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "abuse.json"
            manifest = json.loads(ABUSE_MANIFEST.read_text(encoding="utf-8"))
            manifest["matrix"]["capabilities"].remove("filesystem")
            path.write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )

            result = run_script(ABUSE_RUNNER, "--manifest", str(path), "--validate-manifest")

        self.assertEqual(result.returncode, 2)
        self.assertIn("missing required abuse case", result.stderr)

    def test_abuse_manifest_validate_manifest_passes_real_manifest(self):
        result = run_script(ABUSE_RUNNER, "--manifest", str(ABUSE_MANIFEST), "--validate-manifest")

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["cases"], 68)

    def test_abuse_manifest_requires_targeted_cases_to_stay_required(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "abuse.json"
            manifest = json.loads(ABUSE_MANIFEST.read_text(encoding="utf-8"))
            manifest["targetedCases"][0]["required"] = False
            path.write_text(json.dumps(manifest), encoding="utf-8")

            result = run_script(ABUSE_RUNNER, "--manifest", str(path), "--validate-manifest")

        self.assertEqual(result.returncode, 2)
        self.assertIn("required targeted abuse case", result.stderr)

    def test_parser_abuse_manifest_records_required_vectors(self):
        result = run_script(PARSER_CHECK, "--manifest", str(PARSER_MANIFEST), "--list")

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        cases = payload["cases"]
        self.assertTrue(REQUIRED_PARSER_SURFACES.issubset({case["surface"] for case in cases}))
        self.assertTrue(REQUIRED_PARSER_VECTORS.issubset({case["vector"] for case in cases}))

    def test_parser_abuse_normal_mode_passes_with_blocked_surfaces_visible(self):
        result = run_script(PARSER_CHECK, "--manifest", str(PARSER_MANIFEST))

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["ok"])
        self.assertFalse(payload["parserAbuseComplete"])
        self.assertGreater(payload["summary"]["blocked"], 0)

    def test_parser_abuse_strict_mode_fails_with_blocked_surfaces(self):
        result = run_script(PARSER_CHECK, "--manifest", str(PARSER_MANIFEST), "--strict")

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["ok"])
        self.assertFalse(payload["parserAbuseComplete"])

    def test_parser_abuse_verify_evidence_fails_bad_command(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "parser.json"
            manifest = json.loads(PARSER_MANIFEST.read_text(encoding="utf-8"))
            for case in manifest["cases"]:
                if case["status"] == "covered":
                    case["evidenceCommands"] = [f"{sys.executable} -c 'import sys; sys.exit(7)'"]
            path.write_text(json.dumps(manifest), encoding="utf-8")

            result = run_script(PARSER_CHECK, "--manifest", str(path), "--verify-evidence")

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["ok"])
        self.assertGreater(payload["summary"]["evidenceFailed"], 0)

    def test_parser_abuse_manifest_requires_current_evidence_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "parser.json"
            path.write_text(
                json.dumps(
                    {
                        "schemaVersion": 1,
                        "suite": "ecritum-parser-abuse",
                        "cases": [
                            {
                                "caseId": "config.invalid_utf8",
                                "surface": "config",
                                "vector": "invalid_utf8",
                                "status": "covered",
                                "evidenceCommands": [],
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            result = run_script(PARSER_CHECK, "--manifest", str(path), "--validate-manifest")

        self.assertEqual(result.returncode, 2)
        self.assertIn("covered case requires evidenceCommands", result.stderr)

    def test_justfile_exposes_security_targets(self):
        justfile = (ROOT / "justfile").read_text(encoding="utf-8")

        self.assertIn("test-security-static:", justfile)
        self.assertIn("test-security-abuse:", justfile)
        self.assertIn("test-security-fuzz:", justfile)
        self.assertIn("--verify-evidence", justfile)

    def test_justfile_exposes_ruby_security_and_metrics_targets(self):
        justfile = (ROOT / "justfile").read_text(encoding="utf-8")

        self.assertIn("security-ruby:", justfile)
        self.assertIn("ruby_abuse_provider.py", justfile)
        self.assertIn("bench-ruby-first-eval:", justfile)
        self.assertIn("bench-ruby-rss:", justfile)

    def test_ruby_abuse_provider_declares_required_capabilities_and_probes(self):
        spec = importlib.util.spec_from_file_location(
            "ruby_abuse_provider_under_test", RUBY_ABUSE_PROVIDER
        )
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        self.assertTrue(REQUIRED_ABUSE_CAPABILITIES.issubset(set(module.CAPABILITIES)))
        # Every capability must have a denial probe, and every required phase
        # must have a phase probe, so generated cases never fall through to a
        # non-denied surface.
        for capability in REQUIRED_ABUSE_CAPABILITIES:
            self.assertIn(capability, module.DENIAL_PROBES)
            self.assertTrue(module.DENIAL_PROBES[capability].strip())
        for phase in REQUIRED_ABUSE_PHASES - {"normal_eval"}:
            self.assertIn(phase, module.PHASE_PROBES)
        # The lexical-bypass introspection vectors must be present so the suite
        # proves runtime-grade denial, not merely lexical denial.
        self.assertIn("require.arbitrary_namespace.denied", module.TARGETED_PROBES)
        self.assertIn("require.refer_denied", module.TARGETED_PROBES)
        self.assertGreaterEqual(len(module.LEXICAL_BYPASS_PROBES), 3)

    def test_release_gates_record_security_requirements(self):
        release_gates = (ROOT / "docs" / "release-gates.md").read_text(encoding="utf-8").lower()

        for term in [
            "hardened runtime",
            "sbom",
            "cve",
            "vulnerability response",
            "revocation",
            "test-security-static",
            "test-security-abuse",
            "test-security-fuzz",
        ]:
            self.assertIn(term, release_gates)


if __name__ == "__main__":
    unittest.main()
