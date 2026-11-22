# Spike findings

Status: complete. The spike validates the layered design of
`libtiff-layered-api-migration.md` at Stage-1-slice scale. ~1,100 lines
of core + C API, ~750 lines of tests; built with `-Wall -Wextra`, clean;
all tests pass; the traceability matrix covers 38 requirements, each
implemented and verified.

## What the spike demonstrated

1. **Layer boundary holds.** The safe C API is the only way in; the
   pure-C99 test (`tests/test_capi.c`, compiled as C99 with no C++ in
   the TU) opens, inspects and decodes both synthesised PackBits files
   and the real corpus file `test/images/minisblack-1c-8b.tiff`
   (big-endian, 157×151, three strips, out-of-line LONG arrays) under
   the strict policy. The header set is C-clean.
2. **Ports work as designed.** `IoProvider` has three adapters
   (memory, stdio file, fault-injecting test double); injected I/O
   faults surface as clean status codes, not crashes. The domain
   modules (header, IFD, geometry, codecs) are tested with no mocks at
   all — bytes in, values out — which bears out the hexagonal split.
3. **Strictness policies express legacy quirks cleanly.** Two deviation
   toggles (`tolerate.unsorted-ifd`, `tolerate.unaligned-values`) were
   enough to give strict-refuses / legacy-warns-and-succeeds behaviour
   for unsorted IFDs and odd offsets, each a single named branch. The
   policy design scales to the rest of the registry.
4. **Traceability pays for itself immediately.** On its first run the
   scanner reported four implemented-but-unverified MUSTs and one stale
   annotation. Writing the missing BTF-009 test exposed a real bug: the
   reader accepted LONG8 strip offsets in classic files, which BTF-009
   forbids. The implemented-without-verified CI gate (plan §13.2) is
   worth enforcing from day one.
5. **Rust-style naming** (snake_case modules, `UpperCamelCase` types,
   `Result<T>`/`Error`) caused no friction and reads naturally.

## Friction found (feed into Stage 0/1)

- **C++17 gaps confirmed**: no `std::span`, no `std::expected`. The
  spike wrote minimal stand-ins; the real core should vendor
  maintained single-header implementations (tcb::span, tl::expected)
  rather than growing its own.
- **`Result` propagation is verbose** without a try-operator: every
  fallible call is three lines (`auto r = f(); if (!r) return
  r.take_error(); use(r.value());`). The real core should permit one
  internal `TRY`-style macro, confined to a single header, as a
  pragmatic exception to the no-macros rule — or accept the verbosity.
  This needs a Stage-0 style decision.
- **Error objects allocate** (`std::string` module/message). Fine for
  the spike; the real core needs a no-allocation error path (static
  module names, fixed-size or lazily formatted messages) so OOM and
  fuzzing paths cannot fail while reporting failure.
- **Open-failure reporting**: the spike's `errmsg` buffer on
  `tiff_open_read` confirms the need for the plan's proper
  `tiff_error_t` out-object (§7.2); a bare status code loses too much.
- **PackBits row-boundary rule**: CMP-32773-01 says runs must not cross
  row boundaries; the spike decodes strip-wise and would accept a
  crossing run. Decide in Stage 1 whether the core decodes per-row
  (strict) with a tolerance flag, mirroring what legacy libtiff
  accepts.
- **Database gap found by implementation**: TAG-258-01's statement
  references a `repair.bitspersample-count` deviation that is not yet
  in the deviation registry (10 flags today). The spike warns on count
  mismatch; the flag should be added to the database — the two-way gap
  flow described in plan §13.3 in action.

## Deliberately out of scope

Write path, directory chains beyond the first IFD, rationals and
ASCII values, codecs beyond none/PackBits, the Allocator port (plain
`std::vector` used), memory mapping, tiles, planar data, threading.
Nothing encountered suggests these change the architectural verdict;
they are Stage-1/2/3 work.

## Verdict

Proceed. The spike supports the plan's feasibility claim: the layer
boundaries are buildable and testable exactly as drawn, the policy
mechanism reproduces legacy behaviour without contaminating the core,
and requirement traceability is cheap to maintain and catches real
defects.
