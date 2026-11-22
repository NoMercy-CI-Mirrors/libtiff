# libtiff layered API migration: feasibility and plan (draft)

Status: draft for discussion. Not yet an RFC; intended to become one.

This document follows up on:

- work item 771 — Proposal for conversion of codebase to C++
- work item 772 — Exploration of options for C API refactoring
- work item 956 — LibTIFF maintenance defect patterns and development priorities
- `libtiff-cpp-rust-migration.md` (early C++ roadmap)
- `libtiff-incremental-improvements.md` (conservative C-only plan)
- `~/tiff-specification` — the machine-readable TIFF requirements
  database (§13)
- `spike/` — the Stage-1 feasibility spike validating this plan
  (findings in `spike/FINDINGS.md`)

It supersedes the *architectural* parts of 771 and the earlier roadmap: instead
of compiling the existing code as C++ and modernising it in place, it proposes
a new core with **two** C interfaces — the existing `TIFF*` API preserved as a
compatibility layer, and a new safe `tiff_*` C API that the compatibility
layer is itself built on. The C-only improvements of 772 and the incremental
plan remain valid for the maintenance branch and inform the new API design,
but they do not change the underlying implementation's safety properties.

## 1. Summary and verdict

**The layered approach is feasible.** The decisive observations:

1. The public contract of libtiff is almost entirely mediated by an opaque
   handle (`TIFF *`) and ~206 exported functions. Nothing in the supported
   ABI requires clients to see the internal `struct tiff` layout, so the
   implementation behind the handle can be replaced wholesale as long as the
   206 symbols keep their observable behaviour.
2. The behaviours that are hard to reproduce are *enumerable*: global error
   handlers, the global tag extender, the global codec registry, the varargs
   tag machinery, the exposed `TIFFRGBAImage` struct, mode strings, sentinel
   return conventions, and the directory reader's tolerance of damaged files.
   Each has a workable mapping, catalogued in §10 and §11.
3. The existing test suite and the 23 supported tools, kept on the old API
   unchanged, give a ready-made conformance suite for the compatibility
   layer. Work item 956 shows the defect families the new core must design
   out (repeated size arithmetic, mutable metadata ownership, stale I/O
   state, failure postconditions); these become structural properties of the
   core rather than per-bug patches.

A working spike (`spike/`, findings in `spike/FINDINGS.md`) now backs
these observations in code: the C API boundary, the port interfaces, the
strict/legacy policy toggles and the requirement-trace annotations were
exercised end-to-end on synthesised files and a real corpus image, and
the traceability gate caught a genuine conformance bug (LONG8 offsets
accepted in a classic file, BTF-009) on its first run.

**The honest costs:** this is a multi-person-year effort (rough estimate in
§16); it raises the toolchain floor to C++17 for building (not for using) the
library; a small number of legacy extension points (`TIFFAccessTagMethods`,
out-of-tree codecs that write into `struct tiff` via private headers) cannot
be expressed through the safe C API and need a thin, documented compat-support
interface or an explicit statement of non-support (§10.4); and the project
must sustain the old implementation on a maintenance branch until the compat
layer passes the full suite.
**Recommended shape:**

- C++17 core (`std::expected`-style error handling via a vendored
  `expected` for C++17, `std::expected` when built as C++23), no legacy
  quirks, strictness policies instead of hard-coded leniency; structured
  as a hexagonal (ports-and-adapters) architecture so every boundary is
  mockable and internal coupling stays low (§8.1), with Rust naming
  conventions to ease the later port (§6.1).
- New safe C99 API, function prefix `tiff_`, shipped as **`libtiff_core`**
  — the first of the `libtiff_<component>` family (`libtiff_core`,
  `libtiff_compat`, later `libtiff_cxx`) — designed so it can later be
  implemented by a Rust core without change.
- Existing `TIFF*` API re-implemented over the safe API plus a minimal
  compat-support interface, continuing to ship as `libtiff.so.6` /
  `LIBTIFF_4.x` symbol versions, built from a `libtiff_compat` source
  component (§14).
- Tools and the existing test suite stay on the old API throughout the
  conversion; the new core and safe API get an independent unit and
  integration test suite from day one (§12), with implementation and
  tests both traced to the TIFF specification requirements database
  (§13).

## 2. Context: why now

Work item 956 (November 2026 snapshot) quantifies the maintenance situation:

- 950 accessible issues; bounds/extent plus arithmetic/conversion account
  for 324 reports (34.1%). Issue intake grew from 38 (2024) to 109 (2025) to
  179 (Jan–Nov 2026).
- Recurring defect families are structural, not incidental: repeated size
  calculations diverging between allocation and use; metadata arrays whose
  lengths are derived from mutable tags (`td_colormap` vs `BitsPerSample`);
  stale cached I/O state; codec adapter contract drift; inconsistent failure
  postconditions; and duplicated implementations (tiffcrop.c 12.3 kLOC,
  tif_dirread.c 8.6 kLOC).
- Only ~13% of merged production MRs historically carried tests.

A major contributor has left, and the remaining maintainer bandwidth is
consumed by this defect stream. Incremental hardening (the
`libtiff-incremental-improvements.md` programme) reduces the bleeding but
cannot change the underlying properties of a 1988-vintage C design: unchecked
arithmetic by default, manual ownership, varargs type punning, and global
mutable state. A memory-safe or strongly-typed core removes whole defect
families; the question this document answers is how to get there without
abandoning existing users.

## 3. Goals, non-goals, anti-goals

**Goals**

- G1: Existing C applications continue to compile and run unchanged against
  the `TIFF*` API, with observable behaviour preserved (including error
  handler semantics and message formats where tests depend on them).
- G2: A new, safe, documented C API (`tiff_*`) with consistent error
  handling, no varargs, no global state, unsigned sizes, and explicit
  ownership — usable as a stable FFI boundary for other languages.
- G3: A C++17 core free of legacy inconsistencies, using `expected`-style
  error propagation throughout, checked arithmetic, and RAII ownership.
- G4: A staged path where each stage is releasable and testable, and where
  the final stage permits replacing the C++ core with Rust beneath the same
  safe C API.
- G5: Existing tools and tests remain on the old API during the conversion,
  acting as the conformance suite for the compat layer; the new layers get
  their own independent test suite.

**Non-goals**

- Changing the on-disk TIFF feature set (this is a re-implementation, not a
  format project).
- A new C++ *public* API in the first stages. The C++ core is internal; a
  public C++ API (`libtiff_cxx`) comes later and will be implemented over
  the safe C API (§15), so it survives the Rust transition.
- Thread-safety guarantees beyond "one handle per thread, no shared global
  state" in the first iteration (the safe API is designed not to preclude
  more later; cf. work item 772 §5).

**Anti-goals** (failure modes to design against)

- A compat layer that only *mostly* works: downstreams would be stuck on the
  maintenance branch forever and the project would carry three codebases.
- A "clean" core that quietly re-introduces configuration-dependent
  behaviour because compat needs leak into it. Leniency must be expressed as
  explicit, testable policy objects (§8.3), never as scattered special cases.
- An unsable safe API shipped too early and then broken. The safe API is
  declared stable only at the switchover stage (§16), after the compat layer
  has exercised it against the full legacy suite.

## 4. Target architecture

```
+---------------------------+     +----------------------------+
| existing applications     |     | new applications           |
| (and all in-tree tools,   |     |                            |
|  existing test suite)     |     |                            |
+------------+--------------+     +------+---------------------+
             |                           |
             v                           v
+---------------------------+     +----------------------------+
| legacy C API  (TIFF*)     |     | future public C++ API      |
| "libtiff_compat"          |     | "libtiff_cxx"              |
| varargs tags, global      |     | (header-mostly, over the   |
| handlers, sentinel returns|     |  safe C API)               |
+------------+--------------+     +------+---------------------+
             |  (+ small compat-support  |
             |   interface, §10.4)       |
             v                           v
+--------------------------------------------------------------+
| safe C API  (tiff_*, C99 headers)        "libtiff_core"       |
| status codes, out-params, unsigned sizes, per-handle errors,  |
| no varargs, no globals; the stable FFI boundary               |
+------------------------------+-------------------------------+
                               |
                               v
+--------------------------------------------------------------+
| core implementation (C++17, internal to libtiff_core)        |
| hexagonal ports and adapters (§8.1); Result<T>, checked      |
| arithmetic, RAII + std::span ownership, strictness policies; |
| later replaceable by a Rust core under the same safe C ABI   |
+--------------------------------------------------------------+
```

Layer rules:

- The **core** is never visible to clients. Its headers are not installed.
  No exceptions cross the C boundary; the safe API functions are
  `noexcept` in effect (every core failure is a value, not a throw; `throw`
  is banned in the core except where immediately translated).
- The **safe C API** is the only supported way through to the core. It is a
  pure C99 header set: compilable by a C99 compiler, no C++ constructs, no
  macros that collide with `tiff.h` (§6). The safe C API and the core
  implementation ship together as the `libtiff_core` DSO; the C++
  implementation has no linkage visibility outside it.
- The **legacy API** is a client of the safe API. Where legacy semantics
  cannot be expressed through it (enumerated in §10.4), the compat layer
  uses a private compat-support interface exported only to it — kept small,
  documented, and tracked, because every entry is future Rust-boundary work.
- The **old implementation** (current C code) remains intact on a
  maintenance branch and, during stages 0–4, in-tree, selected by a build
  option; CI builds both and compares (§12.3).

## 5. Relationship to the previous proposals

| Proposal | Disposition |
|---|---|
| 771: compile existing `.c` as C++, modernise in place | Not pursued for the core: in-place conversion inherits the existing structure and its defect families, and 956's data suggests the structure *is* the problem. Retained from 771: the error-code taxonomy, the tag-traits idea (as the constexpr tag registry, §8.4), codec interface shape, testing ideas, and the design influences (libertiff, Bio-Formats patterns). |
| 771: C ABI frozen, `extern "C"` wrappers | Retained, but the wrappers sit over the safe API rather than over in-place-converted internals. |
| 772: additive C API improvements (`WithErr` variants, per-instance error state, exported field helpers) | The error-model analysis (six return patterns, error-code bands) is adopted wholesale for the safe API (§9.2). The additive variants themselves remain available as maintenance-branch work but are not prerequisites. |
| Incremental improvements plan | Continues to apply to the maintenance branch (sentinel cleanup, checked arithmetic, fuzzing expansion). Its unit-test framework analysis is reused for the safe-API C test harness (§12.2). |
| 956 | Its defect families become explicit core design requirements (§8.2) and its recommended contracts (stored lengths, explicit units, failure postconditions) are core invariants. |

## 6. Naming, linkage, and coexistence

Both libraries must be loadable into one process, and both header sets
includable in one translation unit (mixed-API applications are the migration
path). Analysis of the current exports:

- All 206 versioned symbols start with `TIFF` or `_TIFF`, except nothing —
  however, the *headers* also declare unversioned/unexported names:
  `_TIFFcalloc` (declared in `tiffio.h`, **absent from `libtiff.map`** — a
  pre-existing export inconsistency to fix independently) and the LogLuv
  helpers (`LogL16toY`, `LogL10toY`, `LogL16fromY`, `LogL10fromY`,
  `uv_decode`, `uv_encode`, `LogLuv24toXYZ`, `LogLuv32toXYZ`,
  `LogLuv24fromXYZ`, `LogLuv32fromXYZ`, `XYZtoRGB24`), which are public on
  static/Windows builds but `local:` on ELF. These lowercase names do not
  collide with a `tiff_` prefix.
- The C struct tag namespace: `struct tiff` is taken by the legacy opaque
  type (`typedef struct tiff TIFF;`). The new handle must use a different
  tag: `typedef struct tiff_file tiff_file_t;`.
- The macro namespace is the real hazard: `tiff.h` defines ~635 macros
  (`TIFFTAG_*`, `COMPRESSION_*`, `PHOTOMETRIC_*`, `ORIENTATION_*`, …) —
  including bare names like `COMPRESSION_NONE`. The new headers therefore
  define **no object-like macros for constants at all**: every constant is
  an `enum` or `static const` in a `tiff_`-prefixed spelling
  (`tiff_compression_none`), so including `tiff.h` and `tiff_core.h` together
  is always safe in either order.

Conventions:

| Kind | Legacy | Safe C API | C++ core |
|---|---|---|---|
| functions | `TIFFReadDirectory` | `tiff_dir_read` | `tiff::core::...` (internal) |
| handle | `TIFF *` | `tiff_file_t *` (opaque, `struct tiff_file`) | `tiff::core::File` |
| status | int 1/0, -1, NULL, void | `tiff_status_t` (enum, 0 = ok) | `tiff::Result<T>` |
| sizes | `tmsize_t` (signed) | `size_t` / `uint64_t`, unsigned | `std::size_t`, strong types |
| constants | `#define TIFFTAG_IMAGEWIDTH 256` | `enum tiff_tag_id { tiff_tag_image_width = 256, … }` | `inline constexpr TagId IMAGE_WIDTH{256};` |
| headers | `<tiffio.h>`, `<tiff.h>` | `<tiff_core/tiff_core.h>` (+ `<tiff_core/tags.h>`, `<tiff_core/codecs.h>`) | not installed |
| library | `libtiff.so.6`, `LIBTIFF_4.x` nodes | `libtiff_core.so.0`, `LIBTIFF_CORE_1.0` node | (inside libtiff_core) |

On the C++ side there are no `#define`s and no unscoped enums: tag ids,
compression schemes, photometric values and the like are `constexpr`
strongly-typed values in namespaces (`tiff::tags::IMAGE_WIDTH`,
`tiff::compression::LZW`), usable in `switch` via their underlying ids but
not implicitly convertible to bare integers. The safe C API's enums are
generated from the same single source of truth as the C++ constants
(§8.4), so the two can never drift.

Naming follows the **`libtiff_<component>` family pattern**:
`libtiff_core` (safe C API + internal core implementation),
`libtiff_compat` (the legacy `TIFF*` surface, §14), and later
`libtiff_cxx` (the public C++ API, §15). The `tiff_` function prefix and
`_t` type suffix apply to the safe C API.

### 6.1 C++ naming: Rust conventions

To keep the eventual Rust port (§15) as mechanical as possible — and to
make the C++/Rust correspondence reviewable module-by-module when it
happens — the C++ core adopts Rust's naming conventions:

| Element | Convention | Example |
|---|---|---|
| namespaces (= Rust modules) | `snake_case`, one directory per top-level module | `tiff::core::geometry` |
| types (classes, structs, enums, aliases) | `UpperCamelCase` | `File`, `DirectoryBuilder`, `ReadPolicy`, `TagId` |
| functions, methods, variables | `snake_case` | `read_strip`, `byte_count` |
| constants (`constexpr` globals) | `SCREAMING_SNAKE_CASE` | `tags::IMAGE_WIDTH`, `MAX_DIR_COUNT` |
| enumerators | `UpperCamelCase` | `ByteOrder::LittleEndian` |
| error handling | `Result<T>` = `expected<T, Error>` | mirrors Rust's `Result<T, E>` |
| file names | `snake_case` matching the module | `core/geometry.cpp` |

The core defines no macros, with one sanctioned exception: a single
internal `TRY` macro for `Result` propagation (§7.1), confined to one
core-private header and never present in any installed header. Since the
core never includes the legacy headers, `SCREAMING_SNAKE_CASE` constants
cannot collide with `tiff.h` defines.
Module boundaries are chosen to map one-to-one onto future crate modules
(§8.1's ports and domain modules are that map).

## 7. Error handling design

### 7.1 Core

- Every fallible core operation returns `tiff::Result<T>` =
  `expected<T, tiff::Error>`. `tiff::Error` carries: a stable error code
  (taxonomy below), the module/component name, a formatted human message,
  and optionally a source location and the offending offset/tag id.
- Minimum C++17 means `std::expected` (C++23) is not assumed: `Result` is
  implemented over a vendored single-header `expected` under C++17,
  switching to `std::expected` when the compiler provides it. The spike
  confirmed vendoring is unavoidable (likewise for `span`); maintained
  implementations (tl::expected, tcb::span) are preferred over in-tree
  ones. The alias is internal, so the choice is not ABI.
- Error propagation uses a single internal `TRY(expr)` macro — decided
  from the spike, which measured three lines of boilerplate per fallible
  call without one (C++17 has no try-operator). It lives in one
  core-private header and is the only macro in the core (§6.1).
- `Error` must be constructible without allocating (static module
  names; fixed-size or lazily formatted message text), so reporting a
  failure can never itself fail under memory exhaustion — a spike
  finding.
- Error codes use the banded taxonomy from work item 772 §1: memory
  (1–99), I/O (100–199), format (200–299), tag (300–399), codec (400–499),
  state (500–599), parameter (600–699). The band values are shared verbatim
  with `tiff_status_t` in the safe C API.
- Warnings are not errors: the core emits warnings through a per-file
  diagnostic sink (a callback owned by the handle), never through globals.

### 7.2 Safe C API

- Every fallible function returns `tiff_status_t`; results travel through
  out-parameters. 0 (`tiff_ok`) is success; nonzero values are the banded
  codes above.
- Per-handle last-error detail: `tiff_last_error(const tiff_file_t *)`
  returns a `const tiff_error_t *` (owned by the handle, valid until the
  next API call on that handle) with accessors `tiff_error_code()`,
  `tiff_error_message()`, `tiff_error_module()`. Open failures, which have
  no handle, report through an out-parameter on the open call
  (`tiff_open_*` takes an optional `tiff_error_t *` buffer), not through
  thread-locals.
- Optional per-handle diagnostic callbacks (error + warning) are set via
  the open-options object; they receive the structured error, not varargs.
- `tiff_close()` returns `tiff_status_t` (fixing the `TIFFClose`
  cannot-report-errors defect); `tiff_close_abandon()` closes without
  flushing for error-path cleanup.

### 7.3 Compat layer

The legacy API has six observable conventions (772's inventory): int 1/0
(62 functions), `tmsize_t` count-or-−1 (18), `TIFF*`-or-NULL (8),
unsigned value with ambiguous 0 (25), void (28), other (22). The compat
layer reproduces each exactly:

- Each legacy function calls the safe API, and on failure converts
  `tiff_last_error()` into (a) the legacy return sentinel and (b) a call to
  the registered legacy handler chain.
- Global handler state (`TIFFSetErrorHandler`, `TIFFSetErrorHandlerExt`,
  warning equivalents) lives **in the compat layer only**, as process-wide
  state with the current semantics (last setter wins, returns previous).
  Compat installs its own per-handle diagnostic callback on every handle it
  opens; that callback re-dispatches to the legacy global/ext/R handler
  chain with the current precedence rules and the current message
  formatting (module string, `printf`-style expansion, the "Warning, "
  prefix behaviour of the default handlers), so existing handlers and
  message-matching tests see identical text.
- `TIFFErrorExtR`/`TIFFWarningExtR` and the OpenOptions handler setters map
  to per-handle callbacks directly (they are already per-handle in spirit).

## 8. The C++ core

### 8.1 Hexagonal architecture (ports and adapters)

The core is structured hexagonally: every external effect passes through
a **port** — a small interface owned by the domain — and **adapters**
implement ports; the domain never names an adapter. Consequences: every
boundary is mockable, the domain is testable without real files, I/O or
codecs, and internal coupling is bounded by port signatures (a codec sees
only its port context, never the whole file object).

**Driving side (who calls in):**

- the safe C API is the primary driving adapter (`capi/` translates
  `tiff_*` calls onto application-layer use-cases);
- the test suites drive the domain and use-cases directly (unit level)
  and the safe C API (integration level);
- `libtiff_compat` drives only the safe C API, never the core.

**Driven ports (what the core calls out to), each with production
adapters and test doubles shipped in the test suite:**

| Port | Purpose | Production adapters | Test doubles |
|---|---|---|---|
| `IoProvider` | read/seek/size/write/flush/truncate | fd, stdio, client callbacks (`tiff_io_callbacks_t`), read-only memory | in-memory buffer; fault injector (short reads, failing seeks) |
| `Allocator` | bounded allocation, per-file limits | malloc-backed with limit accounting | counting / scheduled-failure allocator |
| `DiagnosticSink` | structured errors and warnings | per-handle callback dispatch (safe API) | capturing sink asserted on by tests |
| `Codec` | strip/tile encode/decode | one adapter per scheme (none, PackBits, LZW, Deflate, JPEG, …) | identity codec; recording codec; fault injector |
| `TagRegistry` | tag metadata lookup (+ per-file overlay) | generated `constexpr` table (§8.4) | synthetic registries for edge cases |

**Domain (the inside of the hexagon) — pure value types and functions,
unit-testable with no mocks at all:**

- `tiff::core::header` — classic/BigTIFF header encode/decode: bytes in
  (`std::span<const std::byte>`), value objects out;
- `tiff::core::ifd` — IFD and entry encode/decode and validation,
  operating on spans, never on files;
- `tiff::core::geometry` — the single implementation of scanline/strip/
  tile size arithmetic, used by reader, writer and allocator alike (the
  956 "repeated size calculation" family collapses into one audited
  module), all checked, all `Result`-returning;
- `tiff::core::model` — `Directory` (immutable snapshot of typed,
  validated fields, every array carrying its own length — the 956
  colormap family is structurally impossible), `DirectoryBuilder`,
  `ReadPolicy`, `TagValue`, and strong types (`ByteCount`, `FileOffset`,
  `RowIndex`, …).

**Application layer (use-cases orchestrating domain + ports):** `File`
(lifecycle), `DirReader`, `DirWriter`, `StripReader`/`StripWriter`,
`TileReader`/`TileWriter`, `RgbaReader` — each receives its ports by
constructor injection; nothing reaches for a global. Codec instances are
per-file; the process-wide codec table is immutable after initialisation.
These use-cases are the units the safe C API calls.

### 8.2 Invariants (from 956's defect families)

1. Validated dimensions are computed once per operation and carried as a
   value object; narrowing happens at use sites via checked casts.
2. Every array field stores and validates its own element count.
3. Cached I/O state (EOF position, rewrite capacity) is invalidated by the
   operations that change it; staleness is a type-system property (the
   cache is owned by the writer object whose mutations invalidate it).
4. Codec adapter contracts are explicit: decoders return
   `expected<decoded_extent>`; "partial success" is a distinct status, not
   a sign convention.
5. Failure postconditions are stated per mutating operation and tested:
   on error, either the directory is unchanged, or the operation documents
   the residue. Allocation-failure paths are unit-tested with a failing
   allocator.
6. Ownership is explicit in types. Owning data lives in `std::vector` /
   `std::unique_ptr` members; every non-owning buffer parameter is a
   `std::span<std::byte>` / `std::span<const T>` — never pointer plus
   length, never a raw owning pointer. Pixel buffers are always
   owner-provided spans (the core allocates no pixel memory), and the
   core never stores a span into caller memory beyond the call that
   received it.

### 8.3 Strictness policies, not legacy quirks

The current reader tolerates damaged files in ways users depend on
(`EstimateStripByteCounts` for missing/wrong byte counts, old-style LZW,
out-of-order IFDs, tolerated FAX damage, unknown-tag skipping). The core
must support these *as explicit policy*, or the compat layer cannot
reproduce behaviour. Design:

- `ReadPolicy` — a value object of named toggles plus numeric limits
  (max single/cumulated allocation, max directory count, …).
- Toggle names come from the requirements database's deviation registry
  (§13), which defines `tolerate.unsorted-ifd`, `tolerate.missing-nul`,
  `tolerate.unaligned-values`, `tolerate.type-mismatch`,
  `tolerate.lzw-old-style`, `tolerate.deflate-32946`,
  `tolerate.iptc-as-long`, `tolerate.datetime-format`,
  `tolerate.packbits-cross-row-runs`, `repair.rowsperstrip-zero` and
  `repair.colormap-8bit`, plus per-rule repair flags defined by a
  rule's own repair block and indexed by the registry (such as
  `repair.bitspersample-count` on DEP-010). Quirks that compat work
  uncovers beyond these (strip-byte-count estimation,
  truncated-last-strip tolerance, …) are added to the registry first
  and implemented second, so core leniency and the documented
  specification deviations can never drift apart.
- The spike fed this flow twice, and both items are merged in the
  database (its `reg-review-libtiff.md`): TAG-258-01 now points at
  DEP-010's `repair.bitspersample-count` repair (single value
  replicated across samples; other short counts stay at error), and
  `tolerate.packbits-cross-row-runs` settles CMP-32773-01's
  row-boundary question — strip/tile decode accepts runs that span
  rows, while runs overrunning the decoded size are damaged data
  outside the flag. The core's PackBits decoder implements exactly
  those semantics.
- Presets: `ReadPolicy::strict()` (new-API default),
  `ReadPolicy::legacy()` (compat default, matching current observable
  behaviour).
- Each toggle is independently unit-tested; the quirk inventory in §11 is
  the checklist. The core code stays clean because each tolerance is one
  guarded, named branch, not an emergent property.

The safe C API exposes the same policies on `tiff_open_options_t`
(`tiff_open_options_set_policy(opts, tiff_policy_strict | …)` plus
individual setters), so security-sensitive consumers can get strictness the
legacy API never offered.

### 8.4 Tag knowledge as a single source of truth

One table defines every known tag: id, name, TIFF datatypes, count rule
(fixed / variable / SamplesPerPixel), writability, which directory kinds
it may appear in, and default. The specification side of this table
already exists as the requirements database's tag registries
(`tags.yaml`, `tags-exif.yaml`, `tags-dng.yaml`, `tags-fx.yaml`; §13):
the libtiff build generates from a pinned copy, adding only
libtiff-specific columns (set/get plumbing, pseudo-tag routing). From it
are generated: the C++ `constexpr` registry, the safe C
API's `tiff_tag_id` enum and `tiff_tag_info()` data, and the compat layer's
`TIFFField` tables (which must match today's `TIFFFieldArray` contents
exactly — verifiable by a test that walks both). Runtime registration of
custom tags extends a per-handle overlay, not a global.

## 9. The safe C API

### 9.1 Conventions

- C99 headers; no varargs anywhere; no function-like macros in the public
  surface; all sizes unsigned (`size_t` for memory, `uint64_t` for file
  quantities); no signed sentinels.
- Caller-provided buffers everywhere on hot paths (the library does not
  allocate pixel memory); every buffer parameter is pointer **and**
  capacity; actual lengths are returned via out-params. No ownership
  crosses the boundary except objects with explicit `_new`/`_free` pairs
  (`tiff_open_options_t`, `tiff_file_t`).
- Strings returned by the library (`tiff_error_message`, tag names) are
  const, UTF-8, owned by the handle, valid until the next call on it.
- Thread model v1: a `tiff_file_t` may be used by one thread at a time;
  distinct handles are fully independent (no shared mutable globals).
  Stated in the header; stronger guarantees may come later (772 §5's
  per-operation contexts remain compatible with this surface).
- Every function documents its failure postcondition.

### 9.2 Representative signatures

```c
typedef struct tiff_file tiff_file_t;
typedef enum tiff_status { tiff_ok = 0, /* banded codes, §7.1 */ } tiff_status_t;

/* lifecycle */
tiff_status_t tiff_open_read(const char *path,
                             const tiff_open_options_t *opts /*nullable*/,
                             tiff_file_t **out);
tiff_status_t tiff_open_write(const char *path, tiff_format_t format,
                              const tiff_open_options_t *opts,
                              tiff_file_t **out);
tiff_status_t tiff_open_append(const char *path, const tiff_open_options_t *opts,
                               tiff_file_t **out);
tiff_status_t tiff_open_client(const tiff_io_callbacks_t *io, void *user_data,
                               tiff_open_mode_t mode,
                               const tiff_open_options_t *opts,
                               tiff_file_t **out);
tiff_status_t tiff_close(tiff_file_t *);          /* flushes; reports */
void          tiff_close_abandon(tiff_file_t *);  /* never fails */

/* directories */
tiff_status_t tiff_dir_count(tiff_file_t *, uint32_t *count);
tiff_status_t tiff_dir_select(tiff_file_t *, uint32_t index);
tiff_status_t tiff_dir_select_offset(tiff_file_t *, uint64_t offset);
tiff_status_t tiff_dir_write(tiff_file_t *);

/* typed tag access (no varargs) */
tiff_status_t tiff_tag_get_u32(tiff_file_t *, tiff_tag_id_t, uint32_t *value);
tiff_status_t tiff_tag_get_u16_array(tiff_file_t *, tiff_tag_id_t,
                                     uint16_t *buf, size_t capacity,
                                     size_t *count /* query with buf==NULL */);
tiff_status_t tiff_tag_set_u32(tiff_file_t *, tiff_tag_id_t, uint32_t value);

/* geometry (checked, unsigned) */
tiff_status_t tiff_scanline_size(tiff_file_t *, uint64_t *size);
tiff_status_t tiff_strip_count(tiff_file_t *, uint32_t *count);

/* pixel I/O (caller buffers, explicit capacity) */
tiff_status_t tiff_strip_read(tiff_file_t *, uint32_t strip,
                              void *buf, size_t capacity, size_t *produced);
tiff_status_t tiff_rgba_read_image(tiff_file_t *, uint32_t *buf,
                                   size_t capacity_pixels,
                                   tiff_rgba_options_t options);
```

### 9.3 Scope decisions

In scope: everything required to implement the compat layer (open/close,
directories including EXIF/GPS/custom, typed tag get/set/unset/enumerate,
tag metadata introspection, runtime tag registration, geometry, scanline/
strip/tile encoded+raw I/O, external-buffer decode, strile offset/count
access, RGBA decoding, codec enumeration, policy options). Out of scope for
v1 (stay compat-only until demanded): byte-swap/bit-reversal utilities,
LogLuv/Lab/YCbCr conversion helpers as standalone entry points, memory-
allocation wrappers (unnecessary: no ownership transfer), `mmap` control
(an I/O-provider implementation detail), display structures.

## 10. The compat layer

### 10.1 Principle

`libtiff_compat` re-implements every exported legacy symbol over the safe
API. It is a *client* with privileged knowledge of defaults, not a second
door into the core. It owns all legacy-only machinery:

- varargs parsing for `TIFFGetField`/`TIFFSetField`/`TIFFVGetField`/
  `TIFFVSetField`/`TIFFGetFieldDefaulted` (+ the `TIFF_VARIABLE` /
  `TIFF_SPP` / pass-count conventions, `uint16_vap` promotion), translating
  to typed safe-API calls using the shared tag metadata;
- the global error/warning handler chain (§7.3);
- the global tag-extender chain (`TIFFSetTagExtender`): compat runs
  extenders at open/directory-create time and translates their
  `TIFFMergeFieldInfo`/`TIFFMergeFields` calls into safe-API runtime tag
  registration on that handle;
- legacy type aliases and headers (`tiffio.h`, `tiff.h` unchanged);
- mode-string parsing (`"r"`, `"w"`, `"a"`, `l/L/B/M/m/C/c/h/4/8/D/O`
  flags) mapped to open functions + options;
- the `TIFFRGBAImage` public struct: the current `tif_getimage.c` moves
  into compat nearly verbatim (it is already a client of the rest of the
  API), preserving struct layout and the user-replaceable `put` routine
  contract bit-for-bit;
- `TIFFPrintDirectory` output formatting (tests diff its output).

### 10.2 Handle identity

`TIFF *` remains opaque. In the compat implementation, `struct tiff`
becomes a compat-owned wrapper holding the `tiff_file_t *`, the legacy
client-data pointers, cached procs, name, mode flags, and the per-handle
compat state (current strip/tile/row counters where the safe API does not
expose equivalents). Code that includes `tiffiop.h` today (22 tools/tests
do) is *not* part of the supported ABI and is handled in §10.4/§12.4.

### 10.3 Client I/O procs

`TIFFClientOpen(Ext)`'s eight procs map onto `tiff_io_callbacks_t`
one-to-one (read, write, seek, close, size) with two exceptions:

- map/unmap procs: the safe API treats memory mapping as an I/O-provider
  internal. Compat accepts the procs and implements a mapped provider when
  they are non-null, preserving `TIFFIsMSB2LSB`-era observable behaviour;
  if the core's provider abstraction does not support mapping in v1, compat
  falls back to read-based I/O (observable only through performance and
  through `tif_flags`, which is not public ABI). Decision needed; low risk.
- `TIFFGetReadProc`/`GetSeekProc`/etc. return exactly the procs the client
  passed (compat caches them), so round-tripping is preserved.

### 10.4 The hard cases (compat-support interface or documented limits)

These cannot be expressed through the safe C API and need either a private
interface exported only to libtiff_compat, or an explicit support decision:

1. **`TIFFAccessTagMethods`** — lets applications replace per-handle
   `vset`/`vget`/`printdir` with va_list-based methods. Compat can honour
   it *within its own varargs layer* (compat dispatches tag calls through
   the method table before translating to the safe API), so no core
   support is needed. Keep.
2. **Out-of-tree codecs via `TIFFRegisterCODEC`** — the `TIFFInitMethod`
   contract has the codec write function pointers into `struct tiff`
   fields declared in the *private* `tiffiop.h`. This was never a stable
   ABI (it requires internal headers that are not installed by default).
   Options: (a) compat-support hooks that adapt the legacy
   `TIFFCodeMethod` signatures onto a core codec shim — moderate work,
   full compatibility; (b) declare externally-registered codecs supported
   only through a new safe-API codec interface, and `TIFFRegisterCODEC`
   returns failure for unknown schemes. **Recommendation: (a)**, since
   fax/ojpeg-style forks exist in the wild, but decide via RFC after
   surveying actual users.
3. **`TIFFSetCompressionScheme`, `_TIFFRewriteField`, `_TIFFGetExifFields`,
   `_TIFFGetGpsFields`, `_TIFFCheckMalloc`, `_TIFFCheckRealloc`,
   `_TIFFMultiply32/64`, `_TIFFClampDoubleToUInt32`, `_TIFFAdd64`,
   `_TIFFAddSSize`, `_TIFFCastUInt64ToSSize`, `_TIFFCastUInt64ToUInt32`,
   `_TIFFComputeBitOffset`, `_TIFFComputeRowOffset`, `_TIFFComputeRowSize64`,
   `_TIFFMultiplySSize`** — exported private symbols that exist for
   in-tree tools/tests. They are versioned ABI, so compat must keep
   exporting them; implementations are small and live wholly in compat
   (arithmetic helpers) or map to safe-API equivalents (field arrays,
   rewrite-field → safe tag/directory calls).
4. **Buffer-control entry points** (`TIFFReadBufferSetup`,
   `TIFFWriteBufferSetup`, `TIFFSetWriteOffset`, `TIFFSetupStrips`,
   `TIFFWriteCheck`) — expose internal buffer/offset staging. Compat
   emulates the observable parts (user-supplied working buffers, forced
   write offsets) through open-options and compat-support hooks; exact
   disposition per function in §11. These are the least-clean mappings and
   the main source of compat-support surface.

## 11. Full inventory

### 11.1 Exported functions (all 206 versioned symbols)

Legend — Compat: how the legacy symbol is implemented. **S** = pure mapping
onto the safe API; **C** = implemented entirely within the compat layer
(legacy-only machinery); **H** = needs compat-support hooks beyond the safe
API (§10.4). Safe API: the counterpart new users call (— = deliberately no
equivalent).

#### Open / close / lifecycle

| Legacy | Compat | Safe API counterpart | Notes |
|---|---|---|---|
| `TIFFOpen` | S | `tiff_open_read` / `tiff_open_write` / `tiff_open_append` | mode-string parsing in compat |
| `TIFFOpenW` (win32) | S | `tiff_open_read_w` etc. (wchar variants, win32 only) | |
| `TIFFOpenExt`, `TIFFOpenWExt` | S | same + `tiff_open_options_t` | |
| `TIFFFdOpen`, `TIFFFdOpenExt` | S | `tiff_open_fd` | fd ownership rules preserved (mode `O` etc.) |
| `TIFFClientOpen`, `TIFFClientOpenExt` | S | `tiff_open_client` | §10.3; map/unmap procs special-cased |
| `TIFFClose` | S | `tiff_close` | legacy: void, errors only via handlers — compat swallows status after dispatching diagnostics |
| `TIFFCleanup` | H | — | close-without-closing-fd; safe API: `tiff_close_abandon` + client-I/O close semantics |
| `TIFFFlush`, `TIFFFlushData` | S | `tiff_flush`, `tiff_flush_data` | |
| `TIFFOpenOptionsAlloc/Free` | S | `tiff_open_options_new/free` | |
| `TIFFOpenOptionsSetMaxSingleMemAlloc` | S | `tiff_open_options_set_max_single_alloc` | |
| `TIFFOpenOptionsSetMaxCumulatedMemAlloc` | S | `tiff_open_options_set_max_total_alloc` | |
| `TIFFOpenOptionsSetWarnAboutUnknownTags` | S | policy toggle (§8.3) | |
| `TIFFOpenOptionsSetErrorHandlerExtR`, `…SetWarningHandlerExtR` | S | `tiff_open_options_set_error_cb` / `_warning_cb` | structured callback instead of va_list |

#### Handle accessors / predicates

| Legacy | Compat | Safe API counterpart | Notes |
|---|---|---|---|
| `TIFFGetVersion` | C | `tiff_version_string`, `tiff_version_number` | distinct strings per library |
| `TIFFFileName`, `TIFFSetFileName` | C | `tiff_file_name` (get only) | setter is compat-only state |
| `TIFFFileno`, `TIFFSetFileno` | C/H | `tiff_file_descriptor` (get) | setter has no safe equivalent (mutating I/O identity); compat keeps it working via I/O-provider swap hook |
| `TIFFClientdata`, `TIFFSetClientdata` | C/H | user_data fixed at open | as above |
| `TIFFGetMode`, `TIFFSetMode` | C/H | `tiff_open_mode(f)` (get) | mode mutation is compat-only |
| `TIFFIsTiled` | S | `tiff_is_tiled` | |
| `TIFFIsByteSwapped` | S | `tiff_file_byte_order` + host query | |
| `TIFFIsBigEndian` | S | `tiff_file_byte_order` | |
| `TIFFIsBigTIFF` | S | `tiff_file_format` (classic/big) | |
| `TIFFIsMSB2LSB` | S | `tiff_fill_order` | |
| `TIFFIsUpSampled` | S | `tiff_is_upsampled` | |
| `TIFFGetReadProc/WriteProc/SeekProc/CloseProc/SizeProc/MapFileProc/UnmapFileProc` | C | — | compat returns cached client procs |
| `TIFFCurrentRow/Strip/Tile` | C | — | sequential-I/O cursors tracked by compat; safe API is explicit-index |
| `TIFFCurrentDirectory`, `TIFFCurrentDirOffset` | S | `tiff_dir_index`, `tiff_dir_offset` | |
| `TIFFGetClientInfo`, `TIFFSetClientInfo` | C | — | named client-data list is compat-owned |

#### Directory navigation and lifecycle

| Legacy | Compat | Safe API counterpart | Notes |
|---|---|---|---|
| `TIFFReadDirectory` | S | implicit in `tiff_dir_select` / `tiff_dir_next` | legacy "returns 0 at last dir" semantics reproduced by compat cursor |
| `TIFFNumberOfDirectories` | S | `tiff_dir_count` | legacy loops/IFD-cycle guards preserved via policy |
| `TIFFSetDirectory` | S | `tiff_dir_select` | |
| `TIFFSetSubDirectory` | S | `tiff_dir_select_offset` | SubIFD navigation |
| `TIFFLastDirectory` | S | `tiff_dir_is_last` | |
| `TIFFUnlinkDirectory` | S | `tiff_dir_unlink` | 1-based legacy index quirk stays in compat |
| `TIFFCreateDirectory` | S | `tiff_dir_append` | |
| `TIFFFreeDirectory` | S | `tiff_dir_reset` | |
| `TIFFCreateCustomDirectory`, `TIFFCreateEXIFDirectory`, `TIFFCreateGPSDirectory` | S | `tiff_dir_append_custom(kind)` | kinds: exif/gps/custom field array |
| `TIFFReadCustomDirectory`, `TIFFReadEXIFDirectory`, `TIFFReadGPSDirectory` | S | `tiff_dir_read_custom(offset, kind)` | `TIFFFieldArray*` argument mapped to kind/registered set |
| `TIFFWriteDirectory` | S | `tiff_dir_write` | |
| `TIFFWriteCustomDirectory` | S | `tiff_dir_write_custom(&offset_out)` | |
| `TIFFCheckpointDirectory` | S | `tiff_dir_checkpoint` | |
| `TIFFRewriteDirectory` | S | `tiff_dir_rewrite` | the 956 stale-EOF family is a core invariant test |
| `TIFFDeferStrileArrayWriting`, `TIFFForceStrileArrayWriting` | S | `tiff_dir_defer_strile_arrays`, `tiff_dir_flush_strile_arrays` | |
| `TIFFPrintDirectory` | C | — | exact output format preserved in compat; new API offers `tiff_dir_dump` (new format) later |

#### Tag / field access

| Legacy | Compat | Safe API counterpart | Notes |
|---|---|---|---|
| `TIFFGetField`, `TIFFVGetField` | C→S | `tiff_tag_get_<type>[_array]` family | varargs decode in compat via shared tag metadata |
| `TIFFGetFieldDefaulted`, `TIFFVGetFieldDefaulted` | C→S | `tiff_tag_get_*_defaulted` or `tiff_tag_get_default` | defaults table shared |
| `TIFFSetField`, `TIFFVSetField` | C→S | `tiff_tag_set_<type>[_array]` | includes pseudo-tag handling (below) |
| `TIFFUnsetField` | S | `tiff_tag_unset` | |
| `TIFFGetTagListCount`, `TIFFGetTagListEntry` | S | `tiff_tag_count` / `tiff_tag_at` | enumerates set custom tags |
| `TIFFFindField`, `TIFFFieldWithTag`, `TIFFFieldWithName` | S | `tiff_tag_info_by_id` / `_by_name` | returns `const tiff_tag_info_t *` |
| `TIFFFieldTag/Name/DataType/PassCount/ReadCount/WriteCount/SetGetSize/SetGetCountSize/IsAnonymous` | S | `tiff_tag_info_*` accessors | |
| `TIFFMergeFieldInfo` (obsoleted) | S | `tiff_tag_register` | `TIFFFieldInfo` → `tiff_tag_def_t` translation in compat |
| `TIFFSetTagExtender` | C | — (per-handle `tiff_tag_register`) | global chain emulated by compat (§10.1) |
| `TIFFAccessTagMethods` | H | — | honoured inside compat's dispatch (§10.4.1) |
| `TIFFDataWidth` | S | `tiff_datatype_size` | |
| Codec pseudo-tags (`TIFFTAG_JPEGQUALITY`, `ZIPQUALITY`, …) | C→S | `tiff_codec_option_set_i32` / `_get_i32` (per-handle codec options) | keeps core tag model clean; compat routes pseudo-tags here |

#### Geometry and layout

| Legacy | Compat | Safe API counterpart | Notes |
|---|---|---|---|
| `TIFFScanlineSize`, `TIFFScanlineSize64` | S | `tiff_scanline_size` (uint64 out-param) | legacy signed truncation + 0-on-error stays in compat |
| `TIFFRasterScanlineSize`, `…64` | S | `tiff_raster_scanline_size` | |
| `TIFFStripSize`, `…64`; `TIFFVStripSize`, `…64` | S | `tiff_strip_size`, `tiff_strip_size_rows` | |
| `TIFFRawStripSize`, `…64` | S | `tiff_strip_raw_size` | |
| `TIFFTileSize`, `…64`; `TIFFVTileSize`, `…64`; `TIFFTileRowSize`, `…64` | S | `tiff_tile_size`, `tiff_tile_size_rows`, `tiff_tile_row_size` | |
| `TIFFDefaultStripSize`, `TIFFDefaultTileSize` | S | `tiff_default_strip_rows`, `tiff_default_tile_size` | |
| `TIFFComputeStrip`, `TIFFNumberOfStrips` | S | `tiff_strip_for_location`, `tiff_strip_count` | |
| `TIFFComputeTile`, `TIFFCheckTile`, `TIFFNumberOfTiles` | S | `tiff_tile_for_location`, `tiff_tile_location_valid`, `tiff_tile_count` | |
| `TIFFSetupStrips` | H | — | writer-internal staging; compat hook |
| `TIFFGetMaxCompressionRatio` | S | `tiff_codec_max_ratio` | |

#### Image data I/O

| Legacy | Compat | Safe API counterpart | Notes |
|---|---|---|---|
| `TIFFReadScanline`, `TIFFWriteScanline` | S | `tiff_scanline_read/write(f, row, plane, buf, cap)` | legacy implicit-size buffer (no capacity!) — compat passes computed scanline size |
| `TIFFReadEncodedStrip`, `TIFFReadEncodedTile` | S | `tiff_strip_read`, `tiff_tile_read` | legacy `size=-1` = "whole" quirk in compat |
| `TIFFReadRawStrip`, `TIFFReadRawTile` | S | `tiff_strip_read_raw`, `tiff_tile_read_raw` | |
| `TIFFWriteEncodedStrip/Tile`, `TIFFWriteRawStrip/Tile` | S | `tiff_strip_write[_raw]`, `tiff_tile_write[_raw]` | |
| `TIFFReadTile`, `TIFFWriteTile` | S | `tiff_tile_read_at`, `tiff_tile_write_at` (x,y,z,sample) | |
| `TIFFReadFromUserBuffer` | S | `tiff_decode_buffer` | decode caller-supplied raw data |
| `TIFFReadBufferSetup`, `TIFFWriteBufferSetup` | H | — | user-owned working buffers; open-option or compat hook |
| `TIFFSetWriteOffset` | H | — | raw offset override; compat hook |
| `TIFFWriteCheck` | C | — | compat re-implements the mode/state validation over safe-API queries |
| `TIFFGetStrileOffset/ByteCount[WithErr]` | S | `tiff_strip_offset`, `tiff_strip_byte_count` (status-returning) | WithErr variants map 1:1 |

#### RGBA convenience layer

| Legacy | Compat | Safe API counterpart | Notes |
|---|---|---|---|
| `TIFFReadRGBAImage`, `TIFFReadRGBAImageOriented` | C→S | `tiff_rgba_read_image(f, buf, cap, opts)` | legacy getimage code moves into compat (§10.1) |
| `TIFFReadRGBAStrip[Ext]`, `TIFFReadRGBATile[Ext]` | C→S | `tiff_rgba_read_strip`, `tiff_rgba_read_tile` | |
| `TIFFRGBAImageOK` | C→S | `tiff_rgba_supported(f, tiff_error_t*)` | 1024-char reason buffer convention compat-only |
| `TIFFRGBAImageBegin/Get/End` | C | — | exposed-struct API, compat-only forever; new API covers the use cases via options (stop-on-error, orientation, put-routine cases become caller post-processing) |

#### Error / warning handling

| Legacy | Compat | Safe API counterpart | Notes |
|---|---|---|---|
| `TIFFError`, `TIFFErrorExt`, `TIFFWarning`, `TIFFWarningExt` | C | — | app-initiated dispatch into handler chain; compat keeps entire mechanism |
| `TIFFErrorExtR`, `TIFFWarningExtR` | C | `tiff_emit_error` / `tiff_emit_warning` (for codec/extension authors) | |
| `TIFFSetErrorHandler[Ext]`, `TIFFSetWarningHandler[Ext]` | C | — (per-handle callbacks in options) | global state lives in compat (§7.3) |

#### Codec registry

| Legacy | Compat | Safe API counterpart | Notes |
|---|---|---|---|
| `TIFFFindCODEC`, `TIFFIsCODECConfigured` | S | `tiff_codec_info(scheme)`, `tiff_codec_available` | |
| `TIFFGetConfiguredCODECs` | S | `tiff_codec_list(buf, cap, &count)` | legacy returns malloc'd array caller frees with `_TIFFfree` — preserved in compat |
| `TIFFRegisterCODEC`, `TIFFUnRegisterCODEC` | H | `tiff_codec_register(const tiff_codec_vtable_t*)` | §10.4.2: legacy init-method contract via adapter hooks |
| `TIFFSetCompressionScheme` (private, exported) | H | — | used by tools; compat hook |

#### Byte order / bit utilities (compat-only; stateless)

| Legacy | Compat | Safe API | Notes |
|---|---|---|---|
| `TIFFSwabShort/Long/Long8/Float/Double` | C | — | trivially reimplemented in compat |
| `TIFFSwabArrayOfShort/Triples/Long/Long8/Float/Double` | C | — | |
| `TIFFReverseBits`, `TIFFGetBitRevTable` | C | — | table identity (pointer comparisons unlikely but table contents fixed) |

#### Colour conversion helpers

| Legacy | Compat | Safe API | Notes |
|---|---|---|---|
| `TIFFYCbCrToRGBInit`, `TIFFYCbCrtoRGB` | C | deferred (`tiff_color_*` v2 candidate) | public struct with caller-layout memory contract (§11.2) — compat verbatim |
| `TIFFCIELabToRGBInit`, `TIFFCIELabToXYZ`, `TIFFXYZToRGB` | C | deferred | |
| LogLuv group (11 fns, header-public, not in `.map`): `LogL16toY`, `LogL10toY`, `LogL16fromY`, `LogL10fromY`, `uv_decode`, `uv_encode`, `LogLuv24toXYZ`, `LogLuv32toXYZ`, `LogLuv24fromXYZ`, `LogLuv32fromXYZ`, `XYZtoRGB24` | C | deferred | export inconsistency: public on static/Windows, local on ELF; keep as-is in compat |

#### Memory and arithmetic helpers (exported `_TIFF*`)

| Legacy | Compat | Safe API | Notes |
|---|---|---|---|
| `_TIFFmalloc`, `_TIFFrealloc`, `_TIFFfree`, `_TIFFcalloc` | C | — | must remain: legacy API transfers ownership of buffers freed with `_TIFFfree` (e.g. `TIFFGetConfiguredCODECs`). `_TIFFcalloc` missing from `libtiff.map` — fix on maintenance branch now |
| `_TIFFmemset`, `_TIFFmemcpy`, `_TIFFmemcmp` | C | — | |
| `_TIFFCheckMalloc`, `_TIFFCheckRealloc` | C | — | exported-private; tools/tests |
| `_TIFFMultiply32`, `_TIFFMultiply64`, `_TIFFAdd64`, `_TIFFAddSSize`, `_TIFFMultiplySSize` | C | — | checked arithmetic exists separately inside core |
| `_TIFFCastUInt64ToSSize`, `_TIFFCastUInt64ToUInt32`, `_TIFFClampDoubleToUInt32` | C | — | |
| `_TIFFComputeBitOffset`, `_TIFFComputeRowOffset`, `_TIFFComputeRowSize64` | C | — | 4.7.2 additions |
| `_TIFFRewriteField` | H | — | exported-private directory surgery; compat hook over writer |
| `_TIFFGetExifFields`, `_TIFFGetGpsFields` | S | `tiff_tag_table(kind)` | |

### 11.2 Public data structures and types

| Legacy type | Kind | Disposition |
|---|---|---|
| `TIFF` (`struct tiff`) | opaque | compat wrapper over `tiff_file_t` (§10.2); layout private as today |
| `TIFFOpenOptions` | opaque | maps to `tiff_open_options_t` |
| `TIFFField`, `TIFFFieldArray` | opaque | compat views over shared tag metadata; pointer identity stable per handle |
| `tmsize_t`, `tsize_t` | signed size typedef | compat headers unchanged; safe API uses `size_t`/`uint64_t` |
| `toff_t` (uint64), `tdir_t` (uint32) | typedef | compat unchanged; safe API: `uint64_t` offsets, `uint32_t` dir index |
| `ttag_t`, `tsample_t`, `tstrile_t`, `tstrip_t`, `ttile_t`, `tdata_t` | deprecated typedefs | compat unchanged; no safe-API equivalents |
| `thandle_t` | void* / win32 handle | compat unchanged; safe API: `void *user_data` |
| `TIFFHeaderCommon/Classic/Big` | exposed structs | compat unchanged; safe API exposes parsed header info via accessors, not structs |
| `TIFFDataType` enum (+ `uint16_vap`) | enum | compat unchanged; safe API `tiff_datatype_t` (same values) |
| `TIFFRGBAImage` + `tileContigRoutine`, `tileSeparateRoutine` | exposed struct + callbacks | frozen in compat, bit-identical layout (§10.1); no safe-API equivalent |
| `TIFFRGBValue`, `TIFFDisplay`, `TIFFYCbCrToRGB`, `TIFFCIELabToRGB` | exposed structs | compat-only (colour helpers); caller-allocated-table contract of `TIFFYCbCrToRGBInit` preserved verbatim |
| `TIFFCodec`, `TIFFInitMethod` | exposed struct + callback | compat-only; safe API uses `tiff_codec_info_t` (read) and `tiff_codec_vtable_t` (registration) |
| `TIFFTagMethods`, `TIFFVSetMethod`, `TIFFVGetMethod`, `TIFFPrintMethod` | exposed struct + va_list callbacks | compat-only (§10.4.1) |
| `TIFFFieldInfo` | obsoleted exposed struct | compat-only; translated to `tiff_tag_def_t` |
| `TIFFRational_t` | exposed struct | safe API `tiff_rational_t` (u32/u32) + `tiff_srational_t` |
| I/O proc typedefs (`TIFFReadWriteProc`, `TIFFSeekProc`, `TIFFCloseProc`, `TIFFSizeProc`, `TIFFMapFileProc`, `TIFFUnmapFileProc`) | callbacks | compat unchanged; safe API `tiff_io_callbacks_t` (struct of pointers, no map/unmap) |
| Error handler typedefs (`TIFFErrorHandler`, `TIFFErrorHandlerExt`, `TIFFErrorHandlerExtR`) | callbacks | compat unchanged; safe API structured `tiff_diag_cb_t` |
| `TIFFExtendProc` | callback | compat-only (tag extender) |
| ~635 macros in `tiff.h` (351 `TIFFTAG_*`, plus `COMPRESSION_*`, `PHOTOMETRIC_*`, `PLANARCONFIG_*`, `ORIENTATION_*`, `RESUNIT_*`, `SAMPLEFORMAT_*`, `FILLORDER_*`, `EXTRASAMPLE_*`, `INKSET_*`, `FAXMODE_*`, `JPEGPROC_*`, `SGILOG*`, DNG tags, pseudo-tags, …) | object macros | compat headers unchanged; safe API re-expresses every needed constant as prefixed enums (`tiff_tag_id_t`, `tiff_compression_t`, `tiff_photometric_t`, …) generated from the shared tag/constant table; C++ core uses `constexpr` equivalents (§6) |
| `tiffio.h` macros (`TIFFPRINT_*`, `TIFFGetR/G/B/A`, `D65_*`, `D50_*`, `U_NEU`, `V_NEU`, `UVSCALE`, `TIFF_VARIABLE*`, `TIFF_SPP`, `TIFF_ANY`, `FIELD_CUSTOM`, `CIELABTORGB_TABLE_RANGE`, `TIFF_TMSIZE_T_MAX`) | object/function macros | compat unchanged; safe API: `tiff_rgba_r(x)` style inline functions where needed |

## 12. Testing strategy

### 12.1 Constraint: legacy suite stays put

All 23 supported tools and the entire existing test suite (~154 test
programs/scripts plus image corpus) remain on the `TIFF*` API, **unmodified
in intent**, for the whole conversion. They are the executable
specification of the compat layer. The only permitted changes are build
wiring (choosing which implementation they link against) and new
assertions; no test is weakened or ported during stages 0–5.

### 12.2 New independent test suite

A parallel suite, new directories, from day one of core work:

- **Core unit tests** (C++): GoogleTest + GoogleMock over the port
  doubles of §8.1 (mock `IoProvider`, scheduled-failure `Allocator`,
  fault-injecting `Codec`, capturing `DiagnosticSink`). Every geometry
  function, every policy toggle, every failure postcondition, every error
  code path. Property-based checks for the checked-arithmetic layer.
- **Safe C API tests** (pure C99, compiled with a C compiler, not C++):
  proves the headers are C-clean and the ABI usable without C++; exercises
  every exported `tiff_*` function, including error reporting, buffer
  under-capacity behaviour, and policy effects. These are the reference
  examples for the eventual API documentation.
- **Integration tests**: round-trips over the shared image corpus through
  the safe API alone (read, re-write, re-read, compare), per codec, per
  format (classic/BigTIFF, endianness, tiled/stripped, planar configs).
- **Fuzzing**: new OSS-Fuzz targets on the safe API from the first
  readable slice (directory parse, strip decode, RGBA), with the strict
  *and* legacy policies both fuzzed.
- Both build systems run both suites (`test/CMakeLists.txt` +
  `test/Makefile.am` discipline per AGENTS.md).
- **Requirement annotations**: every new test declares the requirement
  IDs it verifies (§13.1); CI builds the traceability matrix, and a
  stage gate cannot pass with unverified MUST-level requirements in that
  stage's declared scope (§13.2).

### 12.3 Differential testing (the conversion's safety net)

While both implementations are in-tree (stages 1–5):

- CI builds libtiff twice: `-Dimplementation=legacy` (today's C) and
  `-Dimplementation=core` (compat-on-core). The full legacy test suite and
  tool invocations run against both; outputs (files produced, stdout of
  tools such as `tiffinfo`/`tiffdump`, exit codes, emitted diagnostics)
  are diffed.
- A corpus differ decodes every image in the corpus (plus a saved fuzz
  corpus) through both implementations and compares decoded pixels,
  reported tags, and error outcomes. Divergences are triaged into: compat
  bug; core bug; or legacy bug we choose not to reproduce (each of the
  last category gets a documented decision and, where sensible, a fix on
  the maintenance branch too).

### 12.4 Private-header users

22 tools/tests currently include `tiffiop.h`. Under the compat
implementation, `tiffiop.h` ceases to describe the real handle. Each such
use is audited (most need `_TIFF*` exported-privates that compat keeps;
work item 772 §2 catalogued the rest); where a genuine internal is needed
(`fax2tiff`'s codec-pipeline poking), the safe API's codec-control calls
(772's option 2C equivalent: `tiff_codec_setup_decode`,
`tiff_codec_decode_run`) replace it. This is the one sanctioned API change
inside tools during the conversion, done per-tool with its tests green on
both implementations.

## 13. Specification traceability

`~/tiff-specification` (its own repository, with its own CI and a
published Sphinx/JSON/SQLite rendering) restates the collected TIFF
specification documents as a machine-readable requirements database,
built for exactly this purpose — its README names "citation from an
implementation (libtiff source, unit tests, integration tests)" and
"driving a file validator" as design goals. Current contents (a work in
progress, but stable enough to plan against): **377 consolidated
requirements** — DEP 187 (cross-tag dependencies), TAG 92 (per-tag),
CMP 33 (per-compression-scheme), STR 19 (classic container), GEN 17
(reader/writer obligations), CLS 15 (conformance classes), BTF 14
(BigTIFF) — distilled from 1,729 extraction entries across 17 source
documents. Each requirement carries a stable ID, level
(MUST/SHOULD/MAY/INFO), actor (file/reader/writer), conformance class,
version range, container applicability, status, and full source
citations down to document section and page; a deviation registry names
the tolerated vendor deviations (§8.3). Canonical YAML generates
Markdown, `requirements.json`, and SQLite with traceability views.

### 13.1 Three-level trace

1. **Specification → design.** Core design artefacts cite requirement
   IDs. Two unifications are already in this document: §8.3's policy
   toggles *are* the database's `tolerate.*`/`repair.*` deviation flags,
   and §8.4's tag table is generated from its tag registries.
   Library-design requirements that are not spec-derived (API contracts,
   allocation limits, failure postconditions, compat behavioural
   fidelity) get their own small requirements file in the libtiff
   repository (`LIB-*` IDs, same YAML schema), so the trace matrix has
   one namespace covering both kinds.
2. **Design → implementation.** Implementing code carries structured
   annotations at the implementing site: `// [impl STR-008]`,
   `// [impl DEP-042 LIB-017]`. A requirement may be implemented at
   several sites and a site may implement several requirements.
3. **Implementation → verification.** Tests declare what they verify:
   `// [verify STR-008]` in C tests, GoogleTest
   `RecordProperty("requirements", "STR-008 …")` in C++ tests, and
   per-entry metadata for corpus/fuzz artefacts.

### 13.2 Tooling and CI

- A trace scanner (prototyped as `spike/tools/trace_scan.py`; natural
  home: `tools/` in this repository, reusing
  the database's `tools/reqdb` model code; the database side already has
  `gapscan.py`, which scans a libtiff checkout against the tag
  registries) parses annotations, validates every cited ID against the
  pinned database, and emits the traceability matrix: requirement →
  implementing sites → verifying tests.
- CI fails on: unknown or retired requirement IDs; MUST-level
  requirements inside a stage's declared scope with no implementation
  site; and implementation sites with no verifying test. SHOULD/MAY
  coverage and requirement changes after a database version bump are
  reported, not fatal.
- The published matrix gives reviewers and security auditors the claim
  "requirement X is implemented here and tested there", and gives each
  stage gate (§16) a measurable definition of done.

### 13.3 Logistics

- The database stays in its own repository: it has its own sources,
  review history and publishing pipeline. libtiff **pins a tagged
  release** and vendors the generated `requirements.json` (one committed
  file) so builds and CI are reproducible offline. Bumping the pin is an
  ordinary reviewed MR that re-runs the matrix and surfaces newly added
  or changed requirements as work items.
- Annotations cite bare IDs; the pinned version plus the published site
  resolve them. IDs are stable across database reviews (splits create
  new IDs such as STR-018/STR-019 rather than renumbering), which is
  what makes in-code citation safe.
- The database's conformance classes (CLS-*) map onto validator profiles
  and policy presets; a future validator tool driven from the
  machine-readable DEP conditions/assertions/severities is a natural
  follow-on but out of scope for the migration stages.
- Gaps run in both directions and are tracked separately: the database's
  `gaps.md` records libtiff behaviour not yet covered by requirements
  (burned down as the database matures), while the matrix records
  requirements not yet implemented or verified (consumed by stage
  gates). The database being in progress therefore does not block the
  early stages: Stage 1's scope (STR/BTF/GEN) is already complete and
  review-hardened.

## 14. Packaging, ABI, and the libtiff_compat split

Binary compatibility pins the DSO identity: existing binaries link
`libtiff.so.6` with `LIBTIFF_4.x` versioned symbols. Therefore:

- `libtiff.so.6` continues to exist and keeps its soname and all version
  nodes. At switchover it is built from the compat layer and (privately)
  links/embeds `libtiff_core`. Symbol versioning hides everything except the
  legacy surface; `tiff_*` symbols live in `libtiff_core.so.0` with their own
  `LIBTIFF_CORE_*` nodes.
- Two linking layouts are possible: (a) `libtiff.so.6` `DT_NEEDED` on
  `libtiff_core.so.0`; (b) compat statically embeds the core (one DSO, no new
  runtime dependency for existing users). **(b) is safer for distros at
  switchover; (a) is cleaner once libtiff_core is established.** Start with
  (b), move to (a) by agreement with packagers.
- "Hiving off libtiff_compat" therefore means: *source* separation
  immediately (a `compat/` top-level component with its own tests), and
  *packaging* separation later, without ever renaming the installed legacy
  DSO. New applications link `-ltiff_core` and need not have libtiff installed
  at all.
- pkg-config: `libtiff-4.pc` unchanged; new `libtiff_core.pc`. CMake config
  packages likewise (`TiffCore::tiff_core` imported target).
- Version script discipline: the compat `libtiff.map` is frozen except for
  maintenance additions; `libtiff_core.map` starts at `LIBTIFF_CORE_1.0` and is
  additive-only once declared stable.

## 15. The Rust path (design constraint now, work later)

The safe C API is designed as the permanent boundary so that the core can
be replaced beneath it:

- No C++ types, exceptions, or ownership idioms appear in `tiff_core.h`; all
  callbacks are C function pointers + `void *user_data`; all memory either
  caller-owned or handle-owned with documented lifetime — all directly
  expressible from Rust (`#[no_mangle] extern "C"`, cbindgen-compatible).
- The error-code bands, policy toggles, and tag metadata table are
  language-neutral data, reusable as the Rust crate's source of truth —
  and the requirement annotations (§13) carry over, so the Rust port
  inherits a complete, testable specification trace from day one.
- The core's Rust naming conventions and module layout (§6.1) and its
  ports-and-adapters seams (§8.1) make the port largely mechanical:
  modules, types and constants already carry their Rust names, and each
  port maps onto a trait.
- When the time comes: core modules are replaced incrementally behind the
  same ABI (geometry and directory parsing first, codecs last, mirroring
  the C++ build-out), validated by the *same* safe-API test suite, C
  integration tests, differential corpus runs, and the legacy suite via
  compat — none of which need changing.
- The eventual public C++ API (`libtiff_cxx`, the original 771 goal) is
  then written **over the safe C API** (header-mostly,
  `std::expected`-based), so it is implementation-agnostic and survives
  the core swap.
- Open question, flagged early for the RFC: platform coverage. Rust's
  tier-2/3 coverage is narrower than "any C99/C++17 compiler". Options:
  keep the C++ core maintained for exotic platforms (dual-core cost), or
  declare a platform floor at that point. This decision is deferred and
  does not block the C++ stages.

## 16. Staged migration plan

Each stage is releasable; gates are explicit. Rough effort in parentheses
(single experienced-developer months, wall-clock will differ; total ≈
24–40 pm, i.e. a multi-year programme at current staffing — co-funding or
contributors needed, which the RFC should say plainly).

**Stage 0 — Decisions and scaffolding (2–3 pm).**
RFC from this document; community sign-off on the `libtiff_<component>`
naming, §10.4 support levels, C++17 floor, GoogleTest adoption. One
style decision is already made from the spike: the core permits a single
internal `TRY` macro for error propagation (§7.1). Repo
layout (`core/`, `capi/`, `compat/` [initially empty], `test/core/`,
`test/capi/`). Toolchain: CMake + Autotools C++17 wiring, vendored
`expected`, clang-format/clang-tidy profiles for new code, CI jobs
(sanitizers on by default for new suites). Traceability (§13): pin a
first tiff-specification release, vendor its `requirements.json`, land
the annotation format and trace scanner, agree the `LIB-*`
library-requirements file. Fix `_TIFFcalloc` export on the maintenance
branch. Gate: RFC accepted; empty-but-building skeleton in CI on all
primary platforms, with the (initially empty) traceability matrix
published.

**Stage 1 — Vertical slice, read-only (4–6 pm).**
Core: I/O provider, header parse (classic+BigTIFF), IFD parse with
policies, geometry module, tag table generator + baseline tags, codecs
none/PackBits/LZW/Deflate, strip/tile read. Safe API over the slice.
Full new-suite coverage + fuzz targets. Gate: corpus subset decodes
identically to legacy (differential harness online); fuzzers run 2 weeks
without findings on strict and legacy policies; every STR/BTF MUST and
the slice's GEN/TAG/CMP MUSTs traced to implementation and verifying
tests in the matrix (§13.2).

**Stage 2 — Full read path (6–9 pm).**
All directory kinds (EXIF/GPS/custom/SubIFD), full tag system incl.
runtime registration and defaults, remaining decoders (JPEG, OJPEG
[legacy-codec build option], CCITT fax, Thunder, NeXT, SGILog, LERC/ZSTD/
WebP/JBIG per configuration), RGBA decode, client I/O. Gate: whole corpus
differential-clean on decode; policy matrix tested.

**Stage 3 — Write path (5–8 pm).**
Directory writer/builder, append/rewrite/checkpoint, strile-array
deferral, all encoders, custom directory write. Gate: round-trip
differential (legacy reads what core writes and vice versa, tag-for-tag,
plus `tiffcmp`-level pixel identity).

**Stage 4 — Compat layer (5–8 pm).**
`compat/` implements all 206 symbols per §11 tables; compat-support
interface kept to the audited minimum; varargs machinery; handler chains;
getimage move; `tiffiop.h` user remediation (§12.4). Gate: **entire
legacy test suite and all 23 tools pass, unmodified, against
compat-on-core on all CI platforms**; differential harness shows no
unexplained divergence; ABI checker (abidiff) reports identical exported
surface for `libtiff.so.6`.

**Stage 5 — Switchover (2–3 pm).**
`-Dimplementation=core` becomes the default; legacy implementation moves
to a maintenance branch (security fixes only, time-boxed by RFC). Release
(suggested: libtiff 5.0.0, soname unchanged at `.so.6`; `libtiff_core` 1.0
declared stable at the same time). Distro/packager coordination window.
Gate: one full release cycle with both implementations selectable; no
regression reports attributable to compat before flipping the default.

**Stage 6 — Separation and adoption (ongoing).**
`libtiff_compat` packaging split per §14 as packagers prefer; migration
guide (old-call → new-call table from §11 is the skeleton); tools migrate
to the safe API one at a time *after* switchover, serving as worked
examples (reversing the Stage-0–5 freeze); new-feature policy: new
functionality lands in the safe API first, compat only receives fixes.

**Stage 7 — Rust core and public C++ API (not scheduled).** Per §15,
entered by its own RFC once libtiff_core is stable and adopted.

## 17. Build systems and toolchain floor

- Both CMake and Autotools gain C++17 requirements **only when building
  the library**; consuming either C API needs only C89/C99 headers.
  Autotools: `AX_CXX_COMPILE_STDCXX(17, noext, mandatory)`; CMake:
  `target_compile_features(tiff_core PRIVATE cxx_std_17)`.
- Platforms without C++17 keep working via the maintenance branch; the
  RFC must state the support window.
- No new mandatory third-party dependencies: the `expected` and `span`
  polyfills are vendored single-header libraries (tl::expected,
  tcb::span — license review at Stage 0); GoogleTest is test-only
  (FetchContent/vendored, like other projects; never installed).
- `-Dfatal-warnings=ON -Dextra-warnings=ON` discipline extends to the new
  code with a stricter new-code-only profile (clang-tidy checks for
  naked `new`, unchecked narrowing, missing `[[nodiscard]]` — every
  `tiff::Result` return is `[[nodiscard]]`).

## 18. Risks and open questions

| # | Risk / question | Mitigation / where decided |
|---|---|---|
| 1 | Behavioural fidelity of compat (quirk long tail: EstimateStripByteCounts, old-style LZW, tolerated fax damage, error-message text) | Policy objects (§8.3) + differential harness (§12.3) + legacy suite as conformance oracle; quirks become named, tested toggles |
| 2 | Out-of-tree codecs and `tiffiop.h` users | §10.4 decision via RFC; survey downstream first (GDAL, ImageMagick, poppler, Qt, pillow, OpenCV are the de-facto compat gate) |
| 3 | Effort vs. maintainer bandwidth | Plan is explicit that this needs funding/contributors; stages are independently valuable (Stage 1–2 alone yields a fuzzable safe reader usable by security-sensitive consumers) |
| 4 | Performance regressions (checked arithmetic, layer hops) | Benchmarks in the new suite from Stage 1 (decode/encode throughput per codec); compat fast-paths where measurements demand; checked arithmetic compiles to branch-on-overflow intrinsics |
| 5 | C++17 floor excludes platforms | Maintenance branch window; survey in RFC |
| 6 | Two implementations in-tree temporarily (confusion, drift) | Build-option selection, differential CI keeps them honest, hard freeze on legacy except security fixes from Stage 4 |
| 7 | Safe API design errors locked in by stability promise | API declared unstable until Stage 5; compat is the first consumer and the best possible API review |
| 8 | Requirements database still in progress | Pinned releases (§13.3); STR/BTF/GEN families already complete and review-hardened cover the early stages; `gaps.md` burn-down proceeds in parallel; stage scopes declare which requirement families must be complete before their gate |
| 9 | mmap support in core v1 | §10.3; decide by measuring whether read-path performance needs it |
| 10 | Rust platform floor | Deferred to Stage-7 RFC (§15) |

## 19. Suggested immediate next steps

1. Circulate this document for maintainer comment; fold into an RFC under
   `doc/project/rfcs/` alongside the work items.
2. Land the independent quick fixes now (maintenance branch): `_TIFFcalloc`
   export, LogLuv export inconsistency documentation.
3. Done: the Stage-1 spike (`spike/`, findings in `spike/FINDINGS.md`)
   validated the layer boundaries, port interfaces, policy design and
   traceability annotations on real code, and the verify gate caught a
   BTF-009 conformance bug. Its fold-forward decisions are recorded in
   §7.1, §8.3 and Stage 0.
4. Pin a first tiff-specification release and vendor its
   `requirements.json` (§13); the scanner prototype
   (`spike/tools/trace_scan.py`) currently reads a live checkout. The
   spike-found registry items are resolved and merged in the database
   (its `reg-review-libtiff.md`: the registry flag index covering
   DEP-010's `repair.bitspersample-count`, and the new
   `tolerate.packbits-cross-row-runs` flag cited by CMP-32773-01).
5. Survey the major downstreams on §10.4 (custom codecs, tag methods,
   `tiffiop.h` usage) — their answers size the compat-support interface.

---

*Assisted-by: Claude Code (Claude Fable 5). Reviewed and edited by the
human submitter.*
