#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECK_DEP_DELTA = ROOT / "scripts" / "check-dep-delta.py"
LICENSE_REPORT = ROOT / "scripts" / "license-report.py"


class CheckDepDeltaTest(unittest.TestCase):
    def test_dependency_delta_accepts_first_party_mit_baseline(self):
        completed = subprocess.run(
            [sys.executable, str(CHECK_DEP_DELTA)],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        payload = json.loads(completed.stdout)
        first_party = next(item for item in payload["current"]["shipped"] if item["name"] == "EcritumRuntime.xcframework")
        self.assertEqual(first_party["spdx"], "MIT")
        shipped = {item["name"]: item for item in payload["current"]["shipped"]}
        self.assertEqual(shipped["org.graalvm.polyglot:python"]["spdx"], "MIT AND PSF-2.0 AND UPL-1.0")
        self.assertEqual(shipped["org.graalvm.python:python-language"]["spdx"], "UPL-1.0 AND MIT AND PSF-2.0")
        self.assertEqual(shipped["org.graalvm.python:python-resources"]["spdx"], "UPL-1.0 AND MIT AND PSF-2.0")
        self.assertEqual(shipped["org.graalvm.truffle:truffle-nfi"]["spdx"], "UPL-1.0")
        self.assertEqual(shipped["org.bouncycastle:bcprov-jdk18on"]["spdx"], "LicenseRef-Bouncy-Castle")

    def test_dependency_delta_fails_on_spdx_change(self):
        env = os.environ.copy()
        env["SOURCE_DATE_EPOCH"] = "0"
        report_completed = subprocess.run(
            [sys.executable, str(LICENSE_REPORT)],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )
        report = json.loads(report_completed.stdout)
        for package in report["packages"]:
            if package["name"] == "org.luaj:luaj-jme":
                package["licenseConcluded"] = "NOASSERTION"
                package["licenseDeclared"] = "NOASSERTION"
                break
        else:
            self.fail("org.luaj:luaj-jme package missing from license report")

        command = (
            "import json; "
            f"print({json.dumps(json.dumps(report))})"
        )
        completed = subprocess.run(
            [
                sys.executable,
                str(CHECK_DEP_DELTA),
                "--license-report-command",
                sys.executable,
                "-c",
                command,
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 1)
        self.assertIn("org.luaj:luaj-jme", completed.stdout)
        self.assertIn("expected version=3.0.1 spdx=MIT actual version=3.0.1 spdx=NOASSERTION", completed.stdout)

    # ---- M12-002 Slice 2: Ruby (TruffleRuby) shipped in the default artifact ----

    # The seven net-new shipped coordinates Ruby adds, with LLVM EXCLUDED.
    RUBY_SHIPPED_COORDINATES = [
        "dev.truffleruby:truffleruby",
        "dev.truffleruby.internal:runtime",
        "dev.truffleruby.internal:resources",
        "dev.truffleruby.internal:annotations",
        "dev.truffleruby.internal:shared",
        "dev.truffleruby.shadowed:joni",
        "org.graalvm.shadowed:jcodings",
    ]

    # ADR-0028: the LLVM/Sulong backend and antlr4 must stay absent from default.
    LLVM_EXCLUDED_COORDINATES = [
        "org.graalvm.llvm:llvm-native",
        "org.graalvm.llvm:llvm-api",
        "org.graalvm.llvm:llvm-language-nfi",
        "org.graalvm.llvm:llvm-language-native",
        "org.graalvm.llvm:llvm-language",
        "org.graalvm.llvm:llvm-language-native-resources",
        "org.graalvm.shadowed:antlr4",
    ]

    def _import_module(self):
        import importlib.util

        spec = importlib.util.spec_from_loader("check_dep_delta", loader=None)
        module = importlib.util.module_from_spec(spec)
        src = CHECK_DEP_DELTA.read_text()
        cut = src.index("parser = argparse")
        exec(compile(src[:cut], str(CHECK_DEP_DELTA), "exec"), module.__dict__)
        return module

    def test_default_baseline_contains_ruby_and_excludes_llvm(self):
        # ADR-0028: Ruby's net-new coordinates are shipped; LLVM/antlr4 are not.
        # The forbidden-coordinate guard keys off the explicit coordinate set, not
        # a 'llvm' substring (antlr4 contains neither 'ruby' nor 'llvm').
        module = self._import_module()
        default_shipped = {item["name"] for item in module.BASELINE["shipped"]}
        for coord in self.RUBY_SHIPPED_COORDINATES:
            self.assertIn(coord, default_shipped, f"shipped Ruby coordinate missing from default baseline: {coord}")
        for coord in self.LLVM_EXCLUDED_COORDINATES:
            self.assertNotIn(coord, default_shipped, f"LLVM-excluded coordinate found in default baseline: {coord}")
        # No errors when the report shares the same shipped set.
        self.assertEqual(module.llvm_excluded_forbidden_coordinate_errors(default_shipped), [])

    def test_forbidden_guard_flags_llvm_in_baseline_and_report(self):
        # Defensive: re-introducing any LLVM/antlr4 coordinate must be flagged in
        # both the baseline and the live report.
        module = self._import_module()
        for coord in self.LLVM_EXCLUDED_COORDINATES:
            errors = module.llvm_excluded_forbidden_coordinate_errors({coord})
            self.assertTrue(
                any(f"shipped report must not contain LLVM-excluded coordinate: {coord}" == e for e in errors),
                f"guard did not flag injected report coordinate: {coord}",
            )

    def test_default_run_matches_baseline_with_ruby_and_exact_spdx(self):
        completed = subprocess.run(
            [sys.executable, str(CHECK_DEP_DELTA)],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertTrue(payload["ok"], payload.get("violations"))
        self.assertEqual(payload["artifactKind"], "default")
        shipped = {item["name"]: item for item in payload["current"]["shipped"]}
        for coord in self.RUBY_SHIPPED_COORDINATES:
            self.assertIn(coord, shipped, f"shipped Ruby coordinate missing from default run: {coord}")
        for coord in self.LLVM_EXCLUDED_COORDINATES:
            self.assertNotIn(coord, shipped, f"LLVM-excluded coordinate present in default run: {coord}")
        self.assertEqual(
            shipped["dev.truffleruby:truffleruby"]["spdx"],
            "EPL-2.0 AND BSD-3-Clause AND BSD-2-Clause AND MIT AND UPL-1.0 AND ICU",
        )
        self.assertEqual(shipped["dev.truffleruby.shadowed:joni"]["spdx"], "MIT")
        self.assertEqual(shipped["org.graalvm.shadowed:jcodings"]["spdx"], "MIT")


if __name__ == "__main__":
    unittest.main()
