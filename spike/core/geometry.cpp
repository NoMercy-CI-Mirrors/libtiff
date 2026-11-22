#include "core/geometry.hpp"

#include <limits>

namespace tiff::core {

namespace {
constexpr const char *MODULE = "geometry";
}

Result<std::uint64_t> checked_mul(std::uint64_t a, std::uint64_t b) {
    if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a)
        return Error{ErrorCode::TAG_BAD_VALUE, MODULE,
                     "arithmetic overflow in size computation"};
    return a * b;
}

Result<std::uint64_t> checked_add(std::uint64_t a, std::uint64_t b) {
    if (b > std::numeric_limits<std::uint64_t>::max() - a)
        return Error{ErrorCode::TAG_BAD_VALUE, MODULE,
                     "arithmetic overflow in size computation"};
    return a + b;
}

Result<std::uint64_t> scanline_size_bytes(std::uint32_t width,
                                          std::uint16_t samples_per_pixel,
                                          std::uint16_t bits_per_sample) {
    auto bits = checked_mul(width, samples_per_pixel);
    if (!bits)
        return bits.take_error();
    bits = checked_mul(bits.value(), bits_per_sample);
    if (!bits)
        return bits.take_error();
    auto rounded = checked_add(bits.value(), 7);
    if (!rounded)
        return rounded.take_error();
    return rounded.value() / 8;
}

Result<std::uint32_t> strip_count(std::uint32_t image_length,
                                  std::uint32_t rows_per_strip) {
    if (image_length == 0 || rows_per_strip == 0)
        return Error{ErrorCode::TAG_BAD_VALUE, MODULE,
                     "image length and rows-per-strip must be nonzero"};
    if (rows_per_strip >= image_length)
        return std::uint32_t{1};
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(
                                           image_length) +
                                       rows_per_strip - 1) /
                                      rows_per_strip);
}

Result<std::uint32_t> rows_in_strip(std::uint32_t image_length,
                                    std::uint32_t rows_per_strip,
                                    std::uint32_t strip_index) {
    auto strips = strip_count(image_length, rows_per_strip);
    if (!strips)
        return strips.take_error();
    if (strip_index >= strips.value())
        return Error{ErrorCode::BAD_PARAMETER, MODULE,
                     "strip index out of range"};
    if (rows_per_strip >= image_length)
        return image_length;
    const std::uint64_t first_row =
        static_cast<std::uint64_t>(strip_index) * rows_per_strip;
    const std::uint64_t remaining = image_length - first_row;
    return static_cast<std::uint32_t>(
        remaining < rows_per_strip ? remaining : rows_per_strip);
}

Result<std::uint64_t> strip_decoded_size(std::uint32_t width,
                                         std::uint32_t image_length,
                                         std::uint16_t samples_per_pixel,
                                         std::uint16_t bits_per_sample,
                                         std::uint32_t rows_per_strip,
                                         std::uint32_t strip_index) {
    auto scanline =
        scanline_size_bytes(width, samples_per_pixel, bits_per_sample);
    if (!scanline)
        return scanline.take_error();
    auto rows = rows_in_strip(image_length, rows_per_strip, strip_index);
    if (!rows)
        return rows.take_error();
    return checked_mul(scanline.value(), rows.value());
}

} // namespace tiff::core
