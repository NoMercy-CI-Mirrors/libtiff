// tiff_core spike: fundamental types, tag constants, ReadPolicy.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tiff::core {

// Production code would use std::byte; the spike uses unsigned char to
// keep casts out of the way.
using Byte = unsigned char;

enum class ByteOrder { LittleEndian, BigEndian };
enum class Container { Classic, Big };

// TIFF field types. [impl STR-010 STR-011 BTF-009]
enum class FieldType : std::uint16_t {
    Byte = 1,
    Ascii = 2,
    Short = 3,
    Long = 4,
    Rational = 5,
    SByte = 6,
    Undefined = 7,
    SShort = 8,
    SLong = 9,
    SRational = 10,
    Float = 11,
    Double = 12,
    Ifd = 13,
    Long8 = 16,
    SLong8 = 17,
    Ifd8 = 18,
};

// Size in bytes of one value of the given raw field type, or 0 when the
// type code is unknown (the caller skips the field). [impl GEN-017]
std::size_t field_type_size(std::uint16_t type);

using TagId = std::uint16_t;

namespace tags {
inline constexpr TagId IMAGE_WIDTH = 256;
inline constexpr TagId IMAGE_LENGTH = 257;
inline constexpr TagId BITS_PER_SAMPLE = 258;
inline constexpr TagId COMPRESSION = 259;
inline constexpr TagId PHOTOMETRIC_INTERPRETATION = 262;
inline constexpr TagId STRIP_OFFSETS = 273;
inline constexpr TagId SAMPLES_PER_PIXEL = 277;
inline constexpr TagId ROWS_PER_STRIP = 278;
inline constexpr TagId STRIP_BYTE_COUNTS = 279;
} // namespace tags

namespace compression {
inline constexpr std::uint16_t NONE = 1;
inline constexpr std::uint16_t PACKBITS = 32773;
} // namespace compression

// Strictness policy (plan §8.3). Toggle names follow the requirements
// database's deviation registry; strict() is the new-API default,
// legacy() reproduces current libtiff tolerances.
struct ReadPolicy {
    // tolerate.unsorted-ifd [impl STR-009]
    bool tolerate_unsorted_ifd = false;
    // tolerate.unaligned-values (odd offsets; libtiff writes 2-byte
    // alignment and tolerates odd) [impl STR-003 STR-018]
    bool tolerate_unaligned_values = false;
    std::uint64_t max_single_alloc = 64ull << 20;

    static ReadPolicy strict() { return ReadPolicy{}; }
    static ReadPolicy legacy() {
        ReadPolicy p;
        p.tolerate_unsorted_ifd = true;
        p.tolerate_unaligned_values = true;
        return p;
    }
};

} // namespace tiff::core
