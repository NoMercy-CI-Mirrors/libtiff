#!/usr/bin/env python3
"""Spike traceability scanner (plan section 13.2).

Scans source files for structured requirement annotations:

    [impl REQ-ID ...]     at an implementing site
    [verify REQ-ID ...]   in a test that verifies the requirement

validates every cited ID against the requirements database's generated
requirements.json, and prints the traceability matrix: requirement ->
implementing sites -> verifying tests. Exits nonzero on unknown IDs, on
MUST-level requirements that are implemented but never verified, and on
verified-but-never-implemented IDs (which usually indicate a stale
annotation).
"""

import argparse
import json
import re
import sys
from pathlib import Path

ANNOTATION = re.compile(r"\[(impl|verify)\s+([A-Z][A-Z0-9]*-[A-Za-z0-9.-]+"
                        r"(?:\s+[A-Z][A-Z0-9]*-[A-Za-z0-9.-]+)*)\]")
SOURCE_SUFFIXES = {".c", ".h", ".cpp", ".hpp"}


def scan(root: Path):
    sites = {}  # req id -> {"impl": [locations], "verify": [locations]}
    for path in sorted(root.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        for lineno, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), 1):
            for kind, ids in ANNOTATION.findall(line):
                for req_id in ids.split():
                    entry = sites.setdefault(req_id,
                                             {"impl": [], "verify": []})
                    entry[kind].append(f"{path.relative_to(root)}:{lineno}")
    return sites


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--db", type=Path, required=True,
                        help="requirements.json from the tiff-specification "
                             "database (a pinned copy in the real setup)")
    args = parser.parse_args()

    db = json.loads(args.db.read_text(encoding="utf-8"))
    requirements = {r["id"]: r for r in db["requirements"]}

    sites = scan(args.root)
    unknown = sorted(i for i in sites if i not in requirements)

    print(f"{'requirement':<14} {'level':<11} impl verify")
    print("-" * 44)
    impl_unverified = []
    verify_only = []
    for req_id in sorted(sites):
        if req_id in unknown:
            continue
        level = requirements[req_id].get("level", "?")
        n_impl = len(sites[req_id]["impl"])
        n_verify = len(sites[req_id]["verify"])
        print(f"{req_id:<14} {level:<11} {n_impl:>4} {n_verify:>6}")
        if n_impl and not n_verify and level == "MUST":
            impl_unverified.append(req_id)
        if n_verify and not n_impl:
            verify_only.append(req_id)

    print(f"\n{len(sites) - len(unknown)} requirement(s) referenced; "
          f"{sum(1 for i in sites if sites[i]['impl'])} implemented, "
          f"{sum(1 for i in sites if sites[i]['verify'])} verified.")

    status = 0
    if unknown:
        print(f"ERROR: unknown requirement id(s): {', '.join(unknown)}")
        status = 1
    if impl_unverified:
        print("ERROR: MUST requirement(s) implemented but not verified: "
              + ", ".join(impl_unverified))
        for req_id in impl_unverified:
            for loc in sites[req_id]["impl"]:
                print(f"    {req_id}: {loc}")
        status = 1
    if verify_only:
        print("ERROR: verified but no implementation site (stale "
              "annotation?): " + ", ".join(verify_only))
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
