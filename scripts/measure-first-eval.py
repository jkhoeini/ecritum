#!/usr/bin/env python3
import argparse
import json


parser = argparse.ArgumentParser(description="Report the first-eval budget placeholder.")
parser.add_argument("--max-p50-ms", type=float, default=500.0)
parser.add_argument("--max-p95-ms", type=float, default=1000.0)
args = parser.parse_args()

payload = {
    "name": "first-eval",
    "status": "not_applicable",
    "budget_ms": {
        "p50": args.max_p50_ms,
        "p95": args.max_p95_ms,
    },
    "ok": True,
    "violations": [],
    "not_applicable_reason": "eval ABI is not implemented yet; enforce this gate no later than M3",
    "future_command": "mise exec -- just bench-first-eval",
}
print(json.dumps(payload, indent=2, sort_keys=True))
raise SystemExit(0)
