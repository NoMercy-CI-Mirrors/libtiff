#!/usr/bin/env python3
"""Check structural documentation coverage in a generated Doxygen XML tree.

This checks extracted declarations only. Undocumented declarations are not
extracted, so pass the Doxygen warnings log as well to also fail on missing
documentation and invalid markup. Neither check can establish semantic
accuracy, source coverage in inactive branches, or the safety of documented
operations.
"""

import argparse
from collections import Counter
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


# Graphviz size limits are a rendering choice, not a documentation defect.
GRAPH_SIZE_WARNING = re.compile(
    r"warning: .*graph for .* not generated, too many nodes \(\d+\), "
    r"threshold is \d+\."
)


def check_warnings_log(path):
    """Return the lines of a Doxygen warnings log that are not graph limits."""
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as error:
        return [f"{path}: cannot read Doxygen warnings log: {error}"]
    return [
        line for line in lines
        if line.strip() and not GRAPH_SIZE_WARNING.search(line)
    ]


def content(element):
    return "".join(element.itertext()).strip() if element is not None else ""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xml", type=Path, help="Doxygen XML output directory")
    parser.add_argument(
        "--warnings-log", type=Path,
        help="Doxygen WARN_LOGFILE; any warning other than a graph size "
        "limit is reported as an issue",
    )
    args = parser.parse_args()
    warnings = []
    if args.warnings_log is not None:
        warnings = check_warnings_log(args.warnings_log)
    index = ET.parse(args.xml / "index.xml")
    members = {}
    issues = []
    types = 0
    files = 0
    for compound in index.findall("compound"):
        root = ET.parse(args.xml / (compound.attrib["refid"] + ".xml"))
        definition = root.find("compounddef")
        if definition is None:
            continue
        # Markdown and .dox pages are file compounds too, but are not sources.
        if definition.get("kind") == "file" and not re.search(
            r"\.(md|dox)$", content(definition.find("compoundname"))
        ):
            files += 1
            if not content(definition.find("briefdescription")):
                issues.append(
                    content(definition.find("compoundname")) + ": file brief"
                )
        if definition.get("kind") in {"struct", "union", "class"}:
            types += 1
            if not content(definition.find("briefdescription")):
                issues.append(
                    content(definition.find("compoundname")) + ": type brief"
                )
        for member in definition.findall("./sectiondef/memberdef"):
            members[member.attrib["id"]] = member

    enumerators = 0
    for member in members.values():
        name = member.findtext("name") or member.attrib["id"]
        location = member.find("location")
        where = (
            f"{location.get('file')}:{location.get('line')}: {name}"
            if location is not None else name
        )

        def report(message):
            issues.append(where + ": " + message)

        if not content(member.find("briefdescription")):
            report("missing brief")
        for value in member.findall("enumvalue"):
            enumerators += 1
            if not content(value.find("briefdescription")):
                report("missing enumerator brief: " + value.findtext("name", ""))

        kind = member.get("kind")
        if kind not in {"function", "typedef", "variable", "define"}:
            continue
        parameters = member.findall("param")
        # @retval names share the parametername element but carry no direction.
        documented = member.findall(
            ".//parameterlist[@kind='param']/parameteritem/"
            "parameternamelist/parametername"
        )
        documented_names = {parameter.text for parameter in documented}
        for parameter in documented:
            if parameter.get("direction") not in {"in", "out", "inout"}:
                report("missing parameter direction: " + (parameter.text or ""))
        # PACK has two source-local arities that Doxygen merges. Its contract
        # describes both argument lists/directions in prose, with a return.
        if kind == "define" and name == "PACK":
            continue
        for parameter in parameters:
            parameter_type = content(parameter.find("type"))
            if parameter_type == "void":
                continue
            names = {
                parameter.findtext("declname"), parameter.findtext("defname")
            } - {None, ""}
            if parameter_type == "...":
                names = {"..."}
            # Empty macro parentheses are represented by an empty param node.
            if not names and kind == "define" and not parameter_type:
                continue
            if not names.intersection(documented_names):
                report("undocumented parameter: " + "/".join(sorted(names)))
        if kind == "function":
            result = content(member.find("type"))
            result = re.sub(r"\b(static|inline|constexpr)\b", "", result).strip()
            if not re.search(r"\bvoid$", result):
                has_return = (
                    member.find(".//simplesect[@kind='return']") is not None
                )
                has_retval = (
                    member.find(".//parameterlist[@kind='retval']") is not None
                )
                if not has_return and not has_retval:
                    report("missing return contract")

    counts = Counter(member.get("kind") for member in members.values())
    print(f"Checked {len(members)} unique extracted members, {types} compound "
          f"types, {files} files and {enumerators} enumerators")
    print(", ".join(f"{kind}: {count}" for kind, count in sorted(counts.items())))
    status = 0
    if warnings:
        print(f"Doxygen reported {len(warnings)} warning line(s) in "
              f"{args.warnings_log}:", file=sys.stderr)
        print("\n".join(warnings), file=sys.stderr)
        status = 1
    elif args.warnings_log is not None:
        print("Doxygen warnings log has no documentation warnings")
    if issues:
        print(f"Doxygen XML check found {len(issues)} issue(s):",
              file=sys.stderr)
        print("\n".join(issues), file=sys.stderr)
        status = 1
    else:
        print("Brief, parameter-direction and function-return checks passed")
    return status


if __name__ == "__main__":
    sys.exit(main())
