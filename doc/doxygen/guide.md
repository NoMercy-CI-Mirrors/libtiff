# Documenting and building LibTIFF {#documentation_guide}

This guide defines conventions for the @ref index "LibTIFF library reference".
The Sphinx manual and man pages remain maintained alongside it. See
@ref documentation_completion for the module inventory and validation scope.

## Building and installing

CMake 3.20 or later and Doxygen 1.9.1 or later are required. CI checks the
reference with Doxygen 1.9.1 and 1.9.8, and it has also been checked with
Doxygen 1.18.0. Graphviz `dot` is optional. The Autotools build distributes
these sources but does not build the reference. CMake's standard
`FindDoxygen` module discovers both tools and `doxygen_add_docs()` generates
the configuration; do not maintain a second handwritten Doxyfile.

```sh
cmake -S . -B build-doxygen -Dtiff-docs=ON -Ddoxygen=ON
cmake --build build-doxygen --target tiff-doc-doxygen
```

The `doxygen` option defaults to `OFF`, so ordinary builds and installs do not
generate or install this reference. Pass `-Ddoxygen=ON` to enable it; this
requires a suitable executable and fails at configuration time otherwise. When
enabled, the named target also runs in the default build. `-Dtiff-docs=OFF`
disables all documentation, including this reference.
As with other CMake options, the selected value persists in the build cache.

Outputs are `build-doxygen/doc/doxygen/api/index.html`,
`build-doxygen/doc/doxygen/libtiff.tag`, and `build-doxygen/doc/doxygen/warnings.log`.
With Graphviz, include/included-by, directory, group, call/caller, and
type relationship graphs use linked, interactive SVG. Graphs describe the
relationships Doxygen can discover, rather than a complete runtime call model.
Without Graphviz, the HTML and tagfile still build.

The target reruns when requested, so configuration and source changes are
reflected without relying on incomplete stamp dependencies. Graph generation
can take time. Build before installing:

```sh
cmake --build build-doxygen
cmake --install build-doxygen --component documentation
```

When `tiff-install` is enabled, the HTML is installed under
`${CMAKE_INSTALL_DOCDIR}/api` and the tagfile under `${CMAKE_INSTALL_DOCDIR}`.
Distribute the tagfile together with the HTML from the same build. Generated
output belongs in the build tree and must not be committed.

## Comment conventions

Use `/** ... */` blocks and explicit `@brief`, following the existing four-space
indentation and 80-column formatting. A brief is a single sentence stating the
purpose. Add an extended description for behaviour that needs explanation;
do not repeat the brief in different words.

- Document public declarations in installed headers, including `tiffio.hxx`.
- Document private declarations and members in their existing private headers.
- Document source-local functions, types, and members beside their definitions.
- Maintain one authoritative contract per symbol. Move or reconcile existing
  source documentation when documenting a header; keep implementation rationale
  in ordinary source comments. Do not add header declarations just for Doxygen.
- Add `@file` documentation to processed headers and source files as each module
  is converted, so file-level declarations can appear in the reference.
- Give types, typedefs, enums, enumerators, macros, globals, and struct/union
  members meaningful descriptions. Short descriptions may use a one-line
  `/** @brief ... */` block.
  Explain units, valid values, bit masks, and invariants where applicable.
- Document callback typedefs as callable contracts, including invocation context,
  argument ownership, return semantics, and restrictions on reentry.
- For generated declarations, edit the template or generator, preserving any
  formatting requirements. Keep CMake and Autotools templates consistent. Do not
  hand-edit generated tables or headers.
- Put each documentation block on its own lines before the declaration, macro,
  enumerator or member it describes. Do not put documentation at the end of a
  line of code, such as `#define X 1 /**< ... */`.
- Declare one variable or member per declaration, so that each has its own
  preceding block; Doxygen applies a preceding block only to the first
  variable of a declaration that lists several.
- A member whose type is an anonymous `struct` or `union`, such as
  `union { ... } name;`, is the exception: Doxygen 1.18 attaches a preceding
  block to the anonymous type rather than the member. Document the member with
  a `/**< ... */` block on its own lines immediately after `} name;`.

Every function contract must cover the following where applicable:

1. Every named parameter with `@param[in]`, `@param[out]`, or `@param[in,out]`.
   Direction describes the data/state reached through a pointer as well as
   scalar arguments. A `TIFF *` whose state changes is normally `[in,out]`.
   State nullability, units, buffer capacity, valid ranges, and aliasing rules.
2. `@return` and/or `@retval` describing successful and failed results, including
   sentinel values, sizes, units, and partial results. Void functions need no
   artificial `@return`; describe their observable outcome and failure reporting.
3. Failure conditions, diagnostic callbacks, whether `errno` is meaningful, and
   the validity of output arguments and library state after failure. Do not
   promise preservation or rollback unless the implementation guarantees it.
4. Whether pointers are borrowed or owned, how owned objects must be released,
   whether input ownership transfers, and exactly what invalidates borrowed
   pointers. Include lifetimes of callback context and client I/O objects.
5. Preconditions and side effects: file position, buffered I/O, directory state,
   allocation, callbacks, shared/global state, and concurrency restrictions.

Use `@pre`, `@post`, `@note`, `@warning`, `@deprecated`, `@see`, and named
`@par` sections when they improve clarity. Do not add empty boilerplate sections.
For variadic interfaces, describe accepted promoted types, argument counts,
pointer indirection, and ownership for each supported argument pattern. Apply
the same principles to function-like macros; document repeated argument
evaluation and other macro-specific effects.

The following is a fictional style example, not a LibTIFF interface:

```c
/**
 * @brief Copy a descriptive name into a caller-provided buffer.
 *
 * The required capacity includes the terminating null byte. No allocation
 * occurs, and the caller retains ownership of the buffer.
 *
 * @param[in] item Item whose name is copied; must not be NULL.
 * @param[out] buffer Destination; must not be NULL and must not alias item.
 * @param[in] capacity Destination capacity in bytes.
 * @retval 1 The complete name was copied and null-terminated.
 * @retval 0 The capacity was insufficient; buffer is unchanged.
 * @par Error handling
 * Failure is reported only through the return value; no diagnostic is emitted.
 */
int ExampleCopyName(const ExampleItem *item, char *buffer, size_t capacity);
```

Read implementations and the existing Sphinx/man documentation before writing
contracts. Record disagreements for review rather than inventing guarantees or
changing runtime behaviour as part of documentation work.

## Module organisation

Define stable semantic subgroups in `modules.dox`, nested under
`libtiff_public` or `libtiff_private`. For example, a public lifecycle subgroup
uses `@defgroup libtiff_public_lifecycle Lifecycle` followed by
`@ingroup libtiff_public`; declarations then use
`@ingroup libtiff_public_lifecycle`. Use `@{` / `@}` blocks for adjacent related
declarations when this avoids repetition. Do not place a public declaration in
an internal group merely because its implementation is private. Pair public
contracts with their private equivalents in the matching private subgroup.

When documenting a new module, cover all declarations, members, enumerators,
macros, callbacks, globals and source-local equivalents, and review the rendered
pages and the warnings log. Tools and tests may receive comments independently,
but their sources are not processed by this target. The same applies to
`contrib`, `archive`, `spike`, and tool-only portability code in `port`.

## Coverage and review

`EXTRACT_ALL` stays disabled: enabling it suppresses useful missing-documentation
warnings. Static/private extraction, internal documentation, parameter warnings,
and documentation-error warnings are enabled. Documentation warnings
are recorded in `warnings.log` without failing the default build.

Pass `-Ddoxygen-checks=ON` (with `-Ddoxygen=ON`) to enforce the markup. The
`tiff-doc-doxygen-check` target then runs in the default build after the
reference is generated, and fails on any Doxygen warning other than a Graphviz
graph size limit, or on any issue reported by the XML checks described in
@ref documentation_completion. This covers undocumented declarations, unknown
commands, misnamed parameters, missing parameter directions and missing return
contracts. CI runs these checks with Graphviz on Doxygen 1.9.8 (Ubuntu 24.04,
also for merge requests from forks) and 1.9.1 (Ubuntu 22.04); documentation
changes must pass them:

```sh
cmake -S . -B build-doxygen -Ddoxygen=ON -Ddoxygen-checks=ON
cmake --build build-doxygen --target tiff-doc-doxygen-check
```

A passing check is not proof of complete contracts: verify parameter
directions, return values, ownership, failure states, and side effects
manually.

Input discovery is restricted to library C/C++ headers and sources plus these
documentation pages and configured library headers. Both platform backends and
optional codec files are inputs, but preprocessing still controls which
conditional declarations are visible. Configured headers describe the current
build. JBIG source contracts are additionally enabled for documentation when
that optional dependency is absent. The JPEG source view also exposes the
IJG 9d compatibility helper through documentation-only version macros. These
preprocessing choices select source contracts for the reference; they do not
enable compiled codecs or report the installed dependency version. Review
platform/feature variants separately; @ref documentation_completion describes
the structural checks and their limits. Attribute/deprecation wrappers
are removed for parsing, not from the compiled library. Private symbols remain unstable even though they are included
in the tagfile.

Validate enabled/disabled documentation configurations, missing Doxygen,
Graphviz present/absent, generated HTML/tagfile installation, and external
cross-references when changing the infrastructure. Keep existing Sphinx/man
generation working. Use the repository formatting checks for source comments.

## Cross-references from consumers, tools, and tests

A separate Doxygen project can link library symbols without processing the
library again. In that project's Doxyfile, use the matching installed tagfile
and the URL (or relative path) at which its API HTML is available:

```text
TAGFILES = /prefix/share/doc/tiff/libtiff.tag=https://example.org/libtiff/api
```

With CMake's `doxygen_add_docs()`, set the equivalent variable before creating
the consumer target:

```cmake
set(DOXYGEN_TAGFILES
    "/prefix/share/doc/tiff/libtiff.tag=https://example.org/libtiff/api")
```

Replace the example paths and URL with the actual installation and hosting
locations. Use ordinary Doxygen `@ref` or `@see` references to documented symbols.
The tagfile contains both public and private documentation; consumers should
normally reference the public groups. A future tools/tests documentation target
can use this mechanism without merging those sources into the library site.

A consumer deliberately linking source-local private functions must also set
`EXTRACT_STATIC = YES` (or `DOXYGEN_EXTRACT_STATIC` before `doxygen_add_docs()`).
Without that setting Doxygen can leave those references unlinked even though
the entries are present in the tagfile. The public/private declared interfaces
do not need static extraction solely for external linking.

Configuration references:
[CMake FindDoxygen](https://cmake.org/cmake/help/v3.20/module/FindDoxygen.html)
and [Doxygen configuration](https://www.doxygen.nl/manual/config.html).
