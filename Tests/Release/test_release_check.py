#!/usr/bin/env python3
import json
import os
import stat
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RELEASE_CHECK = ROOT / "scripts" / "release-check.sh"


def make_file(path, size):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as handle:
        handle.truncate(size)


class ReleaseCheckTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ecritum-release-check-test-")
        self.root = Path(self.tmp.name)
        self.artifact = self.root / "EcritumRuntime.xcframework"
        framework = self.artifact / "macos-arm64" / "EcritumRuntime.framework"
        make_file(framework / "EcritumRuntime", 64 * 1024)
        make_file(framework / "Resources" / "libecritum_graal.dylib", 30_000_000)
        self.output_dir = self.root / "release-output"
        self.release_zip = self.root / "release" / "full" / "EcritumRuntime.xcframework.zip"
        self.fake_bin = self.root / "bin"
        self.fake_bin.mkdir()
        self.log = self.root / "just-log.jsonl"
        self.write_fake_just()
        self.write_fake_swift()

    def tearDown(self):
        self.tmp.cleanup()

    def test_full_lane_is_propagated_to_release_gate_steps(self):
        env = os.environ.copy()
        env["JUST"] = str(self.fake_bin / "fake-just")
        env["JUST_LOG"] = str(self.log)
        env["REPO_ROOT"] = str(ROOT)
        env["PATH"] = str(self.fake_bin) + os.pathsep + env["PATH"]
        env["ECRITUM_CONSUMER_ARTIFACT_URL"] = "https://example.invalid/EcritumRuntime.xcframework.zip"
        env.pop("ECRITUM_CONSUMER_ARTIFACT_CHECKSUM", None)

        completed = subprocess.run(
            [
                "bash",
                str(RELEASE_CHECK),
                "--lane",
                "full",
                "--output-dir",
                str(self.output_dir),
                "--artifact",
                str(self.artifact),
                "--release-zip",
                str(self.release_zip),
            ],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        size_payload = json.loads((self.output_dir / "size.json").read_text())
        package_payload = json.loads((self.output_dir / "package.json").read_text())
        reproducibility_payload = json.loads((self.output_dir / "package-reproducibility.json").read_text())
        log_entries = [json.loads(line) for line in self.log.read_text().splitlines()]

        self.assertEqual(size_payload["lane"], "full")
        self.assertTrue(size_payload["ok"])
        self.assertEqual(package_payload["releaseLane"], "full")
        self.assertEqual(reproducibility_payload["releaseLane"], "full")
        self.assertIn(
            {
                "target": "package-artifact",
                "args": [str(self.artifact), str(self.release_zip), "full"],
            },
            log_entries,
        )
        self.assertIn(
            {
                "target": "package-artifact-verify",
                "args": [str(self.artifact), "full"],
            },
            log_entries,
        )
        self.assertIn(
            {
                "target": "test-release-consumer-smoke",
                "args": [
                    "https://example.invalid/EcritumRuntime.xcframework.zip",
                    self.release_zip_checksum(),
                    str(self.release_zip),
                ],
            },
            log_entries,
        )

    def test_default_full_lane_paths_are_lane_scoped(self):
        env = os.environ.copy()
        env["JUST"] = str(self.fake_bin / "fake-just")
        env["JUST_LOG"] = str(self.log)
        env["REPO_ROOT"] = str(ROOT)
        env["PATH"] = str(self.fake_bin) + os.pathsep + env["PATH"]
        env.pop("ECRITUM_CONSUMER_ARTIFACT_URL", None)
        env.pop("ECRITUM_CONSUMER_ARTIFACT_CHECKSUM", None)

        work_root = self.root / "work-root"
        (work_root / "scripts").mkdir(parents=True)
        (work_root / "THIRD_PARTY_NOTICES.md").write_text((ROOT / "THIRD_PARTY_NOTICES.md").read_text())
        (work_root / "scripts" / "size-artifact.py").write_text((ROOT / "scripts" / "size-artifact.py").read_text())
        completed = subprocess.run(
            [
                "bash",
                str(RELEASE_CHECK),
                "--lane",
                "full",
                "--artifact",
                str(self.artifact),
            ],
            cwd=work_root,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        release_zip = work_root / "dist" / "release" / "full" / "EcritumRuntime.xcframework.zip"
        output_dir = work_root / "build" / "release" / "full"
        self.assertTrue((output_dir / "package.json").is_file())
        self.assertTrue((output_dir / "size.json").is_file())
        self.assertTrue(release_zip.is_file())
        self.assertEqual(json.loads((output_dir / "package.json").read_text())["releaseLane"], "full")
        self.assertEqual(json.loads((output_dir / "size.json").read_text())["lane"], "full")

    def test_release_check_ignores_ambient_release_lane(self):
        env = os.environ.copy()
        env["JUST"] = str(self.fake_bin / "fake-just")
        env["JUST_LOG"] = str(self.log)
        env["REPO_ROOT"] = str(ROOT)
        env["PATH"] = str(self.fake_bin) + os.pathsep + env["PATH"]
        env["ECRITUM_RELEASE_LANE"] = "full"
        env.pop("ECRITUM_CONSUMER_ARTIFACT_URL", None)
        env.pop("ECRITUM_CONSUMER_ARTIFACT_CHECKSUM", None)

        completed = subprocess.run(
            [
                "bash",
                str(RELEASE_CHECK),
                "--output-dir",
                str(self.output_dir),
                "--artifact",
                str(self.artifact),
                "--release-zip",
                str(self.release_zip),
            ],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 1)
        size_payload = json.loads((self.output_dir / "size.json").read_text())
        package_payload = json.loads((self.output_dir / "package.json").read_text())
        self.assertEqual(size_payload["lane"], "core")
        self.assertEqual(package_payload["releaseLane"], "core")
        self.assertIn("artifact_bytes", " ".join(size_payload["violations"]))

    def test_invalid_lane_exits_before_running_gates(self):
        completed = subprocess.run(
            ["bash", str(RELEASE_CHECK), "--lane", "experimental"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertIn("invalid release lane", completed.stderr)

    def test_missing_option_values_exit_with_usage(self):
        for option in ["--lane", "--output-dir", "--artifact", "--release-zip"]:
            with self.subTest(option=option):
                completed = subprocess.run(
                    ["bash", str(RELEASE_CHECK), option],
                    cwd=ROOT,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    check=False,
                )

                self.assertEqual(completed.returncode, 2)
                self.assertIn(f"missing value for {option}", completed.stderr)
                self.assertIn("Usage: release-check.sh", completed.stderr)

    def release_zip_checksum(self):
        import hashlib

        digest = hashlib.sha256()
        digest.update(b"release-zip-full")
        return digest.hexdigest()

    def write_fake_just(self):
        script = self.fake_bin / "fake-just"
        script.write_text(
            textwrap.dedent(
                """\
                #!/usr/bin/env python3
                import hashlib
                import json
                import os
                import shutil
                import sys
                from pathlib import Path

                target = sys.argv[1]
                args = sys.argv[2:]
                log = Path(os.environ["JUST_LOG"])
                with log.open("a") as handle:
                    handle.write(json.dumps({"target": target, "args": args}, sort_keys=True) + "\\n")

                def sha256(path):
                    digest = hashlib.sha256()
                    with open(path, "rb") as handle:
                        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                            digest.update(chunk)
                    return digest.hexdigest()

                if target == "package-artifact":
                    artifact, output, lane = args
                    output_path = Path(output)
                    output_path.parent.mkdir(parents=True, exist_ok=True)
                    output_path.write_bytes(("release-zip-" + lane).encode())
                    checksum = sha256(output_path)
                    payload = {
                        "artifact": artifact,
                        "formatVersion": 1,
                        "output": output,
                        "releaseLane": lane,
                        "sha256": checksum,
                        "swiftPackageChecksum": checksum,
                    }
                    Path(str(output_path) + ".json").write_text(json.dumps(payload, indent=2, sort_keys=True) + "\\n")
                    Path(str(output_path) + ".checksum").write_text(checksum + "\\n")
                    print(json.dumps(payload, indent=2, sort_keys=True))
                elif target == "package-artifact-verify":
                    print(json.dumps({"ok": True, "releaseLane": args[1], "violations": []}, indent=2, sort_keys=True))
                elif target == "checksum":
                    print(sha256(Path(args[0])))
                elif target == "sbom":
                    Path(args[0]).write_text(json.dumps({"spdxVersion": "SPDX-2.3"}) + "\\n")
                elif target == "third-party-notices":
                    shutil.copyfile(Path(os.environ["REPO_ROOT"]) / "THIRD_PARTY_NOTICES.md", args[0])
                elif target in {
                    "check-license-texts",
                    "check-license-texts-zip",
                    "check-vulnerability-response",
                    "test-release-consumer-smoke",
                    "license-report-strict",
                    "inspect",
                    "bench-cold-start",
                    "bench-first-eval",
                    "bench-idle-rss",
                    "check-dep-delta",
                    "test",
                    "check-abi",
                    "check-xcframework",
                    "test-packaged-app-smoke",
                }:
                    print(json.dumps({"ok": True, "target": target}, indent=2, sort_keys=True))
                else:
                    print(f"unexpected fake just target: {target}", file=sys.stderr)
                    raise SystemExit(1)
                """
            )
        )
        script.chmod(script.stat().st_mode | stat.S_IXUSR)

    def write_fake_swift(self):
        script = self.fake_bin / "swift"
        script.write_text(
            textwrap.dedent(
                """\
                #!/usr/bin/env python3
                import json
                import sys

                if sys.argv[1:] == ["package", "describe", "--type", "json"]:
                    print(json.dumps({
                        "targets": [
                            {
                                "name": "EcritumRuntime",
                                "path": "remote/archive/EcritumRuntime.xcframework.zip",
                                "type": "binary",
                            }
                        ]
                    }))
                else:
                    print("unexpected fake swift command: " + " ".join(sys.argv[1:]), file=sys.stderr)
                    raise SystemExit(1)
                """
            )
        )
        script.chmod(script.stat().st_mode | stat.S_IXUSR)


if __name__ == "__main__":
    unittest.main()
