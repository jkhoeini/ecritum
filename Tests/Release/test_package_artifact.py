#!/usr/bin/env python3
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PACKAGE_ARTIFACT = ROOT / "scripts" / "package-artifact.py"
CHECK_PACKAGE_REPRODUCIBLE = ROOT / "scripts" / "check-package-reproducible.py"
NORMALIZED_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class PackageArtifactTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ecritum-package-test-")
        self.root = Path(self.tmp.name)
        self.artifact = self.root / "EcritumRuntime.xcframework"
        framework = self.artifact / "macos-arm64" / "EcritumRuntime.framework"
        headers = framework / "Headers"
        resources = framework / "Resources"
        headers.mkdir(parents=True)
        resources.mkdir()
        (self.artifact / "__MACOSX").mkdir()
        (headers / "ecritum.h").write_text("int ecritum_version(char *, unsigned long);\n")
        runtime = framework / "EcritumRuntime"
        runtime.write_text("runtime\n")
        runtime.chmod(runtime.stat().st_mode | stat.S_IXUSR)
        (resources / "libecritum_graal.dylib").write_text("graal\n")
        (framework / ".DS_Store").write_text("skip\n")
        (framework / "._EcritumRuntime").write_text("skip\n")
        (self.artifact / "__MACOSX" / "ignored").write_text("skip\n")

    def tearDown(self):
        self.tmp.cleanup()

    def test_writes_deterministic_zip_manifest_and_checksum(self):
        first = self.run_package("first")
        second = self.run_package("second")
        self.assertEqual(first["zip"].read_bytes(), second["zip"].read_bytes())

        payload = json.loads(first["manifest"].read_text())
        checksum = sha256(first["zip"])
        swiftpm_checksum = subprocess.check_output(
            ["swift", "package", "compute-checksum", str(first["zip"])],
            cwd=ROOT,
            text=True,
        ).strip()
        self.assertEqual(payload["sha256"], checksum)
        self.assertEqual(payload["swiftPackageChecksum"], swiftpm_checksum)
        self.assertEqual(checksum, swiftpm_checksum)
        self.assertEqual(first["checksum"].read_text().strip(), checksum)
        self.assertEqual(payload["root"], "EcritumRuntime.xcframework")
        self.assertEqual(payload["slices"], ["macos-arm64"])
        self.assertRegex(payload["artifactSha256"], r"^[0-9a-f]{64}$")

        with zipfile.ZipFile(first["zip"]) as archive:
            infos = archive.infolist()
            names = [info.filename for info in infos]
            self.assertEqual(names, sorted(names))
            self.assertNotIn("EcritumRuntime.xcframework/macos-arm64/EcritumRuntime.framework/.DS_Store", names)
            self.assertNotIn("EcritumRuntime.xcframework/macos-arm64/EcritumRuntime.framework/._EcritumRuntime", names)
            self.assertNotIn("EcritumRuntime.xcframework/__MACOSX/ignored", names)
            for info in infos:
                self.assertEqual(info.date_time, NORMALIZED_TIMESTAMP)
                self.assertEqual(info.compress_type, zipfile.ZIP_DEFLATED)

    def test_reproducibility_checker_accepts_fixture_artifact(self):
        completed = subprocess.run(
            [
                sys.executable,
                str(CHECK_PACKAGE_REPRODUCIBLE),
                "--artifact",
                str(self.artifact),
                "--package-script",
                str(PACKAGE_ARTIFACT),
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["violations"], [])

    def test_release_manifest_requires_url_and_checksum_together(self):
        completed = self.describe_package({"ECRITUM_RUNTIME_URL": "https://example.invalid/EcritumRuntime.xcframework.zip"})
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("ECRITUM_RUNTIME_URL and ECRITUM_RUNTIME_CHECKSUM", completed.stderr + completed.stdout)

    def test_release_manifest_required_mode_fails_without_release_metadata(self):
        completed = self.describe_package({"ECRITUM_RELEASE_RUNTIME_REQUIRED": "1"})
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("ECRITUM_RELEASE_RUNTIME_REQUIRED requires", completed.stderr + completed.stdout)

    def test_release_manifest_required_mode_uses_remote_binary_target(self):
        completed = self.describe_package({
            "ECRITUM_RELEASE_RUNTIME_REQUIRED": "1",
            "ECRITUM_RUNTIME_URL": "https://example.invalid/EcritumRuntime.xcframework.zip",
            "ECRITUM_RUNTIME_CHECKSUM": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        })
        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        runtime_targets = [target for target in payload["targets"] if target["name"] == "EcritumRuntime"]
        self.assertEqual(len(runtime_targets), 1)
        self.assertEqual(runtime_targets[0]["type"], "binary")
        self.assertEqual(runtime_targets[0]["path"], "remote/archive/EcritumRuntime.xcframework.zip")

    def test_missing_artifact_fails(self):
        shutil.rmtree(self.artifact)
        completed = subprocess.run(
            [
                sys.executable,
                str(PACKAGE_ARTIFACT),
                "--artifact",
                str(self.artifact),
                "--output",
                str(self.root / "missing.zip"),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("missing artifact directory", completed.stderr)

    def describe_package(self, extra_env):
        env = os.environ.copy()
        for key in [
            "ECRITUM_LOCAL_RUNTIME",
            "ECRITUM_LOCAL_RUNTIME_STATE",
            "ECRITUM_RELEASE_RUNTIME_REQUIRED",
            "ECRITUM_RUNTIME_URL",
            "ECRITUM_RUNTIME_CHECKSUM",
        ]:
            env.pop(key, None)
        env.update(extra_env)
        return subprocess.run(
            ["swift", "package", "describe", "--type", "json"],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def run_package(self, name):
        output = self.root / f"{name}.zip"
        manifest = self.root / f"{name}.json"
        checksum = self.root / f"{name}.checksum"
        completed = subprocess.run(
            [
                sys.executable,
                str(PACKAGE_ARTIFACT),
                "--artifact",
                str(self.artifact),
                "--output",
                str(output),
                "--manifest",
                str(manifest),
                "--checksum-output",
                str(checksum),
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        json.loads(completed.stdout)
        return {"zip": output, "manifest": manifest, "checksum": checksum}


if __name__ == "__main__":
    unittest.main()
