#!/usr/bin/env python3
import argparse
import json
import subprocess


BASELINE = {
    "shipped": [
        {"name": "borkdude:edamame", "version": "1.5.37", "spdx": "EPL-1.0"},
        {"name": "borkdude:graal.locking", "version": "0.0.2", "spdx": "EPL-1.0"},
        {"name": "EcritumRuntime.xcframework", "version": "0.1.0-dev", "spdx": "NOASSERTION"},
        {"name": "GraalVM Native Image embedded runtime code", "version": "25.0.2", "spdx": "GPL-2.0-only WITH Classpath-exception-2.0"},
        {"name": "org.babashka:sci", "version": "0.12.51", "spdx": "EPL-1.0"},
        {"name": "org.babashka:sci.impl.types", "version": "0.0.2", "spdx": "EPL-1.0"},
        {"name": "org.clojure:clojure", "version": "1.10.3", "spdx": "EPL-1.0"},
        {"name": "org.clojure:core.specs.alpha", "version": "0.2.56", "spdx": "EPL-1.0"},
        {"name": "org.clojure:spec.alpha", "version": "0.2.194", "spdx": "EPL-1.0"},
        {"name": "org.clojure:tools.reader", "version": "1.5.2", "spdx": "EPL-1.0"},
        {"name": "org.graalvm.js:js-language", "version": "25.0.2", "spdx": "UPL-1.0 AND MIT"},
        {"name": "org.graalvm.polyglot:polyglot", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.regex:regex", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.sdk:collections", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.sdk:jniutils", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.shadowed:icu4j", "version": "25.0.2", "spdx": "ICU"},
        {"name": "org.graalvm.shadowed:xz", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.truffle:truffle-api", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.truffle:truffle-compiler", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.truffle:truffle-runtime", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.luaj:luaj-jme", "version": "3.0.1", "spdx": "MIT"},
    ],
    "build": [
        {"name": "org.graalvm.sdk:nativeimage", "version": "25.0.2", "spdx": "UPL-1.0"},
        {"name": "org.graalvm.sdk:word", "version": "25.0.2", "spdx": "UPL-1.0"},
    ],
    "test": [
        {"name": "org.junit.jupiter:junit-jupiter", "version": "5.14.1", "spdx": "EPL-2.0"},
    ],
}

FULL_ONLY_SHIPPED_PACKAGES = {
    "org.graalvm.polyglot:polyglot",
    "org.graalvm.sdk:collections",
    "org.graalvm.js:js-language",
    "org.graalvm.regex:regex",
    "org.graalvm.truffle:truffle-api",
    "org.graalvm.shadowed:icu4j",
    "org.graalvm.shadowed:xz",
    "org.graalvm.truffle:truffle-runtime",
    "org.graalvm.sdk:jniutils",
    "org.graalvm.truffle:truffle-compiler",
    "org.luaj:luaj-jme",
}


def package_scope(package):
    comment = package["annotations"][0]["comment"]
    for part in comment.split(";"):
        part = part.strip()
        if part.startswith("ecritum-scope="):
            return part.split("=", 1)[1]
    return "unknown"


parser = argparse.ArgumentParser(description="Check dependency/license inventory delta against the reviewed release baseline.")
parser.add_argument("--lane", choices=["core", "full"], default="full")
parser.add_argument("--license-report-command", nargs=argparse.REMAINDER, default=["python3", "scripts/license-report.py"])
args = parser.parse_args()
if not args.license_report_command:
    args.license_report_command = ["python3", "scripts/license-report.py", "--lane", args.lane]
elif args.license_report_command == ["python3", "scripts/license-report.py"]:
    args.license_report_command = ["python3", "scripts/license-report.py", "--lane", args.lane]

baseline = json.loads(json.dumps(BASELINE))
if args.lane == "core":
    baseline["shipped"] = [
        item for item in baseline["shipped"]
        if item["name"] not in FULL_ONLY_SHIPPED_PACKAGES
    ]

completed = subprocess.run(args.license_report_command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
violations = []
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
    "baseline": baseline,
    "current": current,
    "lane": args.lane,
    "ok": not violations,
    "violations": violations,
    "license_report_command": args.license_report_command,
}
print(json.dumps(payload, indent=2, sort_keys=True))
raise SystemExit(0 if payload["ok"] else 1)
