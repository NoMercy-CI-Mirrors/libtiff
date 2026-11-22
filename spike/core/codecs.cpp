// tiff_core spike: codec adapters for the Codec port.
#include "core/ports.hpp"

namespace tiff::core {

namespace {

constexpr const char *MODULE = "codec";

// Compression 1: data packed as tightly as possible, no compression; the
// coded strip is exactly the decoded strip. [impl CMP-1-01]
class NoneCodec final : public Codec {
  public:
    Result<std::size_t> decode_strip(Span<const Byte> src,
                                     Span<Byte> dst) override {
        if (src.size() != dst.size())
            return Error{ErrorCode::CODEC_DECODE, MODULE,
                         "uncompressed strip size does not match the "
                         "expected decoded size"};
        for (std::size_t i = 0; i < src.size(); ++i)
            dst[i] = src[i];
        return dst.size();
    }
};

// Compression 32773: PackBits byte-oriented run-length coding.
// Control byte n in [0,127]: copy n+1 literal bytes; n in [-127,-1]:
// repeat the next byte 1-n times; -128: no-op. [impl CMP-32773-01]
class PackBitsCodec final : public Codec {
  public:
    Result<std::size_t> decode_strip(Span<const Byte> src,
                                     Span<Byte> dst) override {
        std::size_t in = 0, out = 0;
        while (out < dst.size()) {
            if (in >= src.size())
                return Error{ErrorCode::CODEC_DECODE, MODULE,
                             "PackBits data exhausted before strip was "
                             "fully decoded"};
            const signed char n = static_cast<signed char>(src[in++]);
            if (n >= 0) {
                const std::size_t run = static_cast<std::size_t>(n) + 1;
                if (in + run > src.size() || out + run > dst.size())
                    return Error{ErrorCode::CODEC_DECODE, MODULE,
                                 "PackBits literal run overflows input or "
                                 "output"};
                for (std::size_t k = 0; k < run; ++k)
                    dst[out + k] = src[in + k];
                in += run;
                out += run;
            } else if (n == -128) {
                // no-op
            } else {
                const std::size_t run = static_cast<std::size_t>(1 - n);
                if (in >= src.size() || out + run > dst.size())
                    return Error{ErrorCode::CODEC_DECODE, MODULE,
                                 "PackBits replicate run overflows input or "
                                 "output"};
                const Byte v = src[in++];
                for (std::size_t k = 0; k < run; ++k)
                    dst[out + k] = v;
                out += run;
            }
        }
        // Trailing bytes after the expected decode length indicate a
        // malformed strip (strict reading; the compat layer may need a
        // tolerance here).
        if (in != src.size())
            return Error{ErrorCode::CODEC_DECODE, MODULE,
                         "trailing bytes after PackBits strip"};
        return out;
    }
};

} // namespace

// [impl TAG-259-01]
std::unique_ptr<Codec> make_codec(std::uint16_t compression_scheme) {
    switch (compression_scheme) {
    case compression::NONE:
        return std::unique_ptr<Codec>(new NoneCodec);
    case compression::PACKBITS:
        return std::unique_ptr<Codec>(new PackBitsCodec);
    default:
        return nullptr;
    }
}

} // namespace tiff::core
