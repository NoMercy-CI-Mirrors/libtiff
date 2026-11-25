@page jbig JBIG plane codec

Transfer complete JBIG image planes through strip callbacks.

The @ref libtiff_private_jbig group covers single-strip setup, dependency
encoding/decoding, output callbacks and installation. No persistent outer
codec state is allocated. Temporary dependency objects are freed with the
matching dependency cleanup functions.

Encoding requires a complete single-sample one-bit plane. The void dependency
output callback ignores its buffered-copy status, so outer encode success
does not prove that every payload write succeeded. Decoder short images warn
and zero-pad; oversized decoded planes fail. Raw input can be bit-reversed
in place, and failure does not restore those bytes.

Installation disables mapped raw access and detaches a previous mapped raw
view so later bit reversal can use writable storage. Only strip transfers are
installed; row and tile access retain fallback methods. The reference includes
these optional source contracts even when JBIG is absent from the built library.
