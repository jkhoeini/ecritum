#!/usr/bin/env python3
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BUILD_XCFRAMEWORK = ROOT / "scripts" / "build-xcframework.sh"


class BuildXCFrameworkPublicReleaseTest(unittest.TestCase):
    def test_public_release_rejects_default_ad_hoc_identity(self):
        completed = subprocess.run(
            ["bash", str(BUILD_XCFRAMEWORK), "--public-release"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertIn("require a non-ad-hoc code signing identity", completed.stderr)

    def test_public_release_rejects_skip_sign(self):
        completed = subprocess.run(
            [
                "bash",
                str(BUILD_XCFRAMEWORK),
                "--public-release",
                "--skip-sign",
                "--sign-identity",
                "Developer ID Application: Ecritum Test (TEAMID1234)",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertIn("cannot skip code signing", completed.stderr)


if __name__ == "__main__":
    unittest.main()
