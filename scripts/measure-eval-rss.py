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

#include <mach/mach.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t resident_size(void) {
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    kern_return_t result = task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count);
    if (result != KERN_SUCCESS) {
        return 0;
    }
    return (uint64_t)info.resident_size;
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
    uint64_t rss_after_context = resident_size();

    status = ecritum_eval_start(context, view(__LANGUAGE__), bytes(__SOURCE__), view(__SOURCE_NAME__), empty_bytes(), &job, &error);
    if (status != ECRITUM_OK) return 12;
    status = ecritum_job_wait(job, 5000000000ULL, &state, &error);
    if (status != ECRITUM_OK) return 13;
    if (state != ECRITUM_JOB_SUCCEEDED) return 14;
    status = ecritum_job_result(job, &result, &error);
    if (status != ECRITUM_OK) return 15;
    status = ecritum_value_get_int(result, &raw);
    if (status != ECRITUM_OK || raw != 42) return 16;
    uint64_t rss_after_eval = resident_size();

    (void)ecritum_value_destroy(&result);
    (void)ecritum_job_destroy(&job, &error);
    (void)ecritum_context_destroy(&context, &error);
    (void)ecritum_runtime_destroy(&runtime, &error);

    printf("%llu %llu\n",
        (unsigned long long)rss_after_context,
        (unsigned long long)rss_after_eval);
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


parser = argparse.ArgumentParser(description="Measure RSS after an eval through the packaged C ABI.")
parser.add_argument("--artifact", default="dist/local/EcritumRuntime.xcframework")
parser.add_argument("--build-dir", default="build/perf")
parser.add_argument("--name", default="eval-rss")
parser.add_argument("--language", default="clojure")
parser.add_argument("--source", default="(+ 40 2)")
parser.add_argument("--source-name", default="bench-eval-rss.clj")
parser.add_argument("--runs", type=int, default=10)
parser.add_argument("--max-rss-after-eval-bytes", type=int, default=512 * 1024 * 1024)
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
    print(json.dumps({
        "name": args.name,
        "artifact": str(artifact),
        "ok": False,
        "violations": [f"missing artifact binary: {binary}"],
    }, indent=2, sort_keys=True))
    raise SystemExit(1)

build_dir = Path(args.build_dir)
build_dir.mkdir(parents=True, exist_ok=True)
source = build_dir / f"{args.name}.c"
runner = build_dir / args.name
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

context_rss = []
eval_rss = []
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
    parts = completed.stdout.strip().split()
    if len(parts) != 2:
        failures.append({"returncode": 0, "stderr": f"unexpected output: {completed.stdout!r}"})
        continue
    context_value, eval_value = [int(part) for part in parts]
    context_rss.append(context_value)
    eval_rss.append(eval_value)

violations = []
if failures:
    violations.append(f"{len(failures)} run(s) failed")
metrics = {}
if eval_rss:
    metrics = {
        "rss_after_context_bytes": stats(context_rss),
        "rss_after_eval_bytes": stats(eval_rss),
    }
    if metrics["rss_after_eval_bytes"]["p95"] > args.max_rss_after_eval_bytes:
        violations.append("RSS after eval p95 exceeds budget")

payload = {
    "name": args.name,
    "artifact": str(artifact),
    "framework_binary": str(binary),
    "language": args.language,
    "runs_requested": args.runs,
    "runs_completed": len(eval_rss),
    "budget_bytes": {
        "rss_after_eval_p95": args.max_rss_after_eval_bytes,
    },
    "metrics": metrics,
    "failures": failures,
    "ok": not violations,
    "violations": violations,
}
print(json.dumps(payload, indent=2, sort_keys=True))
raise SystemExit(0 if payload["ok"] else 1)
