#!/usr/bin/env python3
import argparse
import json
import math
import subprocess
import time


def percentile(values, pct):
    ordered = sorted(values)
    index = max(0, math.ceil((pct / 100.0) * len(ordered)) - 1)
    return ordered[index]


parser = argparse.ArgumentParser(description="Measure repeated command elapsed time as JSON.")
parser.add_argument("--name", required=True)
parser.add_argument("--runs", type=int, default=10)
parser.add_argument("--max-p50-ms", type=float, default=250.0)
parser.add_argument("--max-p95-ms", type=float, default=500.0)
parser.add_argument("--timeout-seconds", type=float, default=10.0)
parser.add_argument("--expect-stdout")
parser.add_argument("command", nargs=argparse.REMAINDER)
args = parser.parse_args()

if not args.command:
    raise SystemExit("missing command")
if args.command[0] == "--":
    args.command = args.command[1:]
if not args.command:
    raise SystemExit("missing command")
if args.runs <= 0:
    raise SystemExit("--runs must be positive")

elapsed = []
failures = []
stdout_last = ""
stderr_last = ""
for _ in range(args.runs):
    started = time.perf_counter()
    try:
        completed = subprocess.run(
            args.command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=args.timeout_seconds,
        )
    except subprocess.TimeoutExpired as exc:
        stdout_last = (exc.stdout or "").strip()
        stderr_last = (exc.stderr or "").strip()
        failures.append({"timeout_seconds": args.timeout_seconds, "stderr": stderr_last})
        continue
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    stdout_last = completed.stdout.strip()
    stderr_last = completed.stderr.strip()
    if completed.returncode != 0:
        failures.append({"returncode": completed.returncode, "stderr": stderr_last})
    elif args.expect_stdout is not None and stdout_last != args.expect_stdout:
        failures.append({"stdout": stdout_last, "expected_stdout": args.expect_stdout})
    else:
        elapsed.append(elapsed_ms)

violations = []
if failures:
    violations.append(f"{len(failures)} run(s) failed")
metrics = {}
if elapsed:
    metrics = {
        "p50": round(percentile(elapsed, 50), 3),
        "p95": round(percentile(elapsed, 95), 3),
        "min": round(min(elapsed), 3),
        "max": round(max(elapsed), 3),
    }
    if metrics["p50"] > args.max_p50_ms:
        violations.append("p50 exceeds budget")
    if metrics["p95"] > args.max_p95_ms:
        violations.append("p95 exceeds budget")

payload = {
    "name": args.name,
    "command": args.command,
    "runs_requested": args.runs,
    "runs_completed": len(elapsed),
    "budget_ms": {
        "p50": args.max_p50_ms,
        "p95": args.max_p95_ms,
    },
    "expected_stdout": args.expect_stdout,
    "elapsed_ms": metrics,
    "stdout_last": stdout_last,
    "stderr_last": stderr_last,
    "ok": not violations,
    "violations": violations,
}
print(json.dumps(payload, indent=2, sort_keys=True))
raise SystemExit(0 if payload["ok"] else 1)
