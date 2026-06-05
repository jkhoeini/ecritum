import json
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts" / "run-conformance.py"
MANIFEST = ROOT / "Tests" / "Conformance" / "manifest.json"
FIXTURE_PROVIDER = ROOT / "Tests" / "Conformance" / "fixtures" / "provider.py"


REQUIRED_CASE_IDS = {
    "eval.scalar",
    "eval.array",
    "eval.object",
    "host.script_calls_host_function",
    "host.host_function_returns_value",
    "error.script_to_structured_error",
    "timeout.long_running_script",
    "permission.default_deny.filesystem",
    "permission.default_deny.network",
    "permission.default_deny.process",
    "permission.default_deny.environment",
    "permission.default_deny.reflection",
    "permission.default_deny.class_loading",
    "permission.default_deny.native_library_loading",
    "permission.default_deny.unrestricted_java_lookup",
    "permission.filesystem.allowed_root_inside",
    "permission.filesystem.allowed_root_outside_denied",
    "lifecycle.c.create_destroy_no_leak",
    "lifecycle.swift.create_destroy_no_leak",
    "allocation.public_c_release_coverage",
}


def run_conformance(*args):
    return subprocess.run(
        [sys.executable, str(RUNNER), "--manifest", str(MANIFEST), *args],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def fixture_provider(mode="scaffold"):
    return [sys.executable, str(FIXTURE_PROVIDER), "--mode", mode]


def provider_script(source):
    directory = tempfile.TemporaryDirectory()
    path = Path(directory.name) / "provider.py"
    path.write_text(textwrap.dedent(source), encoding="utf-8")
    return directory, [sys.executable, str(path)]


class ConformanceRunnerTests(unittest.TestCase):
    def test_required_case_catalog_contains_all_project_cases(self):
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))

        required_case_ids = {case["caseId"] for case in manifest["cases"] if case["required"]}

        self.assertEqual(required_case_ids, REQUIRED_CASE_IDS)
        self.assertEqual(len({case["caseId"] for case in manifest["cases"]}), len(manifest["cases"]))

    def test_missing_eval_abi_cases_are_pending_not_pass(self):
        result = run_conformance("--provider", *fixture_provider("scaffold"))

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        cases = {case["caseId"]: case for case in payload["cases"]}
        self.assertEqual(cases["eval.scalar"]["status"], "pending_capability")
        self.assertEqual(cases["eval.array"]["status"], "pending_capability")
        self.assertEqual(cases["eval.object"]["status"], "pending_capability")

    def test_pending_capability_requires_explicit_reason(self):
        tempdir, provider = provider_script(
            """
            import json
            import sys
            request = json.load(sys.stdin)
            print(json.dumps({
                "protocolVersion": 1,
                "provider": {"id": "bad-pending", "capabilities": []},
                "results": [
                    {"caseId": case["caseId"], "status": "pending_capability", "reason": ""}
                    for case in request["cases"]
                ],
            }))
            """
        )
        self.addCleanup(tempdir.cleanup)

        result = run_conformance("--case", "eval.scalar", "--provider", *provider)

        self.assertEqual(result.returncode, 2)
        self.assertIn("pending_capability", result.stderr)
        self.assertIn("reason", result.stderr)

    def test_provider_declaring_capability_cannot_return_pending(self):
        tempdir, provider = provider_script(
            """
            import json
            import sys
            request = json.load(sys.stdin)
            print(json.dumps({
                "protocolVersion": 1,
                "provider": {"id": "false-pending", "capabilities": ["eval"]},
                "results": [
                    {
                        "caseId": request["cases"][0]["caseId"],
                        "status": "pending_capability",
                        "reason": "not implemented",
                    }
                ],
            }))
            """
        )
        self.addCleanup(tempdir.cleanup)

        result = run_conformance("--case", "eval.scalar", "--provider", *provider)

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["ok"])
        self.assertFalse(payload["conformant"])
        self.assertEqual(payload["cases"][0]["status"], "fail")
        self.assertIn("declares required capabilities", payload["cases"][0]["reason"])

    def test_summary_reports_language_conformance_false_when_pending_exists(self):
        result = run_conformance("--provider", *fixture_provider("scaffold"))

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["ok"])
        self.assertFalse(payload["conformant"])
        self.assertGreater(payload["summary"]["pending"], 0)

    def test_strict_mode_exits_nonzero_for_pending_required_cases(self):
        result = run_conformance("--strict", "--provider", *fixture_provider("scaffold"))

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["ok"])
        self.assertFalse(payload["conformant"])

    def test_c_abi_provider_reports_lifecycle_and_allocation_release_cases(self):
        result = run_conformance("--category", "lifecycle", "--category", "allocation", "--provider", *fixture_provider("c-abi"))

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        cases = {case["caseId"]: case for case in payload["cases"]}
        self.assertEqual(cases["lifecycle.c.create_destroy_no_leak"]["status"], "pass")
        self.assertEqual(cases["lifecycle.swift.create_destroy_no_leak"]["status"], "pending_capability")
        self.assertEqual(cases["allocation.public_c_release_coverage"]["status"], "pass")
        self.assertIn("mise exec -- just test-c-abi-lifecycle", cases["lifecycle.c.create_destroy_no_leak"]["evidence"]["commands"])

    def test_swift_model_provider_reports_value_error_policy_cases(self):
        result = run_conformance("--category", "model", "--provider", *fixture_provider("swift-model"))

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        cases = {case["caseId"]: case for case in payload["cases"]}
        self.assertEqual(cases["model.swift.value_kinds"]["status"], "pass")
        self.assertEqual(cases["model.swift.structured_errors"]["status"], "pass")
        self.assertEqual(cases["model.swift.policy_config_serialization"]["status"], "pass")

    def test_malformed_provider_output_is_runner_error(self):
        tempdir, provider = provider_script(
            """
            print("not json")
            """
        )
        self.addCleanup(tempdir.cleanup)

        result = run_conformance("--case", "eval.scalar", "--provider", *provider)

        self.assertEqual(result.returncode, 2)
        self.assertIn("invalid provider JSON", result.stderr)

    def test_provider_pass_without_declared_capability_fails(self):
        tempdir, provider = provider_script(
            """
            import json
            import sys
            request = json.load(sys.stdin)
            print(json.dumps({
                "protocolVersion": 1,
                "provider": {"id": "false-pass", "capabilities": []},
                "results": [
                    {
                        "caseId": request["cases"][0]["caseId"],
                        "status": "pass",
                        "reason": "trust me",
                    }
                ],
            }))
            """
        )
        self.addCleanup(tempdir.cleanup)

        result = run_conformance("--case", "eval.scalar", "--provider", *provider)

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertEqual(payload["cases"][0]["status"], "fail")
        self.assertIn("without declaring required capabilities", payload["cases"][0]["reason"])

    def test_provider_pass_must_match_expected_actual(self):
        tempdir, provider = provider_script(
            """
            import json
            import sys
            request = json.load(sys.stdin)
            print(json.dumps({
                "protocolVersion": 1,
                "provider": {"id": "bare-pass", "capabilities": ["eval"]},
                "results": [
                    {
                        "caseId": request["cases"][0]["caseId"],
                        "status": "pass",
                        "reason": "trust me",
                    }
                ],
            }))
            """
        )
        self.addCleanup(tempdir.cleanup)

        result = run_conformance("--case", "eval.scalar", "--provider", *provider)

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["ok"])
        self.assertFalse(payload["conformant"])
        self.assertEqual(payload["cases"][0]["status"], "fail")
        self.assertIn("missing actual.value", payload["cases"][0]["reason"])

    def test_provider_pass_with_wrong_expected_actual_fails(self):
        tempdir, provider = provider_script(
            """
            import json
            import sys
            request = json.load(sys.stdin)
            print(json.dumps({
                "protocolVersion": 1,
                "provider": {"id": "wrong-actual", "capabilities": ["eval"]},
                "results": [
                    {
                        "caseId": request["cases"][0]["caseId"],
                        "status": "pass",
                        "reason": "wrong result",
                        "actual": {"value": {"kind": "int", "value": 41}},
                    }
                ],
            }))
            """
        )
        self.addCleanup(tempdir.cleanup)

        result = run_conformance("--case", "eval.scalar", "--provider", *provider)

        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertEqual(payload["cases"][0]["status"], "fail")
        self.assertIn("actual.value does not match expected", payload["cases"][0]["reason"])

    def test_validate_manifest_outputs_case_count(self):
        result = run_conformance("--validate-manifest")

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertEqual(payload["ok"], True)
        self.assertEqual(payload["cases"], 23)

    def test_unknown_case_id_is_runner_error(self):
        result = run_conformance("--case", "missing.case", "--list")

        self.assertEqual(result.returncode, 2)
        self.assertIn("unknown case id", result.stderr)

    def test_empty_selection_is_runner_error(self):
        result = run_conformance("--category", "missing", "--provider", *fixture_provider("scaffold"))

        self.assertEqual(result.returncode, 2)
        self.assertIn("no conformance cases selected", result.stderr)

    def test_provider_stderr_is_forwarded_to_stderr_not_json(self):
        tempdir, provider = provider_script(
            """
            import json
            import sys
            request = json.load(sys.stdin)
            print("provider diagnostic", file=sys.stderr)
            print(json.dumps({
                "protocolVersion": 1,
                "provider": {"id": "diagnostic-provider", "capabilities": ["eval"]},
                "results": [
                    {
                        "caseId": request["cases"][0]["caseId"],
                        "status": "pass",
                        "actual": {"value": {"kind": "int", "value": 42}},
                    }
                ],
            }))
            """
        )
        self.addCleanup(tempdir.cleanup)

        result = run_conformance("--case", "eval.scalar", "--provider", *provider)

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertNotIn("providerStderr", payload)
        self.assertIn("provider diagnostic", result.stderr)

    def test_help_documents_streams_and_exit_codes(self):
        result = subprocess.run(
            [sys.executable, str(RUNNER), "--help"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("stdout", result.stdout)
        self.assertIn("stderr", result.stdout)
        self.assertIn("Exit codes", result.stdout)

    def test_fixture_provider_does_not_offer_all_pass_mode(self):
        result = subprocess.run(
            [sys.executable, str(FIXTURE_PROVIDER), "--help"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(result.returncode, 0)
        self.assertNotIn("all-pass", result.stdout)

    def test_provider_crash_is_runner_error(self):
        tempdir, provider = provider_script(
            """
            import sys
            sys.exit(7)
            """
        )
        self.addCleanup(tempdir.cleanup)

        result = run_conformance("--case", "eval.scalar", "--provider", *provider)

        self.assertEqual(result.returncode, 2)
        self.assertIn("provider exited with status 7", result.stderr)

    def test_provider_timeout_is_runner_error(self):
        tempdir, provider = provider_script(
            """
            import time
            time.sleep(5)
            """
        )
        self.addCleanup(tempdir.cleanup)

        result = run_conformance(
            "--case",
            "eval.scalar",
            "--provider-timeout-seconds",
            "0.1",
            "--provider",
            *provider,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("provider timed out", result.stderr)

    def test_filter_runs_selected_category_only(self):
        result = run_conformance("--category", "model", "--provider", *fixture_provider("swift-model"))

        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertEqual({case["category"] for case in payload["cases"]}, {"model"})

    def test_list_cases_outputs_deterministic_case_catalog(self):
        first = run_conformance("--list")
        second = run_conformance("--list")

        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(first.stdout, second.stdout)
        payload = json.loads(first.stdout)
        self.assertEqual(payload["schemaVersion"], 1)
        self.assertIn("eval.scalar", [case["caseId"] for case in payload["cases"]])


if __name__ == "__main__":
    unittest.main()
