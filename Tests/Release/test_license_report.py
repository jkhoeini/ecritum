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
        self.assertIn("org.graalvm.polyglot:python", package_names)
        self.assertIn("org.graalvm.python:python-language", package_names)
        self.assertIn("org.graalvm.python:python-resources", package_names)
        self.assertIn("org.graalvm.tools:profiler-tool", package_names)
        self.assertIn("org.graalvm.shadowed:json", package_names)
        self.assertIn("org.graalvm.truffle:truffle-nfi", package_names)
        self.assertIn("org.graalvm.truffle:truffle-nfi-libffi", package_names)
        self.assertIn("org.graalvm.truffle:truffle-nfi-panama", package_names)
        self.assertIn("org.bouncycastle:bcprov-jdk18on", package_names)
        self.assertIn("org.bouncycastle:bcpkix-jdk18on", package_names)
        self.assertIn("org.bouncycastle:bcutil-jdk18on", package_names)
        self.assertIn("org.luaj:luaj-jme", package_names)
        self.assertEqual(package_named(report, "org.graalvm.python:python-language")["licenseConcluded"], "UPL-1.0 AND MIT AND PSF-2.0")
        self.assertEqual(package_named(report, "org.bouncycastle:bcprov-jdk18on")["licenseConcluded"], "LicenseRef-Bouncy-Castle")
        self.assertIn("artifact-kind=default", report["annotations"][0]["comment"])
        self.assertIn("included-runtimes=clojure,javascript,lua,python,ruby", report["annotations"][0]["comment"])

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

    # ---- M12-002 Slice 2: Ruby (TruffleRuby) shipped in the default artifact ----

    # The seven net-new shipped coordinates Ruby adds beyond the pre-Ruby
    # 4-language baseline, with LLVM EXCLUDED per ADR-0028. SPDX strings are the
    # generated, POM-ordered, deduped, " AND "-joined licenseConcluded values.
    RUBY_SHIPPED_SPDX = {
        "dev.truffleruby:truffleruby": "EPL-2.0 AND BSD-3-Clause AND BSD-2-Clause AND MIT AND UPL-1.0 AND ICU",
        "dev.truffleruby.internal:runtime": "EPL-2.0 AND BSD-3-Clause AND BSD-2-Clause AND MIT",
        "dev.truffleruby.internal:resources": "EPL-2.0 AND MIT AND BSD-2-Clause AND BSD-3-Clause",
        "dev.truffleruby.internal:annotations": "EPL-2.0",
        "dev.truffleruby.internal:shared": "EPL-2.0",
        "dev.truffleruby.shadowed:joni": "MIT",
        "org.graalvm.shadowed:jcodings": "MIT",
    }

    # ADR-0028: the LLVM/Sulong backend and antlr4 (transitive only under the
    # excluded llvm-language) must NEVER appear in the default shipped artifact.
    LLVM_EXCLUDED_COORDINATES = [
        "org.graalvm.llvm:llvm-native",
        "org.graalvm.llvm:llvm-api",
        "org.graalvm.llvm:llvm-language-nfi",
        "org.graalvm.llvm:llvm-language-native",
        "org.graalvm.llvm:llvm-language",
        "org.graalvm.llvm:llvm-language-native-resources",
        "org.graalvm.shadowed:antlr4",
    ]

    def test_default_mode_includes_ruby_shipped_packages_with_exact_spdx(self):
        completed = run_license_report()
        self.assertEqual(completed.returncode, 0, completed.stderr)
        report = json.loads(completed.stdout)
        for coord, expected_spdx in self.RUBY_SHIPPED_SPDX.items():
            pkg = package_named(report, coord)
            self.assertEqual(pkg["licenseConcluded"], expected_spdx, f"{coord} SPDX mismatch")
            self.assertEqual(pkg["licenseDeclared"], expected_spdx)
            self.assertEqual(annotation_value(pkg, "ecritum-scope"), "shipped")

    def test_default_mode_excludes_llvm_and_antlr4(self):
        # ADR-0028: TruffleRuby ships with LLVM excluded; antlr4 is transitive
        # only under the excluded llvm-language and must also be absent.
        report = json.loads(run_license_report().stdout)
        names = {item["name"] for item in report["packages"]}
        for coord in self.LLVM_EXCLUDED_COORDINATES:
            self.assertNotIn(coord, names, f"LLVM-excluded coordinate leaked into default: {coord}")

    def test_default_sbom_kind_namespace_and_runtimes(self):
        report = json.loads(run_license_report().stdout)
        self.assertEqual(report["documentNamespace"], "https://ecritum.dev/spdx/ecritum-license-inventory")
        doc_comment = report["annotations"][0]["comment"]
        self.assertIn("artifact-kind=default", doc_comment)
        self.assertIn("included-runtimes=clojure,javascript,lua,python,ruby;", doc_comment)
        self.assertEqual(len(report["annotations"]), 1)
        self.assertEqual(report["documentDescribes"], ["SPDXRef-Package-EcritumRuntime"])
        # No shipped license blockers and no POM metadata errors.
        self.assertIn("blockers=[]", doc_comment)

    def test_ruby_resources_surface_annotation_is_separate_object(self):
        # The resources package carries the denied-surface classification as a
        # SEPARATE annotation, never in annotations[0] (which the ';'/'=' parser
        # would corrupt on tokens like 'ffi-fiddle').
        report = json.loads(run_license_report().stdout)
        resources = package_named(report, "dev.truffleruby.internal:resources")
        self.assertNotIn("ruby-denied-surface", resources["annotations"][0]["comment"])
        surface = [a["comment"] for a in resources["annotations"] if a["comment"].startswith("ruby-denied-surface=")]
        self.assertEqual(
            surface,
            ["ruby-denied-surface=rubygems,bundler,openssl,sockets,ffi-fiddle,native-extensions,native-so"],
        )
        # annotations[0] still parses cleanly for scope (no corruption).
        self.assertEqual(annotation_value(resources, "ecritum-scope"), "shipped")

    def test_default_mode_is_deterministic_under_source_date_epoch(self):
        first = run_license_report()
        second = run_license_report()
        self.assertEqual(first.stdout, second.stdout)


if __name__ == "__main__":
    unittest.main()
