#!/usr/bin/env python3
import hashlib
import json
import os
import stat
import subprocess
import sys
import tempfile
import textwrap
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECK_PUBLIC_SIGNING = ROOT / "scripts" / "check-public-signing.py"


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class PublicSigningTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ecritum-public-signing-test-")
        self.root = Path(self.tmp.name)
        self.artifact = self.root / "EcritumRuntime.xcframework"
        self.release_zip = self.root / "EcritumRuntime.xcframework.zip"
        self.notary_submit = self.root / "notary-submit.json"
        self.notary_log = self.root / "notary-log.json"
        self.stapling_exception = self.root / "stapling-exception.json"
        self.fake_bin = self.root / "bin"
        self.fake_bin.mkdir()
        self.write_artifact()
        self.write_release_zip()
        self.release_zip_sha = sha256(self.release_zip)
        self.write_package_sidecars()
        self.write_evidence()
        self.write_fake_codesign()

    def tearDown(self):
        self.tmp.cleanup()

    def test_accepts_developer_id_hardened_runtime_notarized_zip_exception(self):
        completed = self.run_check()

        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["releaseZipSha256"], self.release_zip_sha)
        self.assertEqual(payload["notarization"]["status"], "Accepted")
        self.assertEqual(payload["stapling"]["mode"], "zip-exception")
        self.assertEqual(len(payload["codeSignatures"]), 2)
        self.assertEqual(len(payload["postUnpackCodeSignatures"]), 2)

    def test_rejects_ad_hoc_signature(self):
        completed = self.run_check(mode="adhoc")

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("ad-hoc signature", completed.stdout)

    def test_rejects_non_developer_id_authority(self):
        completed = self.run_check(mode="wrong-authority")

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("Developer ID Application", completed.stdout)

    def test_rejects_missing_hardened_runtime(self):
        completed = self.run_check(mode="missing-runtime")

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("missing hardened runtime", completed.stdout)

    def test_rejects_get_task_allow_entitlement(self):
        completed = self.run_check(mode="get-task-allow")

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("get-task-allow", completed.stdout)

    def test_rejects_rejected_notarization(self):
        self.write_evidence(notary_status="Invalid", log_status="Invalid")

        completed = self.run_check()

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("expected 'Accepted'", completed.stdout)

    def test_rejects_stale_notary_checksum(self):
        self.write_evidence(release_zip_sha="0" * 64)

        completed = self.run_check()

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("does not match release zip SHA-256", completed.stdout)

    def test_rejects_missing_zip_stapling_exception(self):
        completed = self.run_check(include_stapling_exception=False)

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("SwiftPM zip artifacts require --stapling-exception-json", completed.stdout)

    def write_artifact(self):
        framework = self.artifact / "macos-arm64" / "EcritumRuntime.framework"
        resources = framework / "Resources"
        resources.mkdir(parents=True)
        runtime = framework / "EcritumRuntime"
        private_dylib = resources / "libecritum_graal.dylib"
        runtime.write_text("runtime\n")
        private_dylib.write_text("graal\n")
        runtime.chmod(runtime.stat().st_mode | stat.S_IXUSR)
        private_dylib.chmod(private_dylib.stat().st_mode | stat.S_IXUSR)
        (resources / "ecritum-runtime.json").write_text(json.dumps({
            "artifactKind": "default",
            "formatVersion": 1,
            "implementationProfile": "full",
            "includedRuntimes": ["clojure", "javascript", "lua", "python", "ruby"],
        }) + "\n")

    def write_release_zip(self):
        with zipfile.ZipFile(self.release_zip, "w") as archive:
            for path in sorted(self.artifact.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(self.artifact.parent))

    def write_package_sidecars(self):
        Path(str(self.release_zip) + ".checksum").write_text(self.release_zip_sha + "\n")
        Path(str(self.release_zip) + ".json").write_text(json.dumps({
            "artifactKind": "default",
            "formatVersion": 1,
            "sha256": self.release_zip_sha,
            "swiftPackageChecksum": self.release_zip_sha,
        }) + "\n")

    def write_evidence(self, *, notary_status="Accepted", log_status="Accepted", release_zip_sha=None):
        release_zip_sha = release_zip_sha or self.release_zip_sha
        submission_id = "2efe2717-52ef-43a5-96dc-0797e4ca1041"
        self.notary_submit.write_text(json.dumps({
            "id": submission_id,
            "name": self.release_zip.name,
            "releaseZipSha256": release_zip_sha,
            "status": notary_status,
        }) + "\n")
        self.notary_log.write_text(json.dumps({
            "issues": [],
            "jobId": submission_id,
            "releaseZipSha256": release_zip_sha,
            "status": log_status,
        }) + "\n")
        self.stapling_exception.write_text(json.dumps({
            "acceptedBy": "Engineering Manager",
            "acceptedDate": "2026-06-06",
            "artifactFormat": "zip",
            "notarizationSubmissionId": submission_id,
            "reason": "SwiftPM binary target zip archives cannot be stapled directly; the contained code is validated after unpack.",
            "releaseZip": self.release_zip.name,
            "releaseZipSha256": release_zip_sha,
            "source": "ADR-010",
        }) + "\n")

    def write_fake_codesign(self):
        script = self.fake_bin / "codesign"
        script.write_text(textwrap.dedent(
            """\
            #!/usr/bin/env python3
            import os
            import sys

            args = sys.argv[1:]
            mode = os.environ.get("FAKE_CODESIGN_MODE", "valid")

            if "--verify" in args:
                if mode == "verify-fails":
                    print("invalid signature", file=sys.stderr)
                    raise SystemExit(1)
                raise SystemExit(0)

            if "--entitlements" in args:
                if mode == "get-task-allow":
                    print("<?xml version='1.0' encoding='UTF-8'?><plist version='1.0'><dict><key>com.apple.security.get-task-allow</key><true/></dict></plist>")
                else:
                    print("<?xml version='1.0' encoding='UTF-8'?><plist version='1.0'><dict/></plist>")
                raise SystemExit(0)

            if "-dv" in args:
                lines = [
                    "Executable=" + args[-1],
                    "Identifier=dev.ecritum.runtime",
                    "Format=Mach-O thin (arm64)",
                    "CodeDirectory v=20500 size=512 flags=0x10000(runtime) hashes=8+2 location=embedded",
                    "Signature size=9000",
                    "Authority=Developer ID Application: Ecritum Test (TEAMID1234)",
                    "Authority=Developer ID Certification Authority",
                    "Authority=Apple Root CA",
                    "Timestamp=Jun 6, 2026 at 10:00:00 AM",
                    "TeamIdentifier=TEAMID1234",
                    "Runtime Version=14.0.0",
                    "CDHash=0123456789abcdef",
                ]
                if mode == "adhoc":
                    lines = [
                        "Executable=" + args[-1],
                        "Signature=adhoc",
                        "TeamIdentifier=not set",
                    ]
                elif mode == "wrong-authority":
                    lines = [line.replace("Developer ID Application", "Apple Development") for line in lines]
                elif mode == "missing-runtime":
                    lines = [
                        line.replace(" flags=0x10000(runtime)", "")
                        for line in lines
                        if not line.startswith("Runtime Version=")
                    ]
                elif mode == "missing-timestamp":
                    lines = [line for line in lines if not line.startswith("Timestamp=")]
                print("\\n".join(lines), file=sys.stderr)
                raise SystemExit(0)

            print("unexpected fake codesign command: " + " ".join(args), file=sys.stderr)
            raise SystemExit(1)
            """
        ))
        script.chmod(script.stat().st_mode | stat.S_IXUSR)

    def run_check(self, *, mode="valid", include_stapling_exception=True):
        env = os.environ.copy()
        env["PATH"] = str(self.fake_bin) + os.pathsep + env["PATH"]
        env["FAKE_CODESIGN_MODE"] = mode
        command = [
            sys.executable,
            str(CHECK_PUBLIC_SIGNING),
            "--artifact",
            str(self.artifact),
            "--release-zip",
            str(self.release_zip),
            "--notary-submit-json",
            str(self.notary_submit),
            "--notary-log-json",
            str(self.notary_log),
        ]
        if include_stapling_exception:
            command.extend(["--stapling-exception-json", str(self.stapling_exception)])
        return subprocess.run(
            command,
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )


if __name__ == "__main__":
    unittest.main()
