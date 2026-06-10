#!/usr/bin/env python3
import argparse
import json
import math
import os
import platform
import subprocess
import textwrap
import time
from pathlib import Path


RUNNER_SOURCE = r"""
#include <dlfcn.h>
#include <mach/mach.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef int (*ecritum_version_fn)(char *, size_t);

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static uint64_t resident_size(void) {
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    kern_return_t result = task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count);
    if (result != KERN_SUCCESS) {
        return 0;
    }
    return (uint64_t)info.resident_size;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }

    uint64_t dlopen_start = now_ns();
    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    uint64_t dlopen_end = now_ns();
    if (handle == NULL) {
        fprintf(stderr, "%s\n", dlerror());
        return 3;
    }
    uint64_t rss_after_dlopen = resident_size();

    uint64_t dlsym_start = now_ns();
    ecritum_version_fn version = (ecritum_version_fn)dlsym(handle, "ecritum_version");
    uint64_t dlsym_end = now_ns();
    if (version == NULL) {
        fprintf(stderr, "%s\n", dlerror());
        return 4;
    }

    char buffer[64];
    uint64_t call_start = now_ns();
    int status = version(buffer, sizeof(buffer));
    uint64_t call_end = now_ns();
    if (status != 0) {
        fprintf(stderr, "status=%d\n", status);
        return 5;
    }
    if (strcmp(buffer, "0.2.0") != 0) {
        fprintf(stderr, "version=%s\n", buffer);
        return 6;
    }
    uint64_t rss_after_call = resident_size();

    printf("%llu %llu %llu %llu %llu\n",
        (unsigned long long)(dlopen_end - dlopen_start),
        (unsigned long long)(dlsym_end - dlsym_start),
        (unsigned long long)(call_end - call_start),
        (unsigned long long)rss_after_dlopen,
        (unsigned long long)rss_after_call);
    return 0;
}
"""


def percentile(values, pct):
    ordered = sorted(values)
    index = max(0, math.ceil((pct / 100.0) * len(ordered)) - 1)
    return ordered[index]


def ms(ns):
    return ns / 1_000_000.0


def stats(values):
    return {
        "p50": round(percentile(values, 50), 3),
        "p95": round(percentile(values, 95), 3),
        "min": round(min(values), 3),
        "max": round(max(values), 3),
    }


parser = argparse.ArgumentParser(description="Measure Ecritum runtime load/call/RSS budgets as JSON.")
parser.add_argument("--artifact", default="dist/local/EcritumRuntime.xcframework")
parser.add_argument("--build-dir", default="build/perf")
parser.add_argument("--runs", type=int, default=30)
parser.add_argument("--mode", choices=["startup", "rss", "all"], default="all")
parser.add_argument("--max-process-p50-ms", type=float, default=250.0)
parser.add_argument("--max-process-p95-ms", type=float, default=500.0)
parser.add_argument("--max-dlopen-dlsym-p95-ms", type=float, default=250.0)
parser.add_argument("--max-first-call-p95-ms", type=float, default=750.0)
parser.add_argument("--max-rss-after-call-bytes", type=int, default=75 * 1024 * 1024)
parser.add_argument("--timeout-seconds", type=float, default=10.0)
args = parser.parse_args()

if args.runs <= 0:
    raise SystemExit("--runs must be positive")

machine = platform.machine()
slice_name = f"macos-{machine}"
artifact = Path(args.artifact)
framework_binary = artifact / slice_name / "EcritumRuntime.framework" / "EcritumRuntime"
if not framework_binary.exists():
    raise SystemExit(f"missing artifact binary: {framework_binary}")

build_dir = Path(args.build_dir)
build_dir.mkdir(parents=True, exist_ok=True)
source = build_dir / "runtime-bench.c"
runner = build_dir / "runtime-bench"
source.write_text(textwrap.dedent(RUNNER_SOURCE).strip() + "\n")
subprocess.run(
    [
        "clang",
        "-target",
        f"{machine}-apple-macos14.0",
        "-mmacosx-version-min=14.0",
        str(source),
        "-o",
        str(runner),
    ],
    check=True,
)

process_ms = []
dlopen_ms = []
dlsym_ms = []
dlopen_dlsym_ms = []
first_call_ms = []
rss_after_dlopen = []
rss_after_call = []
failures = []

for _ in range(args.runs):
    started = time.perf_counter()
    try:
        completed = subprocess.run(
            [str(runner), str(framework_binary)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=args.timeout_seconds,
        )
    except subprocess.TimeoutExpired as exc:
        failures.append(
            {
                "timeout_seconds": args.timeout_seconds,
                "stderr": (exc.stderr or "").strip(),
            }
        )
        continue
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    if completed.returncode != 0:
        failures.append(
            {
                "returncode": completed.returncode,
                "stderr": completed.stderr.strip(),
            }
        )
        continue
    parts = completed.stdout.strip().split()
    if len(parts) != 5:
        failures.append({"returncode": 0, "stderr": f"unexpected output: {completed.stdout!r}"})
        continue
    dlopen_ns, dlsym_ns, call_ns, rss_dlopen, rss_call = [int(part) for part in parts]
    process_ms.append(elapsed_ms)
    dlopen_ms.append(ms(dlopen_ns))
    dlsym_ms.append(ms(dlsym_ns))
    dlopen_dlsym_ms.append(ms(dlopen_ns + dlsym_ns))
    first_call_ms.append(ms(call_ns))
    rss_after_dlopen.append(rss_dlopen)
    rss_after_call.append(rss_call)

violations = []
if failures:
    violations.append(f"{len(failures)} run(s) failed")

metrics = {}
if process_ms:
    metrics = {
        "process_elapsed_ms": stats(process_ms),
        "dlopen_ms": stats(dlopen_ms),
        "dlsym_ms": stats(dlsym_ms),
        "dlopen_dlsym_ms": stats(dlopen_dlsym_ms),
        "first_call_ms": stats(first_call_ms),
        "rss_after_dlopen_bytes": stats(rss_after_dlopen),
        "rss_after_call_bytes": stats(rss_after_call),
    }
    if args.mode in ("startup", "all"):
        if metrics["process_elapsed_ms"]["p50"] > args.max_process_p50_ms:
            violations.append("process elapsed p50 exceeds budget")
        if metrics["process_elapsed_ms"]["p95"] > args.max_process_p95_ms:
            violations.append("process elapsed p95 exceeds budget")
        if metrics["dlopen_dlsym_ms"]["p95"] > args.max_dlopen_dlsym_p95_ms:
            violations.append("dlopen+dlsym p95 exceeds budget")
        if metrics["first_call_ms"]["p95"] > args.max_first_call_p95_ms:
            violations.append("first wrapper call p95 exceeds budget")
    if args.mode in ("rss", "all"):
        if metrics["rss_after_call_bytes"]["p95"] > args.max_rss_after_call_bytes:
            violations.append("RSS after first call p95 exceeds budget")

payload = {
    "artifact": str(artifact),
    "framework_binary": str(framework_binary),
    "mode": args.mode,
    "runs_requested": args.runs,
    "timeout_seconds": args.timeout_seconds,
    "runs_completed": len(process_ms),
    "budgets": {
        "process_elapsed_p50_ms": args.max_process_p50_ms,
        "process_elapsed_p95_ms": args.max_process_p95_ms,
        "dlopen_dlsym_p95_ms": args.max_dlopen_dlsym_p95_ms,
        "first_call_p95_ms": args.max_first_call_p95_ms,
        "rss_after_call_p95_bytes": args.max_rss_after_call_bytes,
    },
    "metrics": metrics,
    "failures": failures,
    "ok": not violations,
    "violations": violations,
}
print(json.dumps(payload, indent=2, sort_keys=True))
raise SystemExit(0 if payload["ok"] else 1)
