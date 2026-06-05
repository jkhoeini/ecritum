#!/usr/bin/env python3
import argparse
import json
import sys


def main():
    parser = argparse.ArgumentParser(description="Fixture security abuse provider.")
    parser.add_argument("--mode", choices=["baseline"], default="baseline")
    args = parser.parse_args()

    request = json.load(sys.stdin)
    results = []
    for case in request["cases"]:
        results.append(
            {
                "caseId": case["caseId"],
                "status": "pending_capability",
                "reason": f"{args.mode} provider has no eval/language abuse runtime yet",
            }
        )

    print(
        json.dumps(
            {
                "protocolVersion": 1,
                "provider": {
                    "id": f"security-{args.mode}",
                    "capabilities": [],
                },
                "results": results,
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
