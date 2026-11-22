// tiff_core spike: minimal Span, standing in for std::span (C++20).
// In the real core this is a vendored span polyfill selected at build time;
// the minimum language level is C++17 (plan §7.1, §17).
#pragma once

#include <cstddef>

namespace tiff::core {

template <typename T> class Span {
  public:
    Span() : data_(nullptr), size_(0) {}
    Span(T *data, std::size_t size) : data_(data), size_(size) {}
    template <typename Container>
    Span(Container &c) : data_(c.data()), size_(c.size()) {}

    T *data() const { return data_; }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    T &operator[](std::size_t i) const { return data_[i]; }
    Span subspan(std::size_t offset, std::size_t count) const {
        return Span(data_ + offset, count);
    }
    Span first(std::size_t count) const { return Span(data_, count); }

  private:
    T *data_;
    std::size_t size_;
};

} // namespace tiff::core
