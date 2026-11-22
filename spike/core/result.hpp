// tiff_core spike: Result<T> / Error.
// Stands in for expected<T, Error>; the real core aliases a vendored
// single-header expected under C++17 and std::expected under C++23
// (plan §7.1). Error codes use the banded taxonomy shared with the safe
// C API.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace tiff::core {

enum class ErrorCode : int {
    // memory: 1-99
    OUT_OF_MEMORY = 1,
    ALLOC_LIMIT_EXCEEDED = 2,
    // I/O: 100-199
    IO_OPEN = 100,
    IO_READ = 101,
    IO_EOF = 102,
    // format: 200-299
    BAD_BYTE_ORDER = 200,
    BAD_MAGIC = 201,
    BAD_HEADER = 202,
    BAD_IFD = 203,
    BAD_OFFSET = 204,
    TRUNCATED = 205,
    UNSORTED_IFD = 206,
    BAD_OFFSET_SIZE = 207,
    BAD_RESERVED = 208,
    // tag: 300-399
    TAG_MISSING = 300,
    TAG_BAD_TYPE = 301,
    TAG_BAD_COUNT = 302,
    TAG_BAD_VALUE = 303,
    // codec: 400-499
    CODEC_UNSUPPORTED = 400,
    CODEC_DECODE = 401,
    // state: 500-599
    BAD_STATE = 500,
    // parameter: 600-699
    BAD_PARAMETER = 600,
    BUFFER_TOO_SMALL = 601,
};

struct Error {
    ErrorCode code;
    std::string module;
    std::string message;
};

template <typename T> class [[nodiscard]] Result {
  public:
    Result(T value) : v_(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : v_(std::in_place_index<1>, std::move(error)) {}

    bool has_value() const { return v_.index() == 0; }
    explicit operator bool() const { return has_value(); }

    T &value() & { return std::get<0>(v_); }
    const T &value() const & { return std::get<0>(v_); }
    T &&take_value() { return std::get<0>(std::move(v_)); }

    const Error &error() const { return std::get<1>(v_); }
    Error take_error() { return std::get<1>(std::move(v_)); }

  private:
    std::variant<T, Error> v_;
};

template <> class [[nodiscard]] Result<void> {
  public:
    Result() = default;
    Result(Error error) : e_(std::move(error)) {}

    bool has_value() const { return !e_.has_value(); }
    explicit operator bool() const { return has_value(); }

    const Error &error() const { return *e_; }
    Error take_error() { return std::move(*e_); }

  private:
    std::optional<Error> e_;
};

} // namespace tiff::core
