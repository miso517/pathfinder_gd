#pragma once

// Little-endian byte buffer helpers. The .slc format is defined as little
// endian, so we write the bytes explicitly instead of relying on host layout.

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace pathfinder {

class ByteWriter {
public:
    void u8(uint8_t v) { m_buf.push_back(v); }

    void u16(uint16_t v) {
        m_buf.push_back(static_cast<uint8_t>(v & 0xFF));
        m_buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }

    void u32(uint32_t v) {
        for (int i = 0; i < 4; i++) m_buf.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }

    void u64(uint64_t v) {
        for (int i = 0; i < 8; i++) m_buf.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }

    void f64(double v) {
        uint64_t bits = 0;
        static_assert(sizeof(double) == sizeof(uint64_t), "double must be 64 bit");
        std::memcpy(&bits, &v, sizeof(bits));
        u64(bits);
    }

    /// Writes the low `bytes` of `v`. `bytes` must be 1, 2, 4 or 8.
    void uintBytes(uint64_t v, uint32_t bytes) {
        for (uint32_t i = 0; i < bytes; i++) m_buf.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }

    void raw(void const* data, size_t size) {
        auto const* p = static_cast<uint8_t const*>(data);
        m_buf.insert(m_buf.end(), p, p + size);
    }

    void pad(size_t count) { m_buf.insert(m_buf.end(), count, 0); }

    size_t size() const { return m_buf.size(); }
    void* data() { return m_buf.data(); }
    std::vector<uint8_t>&& take() { return std::move(m_buf); }
    std::vector<uint8_t> const& bytes() const { return m_buf; }

private:
    std::vector<uint8_t> m_buf;
};

class ByteReader {
public:
    ByteReader(uint8_t const* data, size_t size) : m_data(data), m_size(size) {}
    explicit ByteReader(std::vector<uint8_t> const& buf) : m_data(buf.data()), m_size(buf.size()) {}

    bool eof() const { return m_pos >= m_size; }
    size_t remaining() const { return m_size - m_pos; }
    size_t tell() const { return m_pos; }

    bool seek(size_t pos) {
        if (pos > m_size) return false;
        m_pos = pos;
        return true;
    }

    std::optional<uint8_t> u8() {
        if (remaining() < 1) return std::nullopt;
        return m_data[m_pos++];
    }

    std::optional<uint16_t> u16() {
        if (remaining() < 2) return std::nullopt;
        uint16_t v = static_cast<uint16_t>(m_data[m_pos]) | static_cast<uint16_t>(m_data[m_pos + 1] << 8);
        m_pos += 2;
        return v;
    }

    std::optional<uint32_t> u32() {
        if (remaining() < 4) return std::nullopt;
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v |= static_cast<uint32_t>(m_data[m_pos + i]) << (8 * i);
        m_pos += 4;
        return v;
    }

    std::optional<uint64_t> u64() {
        if (remaining() < 8) return std::nullopt;
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) v |= static_cast<uint64_t>(m_data[m_pos + i]) << (8 * i);
        m_pos += 8;
        return v;
    }

    std::optional<double> f64() {
        auto bits = u64();
        if (!bits) return std::nullopt;
        double v = 0.0;
        std::memcpy(&v, &*bits, sizeof(v));
        return v;
    }

    /// Reads the low `bytes` (1, 2, 4 or 8) into a uint64.
    std::optional<uint64_t> uintBytes(uint32_t bytes) {
        if (bytes == 0) return 0ull;
        if (remaining() < bytes) return std::nullopt;
        uint64_t v = 0;
        for (uint32_t i = 0; i < bytes; i++) v |= static_cast<uint64_t>(m_data[m_pos + i]) << (8 * i);
        m_pos += bytes;
        return v;
    }

    bool skip(size_t count) {
        if (remaining() < count) return false;
        m_pos += count;
        return true;
    }

private:
    uint8_t const* m_data;
    size_t m_size;
    size_t m_pos = 0;
};

} // namespace pathfinder
