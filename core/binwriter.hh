#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace splash {

// Little-endian byte buffer with backpatching.
class BinWriter {
  public:
    size_t pos() const { return buf_.size(); }
    void u8(uint8_t v) { buf_.push_back(v); }
    void u16(uint16_t v) {
        u8(uint8_t(v));
        u8(uint8_t(v >> 8));
    }
    void u32(uint32_t v) {
        u16(uint16_t(v));
        u16(uint16_t(v >> 16));
    }
    void i16(int16_t v) { u16(uint16_t(v)); }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void bytes(const void* p, size_t n) {
        auto* b = static_cast<const uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }
    void bytes(const std::string& s) { bytes(s.data(), s.size()); }
    void align4() {
        while (buf_.size() % 4) buf_.push_back(0);
    }
    void patchU32(size_t at, uint32_t v) {
        for (int i = 0; i < 4; i++) buf_.at(at + size_t(i)) = uint8_t(v >> (8 * i));
    }
    const std::vector<uint8_t>& data() const { return buf_; }
    void save(const std::filesystem::path& file) const {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + file.string());
        out.write(reinterpret_cast<const char*>(buf_.data()), std::streamsize(buf_.size()));
    }

  private:
    std::vector<uint8_t> buf_;
};

}  // namespace splash
