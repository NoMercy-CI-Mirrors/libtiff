# libtiff_core Stage-1 spike (THROWAWAY)

This directory is the feasibility spike called for by
`doc/plan/libtiff-layered-api-migration.rst` §19 item 3. It is **throwaway
prototype code**: not shipped, not part of either build system, not held
to production standards, and deliberately minimal. Its only purpose is to
validate, on real code:

1. the layer boundaries (C++17 core → safe C API, with the C API as the
   only way in);
2. the hexagonal port interfaces (`IoProvider`, `Codec`,
   `DiagnosticSink`) and their test doubles;
3. the `ReadPolicy` strict/legacy design for legacy quirks;
4. `Result<T>`-based error propagation under C++17 (vendored minimal
   `expected`/`span` stand-ins);
5. requirement traceability: `[impl ID]` / `[verify ID]` annotations
   checked against the tiff-specification requirements database by
   `tools/trace_scan.py`.

Scope: classic + BigTIFF header, first-IFD parse, baseline grayscale tag
validation with defaults, strip geometry with checked arithmetic, codecs
"none" and PackBits, strip reading through the safe C API, from both
synthesised files and `test/images/minisblack-1c-8b.tiff`.

Build and test:

    cmake -S spike -B spike-build
    cmake --build spike-build
    ctest --test-dir spike-build --output-on-failure

Traceability matrix:

    python3 spike/tools/trace_scan.py --root spike \
        --db ~/tiff-specification/requirements/requirements.json

Findings are recorded in `FINDINGS.md`.
