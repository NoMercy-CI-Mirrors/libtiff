.. _rfc3_layered_api:

===================================================================
RFC 3: A layered API architecture and memory-safe core for libtiff
===================================================================

Author: Roger Leigh

Contact: rleigh at codelibre.net

Status: Proposed

Summary
-------

This RFC proposes a long-term restructuring of libtiff into layered
components, to remove whole classes of recurring security and
reliability defects while keeping every existing application working
unchanged:

* a new core implementation, written in C++17 today and replaceable by
  Rust later, free of the legacy API's inconsistent behaviours;
* a new, safe C API (prefix ``tiff_``, working library name
  ``libtiff_core``) as the core's only public face and a stable FFI
  boundary;
* the existing ``TIFF*`` API preserved in full as a compatibility
  layer (``libtiff_compat``) implemented on top of the safe C API,
  still shipped as ``libtiff.so.6`` with the existing ``LIBTIFF_4.x``
  symbol versions;
* every design and implementation decision traced to the TIFF
  specification requirements database, with tests traced to
  requirements.

Existing applications, the in-tree tools, and the existing test suite
do not change: the tools and test suite deliberately remain on the old
API throughout, serving as the compatibility layer's conformance
suite. New unit and integration tests cover the new core and C API
independently.

A detailed companion document (the layered API migration plan) carries
the full inventory: the disposition of all 206 exported symbols and
every public type, the compatibility quirk catalogue, the error-model
mapping, and the staging detail. This RFC summarises it for decision.

Motivation
----------

An analysis of the issue tracker in November 2026 (`work item 956
<https://gitlab.com/libtiff/libtiff/-/work_items/956>`__) found 950
accessible issues, of which bounds/data-extent and
arithmetic/conversion classes alone account for 34%. Issue intake has
grown from 38 (2024) to 109 (2025) to 179 (January–November 2026),
against shrinking maintainer capacity. The recurring defect families
are structural properties of a 1988-vintage C design: size arithmetic
repeated at every call site, array lengths derived from other mutable
tags, varargs type punning, global mutable state, and inconsistent
failure conventions (six different return-value patterns across the
155+ documented functions).

Incremental hardening of the C codebase continues and remains
worthwhile, but it cannot change those underlying properties. `Work
item 771 <https://gitlab.com/libtiff/libtiff/-/work_items/771>`__
(C++ conversion) and `work item 772
<https://gitlab.com/libtiff/libtiff/-/work_items/772>`__ (C API
refactoring) explored the space; this RFC supersedes 771's
in-place-conversion architecture
with a design that keeps compatibility and safety in separate layers,
so that neither compromises the other.

Architecture
------------

::

  existing applications, tools,          new applications
  existing test suite                         |
        |                                     |
        v                                     v
  +--------------------+          +------------------------+
  | legacy C API       |          | future C++ API         |
  | TIFF* unchanged    |          | "libtiff_cxx"          |
  | "libtiff_compat"   |          | (over the safe C API)  |
  +--------------------+          +------------------------+
        |                                     |
        v                                     v
  +----------------------------------------------------+
  | safe C API (tiff_*, C99)      "libtiff_core"       |
  | status codes, out-params, unsigned sizes,          |
  | per-handle errors, no varargs, no globals          |
  +----------------------------------------------------+
        |
        v
  +----------------------------------------------------+
  | core implementation (C++17, internal; later Rust)  |
  +----------------------------------------------------+

Key properties:

* **The safe C API is the only way into the core.** It uses uniform
  status-code returns with out-parameters, unsigned sizes, explicit
  buffer capacities, caller-provided pixel buffers, per-handle error
  state, and no global state of any kind. It is designed as a
  permanent FFI boundary: when the core is later rewritten in Rust,
  nothing above this line changes.
* **The compatibility layer reproduces current behaviour**, including
  the varargs tag interface, global error/warning handlers and their
  message formats, the tag extender, sentinel return conventions, and
  the ``TIFFRGBAImage`` structure. It continues to ship as
  ``libtiff.so.6`` with the frozen ``LIBTIFF_4.x`` version nodes, so
  existing binaries keep working without relinking.
* **Legacy tolerances become explicit policy.** The current reader's
  acceptance of damaged or non-conforming files is reproduced through
  a strictness-policy object whose toggles are the specification
  database's deviation registry flags (``tolerate.unsorted-ifd``,
  ``tolerate.lzw-old-style``, ``tolerate.packbits-cross-row-runs``,
  and so on). The compatibility layer opens files with the legacy
  policy; new-API users get strict validation by default, with each
  tolerance individually selectable.
* **Specification traceability.** The TIFF specification documents
  have been restated as a machine-readable requirements database (377
  consolidated requirements with stable IDs, levels, actors and source
  citations). Implementation sites and tests carry requirement
  annotations, and CI builds the traceability matrix: every in-scope
  MUST requirement must have implementing code and verifying tests.
  New leniencies are added to the database's deviation registry first
  and implemented second.

Backward compatibility
----------------------

For existing users this proposal is, by construction, a no-op:

* source compatibility: ``tiffio.h``/``tiff.h`` unchanged;
* binary compatibility: soname and symbol versions unchanged;
* behavioural compatibility: enforced by keeping the complete existing
  test suite and all 23 supported tools on the old API, unmodified,
  and by differential CI that builds both the current implementation
  and the new one and compares tool output, decoded pixels, reported
  tags and diagnostics across the whole image corpus.

A small number of legacy extension points cannot be expressed through
the safe C API and need either a minimal private compat-support
interface or an explicit support decision: ``TIFFAccessTagMethods``,
out-of-tree codecs registered via ``TIFFRegisterCODEC`` (whose init
methods write into ``struct tiff`` through private headers, never a
stable ABI), and the buffer-control entry points
(``TIFFReadBufferSetup`` and relatives). A survey of major downstreams
(GDAL, ImageMagick, poppler, Qt, pillow, OpenCV) is proposed to size
this interface before Stage 1.

Toolchain and dependencies
--------------------------

* Building libtiff will additionally require a C++17 compiler.
  *Using* either C API requires only C89/C99 headers, as today.
  Platforms without C++17 remain served by a maintenance branch for a
  window to be agreed in this RFC's discussion.
* No new mandatory dependencies. Two single-header polyfills
  (tl::expected, tcb::span) are vendored while the floor is below
  C++23/C++20; GoogleTest is used by the new test suite only and is
  never installed.
* Both CMake and Autotools builds are maintained throughout, as now.

Staging
-------

Each stage is releasable and gated; the old implementation remains
in-tree and selectable until the switchover, then moves to a
maintenance branch (security fixes only).

* **Stage 0 — decisions and scaffolding**: this RFC; repository
  layout; toolchain wiring; traceability scanner and a pinned
  requirements database release; the ``LIB-*`` file for library-design
  requirements that are not specification-derived.
* **Stage 1 — vertical read slice**: headers, first IFD, baseline
  grayscale tags, strip reading, codecs none/PackBits/LZW/Deflate;
  safe C API over the slice; fuzzers. Gate: corpus subset decodes
  identically to the current implementation; all in-scope MUSTs
  traced and verified.
* **Stage 2 — full read path**: all directory kinds, full tag system,
  all configured decoders, RGBA reading, client I/O. Gate:
  whole-corpus differential clean.
* **Stage 3 — write path**: directory writer,
  append/rewrite/checkpoint, all encoders. Gate: cross-implementation
  round-trip identity.
* **Stage 4 — compatibility layer**: all 206 exported symbols
  implemented over the safe API. Gate: the entire existing test suite
  and all tools pass unchanged against the new implementation on all
  CI platforms; ABI checker reports an identical exported surface.
* **Stage 5 — switchover**: the new implementation becomes the
  default build (suggested: libtiff 5.0.0, soname unchanged); the
  safe C API is declared stable.
* **Stage 6 — separation and adoption**: ``libtiff_compat`` packaging
  split as packagers prefer; migration guide; tools migrate one by
  one as worked examples; new features land in the safe API first.
* **Stage 7 — Rust core and public C++ API** (``libtiff_cxx``): by a
  future RFC, once ``libtiff_core`` is stable and adopted.

Feasibility evidence
--------------------

A Stage-1 feasibility spike (``spike/`` on the development branch)
implemented a thin slice of every layer: classic and BigTIFF parsing,
checked strip geometry, none/PackBits codecs behind a mockable codec
interface, the safe C API, strict/legacy policies, and requirement
tracing. A pure-C99 client decodes both synthesised files and a real
corpus image through the safe API under the strict policy. The
traceability gate demonstrated its value immediately: on its first
run it flagged unverified MUST requirements, and writing the missing
test exposed a genuine conformance bug (64-bit offset types accepted
in a classic file, requirement BTF-009). Two registry defects found
by the spike have already been fed back to the requirements database
and merged.

Costs and risks
---------------

The bulk of the implementation will be produced with AI assistance,
in accordance with the project AI policy: every change is reviewed,
tested and submitted by the current volunteer maintainers, who remain
responsible for all content. The limiting resource is therefore
maintainer review bandwidth rather than implementation effort, and
the proposal is shaped around that: work arrives as small, focused
merge requests carrying their tests and requirement annotations; each
stage is independently valuable (the Stage 1–2 safe reader alone is a
useful, fuzzable artefact for security-sensitive consumers); and a
stage gate passes only on green differential and traceability
evidence, so review can lean on machine-checked conformance rather
than line-by-line re-derivation. The principal technical risk is
behavioural fidelity of the compatibility layer, which the policy
mechanism, the frozen legacy test suite and the differential CI are
designed to contain. Performance parity is tracked by benchmarks from
Stage 1.

What is asked of reviewers
--------------------------

1. Approval of the layered architecture and staging in principle.
2. Naming sign-off: the ``libtiff_<component>`` family
   (``libtiff_core``, ``libtiff_compat``, ``libtiff_cxx``) and the
   ``tiff_`` function prefix.
3. The C++17 build-requirement floor and the maintenance-branch
   window for platforms that cannot meet it.
4. The support decision for the hard legacy extension points
   (out-of-tree codecs, tag-method overrides, buffer-control calls),
   informed by the downstream survey.
5. Agreement on the development model: implementation produced in
   large part with AI assistance under the project AI policy, with
   every merge request reviewed and owned by the current volunteer
   maintainers.

References
----------

* `Work item 771
  <https://gitlab.com/libtiff/libtiff/-/work_items/771>`__ (C++
  conversion proposal), `work item 772
  <https://gitlab.com/libtiff/libtiff/-/work_items/772>`__ (C API
  refactoring options) and `work item 956
  <https://gitlab.com/libtiff/libtiff/-/work_items/956>`__
  (maintenance defect patterns and priorities).
* The :ref:`layered API migration plan <layered_api_migration_plan>`
  (companion document to this RFC, with the full symbol and type
  inventory).
* The `TIFF specification requirements database
  <https://gitlab.com/libtiff/tiff-specification>`__ and its deviation
  registry (published rendering at
  https://libtiff.gitlab.io/tiff-specification/).
* The Stage-1 feasibility spike and its findings
  (``spike/FINDINGS.md`` on the development branch).

Disclosure
----------

This RFC and its companion plan were drafted with AI assistance
(Claude Code, model Claude Fable 5), in accordance with the project AI
policy; the author has reviewed the content and takes responsibility
for it.
