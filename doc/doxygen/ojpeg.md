@page ojpeg Old-style JPEG reconstruction

The decode-only old-style JPEG codec reconstructs a single dependency stream
from interchange data, strile bytes, table-offset tags and synthesized markers.
The apparent WriteStream helpers produce decoder input; they never write TIFF
output. Their returned buffers are borrowed and must be consumed synchronously.

Seeking a later strile can decode and discard intervening rows. Separate-plane
SOS positions are cached for restart. Header probing can correct sampling and
change file position; failures can retain replaced tables, allocated conversion
storage and active dependency sessions until cleanup. Eight-bit one- and
three-component input is supported; the codec rejects all encoding requests.

Dependency wrappers return Boolean normal completion and discard libjpeg header,
row-count and suspension results. A positive wrapper status does not prove full
sample output. OJPEGPostDecode retains the known whole-strile assumption that can
prematurely destroy a session when called once per scanline (issue 337). Tiny
malformed tiled sampling layouts also retain existing conversion limitations.
Use whole-strile access for supported input and check diagnostics/status.

The host dependency configuration validates the ordinary jump-wrapper variant;
external encapsulation builds must supply compatible error routing and lifetimes.

