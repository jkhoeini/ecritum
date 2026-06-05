#!/usr/bin/env python3
import argparse
import json
import subprocess


BASELINE = {
    "shipped": [
        "borkdude:edamame",
        "borkdude:graal.locking",
        "EcritumRuntime.xcframework",
        "GraalVM Native Image embedded runtime code",
        "org.babashka:sci",
        "org.babashka:sci.impl.types",
        "org.clojure:clojure",
        "org.clojure:core.specs.alpha",
        "org.clojure:spec.alpha",
        "org.clojure:tools.reader",
        "org.graalvm.js:js-language",
        "org.graalvm.polyglot:polyglot",
        "org.graalvm.regex:regex",
        "org.graalvm.sdk:collections",
        "org.graalvm.sdk:jniutils",
        "org.graalvm.shadowed:icu4j",
        "org.graalvm.shadowed:xz",
        "org.graalvm.truffle:truffle-api",
        "org.graalvm.truffle:truffle-compiler",
        "org.graalvm.truffle:truffle-runtime",
        "org.luaj:luaj-jme",
    ],
    "build": [
        "org.graalvm.sdk:nativeimage",
        "org.graalvm.sdk:word",
    ],
    "test": [
        "org.junit.jupiter:junit-jupiter",
    ],
}


def package_scope(package):
    comment = package["annotations"][0]["comment"]
    for part in comment.split(";"):
        part = part.strip()
        if part.startswith("ecritum-scope="):
            return part.split("=", 1)[1]
    return "unknown"


parser = argparse.ArgumentParser(description="Check dependency/license inventory delta against the M1 baseline.")
parser.add_argument("--license-report-command", nargs="+", default=["python3", "scripts/license-report.py"])
args = parser.parse_args()

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
        current.setdefault(scope, []).append(package["name"])
    for key in current:
        current[key] = sorted(current[key])

for scope, expected in BASELINE.items():
    expected_sorted = sorted(expected)
    actual = current.get(scope, [])
    added = sorted(set(actual) - set(expected_sorted))
    removed = sorted(set(expected_sorted) - set(actual))
    if added:
        violations.append(f"{scope} dependencies added: {', '.join(added)}")
    if removed:
        violations.append(f"{scope} dependencies removed: {', '.join(removed)}")

payload = {
    "baseline": BASELINE,
    "current": current,
    "ok": not violations,
    "violations": violations,
    "license_report_command": args.license_report_command,
}
print(json.dumps(payload, indent=2, sort_keys=True))
raise SystemExit(0 if payload["ok"] else 1)
