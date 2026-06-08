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
        resources = framework / "Resources"
        make_file(resources / "libecritum_graal.dylib", 30_000_000)
        (resources / "ecritum-runtime.json").write_text(json.dumps({
            "artifactKind": "default",
            "formatVersion": 1,
            "implementationProfile": "full",
            "includedRuntimes": ["clojure", "javascript", "lua", "python", "ruby"],
        }) + "\n")
        self.output_dir = self.root / "release-output"
        self.release_zip = self.root / "release" / "EcritumRuntime.xcframework.zip"
        self.fake_bin = self.root / "bin"
        self.fake_bin.mkdir()
        self.log = self.root / "just-log.jsonl"
        self.write_fake_just()
        self.write_fake_swift()

    def tearDown(self):
        self.tmp.cleanup()

    def test_default_artifact_is_propagated_to_release_gate_steps(self):
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

        self.assertEqual(size_payload["artifactKind"], "default")
        self.assertTrue(size_payload["ok"])
        self.assertEqual(package_payload["artifactKind"], "default")
        self.assertEqual(reproducibility_payload["artifactKind"], "default")
        self.assertTrue((self.output_dir / "python-first-eval.json").exists())
        self.assertTrue((self.output_dir / "python-rss.json").exists())
        self.assertIn({"target": "bench-python-first-eval", "args": []}, log_entries)
        self.assertIn({"target": "bench-python-rss", "args": []}, log_entries)
        self.assertIn(
            {
                "target": "package-artifact",
                "args": [str(self.artifact), str(self.release_zip)],
            },
            log_entries,
        )
        self.assertIn(
            {
                "target": "package-artifact-verify",
                "args": [str(self.artifact)],
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

    def test_default_release_paths_are_unscoped(self):
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
        release_zip = work_root / "dist" / "release" / "EcritumRuntime.xcframework.zip"
        output_dir = work_root / "build" / "release"
        self.assertTrue((output_dir / "package.json").is_file())
        self.assertTrue((output_dir / "size.json").is_file())
        self.assertTrue(release_zip.is_file())
        self.assertEqual(json.loads((output_dir / "package.json").read_text())["artifactKind"], "default")
        self.assertEqual(json.loads((output_dir / "size.json").read_text())["artifactKind"], "default")

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

        self.assertEqual(completed.returncode, 0, completed.stderr)
        size_payload = json.loads((self.output_dir / "size.json").read_text())
        package_payload = json.loads((self.output_dir / "package.json").read_text())
        self.assertEqual(size_payload["artifactKind"], "default")
        self.assertEqual(package_payload["artifactKind"], "default")

    def test_retired_lane_exits_before_running_gates(self):
        completed = subprocess.run(
            ["bash", str(RELEASE_CHECK), "--lane", "full"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertIn("--lane is retired", completed.stderr)

    def test_public_release_requires_notarization_evidence_args(self):
        completed = subprocess.run(
            ["bash", str(RELEASE_CHECK), "--public"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertIn("public release requires --notary-submit-json and --notary-log-json", completed.stderr)

    def test_public_release_requires_hosted_consumer_url(self):
        env = os.environ.copy()
        env["JUST"] = str(self.fake_bin / "fake-just")
        env["JUST_LOG"] = str(self.log)
        env["REPO_ROOT"] = str(ROOT)
        env["PATH"] = str(self.fake_bin) + os.pathsep + env["PATH"]
        env.pop("ECRITUM_CONSUMER_ARTIFACT_URL", None)
        env.pop("ECRITUM_CONSUMER_ARTIFACT_CHECKSUM", None)
        evidence = self.write_public_evidence_files()

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
                "--public",
                "--notary-submit-json",
                str(evidence["submit"]),
                "--notary-log-json",
                str(evidence["log"]),
                "--stapling-exception-json",
                str(evidence["exception"]),
            ],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 1)
        self.assertIn("public release requires ECRITUM_CONSUMER_ARTIFACT_URL", completed.stderr)

    def test_community_release_uses_checked_in_default_manifest_without_hosted_env(self):
        env = os.environ.copy()
        env["JUST"] = str(self.fake_bin / "fake-just")
        env["JUST_LOG"] = str(self.log)
        env["REPO_ROOT"] = str(ROOT)
        env["PATH"] = str(self.fake_bin) + os.pathsep + env["PATH"]
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
                "--community",
            ],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        log_entries = [json.loads(line) for line in self.log.read_text().splitlines()]
        self.assertIn(
            {
                "target": "test-release-consumer-smoke",
                "args": ["", "", str(self.release_zip), "1", "1"],
            },
            log_entries,
        )
        payload = json.loads((self.output_dir / "clean-consumer.json").read_text())
        self.assertTrue(payload["ok"])

    def test_community_release_runs_hosted_consumer_without_public_signing(self):
        env = os.environ.copy()
        env["JUST"] = str(self.fake_bin / "fake-just")
        env["JUST_LOG"] = str(self.log)
        env["REPO_ROOT"] = str(ROOT)
        env["PATH"] = str(self.fake_bin) + os.pathsep + env["PATH"]
        env["ECRITUM_CONSUMER_ARTIFACT_URL"] = "https://example.invalid/EcritumRuntime.xcframework.zip"
        env["ECRITUM_CONSUMER_ARTIFACT_CHECKSUM"] = "a" * 64

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
                "--community",
            ],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        public_signing = json.loads((self.output_dir / "public-signing.json").read_text())
        self.assertFalse(public_signing["ok"])
        self.assertEqual(public_signing["mode"], "community")
        self.assertTrue(public_signing["skipped"])
        self.assertIn("community release does not claim Developer ID signing", public_signing["reason"])
        log_entries = [json.loads(line) for line in self.log.read_text().splitlines()]
        self.assertNotIn("check-public-signing", [entry["target"] for entry in log_entries])
        self.assertIn(
            {
                "target": "test-release-consumer-smoke",
                "args": [
                    "https://example.invalid/EcritumRuntime.xcframework.zip",
                    "a" * 64,
                    str(self.release_zip),
                ],
            },
            log_entries,
        )

    def test_release_mode_cannot_be_both_public_and_community(self):
        completed = subprocess.run(
            ["bash", str(RELEASE_CHECK), "--community", "--public"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertIn("release mode cannot be both --community and --public", completed.stderr)

    def test_public_release_runs_public_signing_gate_and_hosted_consumer(self):
        env = os.environ.copy()
        env["JUST"] = str(self.fake_bin / "fake-just")
        env["JUST_LOG"] = str(self.log)
        env["REPO_ROOT"] = str(ROOT)
        env["PATH"] = str(self.fake_bin) + os.pathsep + env["PATH"]
        env["ECRITUM_CONSUMER_ARTIFACT_URL"] = "https://example.invalid/EcritumRuntime.xcframework.zip"
        env.pop("ECRITUM_CONSUMER_ARTIFACT_CHECKSUM", None)
        evidence = self.write_public_evidence_files()

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
                "--public",
                "--notary-submit-json",
                str(evidence["submit"]),
                "--notary-log-json",
                str(evidence["log"]),
                "--stapling-exception-json",
                str(evidence["exception"]),
            ],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads((self.output_dir / "public-signing.json").read_text())
        self.assertTrue(payload["ok"])
        log_entries = [json.loads(line) for line in self.log.read_text().splitlines()]
        self.assertIn(
            {
                "target": "check-public-signing",
                "args": [
                    str(self.artifact),
                    str(self.release_zip),
                    str(evidence["submit"]),
                    str(evidence["log"]),
                    str(evidence["exception"]),
                    "",
                    str(self.release_zip) + ".json",
                ],
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

    def test_missing_option_values_exit_with_usage(self):
        for option in [
            "--output-dir",
            "--artifact",
            "--release-zip",
            "--notary-submit-json",
            "--notary-log-json",
            "--stapling-exception-json",
            "--stapler-evidence-json",
        ]:
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
        digest.update(b"release-zip-default")
        return digest.hexdigest()

    def write_public_evidence_files(self):
        evidence_dir = self.root / "evidence"
        evidence_dir.mkdir()
        submit = evidence_dir / "notary-submit.json"
        log = evidence_dir / "notary-log.json"
        exception = evidence_dir / "stapling-exception.json"
        submit.write_text("{}\n")
        log.write_text("{}\n")
        exception.write_text("{}\n")
        return {"exception": exception, "log": log, "submit": submit}

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
                    artifact, output = args
                    output_path = Path(output)
                    output_path.parent.mkdir(parents=True, exist_ok=True)
                    output_path.write_bytes(b"release-zip-default")
                    checksum = sha256(output_path)
                    payload = {
                        "artifactKind": "default",
                        "artifact": artifact,
                        "formatVersion": 1,
                        "includedRuntimes": ["clojure", "javascript", "lua", "python", "ruby"],
                        "implementationProfile": "full",
                        "output": output,
                        "sha256": checksum,
                        "swiftPackageChecksum": checksum,
                    }
                    Path(str(output_path) + ".json").write_text(json.dumps(payload, indent=2, sort_keys=True) + "\\n")
                    Path(str(output_path) + ".checksum").write_text(checksum + "\\n")
                    print(json.dumps(payload, indent=2, sort_keys=True))
                elif target == "package-artifact-verify":
                    print(json.dumps({"artifactKind": "default", "includedRuntimes": ["clojure", "javascript", "lua", "python", "ruby"], "ok": True, "violations": []}, indent=2, sort_keys=True))
                elif target == "check-public-signing":
                    print(json.dumps({"ok": True, "target": target, "args": args}, indent=2, sort_keys=True))
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
                    "bench-python-first-eval",
                    "bench-idle-rss",
                    "bench-python-rss",
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
