// tiff_core spike: domain — TIFF header decode. Pure: bytes in, value
// object out; no I/O.
#pragma once

#include "core/result.hpp"
#include "core/span.hpp"
#include "core/types.hpp"

#include <cstdint>

namespace tiff::core {

inline constexpr std::size_t CLASSIC_HEADER_SIZE = 8;
inline constexpr std::size_t BIG_HEADER_SIZE = 16;

struct HeaderInfo {
    ByteOrder order;
    Container container;
    std::uint64_t first_ifd_offset;
};

// Parses the file header from a prefix of the file (at least
// CLASSIC_HEADER_SIZE bytes for classic, BIG_HEADER_SIZE for BigTIFF).
Result<HeaderInfo> parse_header(Span<const Byte> prefix,
                                const ReadPolicy &policy);

} // namespace tiff::core
