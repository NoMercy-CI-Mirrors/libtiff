// tiff_core spike: application layer — File use-case orchestrating the
// domain modules and the driven ports (plan §8.1). Ports arrive by
// constructor injection; there is no global state.
#pragma once

#include "core/model.hpp"
#include "core/ports.hpp"
#include "core/result.hpp"
#include "core/span.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <memory>

namespace tiff::core {

class File {
  public:
    static Result<std::unique_ptr<File>>
    open_read(std::unique_ptr<IoProvider> io, ReadPolicy policy,
              DiagnosticSink *sink = nullptr);

    const Directory &directory() const { return dir_; }
    ByteOrder byte_order() const { return order_; }
    Container container() const { return container_; }

    std::uint32_t strip_count() const {
        return static_cast<std::uint32_t>(dir_.strip_offsets.size());
    }
    Result<std::uint64_t> strip_size(std::uint32_t strip_index) const;
    Result<std::size_t> read_strip(std::uint32_t strip_index, Span<Byte> out);

  private:
    File(std::unique_ptr<IoProvider> io, ReadPolicy policy,
         DiagnosticSink *sink)
        : io_(std::move(io)), policy_(policy), sink_(sink) {
        (void)sink_; // kept for write-path warnings in a fuller slice
    }

    std::unique_ptr<IoProvider> io_;
    ReadPolicy policy_;
    DiagnosticSink *sink_;
    ByteOrder order_ = ByteOrder::LittleEndian;
    Container container_ = Container::Classic;
    std::uint64_t file_size_ = 0;
    Directory dir_;
};

} // namespace tiff::core
