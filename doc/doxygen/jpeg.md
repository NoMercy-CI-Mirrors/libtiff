@page jpeg JPEG-in-TIFF transfers and setup

The @ref libtiff_private_jpeg group combines the @ref jpeg-support dependency
interface with codec setup, marker inspection, normal/raw transfer, pseudo-tags,
sizing and eight/twelve-bit entry points. It implements Technical Note 2
JPEG-in-TIFF; old-style TIFF JPEG has a separate module.

Marker probing can allocate a temporary window, read/seek the first strile and
correct cached YCbCr sampling. It does not restore file position and some
warned unsupported cases still return success. Codec setup validates the
selected precision, components, sampling and MCU geometry, but can leave
method, metadata, dependency or buffer changes on failure.

Raw sampled transfer operates on complete padded clump rows; scanline decoding
is rejected in that mode. RGB colour mode can select ordinary output instead.
Packed twelve-bit pairs use explicit byte/nibble layouts, including odd final
samples. Conditional ABI paths retain their existing allocation and cast
behavior; documentation generation does not prove every precision path safe
or lossless. Some normal-row temporary allocation/write failures are not
fully propagated or cleaned up. Decoder finish errors represented by a negative
dependency result can be accepted by Boolean completion expressions.

Progressive input warns once, has a scan-count guard, and checks estimated
coefficient memory against dependency settings. Environment overrides influence
those policies. Synthetic EOI recovery and warned fractional/oversized-last-strip
handling mean a positive status alone is not proof of intact input.

JPEG tables are codec-owned Ext storage; tag getters return borrowed bytes
invalidated by assignment, generation or cleanup. Destructive table copies can
fail without setter failure, and setup placeholder detection inspects eight
bytes. Supply a valid complete table stream. Pseudo-options use int arguments;
related tag setters can update sampling/size caches even when parent operations
fail. Close/cleanup and wrapper statuses retain their explicitly documented
limits, without general rollback or durability guarantees.

The twelve-bit companion reuses common code with renamed entry points. ABI
reinitialisation replaces/clears outer state and restores a disjoint snapshot
of options and table ownership. It does not itself destroy previous dependency
resources. Keep the handle, saved callback code and preserved table allocation
live throughout the transition and subsequent cleanup.

The documentation source view enables the conditional IJG 9d default-Huffman
helper even when the host dependency is another version. This documents that
compatibility path and does not change the compiled dependency or codec.
