#!/usr/bin/env python3
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECK_LICENSE_TEXTS = ROOT / "scripts" / "check-license-texts.py"
LICENSE_REPORT = ROOT / "scripts" / "license-report.py"
LICENSE_BUNDLE = ROOT / "THIRD_PARTY_LICENSES"
FIRST_PARTY_PACKAGED_LICENSE = "Ecritum-LICENSE.txt"


def annotation(scope):
    return [{
        "annotationType": "OTHER",
        "annotator": "Tool: test",
        "annotationDate": "1970-01-01T00:00:00Z",
        "comment": f"ecritum-scope={scope}; release-blocker=false; license-name=test",
    }]


def report_for(packages):
    return {
        "spdxVersion": "SPDX-2.3",
        "SPDXID": "SPDXRef-DOCUMENT",
        "packages": packages,
    }


def package(name, scope, license_expression):
    return {
        "SPDXID": "SPDXRef-" + name.replace(":", "-").replace(".", "-"),
        "annotations": annotation(scope),
        "licenseConcluded": license_expression,
        "name": name,
        "versionInfo": "1.0.0",
    }


def first_party_package():
    item = package("EcritumRuntime.xcframework", "shipped", "MIT")
    item["SPDXID"] = "SPDXRef-Package-EcritumRuntime"
    item["copyrightText"] = "Copyright (c) 2026 Ecritum contributors"
    item["annotations"][0]["comment"] += "; license-source=LICENSE"
    return item


class LicenseTextsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ecritum-license-texts-test-")
        self.root = Path(self.tmp.name)
        self.bundle = self.root / "THIRD_PARTY_LICENSES"
        shutil.copytree(LICENSE_BUNDLE, self.bundle)

    def tearDown(self):
        self.tmp.cleanup()

    def test_passes_when_required_shipped_license_texts_match_manifest_hashes(self):
        completed = self.run_check()

        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertTrue(payload["ok"])
        self.assertIn("GPL-2.0-only WITH Classpath-exception-2.0", payload["requiredLicenseIds"])
        self.assertIn("MIT", payload["requiredLicenseIds"])
        self.assertTrue(payload["firstPartyLicenseRequired"])

    def test_fails_when_required_license_text_file_is_missing(self):
        (self.bundle / "MIT.txt").unlink()

        completed = self.run_check()

        self.assertEqual(completed.returncode, 1)
        self.assertIn("missing manifest license text MIT", completed.stdout)

    def test_fails_when_packaged_license_text_hash_is_stale(self):
        artifact = self.artifact_with_bundle()
        packaged_mit = next(artifact.glob("*/EcritumRuntime.framework/Resources/Licenses/MIT.txt"))
        packaged_mit.write_text("stale\n")

        completed = self.run_check("--artifact", str(artifact))

        self.assertEqual(completed.returncode, 1)
        self.assertIn("artifact: license text hash mismatch for MIT", completed.stdout)

    def test_artifact_requires_first_party_license_copy_when_report_sources_license(self):
        artifact = self.artifact_with_bundle()
        next(artifact.glob(f"*/EcritumRuntime.framework/Resources/Licenses/{FIRST_PARTY_PACKAGED_LICENSE}")).unlink()
        report = report_for([first_party_package()])

        completed = self.run_check_with_report(report, "--artifact", str(artifact))

        self.assertEqual(completed.returncode, 1)
        self.assertIn("artifact: missing first-party Ecritum license text", completed.stdout)

    def test_artifact_rejects_stale_first_party_license_copy(self):
        artifact = self.artifact_with_bundle()
        next(artifact.glob(f"*/EcritumRuntime.framework/Resources/Licenses/{FIRST_PARTY_PACKAGED_LICENSE}")).write_text("stale\n")
        report = report_for([first_party_package()])

        completed = self.run_check_with_report(report, "--artifact", str(artifact))

        self.assertEqual(completed.returncode, 1)
        self.assertIn("artifact: first-party Ecritum license hash mismatch", completed.stdout)

    def test_fails_when_shipped_spdx_expression_has_no_full_text_rule(self):
        report = report_for([package("example:runtime", "shipped", "Apache-2.0")])

        completed = self.run_check_with_report(report)

        self.assertEqual(completed.returncode, 1)
        self.assertIn("missing expected license-text policy for SPDX expression: Apache-2.0", completed.stdout)

    def test_combined_and_expression_requires_each_license_text(self):
        (self.bundle / "UPL-1.0.txt").unlink()
        report = report_for([package("example:runtime", "shipped", "UPL-1.0 AND MIT")])

        completed = self.run_check_with_report(report)

        self.assertEqual(completed.returncode, 1)
        self.assertIn("missing manifest license text UPL-1.0", completed.stdout)

    def test_zip_mode_checks_release_zip_contents(self):
        release_zip = self.root / "release.zip"
        self.write_zip_with_bundle(release_zip, self.bundle)

        completed = self.run_check("--release-zip", str(release_zip))

        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertTrue(payload["ok"])

    def test_zip_mode_requires_first_party_license_copy_when_report_sources_license(self):
        release_zip = self.root / "release.zip"
        self.write_zip_with_bundle(release_zip, self.bundle, include_first_party_license=False)
        report = report_for([first_party_package()])

        completed = self.run_check_with_report(report, "--release-zip", str(release_zip))

        self.assertEqual(completed.returncode, 1)
        self.assertIn("release zip: missing first-party Ecritum license text", completed.stdout)

    def test_build_and_test_scopes_do_not_force_runtime_license_texts(self):
        report = report_for([
            package("example:runtime", "shipped", "MIT"),
            package("example:build-tool", "build", "Apache-2.0"),
            package("example:test-tool", "test", "Apache-2.0"),
        ])

        completed = self.run_check_with_report(report)

        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

    def run_check(self, *extra_args):
        return subprocess.run(
            [sys.executable, str(CHECK_LICENSE_TEXTS), "--bundle", str(self.bundle), *extra_args],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def run_check_with_report(self, report, *extra_args):
        report_path = self.root / "report.json"
        report_path.write_text(json.dumps(report))
        command = f"import pathlib; print(pathlib.Path({json.dumps(str(report_path))}).read_text())"
        return self.run_check(*extra_args, "--license-report-command", sys.executable, "-c", command)

    def artifact_with_bundle(self):
        artifact = self.root / "EcritumRuntime.xcframework"
        license_dir = artifact / "macos-arm64" / "EcritumRuntime.framework" / "Resources" / "Licenses"
        shutil.copytree(self.bundle, license_dir)
        shutil.copyfile(ROOT / "LICENSE", license_dir / FIRST_PARTY_PACKAGED_LICENSE)
        return artifact

    def write_zip_with_bundle(self, release_zip, bundle, include_first_party_license=True):
        prefix = "EcritumRuntime.xcframework/macos-arm64/EcritumRuntime.framework/Resources/Licenses/"
        with zipfile.ZipFile(release_zip, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(bundle.iterdir()):
                if path.is_file():
                    archive.write(path, prefix + path.name)
            if include_first_party_license:
                archive.write(ROOT / "LICENSE", prefix + FIRST_PARTY_PACKAGED_LICENSE)


if __name__ == "__main__":
    unittest.main()
