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
        self.resources = framework / "Resources"
        self.private_runtime = self.resources / "libecritum_graal.dylib"
        make_file(self.wrapper, 64 * 1024)
        self.wrapper.chmod(self.wrapper.stat().st_mode | stat.S_IXUSR)
        make_file(self.private_runtime, 151_000_000)
        self.metadata = self.resources / "ecritum-runtime.json"
        self.write_runtime_metadata()

    def tearDown(self):
        self.tmp.cleanup()

    def test_default_artifact_accepts_current_combined_runtime_budget(self):
        completed = self.run_size()

        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["artifactKind"], "default")
        self.assertEqual(payload["implementationProfile"], "full")
        self.assertEqual(payload["includedRuntimes"], ["clojure", "javascript", "lua"])
        self.assertEqual(payload["violations"], [])

    def test_legacy_full_metadata_is_accepted_until_artifact_rebuild(self):
        self.metadata.unlink()
        (self.resources / "ecritum-runtime-lane.json").write_text('{"formatVersion":1,"releaseLane":"full"}\n')

        completed = self.run_size()

        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["artifactKind"], "default")
        self.assertEqual(payload["implementationProfile"], "full")

    def test_legacy_core_metadata_is_not_a_default_artifact(self):
        self.metadata.unlink()
        (self.resources / "ecritum-runtime-lane.json").write_text('{"formatVersion":1,"releaseLane":"core"}\n')

        completed = self.run_size()

        self.assertEqual(completed.returncode, 1)
        payload = json.loads(completed.stdout)
        self.assertIn("artifactKind 'internal' is not 'default'", payload["violations"])
        self.assertIn("includedRuntimes missing required default runtimes: javascript, lua", payload["violations"])

    def test_missing_private_runtime_is_a_violation(self):
        self.private_runtime.unlink()

        completed = self.run_size()

        self.assertEqual(completed.returncode, 1)
        payload = json.loads(completed.stdout)
        self.assertIn("private_runtime_bytes missing", payload["violations"])

    def test_retired_lane_argument_is_rejected_by_argparse(self):
        completed = subprocess.run(
            [
                sys.executable,
                str(SIZE_ARTIFACT),
                "--artifact",
                str(self.artifact),
                "--require-artifact",
                "--lane",
                "core",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertIn("unrecognized arguments", completed.stderr)

    def run_size(self):
        return subprocess.run(
            [
                sys.executable,
                str(SIZE_ARTIFACT),
                "--artifact",
                str(self.artifact),
                "--require-artifact",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def write_runtime_metadata(self):
        self.metadata.write_text(json.dumps({
            "artifactKind": "default",
            "formatVersion": 1,
            "implementationProfile": "full",
            "includedRuntimes": ["clojure", "javascript", "lua"],
        }) + "\n")


if __name__ == "__main__":
    unittest.main()
