#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Guard against unchecked arithmetic in allocation sizes.

Computing an allocation size as `count * sizeof(type)` and handing the product
to _TIFFmallocExt()/_TIFFreallocExt() silently truncates when the product does
not fit in tmsize_t/size_t.  On a 32-bit build `(tmsize_t)((size_t)count *
sizeof(double))` with count == 0x20000000 yields 0, so the allocation succeeds
with a zero-sized buffer and the subsequent loop writes `count` elements past
its end.

libtiff already ships the checked helpers that make this impossible:

    _TIFFCheckMalloc(tif, nmemb, elem_size, what)
    _TIFFCheckRealloc(tif, buffer, nmemb, elem_size, what)

They route the multiplication through _TIFFMultiplySSize(), which rejects the
product when it would overflow, and emit a diagnostic naming the element count
and element size.  New code must use them.

This script is a ratchet: it records, per file, how many raw sites still exist
and fails if that number grows.  It never requires a file to be clean, so it
can be adopted before every site is converted, and a file that has been cleaned
up cannot silently regress.

Run manually with:  python3 scripts/check-alloc-arith.py
"""

import os
import re
import sys

# Maximum number of raw `malloc(count * sizeof(x))` sites allowed per file.
# Lower an entry when you convert sites; never raise one.
BASELINE = {
    "tif_compress.c": 3,
    "tif_dir.c": 2,
    "tif_dirread.c": 3,
    "tif_getimage.c": 7,
    "tif_jpeg.c": 2,
    "tif_luv.c": 2,
    "tif_lzw.c": 2,
    "tif_ojpeg.c": 1,
    "tif_pixarlog.c": 4,
    "tif_write.c": 2,
}

RAW_ALLOC = re.compile(r"\b_TIFF(?:m|re)allocExt\s*\(")


def strip_comments(text):
    """Blank out comments while preserving offsets (so line numbers stay true)."""

    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))

    text = re.sub(r"/\*.*?\*/", blank, text, flags=re.S)
    text = re.sub(r"//[^\n]*", blank, text)
    return text


def arguments_of(text, open_paren):
    """Return the argument text of a call whose '(' is at open_paren, or None."""
    depth = 0
    for i in range(open_paren, len(text)):
        c = text[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[open_paren + 1 : i]
    return None


def split_top_level(args):
    """Split an argument list on commas that are not nested in parens."""
    parts, depth, start = [], 0, 0
    for i, c in enumerate(args):
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif c == "," and depth == 0:
            parts.append(args[start:i])
            start = i + 1
    parts.append(args[start:])
    return parts


SIZEOF = re.compile(r"\bsizeof\s*\([^()]*\)|\bsizeof\s+\w+")
IDENT = re.compile(r"[A-Za-z_]\w*")


def is_runtime_product(size_expr):
    """True if size_expr multiplies sizeof by something not known at compile time.

    `6 * sizeof(float)` is a constant and cannot overflow, so it is ignored;
    `count * sizeof(uint32_t)` is not.
    """
    if "sizeof" not in size_expr or "*" not in size_expr:
        return False
    # Drop the sizeof(...) operands, then look for a remaining name: that name
    # is a runtime value feeding the multiplication.
    rest = SIZEOF.sub(" ", size_expr)
    return bool(IDENT.search(rest))


def find_sites(path):
    """Yield (line_number, snippet) for each unchecked product-sized allocation."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        raw = fh.read()
    text = strip_comments(raw)

    for m in RAW_ALLOC.finditer(text):
        args = arguments_of(text, m.end() - 1)
        if args is None:
            continue
        # The size is the last argument of both _TIFFmallocExt(tif, size) and
        # _TIFFreallocExt(tif, buffer, size).
        size_expr = split_top_level(args)[-1]
        if not is_runtime_product(size_expr):
            continue
        line = text.count("\n", 0, m.start()) + 1
        snippet = " ".join(size_expr.split())
        if len(snippet) > 70:
            snippet = snippet[:67] + "..."
        yield line, snippet


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    srcdir = os.path.join(os.path.dirname(here), "libtiff")
    if not os.path.isdir(srcdir):
        sys.stderr.write("cannot locate libtiff/ next to %s\n" % here)
        return 2

    found = {}
    for name in sorted(os.listdir(srcdir)):
        if not name.endswith(".c"):
            continue
        sites = list(find_sites(os.path.join(srcdir, name)))
        if sites:
            found[name] = sites

    failed = False
    for name in sorted(set(found) | set(BASELINE)):
        sites = found.get(name, [])
        allowed = BASELINE.get(name, 0)
        if len(sites) > allowed:
            failed = True
            sys.stderr.write(
                "%s: %d unchecked product-sized allocation(s), %d allowed\n"
                % (name, len(sites), allowed)
            )
            for line, snippet in sites:
                sys.stderr.write("    libtiff/%s:%d: %s\n" % (name, line, snippet))

    if failed:
        sys.stderr.write(
            "\nCompute allocation sizes with the checked helpers instead:\n"
            "    _TIFFCheckMalloc(tif, nmemb, elem_size, module)\n"
            "    _TIFFCheckRealloc(tif, buffer, nmemb, elem_size, module)\n"
            "They reject the multiplication when the product would overflow.\n"
        )
        return 1

    total = sum(len(v) for v in found.values())
    print("check-alloc-arith: ok (%d legacy site(s) within baseline)" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
