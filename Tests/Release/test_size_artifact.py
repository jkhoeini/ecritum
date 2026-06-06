#!/usr/bin/env python3
import json
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SIZE_ARTIFACT = ROOT / "scripts" / "size-artifact.py"


def make_file(path, size):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as handle:
        handle.truncate(size)


class SizeArtifactTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ecritum-size-test-")
        self.root = Path(self.tmp.name)
        self.artifact = self.root / "EcritumRuntime.xcframework"
        framework = self.artifact / "macos-arm64" / "EcritumRuntime.framework"
        self.wrapper = framework / "EcritumRuntime"
        self.private_runtime = framework / "Resources" / "libecritum_graal.dylib"
        make_file(self.wrapper, 64 * 1024)
        self.wrapper.chmod(self.wrapper.stat().st_mode | stat.S_IXUSR)
        make_file(self.private_runtime, 30_000_000)

    def tearDown(self):
        self.tmp.cleanup()

    def test_core_lane_fails_but_full_lane_accepts_large_combined_artifact(self):
        core = self.run_size("core")
        full = self.run_size("full")

        self.assertEqual(core.returncode, 1, core.stdout + core.stderr)
        core_payload = json.loads(core.stdout)
        self.assertEqual(core_payload["lane"], "core")
        self.assertFalse(core_payload["ok"])
        self.assertTrue(any("artifact_bytes" in violation for violation in core_payload["violations"]))
        self.assertTrue(any("private_runtime_bytes" in violation for violation in core_payload["violations"]))

        self.assertEqual(full.returncode, 0, full.stdout + full.stderr)
        full_payload = json.loads(full.stdout)
        self.assertEqual(full_payload["lane"], "full")
        self.assertTrue(full_payload["ok"])
        self.assertEqual(full_payload["violations"], [])

    def test_missing_private_runtime_is_a_violation(self):
        self.private_runtime.unlink()

        completed = self.run_size("full")

        self.assertEqual(completed.returncode, 1)
        payload = json.loads(completed.stdout)
        self.assertIn("private_runtime_bytes missing", payload["violations"])

    def test_invalid_lane_is_rejected_by_argparse(self):
        completed = self.run_size("experimental")

        self.assertEqual(completed.returncode, 2)
        self.assertIn("invalid choice", completed.stderr)

    def run_size(self, lane):
        return subprocess.run(
            [
                sys.executable,
                str(SIZE_ARTIFACT),
                "--artifact",
                str(self.artifact),
                "--require-artifact",
                "--lane",
                lane,
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )


if __name__ == "__main__":
    unittest.main()
