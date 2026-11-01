#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace av {

// Little-endian binary writer for network packets and frames.
class ByteWriter {
public:
    void u8(std::uint8_t v) { buf_.push_back(v); }
    void i8(std::int8_t v) { u8(static_cast<std::uint8_t>(v)); }
    void u16(std::uint16_t v)
    {
        u8(static_cast<std::uint8_t>(v));
        u8(static_cast<std::uint8_t>(v >> 8));
    }
    void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }
    void u32(std::uint32_t v)
    {
        u16(static_cast<std::uint16_t>(v));
        u16(static_cast<std::uint16_t>(v >> 16));
    }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void f32(float v)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        u32(bits);
    }
    // Length-prefixed (u16) UTF-8 string, truncated to 4 KB.
    void str(std::string_view s)
    {
        const auto n = static_cast<std::uint16_t>(std::min<std::size_t>(s.size(), 4096));
        u16(n);
        buf_.insert(buf_.end(), s.begin(), s.begin() + n);
    }
    // Fixed-point helpers.
    void angle16(float radians) { i16(static_cast<std::int16_t>(std::lround(std::remainder(radians, 6.2831853f) / 3.1415927f * 32767.0f))); }
    void snorm8(float v, float scale) { i8(static_cast<std::int8_t>(std::lround(std::max(-1.0f, std::min(1.0f, v / scale)) * 127.0f))); }
    void unorm8(float v, float scale) { u8(static_cast<std::uint8_t>(std::lround(std::max(0.0f, std::min(1.0f, v / scale)) * 255.0f))); }

    const std::vector<std::uint8_t>& data() const { return buf_; }
    std::size_t size() const { return buf_.size(); }
    void clear() { buf_.clear(); }

private:
    std::vector<std::uint8_t> buf_;
};

// Bounds-checked reader. Reading past the end sets a sticky failure flag and
// returns zeros, so a malformed packet can be rejected after decoding.
class ByteReader {
public:
    ByteReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    explicit ByteReader(const std::vector<std::uint8_t>& v) : ByteReader(v.data(), v.size()) {}

    bool ok() const { return ok_; }
    std::size_t remaining() const { return ok_ ? size_ - pos_ : 0; }

    std::uint8_t u8()
    {
        if (!ok_ || pos_ + 1 > size_) {
            ok_ = false;
            return 0;
        }
        return data_[pos_++];
    }
    std::int8_t i8() { return static_cast<std::int8_t>(u8()); }
    std::uint16_t u16()
    {
        const std::uint16_t lo = u8();
        const std::uint16_t hi = u8();
        return static_cast<std::uint16_t>(lo | (hi << 8));
    }
    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    std::uint32_t u32()
    {
        const std::uint32_t lo = u16();
        const std::uint32_t hi = u16();
        return lo | (hi << 16);
    }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    float f32()
    {
        const std::uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, sizeof v);
        return std::isfinite(v) ? v : 0.0f;
    }
    std::string str()
    {
        const std::uint16_t n = u16();
        if (!ok_ || pos_ + n > size_) {
            ok_ = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return s;
    }
    float angle16() { return static_cast<float>(i16()) / 32767.0f * 3.1415927f; }
    float snorm8(float scale) { return static_cast<float>(i8()) / 127.0f * scale; }
    float unorm8(float scale) { return static_cast<float>(u8()) / 255.0f * scale; }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

} // namespace av
