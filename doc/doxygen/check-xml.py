#!/usr/bin/env python3
"""Check structural documentation coverage in a generated Doxygen XML tree.

This checks extracted declarations only. It cannot establish semantic accuracy,
source coverage in inactive branches, or the safety of documented operations.
"""

import argparse
from collections import Counter
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


def content(element):
    return "".join(element.itertext()).strip() if element is not None else ""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xml", type=Path, help="Doxygen XML output directory")
    args = parser.parse_args()
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
    if issues:
        print("\n".join(issues), file=sys.stderr)
        return 1
    print("Brief, parameter-direction and function-return checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
