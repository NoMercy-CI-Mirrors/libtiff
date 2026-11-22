// tiff_core spike: byte-order-aware integer extraction from spans.
// Callers validate bounds once; these accessors are unchecked.
#pragma once

#include "core/span.hpp"
#include "core/types.hpp"

#include <cstdint>

namespace tiff::core {

inline std::uint16_t get_u16(Span<const Byte> b, std::size_t off,
                             ByteOrder order) {
    const std::uint16_t b0 = b[off], b1 = b[off + 1];
    return order == ByteOrder::LittleEndian
               ? static_cast<std::uint16_t>(b0 | (b1 << 8))
               : static_cast<std::uint16_t>((b0 << 8) | b1);
}

inline std::uint32_t get_u32(Span<const Byte> b, std::size_t off,
                             ByteOrder order) {
    if (order == ByteOrder::LittleEndian) {
        return static_cast<std::uint32_t>(b[off]) |
               (static_cast<std::uint32_t>(b[off + 1]) << 8) |
               (static_cast<std::uint32_t>(b[off + 2]) << 16) |
               (static_cast<std::uint32_t>(b[off + 3]) << 24);
    }
    return (static_cast<std::uint32_t>(b[off]) << 24) |
           (static_cast<std::uint32_t>(b[off + 1]) << 16) |
           (static_cast<std::uint32_t>(b[off + 2]) << 8) |
           static_cast<std::uint32_t>(b[off + 3]);
}

inline std::uint64_t get_u64(Span<const Byte> b, std::size_t off,
                             ByteOrder order) {
    if (order == ByteOrder::LittleEndian) {
        return static_cast<std::uint64_t>(get_u32(b, off, order)) |
               (static_cast<std::uint64_t>(get_u32(b, off + 4, order)) << 32);
    }
    return (static_cast<std::uint64_t>(get_u32(b, off, order)) << 32) |
           static_cast<std::uint64_t>(get_u32(b, off + 4, order));
}

} // namespace tiff::core
