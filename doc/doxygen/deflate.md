@page deflate Deflate codec

Implement zlib and optional whole-strile libdeflate compression.

The @ref libtiff_private_deflate group covers all callbacks, dependency allocator
hooks, state members, pseudo-tags and local macros. Zlib uses handle Ext
allocation hooks; libdeflate objects use their dependency allocation/free
functions. Predictor state prefixes the codec allocation.

Whole-strile input can select libdeflate when available, with zlib fallback for
scanlines and other ineligible requests. The backend choice is per strile;
repeating a transfer after whole-strile backend use fails. Decode accepts the
libdeflate insufficient-space status for historical oversized last strips.

Failed zlib decoding can change raw byte counts before committing the raw
pointer. Errors latch for the current strile and clear remaining output;
predecode resets the latch. Quality/backend setters store attempted values
before rejecting them. Quality changes can update active dependency state.
The allocation callback checks a size_t bound but retains unsigned-int
multiplication; its narrower product must also be representable.

Transfer failures provide no rollback of file bytes, cache or stream progress.
Cleanup releases dependency/state objects without completing a frame. Codec
installation does not propagate PredictorInit status.
