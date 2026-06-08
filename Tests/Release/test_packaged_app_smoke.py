#!/usr/bin/env python3
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SMOKE_SCRIPT = ROOT / "scripts" / "test-packaged-app-smoke.sh"
SMOKE_APP = ROOT / "Examples" / "MacSmokeApp" / "Sources" / "EcritumSmokeApp" / "EcritumSmokeApp.swift"


class PackagedAppSmokeContractTest(unittest.TestCase):
    def setUp(self):
        self.script = SMOKE_SCRIPT.read_text()
        self.app = SMOKE_APP.read_text()

    def test_smoke_app_exercises_default_runtime_languages(self):
        self.assertIn("Ecritum.runtimeArtifactAvailable", self.app)
        self.assertIn("languages: [.clojure, .javascript, .lua, .python, .ruby]", self.app)
        self.assertIn('sourceName: "packaged-smoke.clj"', self.app)
        self.assertIn('sourceName: "packaged-smoke.js"', self.app)
        self.assertIn('sourceName: "packaged-smoke.lua"', self.app)
        self.assertIn('sourceName: "packaged-smoke.py"', self.app)
        self.assertIn('sourceName: "packaged-smoke.rb"', self.app)
        self.assertIn("ecritum.app.answer()", self.app)
        self.assertIn("clojure=42 javascript=42 lua=42 python=42 ruby=42", self.app)

    def test_script_runs_copied_app_with_clean_runtime_environment(self):
        self.assertIn('success_line="EcritumSmokeApp version=0.1.0 clojure=42 javascript=42 lua=42 python=42 ruby=42"', self.script)
        run_command = re.search(r'output="\$\(env -i ([^\n]+)\)"', self.script)
        self.assertIsNotNone(run_command)
        self.assertIn('PATH="$run_root/empty-bin"', run_command.group(1))
        self.assertIn('"$run_executable"', run_command.group(1))
        self.assertNotIn("DYLD_", run_command.group(1))
        self.assertIn("LC_DYLD_ENVIRONMENT", self.script)

    def test_script_guards_against_crash_reports(self):
        self.assertIn("crash_report_count()", self.script)
        self.assertIn("poll_crash_report_count()", self.script)
        self.assertIn("sleep 1", self.script)
        self.assertIn('"${executable_name}_*.crash"', self.script)
        self.assertIn('"${executable_name}_*.ips"', self.script)
        self.assertIn("packaged app generated a new $executable_name crash report", self.script)

    def test_script_checks_install_names_rpaths_and_path_leaks(self):
        self.assertIn("@rpath/EcritumRuntime.framework/EcritumRuntime", self.script)
        self.assertIn("@executable_path/../Frameworks", self.script)
        self.assertIn("@loader_path/Resources/libecritum_graal.dylib", self.script)
        self.assertIn('grep -F "$repo_root"', self.script)
        self.assertIn("packaged app contains workspace-local install path", self.script)
        self.assertIn("packaged app links a build-machine runtime path", self.script)
        self.assertRegex(self.script, r"grep -E ['\"]/GraalVM\\|/jdk\\|/native/target\\|/build/native")

    def test_script_does_not_use_missing_framework_negative_launch(self):
        self.assertNotIn("missing-framework", self.script)
        self.assertNotIn("DYLD_FRAMEWORK_PATH", self.script)
        self.assertNotIn("DYLD_LIBRARY_PATH", self.script)
        self.assertNotIn("DYLD_INSERT_LIBRARIES", self.script)


if __name__ == "__main__":
    unittest.main()
