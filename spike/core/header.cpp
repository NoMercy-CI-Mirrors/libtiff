#include "core/header.hpp"

#include "core/bytes.hpp"

namespace tiff::core {

namespace {
constexpr const char *MODULE = "header";
}

Result<HeaderInfo> parse_header(Span<const Byte> prefix,
                                const ReadPolicy &policy) {
    if (prefix.size() < CLASSIC_HEADER_SIZE)
        return Error{ErrorCode::TRUNCATED, MODULE, "file shorter than header"};

    // Byte order marker: "II" little-endian, "MM" big-endian.
    // [impl STR-001 GEN-001]
    ByteOrder order;
    if (prefix[0] == 'I' && prefix[1] == 'I')
        order = ByteOrder::LittleEndian;
    else if (prefix[0] == 'M' && prefix[1] == 'M')
        order = ByteOrder::BigEndian;
    else
        return Error{ErrorCode::BAD_BYTE_ORDER, MODULE,
                     "byte order marker is neither 'II' nor 'MM'"};

    const std::uint16_t version = get_u16(prefix, 2, order);

    if (version == 42) { // [impl STR-002]
        const std::uint64_t first_ifd = get_u32(prefix, 4, order);
        // First IFD offset: even, >= 8. [impl STR-003]
        if (first_ifd < CLASSIC_HEADER_SIZE)
            return Error{ErrorCode::BAD_HEADER, MODULE,
                         "first IFD offset points into the header"};
        if ((first_ifd & 1u) != 0 && !policy.tolerate_unaligned_values)
            return Error{ErrorCode::BAD_HEADER, MODULE,
                         "first IFD offset is odd"};
        return HeaderInfo{order, Container::Classic, first_ifd};
    }

    if (version == 43) { // [impl BTF-001]
        if (prefix.size() < BIG_HEADER_SIZE)
            return Error{ErrorCode::TRUNCATED, MODULE,
                         "file shorter than BigTIFF header"};
        // Offset size must be 8; anything else is an incompatible future
        // revision and the file must be refused. [impl BTF-002]
        if (get_u16(prefix, 4, order) != 8)
            return Error{ErrorCode::BAD_OFFSET_SIZE, MODULE,
                         "BigTIFF offset size is not 8"};
        // Reserved bytes must be 0. [impl BTF-003]
        if (get_u16(prefix, 6, order) != 0)
            return Error{ErrorCode::BAD_RESERVED, MODULE,
                         "BigTIFF reserved header bytes are not zero"};
        // First IFD offset >= 16 (the header occupies bytes 0-15).
        // [impl BTF-004]
        const std::uint64_t first_ifd = get_u64(prefix, 8, order);
        if (first_ifd < BIG_HEADER_SIZE)
            return Error{ErrorCode::BAD_HEADER, MODULE,
                         "first IFD offset points into the header"};
        return HeaderInfo{order, Container::Big, first_ifd};
    }

    // [impl STR-002]
    return Error{ErrorCode::BAD_MAGIC, MODULE,
                 "version number is neither 42 nor 43"};
}

} // namespace tiff::core
