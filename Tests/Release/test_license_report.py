#!/usr/bin/env python3
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LICENSE_REPORT = ROOT / "scripts" / "license-report.py"


def run_license_report(*args):
    env = os.environ.copy()
    env["SOURCE_DATE_EPOCH"] = "0"
    return subprocess.run(
        [sys.executable, str(LICENSE_REPORT), *args],
        cwd=ROOT,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def package_named(report, name):
    matches = [item for item in report["packages"] if item["name"] == name]
    if len(matches) != 1:
        raise AssertionError(f"expected one package named {name}, found {len(matches)}")
    return matches[0]


def purls(package):
    return [
        ref["referenceLocator"]
        for ref in package.get("externalRefs", [])
        if ref.get("referenceCategory") == "PACKAGE-MANAGER"
        and ref.get("referenceType") == "purl"
    ]


def annotation_value(package, key):
    prefix = f"{key}="
    comment = package["annotations"][0]["comment"]
    for part in comment.split(";"):
        part = part.strip()
        if part.startswith(prefix):
            return part[len(prefix):]
    return None


class LicenseReportTest(unittest.TestCase):
    def test_graalvm_native_image_runtime_license_is_resolved_from_adr_011_evidence(self):
        completed = run_license_report()
        self.assertEqual(completed.returncode, 0, completed.stderr)
        report = json.loads(completed.stdout)

        graal_runtime = package_named(report, "GraalVM Native Image embedded runtime code")

        self.assertEqual(graal_runtime["licenseConcluded"], "GPL-2.0-only WITH Classpath-exception-2.0")
        comment = graal_runtime["annotations"][0]["comment"]
        self.assertIn("release-blocker=false", comment)
        self.assertIn("GraalVM Community 25.0.2 LICENSE_NATIVEIMAGE.txt", comment)
        self.assertIn("sha256=11a8fe0c63dcff8bd8674b89a5895dfbcf5f7e5453cf0a33566c4b3fb64e404c", comment)

    def test_strict_mode_accepts_first_party_mit_license(self):
        completed = run_license_report("--strict")

        self.assertEqual(completed.returncode, 0, completed.stderr)
        report = json.loads(completed.stdout)
        first_party = package_named(report, "EcritumRuntime.xcframework")

        self.assertEqual(first_party["licenseConcluded"], "MIT")
        self.assertEqual(first_party["licenseDeclared"], "MIT")
        self.assertEqual(first_party["copyrightText"], "Copyright (c) 2026 Ecritum contributors")
        self.assertEqual(annotation_value(first_party, "license-source"), "LICENSE")
        self.assertIn("release-blocker=false", first_party["annotations"][0]["comment"])
        self.assertNotIn("has unknown shipped license", completed.stderr)

    def test_strict_mode_rejects_missing_first_party_license_file(self):
        with tempfile.TemporaryDirectory(prefix="ecritum-missing-license-") as tmp:
            completed = run_license_report("--strict", "--first-party-license-file", str(Path(tmp) / "LICENSE"))

        self.assertEqual(completed.returncode, 1)
        self.assertIn("missing first-party LICENSE file", completed.stderr)

    def test_strict_mode_rejects_stale_first_party_license_text(self):
        with tempfile.TemporaryDirectory(prefix="ecritum-stale-license-") as tmp:
            stale_license = Path(tmp) / "LICENSE"
            stale_license.write_text("MIT License\n\nCopyright (c) 2026 Someone Else\n")
            completed = run_license_report("--strict", "--first-party-license-file", str(stale_license))

        self.assertEqual(completed.returncode, 1)
        self.assertIn("first-party LICENSE hash mismatch", completed.stderr)

    def test_multi_license_pom_metadata_is_conserved_as_combined_spdx_expression(self):
        completed = run_license_report()
        self.assertEqual(completed.returncode, 0, completed.stderr)
        report = json.loads(completed.stdout)

        js_language = package_named(report, "org.graalvm.js:js-language")

        self.assertEqual(js_language["licenseConcluded"], "UPL-1.0 AND MIT")
        self.assertIn("Universal Permissive License, Version 1.0 AND MIT License", js_language["annotations"][0]["comment"])

    def test_packages_include_purl_external_refs_for_vulnerability_tracking(self):
        completed = run_license_report()
        self.assertEqual(completed.returncode, 0, completed.stderr)
        report = json.loads(completed.stdout)

        js_language = package_named(report, "org.graalvm.js:js-language")
        first_party = package_named(report, "EcritumRuntime.xcframework")
        graal_runtime = package_named(report, "GraalVM Native Image embedded runtime code")

        self.assertIn("pkg:maven/org.graalvm.js/js-language@25.0.2", purls(js_language))
        self.assertIn("pkg:generic/ecritum/EcritumRuntime.xcframework@0.1.0", purls(first_party))
        self.assertIn("pkg:generic/oracle/graalvm-native-image-embedded-runtime@25.0.2", purls(graal_runtime))

    def test_default_artifact_includes_polyglot_runtime_packages(self):
        completed = run_license_report()
        self.assertEqual(completed.returncode, 0, completed.stderr)
        report = json.loads(completed.stdout)
        package_names = {item["name"] for item in report["packages"]}

        self.assertIn("org.babashka:sci", package_names)
        self.assertIn("org.graalvm.sdk:nativeimage", package_names)
        self.assertIn("org.graalvm.polyglot:polyglot", package_names)
        self.assertIn("org.graalvm.js:js-language", package_names)
        self.assertIn("org.luaj:luaj-jme", package_names)
        self.assertIn("artifact-kind=default", report["annotations"][0]["comment"])
        self.assertIn("included-runtimes=clojure,javascript,lua", report["annotations"][0]["comment"])

    def test_notices_include_blockers_scoped_components_and_full_text_warning(self):
        completed = run_license_report("--notices")

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("# Ecritum Third Party Notices", completed.stdout)
        self.assertIn("- None", completed.stdout)
        self.assertNotIn("EcritumRuntime.xcframework has unknown shipped license", completed.stdout)
        self.assertIn("| EcritumRuntime.xcframework | 0.1.0 | MIT | LICENSE |", completed.stdout)
        self.assertIn("| GraalVM Native Image embedded runtime code | 25.0.2 | GPL-2.0-only WITH Classpath-exception-2.0 |", completed.stdout)
        self.assertIn("public release packaging must carry the required upstream license texts", completed.stdout)

    def test_generated_notices_match_checked_in_notices(self):
        completed = run_license_report("--notices")

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(completed.stdout, (ROOT / "THIRD_PARTY_NOTICES.md").read_text())

    def test_strict_mode_rejects_mismatched_graalvm_license_evidence(self):
        with tempfile.TemporaryDirectory(prefix="ecritum-graalvm-evidence-") as tmp:
            home = Path(tmp)
            bin_dir = home / "bin"
            bin_dir.mkdir()
            (home / "LICENSE_NATIVEIMAGE.txt").write_text("not the accepted license evidence\n")
            native_image = bin_dir / "native-image"
            native_image.write_text(
                "#!/usr/bin/env sh\n"
                "cat <<'EOF'\n"
                "native-image 25.0.2 2026-01-20\n"
                "GraalVM Runtime Environment GraalVM CE 25.0.2+10.1 (build 25.0.2+10-jvmci-b01)\n"
                "Substrate VM GraalVM CE 25.0.2+10.1 (build 25.0.2+10, serial gc)\n"
                "EOF\n"
            )
            native_image.chmod(native_image.stat().st_mode | stat.S_IXUSR)

            completed = run_license_report("--strict", "--graalvm-home", str(home))

        self.assertEqual(completed.returncode, 1)
        self.assertIn("GraalVM Native Image license evidence hash mismatch", completed.stderr)

    def test_strict_mode_rejects_missing_expected_maven_pom_metadata(self):
        with tempfile.TemporaryDirectory(prefix="ecritum-empty-m2-") as tmp:
            completed = run_license_report("--strict", "--m2", tmp)

        self.assertEqual(completed.returncode, 1)
        self.assertIn("missing expected POM for org.graalvm.sdk:nativeimage", completed.stderr)


if __name__ == "__main__":
    unittest.main()
