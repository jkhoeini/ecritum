#!/usr/bin/env python3
import argparse
import json
import math
import os
import platform
import subprocess
import textwrap
from pathlib import Path


RUNNER_SOURCE = r"""
#include "ecritum.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static ecritum_bytes_t empty_bytes(void) {
    ecritum_bytes_t value = {0};
    return value;
}

static ecritum_bytes_t bytes(const char *text) {
    ecritum_bytes_t value = {(const uint8_t *)text, strlen(text)};
    return value;
}

static ecritum_string_view_t view(const char *text) {
    ecritum_string_view_t value = {text, strlen(text)};
    return value;
}

int main(void) {
    ecritum_runtime_t runtime = 0;
    ecritum_context_t context = 0;
    ecritum_job_t job = 0;
    ecritum_error_t error = 0;
    ecritum_value_t result = 0;
    int state = -1;
    int64_t raw = 0;

    int status = ecritum_runtime_create(empty_bytes(), &runtime, &error);
    if (status != ECRITUM_OK) return 10;
    status = ecritum_context_create(runtime, empty_bytes(), &context, &error);
    if (status != ECRITUM_OK) return 11;

    uint64_t started = now_ns();
    status = ecritum_eval_start(context, view(__LANGUAGE__), bytes(__SOURCE__), view(__SOURCE_NAME__), empty_bytes(), &job, &error);
    if (status != ECRITUM_OK) return 12;
    status = ecritum_job_wait(job, 5000000000ULL, &state, &error);
    if (status != ECRITUM_OK) return 13;
    if (state != ECRITUM_JOB_SUCCEEDED) return 14;
    status = ecritum_job_result(job, &result, &error);
    if (status != ECRITUM_OK) return 15;
    status = ecritum_value_get_int(result, &raw);
    if (status != ECRITUM_OK || raw != 42) return 16;
    uint64_t finished = now_ns();

    (void)ecritum_value_destroy(&result);
    (void)ecritum_job_destroy(&job, &error);
    (void)ecritum_context_destroy(&context, &error);
    (void)ecritum_runtime_destroy(&runtime, &error);

    printf("%llu\n", (unsigned long long)(finished - started));
    return 0;
}
"""


def percentile(values, pct):
    ordered = sorted(values)
    index = max(0, math.ceil((pct / 100.0) * len(ordered)) - 1)
    return ordered[index]


def stats(values):
    return {
        "p50": round(percentile(values, 50), 3),
        "p95": round(percentile(values, 95), 3),
        "min": round(min(values), 3),
        "max": round(max(values), 3),
    }


parser = argparse.ArgumentParser(description="Measure first eval through the packaged C ABI.")
parser.add_argument("--artifact", default="dist/local/EcritumRuntime.xcframework")
parser.add_argument("--build-dir", default="build/perf")
parser.add_argument("--name", default="first-eval")
parser.add_argument("--language", default="clojure")
parser.add_argument("--source", default="(+ 40 2)")
parser.add_argument("--source-name", default="bench-first-eval.clj")
parser.add_argument("--runs", type=int, default=10)
parser.add_argument("--max-p50-ms", type=float, default=500.0)
parser.add_argument("--max-p95-ms", type=float, default=1000.0)
parser.add_argument("--timeout-seconds", type=float, default=10.0)
args = parser.parse_args()

if args.runs <= 0:
    raise SystemExit("--runs must be positive")

machine = platform.machine()
slice_name = f"macos-{machine}"
artifact = Path(args.artifact)
framework_root = artifact / slice_name
framework = framework_root / "EcritumRuntime.framework"
header_dir = framework / "Headers"
binary = framework / "EcritumRuntime"
if not binary.exists():
    payload = {
        "name": args.name,
        "artifact": str(artifact),
        "ok": False,
        "violations": [f"missing artifact binary: {binary}"],
    }
    print(json.dumps(payload, indent=2, sort_keys=True))
    raise SystemExit(1)

build_dir = Path(args.build_dir)
build_dir.mkdir(parents=True, exist_ok=True)
source = build_dir / "first-eval-bench.c"
runner = build_dir / "first-eval-bench"
runner_source = (
    textwrap.dedent(RUNNER_SOURCE)
    .replace("__LANGUAGE__", json.dumps(args.language))
    .replace("__SOURCE__", json.dumps(args.source))
    .replace("__SOURCE_NAME__", json.dumps(args.source_name))
)
source.write_text(runner_source.strip() + "\n")
subprocess.run(
    [
        "clang",
        "-target",
        f"{machine}-apple-macos14.0",
        "-mmacosx-version-min=14.0",
        "-I",
        str(header_dir),
        "-F",
        str(framework_root),
        "-framework",
        "EcritumRuntime",
        str(source),
        "-o",
        str(runner),
    ],
    check=True,
)

eval_ms = []
failures = []
run_env = os.environ.copy()
run_env["DYLD_FRAMEWORK_PATH"] = str(framework_root)
for _ in range(args.runs):
    try:
        completed = subprocess.run(
            [str(runner)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=args.timeout_seconds,
            env=run_env,
        )
    except subprocess.TimeoutExpired as exc:
        failures.append({"timeout_seconds": args.timeout_seconds, "stderr": (exc.stderr or "").strip()})
        continue
    if completed.returncode != 0:
        failures.append({"returncode": completed.returncode, "stderr": completed.stderr.strip()})
        continue
    try:
        eval_ms.append(int(completed.stdout.strip()) / 1_000_000.0)
    except ValueError:
        failures.append({"returncode": 0, "stderr": f"unexpected output: {completed.stdout!r}"})

violations = []
if failures:
    violations.append(f"{len(failures)} run(s) failed")
metrics = {}
if eval_ms:
    metrics["first_eval_ms"] = stats(eval_ms)
    if metrics["first_eval_ms"]["p50"] > args.max_p50_ms:
        violations.append("first eval p50 exceeds budget")
    if metrics["first_eval_ms"]["p95"] > args.max_p95_ms:
        violations.append("first eval p95 exceeds budget")

payload = {
    "name": args.name,
    "artifact": str(artifact),
    "framework_binary": str(binary),
    "runs_requested": args.runs,
    "runs_completed": len(eval_ms),
    "budget_ms": {
        "p50": args.max_p50_ms,
        "p95": args.max_p95_ms,
    },
    "metrics": metrics,
    "failures": failures,
    "ok": not violations,
    "violations": violations,
}
print(json.dumps(payload, indent=2, sort_keys=True))
raise SystemExit(0 if payload["ok"] else 1)
