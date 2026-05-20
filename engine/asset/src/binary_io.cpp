#include <irreden/asset/binary_io.hpp>
#include <irreden/ir_profile.hpp>

#include <bit>
#include <cstring>
#include <utility>

namespace IRAsset {

namespace detail {

constexpr bool kHostIsLittleEndian = []() {
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
    return __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;
#else
    return true; // x86/arm — every platform the engine supports is LE
#endif
}();

// Write a fixed-width integer in little-endian, host-byte-order-agnostic.
template <typename T> inline void writeLE(BinaryWriter &w, T v) {
    static_assert(std::is_integral_v<T>);
    std::uint8_t bytes[sizeof(T)];
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bytes[i] = static_cast<std::uint8_t>((static_cast<std::uint64_t>(v) >> (i * 8)) & 0xFF);
    }
    w.writeBytes(bytes, sizeof(T));
}

template <typename T> inline Result<T> readLE(BinaryReader &r) {
    static_assert(std::is_integral_v<T>);
    std::uint8_t bytes[sizeof(T)];
    if (auto st = r.readBytes(bytes, sizeof(T)); !st.ok()) {
        return Result<T>::error(st.code_, std::move(st.message_));
    }
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        v |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
    }
    return Result<T>::success(static_cast<T>(v));
}

} // namespace detail

// ---- BinaryWriter primitives -------------------------------------------

void BinaryWriter::writeU8(std::uint8_t v) {
    writeBytes(&v, 1);
}
void BinaryWriter::writeU16(std::uint16_t v) {
    detail::writeLE<std::uint16_t>(*this, v);
}
void BinaryWriter::writeU32(std::uint32_t v) {
    detail::writeLE<std::uint32_t>(*this, v);
}
void BinaryWriter::writeU64(std::uint64_t v) {
    detail::writeLE<std::uint64_t>(*this, v);
}
void BinaryWriter::writeI8(std::int8_t v) {
    writeBytes(&v, 1);
}
void BinaryWriter::writeI16(std::int16_t v) {
    detail::writeLE<std::uint16_t>(*this, static_cast<std::uint16_t>(v));
}
void BinaryWriter::writeI32(std::int32_t v) {
    detail::writeLE<std::uint32_t>(*this, static_cast<std::uint32_t>(v));
}
void BinaryWriter::writeI64(std::int64_t v) {
    detail::writeLE<std::uint64_t>(*this, static_cast<std::uint64_t>(v));
}
void BinaryWriter::writeF32(float v) {
    detail::writeLE<std::uint32_t>(*this, std::bit_cast<std::uint32_t>(v));
}
void BinaryWriter::writeF64(double v) {
    detail::writeLE<std::uint64_t>(*this, std::bit_cast<std::uint64_t>(v));
}

void BinaryWriter::writeTag(const std::array<char, 4> &tag) {
    writeBytes(tag.data(), tag.size());
}

void BinaryWriter::writeVarUInt(std::uint64_t v) {
    while (v >= 0x80) {
        const std::uint8_t b = static_cast<std::uint8_t>((v & 0x7F) | 0x80);
        writeBytes(&b, 1);
        v >>= 7;
    }
    const std::uint8_t b = static_cast<std::uint8_t>(v & 0x7F);
    writeBytes(&b, 1);
}

void BinaryWriter::writeString(std::string_view s) {
    writeVarUInt(static_cast<std::uint64_t>(s.size()));
    if (!s.empty()) {
        writeBytes(s.data(), s.size());
    }
}

// ---- FileBinaryWriter --------------------------------------------------

FileBinaryWriter::FileBinaryWriter(const std::string &path)
    : m_path(path) {
    m_file = std::fopen(path.c_str(), "wb");
    if (!m_file) {
        setFailed("FileBinaryWriter: fopen failed for " + path);
        IRE_LOG_ERROR("FileBinaryWriter: fopen failed for {}", path);
    }
}

FileBinaryWriter::~FileBinaryWriter() {
    if (m_file) {
        std::fclose(m_file);
    }
}

void FileBinaryWriter::writeBytes(const void *data, std::size_t size) {
    if (!m_file || failed() || size == 0) {
        return;
    }
    const std::size_t written = std::fwrite(data, 1, size, m_file);
    if (written != size) {
        setFailed("FileBinaryWriter: short write on " + m_path);
        IRE_LOG_ERROR(
            "FileBinaryWriter: short write on {} ({} of {} bytes)",
            m_path,
            written,
            size
        );
    }
}

std::uint64_t FileBinaryWriter::tell() const {
    if (!m_file) {
        return 0;
    }
    const long pos = std::ftell(m_file);
    return pos < 0 ? 0 : static_cast<std::uint64_t>(pos);
}

void FileBinaryWriter::seek(std::uint64_t pos) {
    if (!m_file) {
        return;
    }
    if (std::fseek(m_file, static_cast<long>(pos), SEEK_SET) != 0) {
        setFailed("FileBinaryWriter: fseek failed on " + m_path);
    }
}

// ---- MemoryBinaryWriter ------------------------------------------------

void MemoryBinaryWriter::writeBytes(const void *data, std::size_t size) {
    if (failed() || size == 0) {
        return;
    }
    const std::uint64_t end = m_pos + size;
    if (end > m_buffer.size()) {
        m_buffer.resize(static_cast<std::size_t>(end));
    }
    std::memcpy(m_buffer.data() + m_pos, data, size); // raw buffer copy — the primitive all higher-level writes go through
    m_pos = end;
}

void MemoryBinaryWriter::seek(std::uint64_t pos) {
    if (pos > m_buffer.size()) {
        m_buffer.resize(static_cast<std::size_t>(pos));
    }
    m_pos = pos;
}

// ---- BinaryReader primitives -------------------------------------------

Result<std::uint8_t> BinaryReader::readU8() {
    std::uint8_t v = 0;
    if (auto st = readBytes(&v, 1); !st.ok()) {
        return Result<std::uint8_t>::error(st.code_, std::move(st.message_));
    }
    return Result<std::uint8_t>::success(v);
}
Result<std::uint16_t> BinaryReader::readU16() {
    return detail::readLE<std::uint16_t>(*this);
}
Result<std::uint32_t> BinaryReader::readU32() {
    return detail::readLE<std::uint32_t>(*this);
}
Result<std::uint64_t> BinaryReader::readU64() {
    return detail::readLE<std::uint64_t>(*this);
}
Result<std::int8_t> BinaryReader::readI8() {
    auto r = readU8();
    if (!r.ok()) {
        return Result<std::int8_t>::error(r.status_.code_, std::move(r.status_.message_));
    }
    return Result<std::int8_t>::success(static_cast<std::int8_t>(r.value_));
}
Result<std::int16_t> BinaryReader::readI16() {
    auto r = readU16();
    if (!r.ok()) {
        return Result<std::int16_t>::error(r.status_.code_, std::move(r.status_.message_));
    }
    return Result<std::int16_t>::success(static_cast<std::int16_t>(r.value_));
}
Result<std::int32_t> BinaryReader::readI32() {
    auto r = readU32();
    if (!r.ok()) {
        return Result<std::int32_t>::error(r.status_.code_, std::move(r.status_.message_));
    }
    return Result<std::int32_t>::success(static_cast<std::int32_t>(r.value_));
}
Result<std::int64_t> BinaryReader::readI64() {
    auto r = readU64();
    if (!r.ok()) {
        return Result<std::int64_t>::error(r.status_.code_, std::move(r.status_.message_));
    }
    return Result<std::int64_t>::success(static_cast<std::int64_t>(r.value_));
}
Result<float> BinaryReader::readF32() {
    auto r = readU32();
    if (!r.ok()) {
        return Result<float>::error(r.status_.code_, std::move(r.status_.message_));
    }
    return Result<float>::success(std::bit_cast<float>(r.value_));
}
Result<double> BinaryReader::readF64() {
    auto r = readU64();
    if (!r.ok()) {
        return Result<double>::error(r.status_.code_, std::move(r.status_.message_));
    }
    return Result<double>::success(std::bit_cast<double>(r.value_));
}

Result<std::array<char, 4>> BinaryReader::readTag() {
    std::array<char, 4> tag{};
    if (auto st = readBytes(tag.data(), 4); !st.ok()) {
        return Result<std::array<char, 4>>::error(st.code_, std::move(st.message_));
    }
    return Result<std::array<char, 4>>::success(tag);
}

Result<std::uint64_t> BinaryReader::readVarUInt() {
    std::uint64_t value = 0;
    int shift = 0;
    // ULEB128 caps at 10 bytes for a 64-bit value (each byte contributes 7 bits,
    // 10 * 7 = 70 ≥ 64). Any longer is malformed.
    for (int i = 0; i < 10; ++i) {
        std::uint8_t b = 0;
        if (auto st = readBytes(&b, 1); !st.ok()) {
            return Result<std::uint64_t>::error(
                BinaryIOError::InvalidVarUInt,
                "varint truncated at offset " + std::to_string(tell()) + " in " + sourceName()
            );
        }
        const std::uint64_t chunk = static_cast<std::uint64_t>(b & 0x7F);
        // Reject overflow: on byte 10 only the lowest bit is legal (bits >= 64).
        if (shift >= 64 || (shift == 63 && chunk > 1)) {
            return Result<std::uint64_t>::error(
                BinaryIOError::InvalidVarUInt,
                "varint overflows uint64 at offset " + std::to_string(tell()) + " in " +
                    sourceName()
            );
        }
        value |= chunk << shift;
        if ((b & 0x80) == 0) {
            return Result<std::uint64_t>::success(value);
        }
        shift += 7;
    }
    return Result<std::uint64_t>::error(
        BinaryIOError::InvalidVarUInt,
        "varint has no terminator (>10 bytes) at offset " + std::to_string(tell()) + " in " +
            sourceName()
    );
}

Result<std::string> BinaryReader::readString() {
    auto lenR = readVarUInt();
    if (!lenR.ok()) {
        return Result<std::string>::error(lenR.status_.code_, std::move(lenR.status_.message_));
    }
    if (lenR.value_ > remaining()) {
        return Result<std::string>::error(
            BinaryIOError::InvalidString,
            "string length " + std::to_string(lenR.value_) + " exceeds remaining bytes (" +
                std::to_string(remaining()) + ") in " + sourceName()
        );
    }
    std::string out(static_cast<std::size_t>(lenR.value_), '\0');
    if (lenR.value_ > 0) {
        if (auto st = readBytes(out.data(), static_cast<std::size_t>(lenR.value_)); !st.ok()) {
            return Result<std::string>::error(st.code_, std::move(st.message_));
        }
    }
    return Result<std::string>::success(std::move(out));
}

// ---- FileBinaryReader --------------------------------------------------

FileBinaryReader::FileBinaryReader(const std::string &path)
    : m_path(path) {
    m_file = std::fopen(path.c_str(), "rb");
    if (!m_file) {
        IRE_LOG_ERROR("FileBinaryReader: fopen failed for {}", path);
        return;
    }
    std::fseek(m_file, 0, SEEK_END);
    const long sz = std::ftell(m_file);
    std::fseek(m_file, 0, SEEK_SET);
    m_size = sz < 0 ? 0 : static_cast<std::uint64_t>(sz);
}

FileBinaryReader::~FileBinaryReader() {
    if (m_file) {
        std::fclose(m_file);
    }
}

BinaryStatus FileBinaryReader::readBytes(void *dest, std::size_t size) {
    if (!m_file) {
        return BinaryStatus::error(BinaryIOError::OpenFailed, "file not open: " + m_path);
    }
    if (size == 0) {
        return BinaryStatus::success();
    }
    const std::size_t got = std::fread(dest, 1, size, m_file);
    if (got != size) {
        return BinaryStatus::error(
            BinaryIOError::Truncated,
            "truncated read at offset " + std::to_string(tell()) + " (" + std::to_string(got) +
                " of " + std::to_string(size) + " bytes) in " + m_path
        );
    }
    return BinaryStatus::success();
}

std::uint64_t FileBinaryReader::tell() const {
    if (!m_file) {
        return 0;
    }
    const long p = std::ftell(m_file);
    return p < 0 ? 0 : static_cast<std::uint64_t>(p);
}

BinaryStatus FileBinaryReader::seek(std::uint64_t pos) {
    if (!m_file) {
        return BinaryStatus::error(BinaryIOError::OpenFailed, "file not open: " + m_path);
    }
    if (pos > m_size) {
        return BinaryStatus::error(
            BinaryIOError::Truncated,
            "seek to " + std::to_string(pos) + " past EOF (" + std::to_string(m_size) + ") in " +
                m_path
        );
    }
    if (std::fseek(m_file, static_cast<long>(pos), SEEK_SET) != 0) {
        return BinaryStatus::error(BinaryIOError::Truncated, "fseek failed on " + m_path);
    }
    return BinaryStatus::success();
}

// ---- MemoryBinaryReader ------------------------------------------------

MemoryBinaryReader::MemoryBinaryReader(const void *data, std::size_t size, std::string sourceName)
    : m_data(static_cast<const std::uint8_t *>(data))
    , m_size(size)
    , m_sourceName(std::move(sourceName)) {}

BinaryStatus MemoryBinaryReader::readBytes(void *dest, std::size_t size) {
    if (size == 0) {
        return BinaryStatus::success();
    }
    if (m_pos + size > m_size) {
        const std::uint64_t avail = m_size > m_pos ? m_size - m_pos : 0;
        return BinaryStatus::error(
            BinaryIOError::Truncated,
            "truncated read at offset " + std::to_string(m_pos) + " (" + std::to_string(avail) +
                " of " + std::to_string(size) + " bytes) in " + m_sourceName
        );
    }
    std::memcpy(dest, m_data + m_pos, size); // raw buffer copy — the primitive all higher-level reads go through
    m_pos += size;
    return BinaryStatus::success();
}

BinaryStatus MemoryBinaryReader::seek(std::uint64_t pos) {
    if (pos > m_size) {
        return BinaryStatus::error(
            BinaryIOError::Truncated,
            "seek to " + std::to_string(pos) + " past end (" + std::to_string(m_size) + ") in " +
                m_sourceName
        );
    }
    m_pos = pos;
    return BinaryStatus::success();
}

} // namespace IRAsset
