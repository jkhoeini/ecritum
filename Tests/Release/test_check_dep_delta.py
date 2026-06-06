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


if __name__ == "__main__":
    unittest.main()
