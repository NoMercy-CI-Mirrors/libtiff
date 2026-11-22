// tiff_core spike: driven ports (plan §8.1) and simple adapters.
// Ports are owned by the domain; adapters implement them. Each port has
// a production adapter and a test double.
#pragma once

#include "core/result.hpp"
#include "core/span.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace tiff::core {

// Storage port.
class IoProvider {
  public:
    virtual ~IoProvider() = default;
    virtual Result<void> read_at(std::uint64_t offset, Span<Byte> out) = 0;
    virtual Result<std::uint64_t> size() = 0;
};

// Diagnostics port: structured warnings (errors travel in Result).
class DiagnosticSink {
  public:
    virtual ~DiagnosticSink() = default;
    virtual void warning(const Error &warning_info) = 0;
};

// Codec port: decode one strip; dst is sized to the expected decoded
// length; returns bytes produced.
class Codec {
  public:
    virtual ~Codec() = default;
    virtual Result<std::size_t> decode_strip(Span<const Byte> src,
                                             Span<Byte> dst) = 0;
};

// Returns nullptr when the scheme is unsupported. [impl TAG-259-01]
std::unique_ptr<Codec> make_codec(std::uint16_t compression_scheme);

// ---- adapters -----------------------------------------------------------

// Production/test adapter: whole file in memory.
class MemoryIo final : public IoProvider {
  public:
    explicit MemoryIo(std::vector<Byte> data) : data_(std::move(data)) {}

    Result<void> read_at(std::uint64_t offset, Span<Byte> out) override {
        if (offset > data_.size() || data_.size() - offset < out.size()) {
            return Error{ErrorCode::IO_EOF, "MemoryIo",
                         "read beyond end of data"};
        }
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = data_[static_cast<std::size_t>(offset) + i];
        return {};
    }

    Result<std::uint64_t> size() override { return std::uint64_t{data_.size()}; }

  private:
    std::vector<Byte> data_;
};

// Production adapter: stdio file.
class FileIo final : public IoProvider {
  public:
    static Result<std::unique_ptr<IoProvider>> open(const char *path) {
        std::FILE *f = std::fopen(path, "rb");
        if (f == nullptr) {
            return Error{ErrorCode::IO_OPEN, "FileIo",
                         std::string("cannot open '") + path + "'"};
        }
        return std::unique_ptr<IoProvider>(new FileIo(f));
    }

    ~FileIo() override {
        if (f_ != nullptr)
            std::fclose(f_);
    }

    Result<void> read_at(std::uint64_t offset, Span<Byte> out) override {
        if (std::fseek(f_, static_cast<long>(offset), SEEK_SET) != 0)
            return Error{ErrorCode::IO_READ, "FileIo", "seek failed"};
        if (std::fread(out.data(), 1, out.size(), f_) != out.size())
            return Error{ErrorCode::IO_EOF, "FileIo", "short read"};
        return {};
    }

    Result<std::uint64_t> size() override {
        if (std::fseek(f_, 0, SEEK_END) != 0)
            return Error{ErrorCode::IO_READ, "FileIo", "seek failed"};
        const long n = std::ftell(f_);
        if (n < 0)
            return Error{ErrorCode::IO_READ, "FileIo", "tell failed"};
        return static_cast<std::uint64_t>(n);
    }

  private:
    explicit FileIo(std::FILE *f) : f_(f) {}
    std::FILE *f_;
};

// Test double: collects warnings for assertions.
class CollectingSink final : public DiagnosticSink {
  public:
    void warning(const Error &warning_info) override {
        warnings.push_back(warning_info);
    }
    std::vector<Error> warnings;
};

} // namespace tiff::core
