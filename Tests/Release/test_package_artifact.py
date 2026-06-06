#!/usr/bin/env python3
import hashlib
import json
import os
import platform
import shutil
import stat
import subprocess
import sys
import tempfile
import textwrap
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PACKAGE_ARTIFACT = ROOT / "scripts" / "package-artifact.py"
CHECK_PACKAGE_REPRODUCIBLE = ROOT / "scripts" / "check-package-reproducible.py"
RELEASE_CONSUMER_SMOKE = ROOT / "scripts" / "test-release-consumer-smoke.py"
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
        license_resources = resources / "Licenses"
        headers.mkdir(parents=True)
        resources.mkdir()
        license_resources.mkdir()
        (self.artifact / "__MACOSX").mkdir()
        (headers / "ecritum.h").write_text("int ecritum_version(char *, unsigned long);\n")
        runtime = framework / "EcritumRuntime"
        runtime.write_text("runtime\n")
        runtime.chmod(runtime.stat().st_mode | stat.S_IXUSR)
        (resources / "libecritum_graal.dylib").write_text("graal\n")
        (resources / "ecritum-runtime-lane.json").write_text('{"formatVersion":1,"releaseLane":"core"}\n')
        (license_resources / "manifest.json").write_text('{"formatVersion":1,"licenseTexts":[]}\n')
        (license_resources / "MIT.txt").write_text("license\n")
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
        self.assertEqual(payload["releaseLane"], "core")
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
            self.assertIn("EcritumRuntime.xcframework/macos-arm64/EcritumRuntime.framework/Resources/Licenses/manifest.json", names)
            self.assertIn("EcritumRuntime.xcframework/macos-arm64/EcritumRuntime.framework/Resources/Licenses/MIT.txt", names)
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
        self.assertEqual(payload["releaseLane"], "core")

    def test_writes_full_release_lane_to_package_manifest(self):
        self.write_release_lane("full")
        result = self.run_package("full-lane", release_lane="full")

        payload = json.loads(result["manifest"].read_text())

        self.assertEqual(payload["releaseLane"], "full")
        self.assertEqual(payload["artifactReleaseLane"], "full")

    def test_rejects_release_lane_that_does_not_match_artifact_metadata(self):
        completed = subprocess.run(
            [
                sys.executable,
                str(PACKAGE_ARTIFACT),
                "--artifact",
                str(self.artifact),
                "--output",
                str(self.root / "mismatch.zip"),
                "--release-lane",
                "full",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("artifact release lane 'core' does not match requested lane 'full'", completed.stderr)

    def test_release_lane_environment_does_not_relabel_package(self):
        env = os.environ.copy()
        env["ECRITUM_RELEASE_LANE"] = "full"
        output = self.root / "env-ignored.zip"
        manifest = self.root / "env-ignored.json"
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
            ],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(json.loads(manifest.read_text())["releaseLane"], "core")

    def test_reproducibility_checker_propagates_full_release_lane(self):
        self.write_release_lane("full")
        completed = subprocess.run(
            [
                sys.executable,
                str(CHECK_PACKAGE_REPRODUCIBLE),
                "--artifact",
                str(self.artifact),
                "--package-script",
                str(PACKAGE_ARTIFACT),
                "--release-lane",
                "full",
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
        self.assertEqual(payload["releaseLane"], "full")

    def write_release_lane(self, lane):
        metadata = self.artifact / "macos-arm64" / "EcritumRuntime.framework" / "Resources" / "ecritum-runtime-lane.json"
        metadata.write_text(json.dumps({"formatVersion": 1, "releaseLane": lane}) + "\n")

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

    def test_release_manifest_accepts_remote_archive_basename_from_url(self):
        completed = self.describe_package({
            "ECRITUM_RELEASE_RUNTIME_REQUIRED": "1",
            "ECRITUM_RUNTIME_URL": "https://files.example.invalid/nr0ybn.zip",
            "ECRITUM_RUNTIME_CHECKSUM": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        })
        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        runtime_targets = [target for target in payload["targets"] if target["name"] == "EcritumRuntime"]
        self.assertEqual(len(runtime_targets), 1)
        self.assertEqual(runtime_targets[0]["type"], "binary")
        self.assertEqual(runtime_targets[0]["path"], "remote/archive/nr0ybn.zip")

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

    def test_release_consumer_smoke_requires_https_url(self):
        completed = subprocess.run(
            [
                sys.executable,
                str(RELEASE_CONSUMER_SMOKE),
                "--artifact-url",
                "http://127.0.0.1/EcritumRuntime.xcframework.zip",
                "--checksum",
                "0" * 64,
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("must use https", completed.stderr)

    def test_release_consumer_smoke_requires_checksum_shape(self):
        completed = subprocess.run(
            [
                sys.executable,
                str(RELEASE_CONSUMER_SMOKE),
                "--artifact-url",
                "https://example.invalid/EcritumRuntime.xcframework.zip",
                "--checksum",
                "not-a-checksum",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("64-character lowercase", completed.stderr)

    def test_release_consumer_smoke_rejects_stale_local_release_zip(self):
        release_zip = self.root / "release.zip"
        release_zip.write_text("stale")
        completed = subprocess.run(
            [
                sys.executable,
                str(RELEASE_CONSUMER_SMOKE),
                "--artifact-url",
                "https://example.invalid/EcritumRuntime.xcframework.zip",
                "--checksum",
                "0" * 64,
                "--release-zip",
                str(release_zip),
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("does not match requested checksum", completed.stderr)

    def test_release_consumer_smoke_rejects_checksum_sidecar_mismatch(self):
        release_zip = self.root / "release.zip"
        release_zip.write_text("artifact")
        checksum = sha256(release_zip)
        Path(str(release_zip) + ".checksum").write_text("0" * 64)

        completed = self.run_release_consumer_smoke_preflight(release_zip, checksum)

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("checksum sidecar does not match", completed.stderr)

    def test_release_consumer_smoke_rejects_package_manifest_mismatch(self):
        release_zip = self.root / "release.zip"
        release_zip.write_text("artifact")
        checksum = sha256(release_zip)
        Path(str(release_zip) + ".json").write_text(json.dumps({
            "sha256": checksum,
            "swiftPackageChecksum": "0" * 64,
        }))

        completed = self.run_release_consumer_smoke_preflight(release_zip, checksum)

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("package manifest checksum does not match", completed.stderr)

    def test_release_consumer_smoke_manifest_only_validates_release_binary_target(self):
        completed = subprocess.run(
            [
                sys.executable,
                str(RELEASE_CONSUMER_SMOKE),
                "--artifact-url",
                "https://example.invalid/EcritumRuntime.xcframework.zip",
                "--checksum",
                "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                "--release-zip",
                str(self.root / "missing-release.zip"),
                "--manifest-only",
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
        self.assertTrue(payload["manifestOnly"])
        self.assertTrue(payload["binaryTargetPath"].startswith("remote/archive/"))
        self.assertTrue(payload["binaryTargetPath"].endswith(".zip"))

    def test_release_consumer_smoke_manifest_only_accepts_remote_archive_basename_from_url(self):
        completed = subprocess.run(
            [
                sys.executable,
                str(RELEASE_CONSUMER_SMOKE),
                "--artifact-url",
                "https://files.example.invalid/nr0ybn.zip",
                "--checksum",
                "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                "--release-zip",
                str(self.root / "missing-release.zip"),
                "--manifest-only",
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
        self.assertEqual(payload["binaryTargetPath"], "remote/archive/nr0ybn.zip")

    def test_release_consumer_smoke_rejects_nested_remote_archive_path(self):
        fake_bin = self.root / "fake-bin"
        fake_swift = fake_bin / "swift"
        fake_bin.mkdir()
        fake_swift.write_text(textwrap.dedent("""\
            #!/usr/bin/env python3
            import json
            print(json.dumps({
                "targets": [
                    {
                        "name": "EcritumRuntime",
                        "path": "remote/archive/nested/EcritumRuntime.xcframework.zip",
                        "type": "binary",
                    }
                ]
            }))
        """))
        fake_swift.chmod(fake_swift.stat().st_mode | stat.S_IXUSR)
        env = os.environ.copy()
        env["PATH"] = str(fake_bin) + os.pathsep + env["PATH"]
        completed = subprocess.run(
            [
                sys.executable,
                str(RELEASE_CONSUMER_SMOKE),
                "--artifact-url",
                "https://example.invalid/EcritumRuntime.xcframework.zip",
                "--checksum",
                "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                "--release-zip",
                str(self.root / "missing-release.zip"),
                "--manifest-only",
            ],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("did not resolve through a remote binary target", completed.stderr)

    def test_release_consumer_smoke_workspace_state_accepts_remote_artifact(self):
        swift_build = self.root / "swift-build"
        artifact = self.remote_workspace_artifact(swift_build)
        self.write_workspace_state(swift_build, [artifact])

        completed = self.run_workspace_state_validation(swift_build)

        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertTrue(payload["ok"])
        expected_framework = (Path(artifact["path"]) / f"macos-{platform.machine()}" / "EcritumRuntime.framework").resolve()
        self.assertEqual(payload["downloadedFramework"], str(expected_framework))

    def test_release_consumer_smoke_workspace_state_rejects_bad_artifacts(self):
        cases = [
            (
                "local",
                [self.local_workspace_artifact()],
                "source type is local",
            ),
            (
                "missing",
                [],
                "expected one EcritumRuntime artifact",
            ),
            (
                "duplicate",
                [
                    self.remote_workspace_artifact(self.root / "duplicate-swift-build", "one"),
                    self.remote_workspace_artifact(self.root / "duplicate-swift-build", "two"),
                ],
                "expected one EcritumRuntime artifact",
            ),
            (
                "missing-source",
                [self.remote_workspace_artifact(self.root / "missing-source-swift-build", source=None)],
                "source type is None",
            ),
        ]
        for name, artifacts, expected_error in cases:
            with self.subTest(name=name):
                swift_build = self.root / f"{name}-swift-build"
                self.write_workspace_state(swift_build, artifacts)

                completed = self.run_workspace_state_validation(swift_build)

                self.assertNotEqual(completed.returncode, 0)
                self.assertIn(expected_error, completed.stderr)

    def test_release_consumer_smoke_workspace_state_rejects_missing_state_file(self):
        completed = self.run_workspace_state_validation(self.root / "missing-state-swift-build")

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("missing SwiftPM workspace state", completed.stderr)

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

    def run_package(self, name, release_lane=None):
        output = self.root / f"{name}.zip"
        manifest = self.root / f"{name}.json"
        checksum = self.root / f"{name}.checksum"
        command = [
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
        ]
        if release_lane:
            command.extend(["--release-lane", release_lane])
        completed = subprocess.run(
            command,
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        json.loads(completed.stdout)
        return {"zip": output, "manifest": manifest, "checksum": checksum}

    def run_workspace_state_validation(self, swift_build):
        return subprocess.run(
            [
                sys.executable,
                str(RELEASE_CONSUMER_SMOKE),
                "--artifact-url",
                "https://example.invalid/EcritumRuntime.xcframework.zip",
                "--checksum",
                "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                "--release-zip",
                str(self.root / "missing-release.zip"),
                "--build-dir",
                str(self.root / "unused-build-env"),
                "--validate-workspace-state",
                str(swift_build),
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def run_release_consumer_smoke_preflight(self, release_zip, checksum):
        return subprocess.run(
            [
                sys.executable,
                str(RELEASE_CONSUMER_SMOKE),
                "--artifact-url",
                "https://example.invalid/EcritumRuntime.xcframework.zip",
                "--checksum",
                checksum,
                "--release-zip",
                str(release_zip),
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def write_workspace_state(self, swift_build, artifacts):
        swift_build.mkdir(parents=True, exist_ok=True)
        (swift_build / "workspace-state.json").write_text(json.dumps({
            "object": {
                "artifacts": artifacts,
                "dependencies": [],
                "prebuilts": [],
            },
            "version": 7,
        }))

    def remote_workspace_artifact(self, swift_build, name="EcritumRuntime", source="remote"):
        artifact_root = swift_build / "artifacts" / "ecritum" / name / "EcritumRuntime.xcframework"
        (artifact_root / f"macos-{platform.machine()}" / "EcritumRuntime.framework").mkdir(parents=True, exist_ok=True)
        artifact = {
            "kind": {"xcframework": {}},
            "packageRef": {
                "identity": "ecritum",
                "kind": "fileSystem",
                "location": str(ROOT),
                "name": "Ecritum",
            },
            "path": str(artifact_root),
            "source": {
                "type": source,
            },
            "targetName": "EcritumRuntime",
        }
        if source is None:
            artifact.pop("source")
        return artifact

    def local_workspace_artifact(self):
        return {
            "kind": {"xcframework": {}},
            "packageRef": {
                "identity": "ecritum",
                "kind": "fileSystem",
                "location": str(ROOT),
                "name": "Ecritum",
            },
            "path": str(ROOT / "dist" / "local" / "EcritumRuntime.xcframework"),
            "source": {
                "type": "local",
            },
            "targetName": "EcritumRuntime",
        }


if __name__ == "__main__":
    unittest.main()
