#!/usr/bin/env python3
import argparse
import json
import subprocess


BASELINE = {
    "shipped": [
        {"name": "borkdude:edamame", "version": "1.5.37", "spdx": "EPL-1.0"},
        {"name": "borkdude:graal.locking", "version": "0.0.2", "spdx": "EPL-1.0"},
        {"name": "EcritumRuntime.xcframework", "version": "0.1.0", "spdx": "MIT"},
        {"name": "GraalVM Native Image embedded runtime code", "version": "25.0.2", "spdx": "GPL-2.0-only WITH Classpath-exception-2.0"},
        {"name": "org.babashka:sci", "version": "0.12.51", "spdx": "EPL-1.0"},
        {"name": "org.babashka:sci.impl.types", "version": "0.0.2", "spdx": "EPL-1.0"},
        {"name": "org.bouncycastle:bcpkix-jdk18on", "version": "1.78.1", "spdx": "LicenseRef-Bouncy-Castle"},
        {"name": "org.bouncycastle:bcprov-jdk18on", "version": "1.78.1", "spdx": "LicenseRef-Bouncy-Castle"},
        {"name": "org.bouncycastle:bcutil-jdk18on", "version": "1.78.1", "spdx": "LicenseRef-Bouncy-Castle"},
        {"name": "org.clojure:clojure", "version": "1.10.3", "spdx": "EPL-1.0"},
        {"name": "org.clojure:core.specs.alpha", "version": "0.2.56", "spdx": "EPL-1.0"},
        {"name": "org.clojure:spec.alpha", "version": "0.2.194", "spdx": "EPL-1.0"},
        {"name": "org.clojure:tools.reader", "version": "1.5.2", "spdx": "EPL-1.0"},
        {"name": "org.graalvm.js:js-language", "version": "25.0.2", "spdx": "UPL-1.0 AND MIT"},
        {"name": "org.graalvm.polyglot:polyglot", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.polyglot:python", "version": "25.0.2", "spdx": "MIT AND PSF-2.0 AND UPL-1.0"},
        {"name": "org.graalvm.python:python", "version": "25.0.2", "spdx": "UPL-1.0 AND MIT AND PSF-2.0"},
        {"name": "org.graalvm.python:python-language", "version": "25.0.2", "spdx": "UPL-1.0 AND MIT AND PSF-2.0"},
        {"name": "org.graalvm.python:python-resources", "version": "25.0.2", "spdx": "UPL-1.0 AND MIT AND PSF-2.0"},
        {"name": "org.graalvm.regex:regex", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.sdk:collections", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.sdk:jniutils", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.shadowed:icu4j", "version": "25.0.2", "spdx": "ICU"},
        {"name": "org.graalvm.shadowed:jcodings", "version": "25.0.2", "spdx": "MIT"},
        {"name": "org.graalvm.shadowed:json", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.shadowed:xz", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.tools:profiler-tool", "version": "25.0.2", "spdx": "GPL-2.0-only WITH Classpath-exception-2.0"},
        {"name": "org.graalvm.truffle:truffle-api", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.truffle:truffle-compiler", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.truffle:truffle-nfi", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.truffle:truffle-nfi-libffi", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.truffle:truffle-nfi-panama", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.truffle:truffle-runtime", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.luaj:luaj-jme", "version": "3.0.1", "spdx": "MIT"},
        # M12-002 Slice 2: Ruby (TruffleRuby) is now a default shipped language
        # with LLVM EXCLUDED per ADR-0028. These are the seven net-new shipped
        # coordinates Ruby adds beyond the pre-Ruby 4-language baseline. The SPDX
        # strings are copied VERBATIM from `SOURCE_DATE_EPOCH=0 python3
        # scripts/license-report.py` (POM-order, deduped, " AND "-joined; never
        # alphabetized). The six org.graalvm.llvm:* artifacts and
        # org.graalvm.shadowed:antlr4 are NOT shipped (antlr4 is transitive under
        # the excluded llvm-language) and are asserted absent below.
        {"name": "dev.truffleruby:truffleruby", "version": "34.0.1", "spdx": "EPL-2.0 AND BSD-3-Clause AND BSD-2-Clause AND MIT AND UPL-1.0 AND ICU"},
        {"name": "dev.truffleruby.internal:runtime", "version": "34.0.1", "spdx": "EPL-2.0 AND BSD-3-Clause AND BSD-2-Clause AND MIT"},
        {"name": "dev.truffleruby.internal:resources", "version": "34.0.1", "spdx": "EPL-2.0 AND MIT AND BSD-2-Clause AND BSD-3-Clause"},
        {"name": "dev.truffleruby.internal:annotations", "version": "34.0.1", "spdx": "EPL-2.0"},
        {"name": "dev.truffleruby.internal:shared", "version": "34.0.1", "spdx": "EPL-2.0"},
        {"name": "dev.truffleruby.shadowed:joni", "version": "34.0.1", "spdx": "MIT"},
    ],
    "build": [
        {"name": "org.graalvm.sdk:nativeimage", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.sdk:word", "version": "25.0.2", "spdx": "UPL-1.0"},
    ],
    "test": [
        {"name": "org.junit.jupiter:junit-jupiter", "version": "5.14.1", "spdx": "EPL-2.0"},
    ],
}

# M12-002 Slice 2 / ADR-0028: TruffleRuby ships with its LLVM/Sulong backend
# EXCLUDED. The six org.graalvm.llvm:* artifacts and org.graalvm.shadowed:antlr4
# (transitive only under the excluded llvm-language) must therefore NEVER appear
# in the default shipped baseline or the live report. This guard keys off the
# explicit coordinate set so antlr4 (which shares no 'llvm' substring) cannot
# slip in silently.
LLVM_EXCLUDED_FORBIDDEN_COORDINATES = [
    "org.graalvm.llvm:llvm-native",
    "org.graalvm.llvm:llvm-api",
    "org.graalvm.llvm:llvm-language-nfi",
    "org.graalvm.llvm:llvm-language-native",
    "org.graalvm.llvm:llvm-language",
    "org.graalvm.llvm:llvm-language-native-resources",
    "org.graalvm.shadowed:antlr4",
]


def llvm_excluded_forbidden_coordinate_errors(current_shipped_names):
    """ADR-0028: neither the reviewed default baseline nor the live shipped
    report may contain the LLVM-excluded coordinates. Checking both the static
    baseline and the live report guards against accidental re-introduction of
    LLVM/antlr4 into the default artifact."""
    baseline_shipped = {item["name"] for item in BASELINE["shipped"]}
    errors = []
    for name in LLVM_EXCLUDED_FORBIDDEN_COORDINATES:
        if name in baseline_shipped:
            errors.append(f"default baseline must not contain LLVM-excluded coordinate: {name}")
        if name in current_shipped_names:
            errors.append(f"shipped report must not contain LLVM-excluded coordinate: {name}")
    return errors


def package_scope(package):
    comment = package["annotations"][0]["comment"]
    for part in comment.split(";"):
        part = part.strip()
        if part.startswith("ecritum-scope="):
            return part.split("=", 1)[1]
    return "unknown"


parser = argparse.ArgumentParser(description="Check dependency/license inventory delta against the reviewed release baseline.")
parser.add_argument("--license-report-command", nargs=argparse.REMAINDER, default=["python3", "scripts/license-report.py"])
args = parser.parse_args()
if not args.license_report_command:
    args.license_report_command = ["python3", "scripts/license-report.py"]

baseline = json.loads(json.dumps(BASELINE))

violations = []

completed = subprocess.run(args.license_report_command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
if completed.returncode != 0:
    violations.append(f"license report exited {completed.returncode}")
    report = None
else:
    report = json.loads(completed.stdout)

current = {"shipped": [], "build": [], "test": []}
if report is not None:
    for package in report["packages"]:
        scope = package_scope(package)
        current.setdefault(scope, []).append({
            "name": package["name"],
            "version": package["versionInfo"],
            "spdx": package["licenseConcluded"],
        })
    for key in current:
        current[key] = sorted(current[key], key=lambda item: item["name"].lower())

# ADR-0028: the LLVM/Sulong backend and antlr4 must stay absent from the default
# shipped artifact (baseline and live report alike).
current_shipped_names = {item["name"] for item in current.get("shipped", [])}
violations.extend(llvm_excluded_forbidden_coordinate_errors(current_shipped_names))

for scope, expected in baseline.items():
    expected_sorted = sorted(expected, key=lambda item: item["name"].lower())
    actual = current.get(scope, [])
    expected_by_name = {item["name"]: item for item in expected_sorted}
    actual_by_name = {item["name"]: item for item in actual}
    added = sorted(set(actual_by_name) - set(expected_by_name))
    removed = sorted(set(expected_by_name) - set(actual_by_name))
    if added:
        violations.append(f"{scope} dependencies added: {', '.join(added)}")
    if removed:
        violations.append(f"{scope} dependencies removed: {', '.join(removed)}")
    for name in sorted(set(actual_by_name) & set(expected_by_name)):
        expected_item = expected_by_name[name]
        actual_item = actual_by_name[name]
        if actual_item != expected_item:
            violations.append(
                f"{scope} dependency changed: {name} expected version={expected_item['version']} spdx={expected_item['spdx']} "
                + f"actual version={actual_item['version']} spdx={actual_item['spdx']}"
            )

payload = {
    "artifactKind": "default",
    "baseline": baseline,
    "current": current,
    "ok": not violations,
    "violations": violations,
    "license_report_command": args.license_report_command,
}
print(json.dumps(payload, indent=2, sort_keys=True))
raise SystemExit(0 if payload["ok"] else 1)
