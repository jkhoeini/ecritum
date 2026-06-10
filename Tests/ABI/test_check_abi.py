#!/usr/bin/env python3
import json
import os
import shutil
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECK_ABI = ROOT / "scripts" / "check-abi.sh"


class CheckAbiTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ecritum-abi-")
        self.root = Path(self.tmp.name)
        self.fake_bin = self.root / "bin"
        self.fake_bin.mkdir()
        self._write_tool("nm", """#!/usr/bin/env bash
set -euo pipefail
binary="${@: -1}"
cat "$binary.symbols"
""")
        self._write_tool("otool", """#!/usr/bin/env bash
set -euo pipefail
binary="${@: -1}"
case "$1" in
  -l) cat "$binary.otool" ;;
  *) echo "unsupported fake otool invocation: $*" >&2; exit 2 ;;
esac
""")

        self.header = self.root / "ecritum.h"
        self.java_status = self.root / "EcritumStatus.java"
        self.swift_status = self.root / "EcritumError.swift"
        self.swift_job_state = self.root / "EcritumLifecycle.swift"
        shutil.copy(ROOT / "Sources" / "CEcritum" / "include" / "ecritum.h", self.header)
        shutil.copy(ROOT / "native" / "src" / "main" / "java" / "ecritum" / "EcritumStatus.java", self.java_status)
        shutil.copy(ROOT / "Sources" / "Ecritum" / "EcritumError.swift", self.swift_status)
        shutil.copy(ROOT / "Sources" / "Ecritum" / "EcritumLifecycle.swift", self.swift_job_state)

        self.manifest = json.loads((ROOT / "docs" / "abi" / "ecritum-c-abi.json").read_text())
        self.manifest["publicHeader"] = str(self.header)
        self.manifest["javaStatus"] = str(self.java_status)
        self.manifest["swiftStatus"] = str(self.swift_status)
        self.manifest["swiftJobState"] = str(self.swift_job_state)
        self.manifest_path = self.root / "manifest.json"
        self._write_manifest()

        self.artifact = self.root / "EcritumRuntime.xcframework"
        for slice_name in ["macos-arm64", "macos-x86_64"]:
            self._create_slice(slice_name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_valid_fixture_passes(self):
        self.assert_ok()

    def test_rejects_extra_public_export(self):
        self._append_symbols("macos-arm64", "0000000000000000 T _ecritum_unmanifested\n")
        self.assert_fails("unmanifested public artifact symbols")

    def test_rejects_private_symbol_pattern_leak(self):
        self._append_symbols("macos-arm64", "0000000000000000 T _graal_attach_thread\n")
        self.assert_fails("private symbol pattern ^_graal_.*$")

    def test_rejects_packaged_header_drift(self):
        packaged = self.artifact / "macos-x86_64" / "EcritumRuntime.framework" / "Headers" / "ecritum.h"
        packaged.write_text(packaged.read_text() + "\n/* drift */\n")
        self.assert_fails("packaged public header differs")

    def test_rejects_renumbered_c_constant(self):
        self.header.write_text(self.header.read_text().replace("#define ECRITUM_ERROR_SCRIPT 17", "#define ECRITUM_ERROR_SCRIPT 99"))
        self.assert_fails("C status ECRITUM_ERROR_SCRIPT expected 17")

    def test_rejects_swift_status_drift(self):
        self.swift_status.write_text(self.swift_status.read_text().replace("case script = 17", "case script = 99"))
        self.assert_fails("Swift status script expected 17")

    def test_rejects_callback_signature_drift(self):
        self.header.write_text(self.header.read_text().replace("void *user_data\n);", "const void *user_data\n);"))
        self.assert_fails("missing public callback declaration")

    def _write_tool(self, name, text):
        path = self.fake_bin / name
        path.write_text(text)
        path.chmod(path.stat().st_mode | stat.S_IXUSR)

    def _write_manifest(self):
        self.manifest_path.write_text(json.dumps(self.manifest, indent=2) + "\n")

    def _create_slice(self, slice_name):
        framework = self.artifact / slice_name / "EcritumRuntime.framework"
        headers = framework / "Headers"
        headers.mkdir(parents=True)
        shutil.copy(self.header, headers / "ecritum.h")
        binary = framework / "EcritumRuntime"
        binary.write_text("")
        symbols = "".join(
            f"0000000000000000 T {function['machoSymbol']}\n"
            for function in self.manifest["functions"]
        )
        (Path(str(binary) + ".symbols")).write_text(symbols)
        (Path(str(binary) + ".otool")).write_text("""Load command 0
          cmd LC_ID_DYLIB
      cmdsize 80
         name @rpath/EcritumRuntime.framework/EcritumRuntime
 current version 0.2.0
compatibility version 0.2.0
""")

    def _append_symbols(self, slice_name, text):
        binary = self.artifact / slice_name / "EcritumRuntime.framework" / "EcritumRuntime"
        with Path(str(binary) + ".symbols").open("a") as handle:
            handle.write(text)

    def run_check(self):
        env = os.environ.copy()
        env["PATH"] = str(self.fake_bin) + os.pathsep + env["PATH"]
        return subprocess.run(
            [str(CHECK_ABI), "--manifest", str(self.manifest_path), "--artifact", str(self.artifact)],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def assert_ok(self):
        completed = self.run_check()
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def assert_fails(self, text):
        completed = self.run_check()
        self.assertNotEqual(completed.returncode, 0, completed.stdout)
        self.assertIn(text, completed.stderr)


if __name__ == "__main__":
    unittest.main()
