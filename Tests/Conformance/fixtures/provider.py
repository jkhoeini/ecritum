#!/usr/bin/env python3
import argparse
import json
import sys


MODES = {
    "c-abi": {
        "c_allocation_release",
        "c_lifecycle",
        "policy_config",
    },
    "scaffold": {
        "c_allocation_release",
        "c_lifecycle",
        "policy_config",
        "swift_error_model",
        "swift_lifecycle",
        "swift_value_model",
    },
    "swift-model": {
        "policy_config",
        "swift_error_model",
        "swift_lifecycle",
        "swift_value_model",
    },
}


def evidence_for(case):
    expected = case.get("expected") if isinstance(case.get("expected"), dict) else {}
    commands = expected.get("evidenceCommands", [])
    if commands:
        return {"commands": commands}
    return {"fixture": "simulated provider response"}


def main():
    parser = argparse.ArgumentParser(description="Fixture Ecritum conformance provider.")
    parser.add_argument("--mode", choices=sorted(MODES), default="scaffold")
    args = parser.parse_args()

    request = json.load(sys.stdin)
    capabilities = sorted(MODES[args.mode])
    capability_set = set(capabilities)

    results = []
    for case in request["cases"]:
        required = set(case.get("capabilities", []))
        missing = sorted(required - capability_set)
        if missing:
            results.append(
                {
                    "caseId": case["caseId"],
                    "status": "pending_capability",
                    "reason": "provider lacks capabilities: " + ", ".join(missing),
                }
            )
        else:
            results.append(
                {
                    "caseId": case["caseId"],
                    "status": "pass",
                    "reason": "fixture provider covers required capabilities",
                    "evidence": evidence_for(case),
                }
            )

    print(
        json.dumps(
            {
                "protocolVersion": 1,
                "provider": {
                    "id": f"fixture-{args.mode}",
                    "capabilities": capabilities,
                },
                "results": results,
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
