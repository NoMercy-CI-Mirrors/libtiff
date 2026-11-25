# Guidance for AI agents and assistants

This file provides guidance for AI coding agents and assistants working on
the libtiff source tree.

## AI policy

The libtiff project permits the responsible use of AI tools. Read and follow
the [AI policy](doc/sphinx/project/ai-policy.rst). In particular:

- A human submitter is fully responsible for every merge request, commit,
  issue, security report and comment. Do not open merge requests or issues,
  or post comments, on the project's behalf without a human who has reviewed
  the content and takes responsibility for it.
- The human submitter must read, understand, review and test all
  AI-generated code and documentation before it is submitted. Keep changes
  focused and explain them clearly so that this review is practical.
- AI tools are not authors. Do not add `Co-authored-by:` or similar
  authorship trailers for AI tools to commit messages. The human submitter
  is the commit author.
- Disclose AI assistance, naming the tool and model (with version, where
  known):
  - in commit messages, with an `Assisted-by: <tool> (<model>)` trailer;
  - in merge request descriptions, stating the tool, model and what it was
    used for;
  - at the end of issue and security report text.
- Do not reproduce existing code or text from other projects, or produce
  code closely derived from it, unless it is known to be compatible with the
  libtiff license. Write original code, and tell the human submitter if
  any output may be derived from an identifiable existing source.
- Do not file or draft bug or security reports that have not been verified
  with a minimal reproducer against the current development sources.

## Project overview

- `libtiff/` - the TIFF library (C).
- `tools/` - supported command-line tools; `archive/` - unsupported tools.
- `test/` - test programs and scripts, with test images in `test/images/`.
- `doc/sphinx/` - Sphinx (reStructuredText) documentation and manual pages.
- `doc/doxygen/` - Doxygen library reference pages and configuration.
- `build/` - CI scripts (not a build directory).

Both CMake and Autotools builds are supported, and changes to the build or
test suite must be made to both (for example, a new test program must be
added to `test/CMakeLists.txt` and `test/Makefile.am`, and a new
Sphinx documentation file to `doc/sphinx/Makefile.am`).

## Building and testing

CMake (as used by CI; see `build/gitlab-ci`):

    cmake -S . -B cmake-build -DCMAKE_BUILD_TYPE=Debug \
        -Dfatal-warnings=ON -Dextra-warnings=ON
    cmake --build cmake-build
    ctest --test-dir cmake-build --output-on-failure

Autotools:

    ./autogen.sh
    ./configure
    make
    make check

All changes must build without warnings and pass the test suite. Bug fixes
should include a regression test where practical.

## Code style

- C code is formatted with `clang-format` using the repository's
  `.clang-format`; the `pre-commit` configuration in
  `.pre-commit-config.yaml` applies it. Do not reformat unrelated code.
- Generated files such as `libtiff/tif_fax3sm.c` and `libtiff/tiffvers.h`
  must not be reformatted.
- The library must remain portable C; avoid compiler- or platform-specific
  constructs unless guarded, and preserve the public API and ABI.
- libtiff parses untrusted input. Check sizes and offsets read from files,
  guard against integer overflow, and report errors through `TIFFErrorExtR()`
  rather than `assert()`.

## Commits and merge requests

- Commit messages use a `component: summary` subject line, for example
  `tif_read: fix signed overflow in TIFFReadRawTile1 bounds check`.
- Make each commit a logically complete change, and keep merge requests
  focused on a single topic.
- Merge requests are made against `master` on
  [GitLab](https://gitlab.com/libtiff/libtiff).
