#ifndef IR_ASSET_BINARY_IO_H
#define IR_ASSET_BINARY_IO_H

/// Little-endian, fixed-width binary I/O primitives shared by every
/// asset format in `engine/asset/` and the ECS world snapshot in
/// `engine/world/`. Two backends — file and in-memory — implement the
/// same abstract `BinaryWriter` / `BinaryReader` interface so callers
/// can write the same chunk-building code against either.
///
/// All integer primitives are stored little-endian regardless of host
/// byte order. Floats are stored as their IEEE 754 bit pattern in the
/// same width as the host (assumes IEEE 754, which is universal on
/// platforms the engine supports).
///
/// Reads return `Result<T>` so a truncated file, bad magic, or malformed
/// varint is recoverable — never a crash. The 7 Save Format Extensibility
/// Rules in `docs/design/entity-editor-epic.md` mandate this contract.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace IRAsset {

/// Error codes returned by binary reads. `Ok` means success; anything
/// else is recoverable (no crash) and carries a diagnostic message.
enum class BinaryIOError {
    Ok = 0,
    OpenFailed,       ///< fopen failed
    Truncated,        ///< read past end-of-file / end-of-buffer
    BadMagic,         ///< asset header magic mismatch
    VersionTooNew,    ///< asset header version above the loader's max
    InvalidVarUInt,   ///< varint had no terminating byte / overflowed u64
    InvalidString,    ///< string length prefix exceeded available bytes
    ChunkOutOfBounds, ///< chunk-table entry points outside the file
    WriteFailed,      ///< fwrite returned fewer bytes than requested
    UnknownTag,       ///< unrecognized value/record discriminant byte (likely a newer writer)
    MigratorMissing,  ///< a known component's on-disk version has no registered reader/migrator
                      ///< (world snapshot P5)
};

/// Status payload returned by void-typed binary operations.
struct BinaryStatus {
    BinaryIOError code_ = BinaryIOError::Ok;
    std::string message_;

    bool ok() const {
        return code_ == BinaryIOError::Ok;
    }
    explicit operator bool() const {
        return ok();
    }

    static BinaryStatus success() {
        return {};
    }
    static BinaryStatus error(BinaryIOError c, std::string msg) {
        return {c, std::move(msg)};
    }
};

/// Generic result wrapper for binary reads. `value_` is default-constructed
/// when `status_.ok()` is false; check `ok()` before consuming it.
template <typename T> struct Result {
    BinaryStatus status_;
    T value_{};

    bool ok() const {
        return status_.ok();
    }
    explicit operator bool() const {
        return ok();
    }

    static Result success(T v) {
        Result r;
        r.value_ = std::move(v);
        return r;
    }
    static Result error(BinaryIOError c, std::string msg) {
        Result r;
        r.status_ = BinaryStatus::error(c, std::move(msg));
        return r;
    }
};

// ---- Writer ------------------------------------------------------------

/// Abstract sink for byte-level writes. File and memory backends below.
/// Concrete primitives (`writeU8`, `writeVarUInt`, ...) are implemented
/// in terms of `writeBytes` so a subclass only has to override the raw
/// byte sink + position tracking.
class BinaryWriter {
  public:
    virtual ~BinaryWriter() = default;

    /// Append @p size bytes from @p data. Sets the writer's failure flag
    /// on any backend error; subsequent calls become no-ops once failed.
    virtual void writeBytes(const void *data, std::size_t size) = 0;

    /// Current write position in bytes from the start.
    virtual std::uint64_t tell() const = 0;

    /// Seek to @p pos. Caller guarantees @p pos lies within the
    /// already-written prefix (for back-patching chunk tables).
    virtual void seek(std::uint64_t pos) = 0;

    /// Returns true after a previous backend write failed.
    bool failed() const {
        return m_failed;
    }

    /// Diagnostic for the first failure encountered.
    const std::string &failureMessage() const {
        return m_failureMessage;
    }

    void writeU8(std::uint8_t v);
    void writeU16(std::uint16_t v);
    void writeU32(std::uint32_t v);
    void writeU64(std::uint64_t v);
    void writeI8(std::int8_t v);
    void writeI16(std::int16_t v);
    void writeI32(std::int32_t v);
    void writeI64(std::int64_t v);
    void writeF32(float v);
    void writeF64(double v);

    /// ULEB128 varint (Protocol Buffers style). 1 byte for values < 128,
    /// up to 10 bytes for u64. Use for counts/IDs where small values are
    /// common.
    void writeVarUInt(std::uint64_t v);

    /// Length-prefixed UTF-8 string. Prefix is a varint byte-count.
    void writeString(std::string_view s);

    /// Write a 4-byte chunk tag (e.g. `{'V','O','X','R'}`).
    void writeTag(const std::array<char, 4> &tag);

  protected:
    void setFailed(std::string msg) {
        if (!m_failed) {
            m_failed = true;
            m_failureMessage = std::move(msg);
        }
    }

  private:
    bool m_failed = false;
    std::string m_failureMessage;
};

/// File-backed writer. Wraps a `FILE*` opened in binary mode.
class FileBinaryWriter final : public BinaryWriter {
  public:
    /// Opens @p path for binary write (truncates). Check `ok()` after
    /// construction — false means the file could not be opened and all
    /// subsequent writes are no-ops.
    explicit FileBinaryWriter(const std::string &path);
    ~FileBinaryWriter() override;

    FileBinaryWriter(const FileBinaryWriter &) = delete;
    FileBinaryWriter &operator=(const FileBinaryWriter &) = delete;

    bool ok() const {
        return m_file != nullptr && !failed();
    }

    void writeBytes(const void *data, std::size_t size) override;
    std::uint64_t tell() const override;
    void seek(std::uint64_t pos) override;

  private:
    std::FILE *m_file = nullptr;
    std::string m_path;
};

/// In-memory writer. Bytes accumulate in an internal `std::vector`.
class MemoryBinaryWriter final : public BinaryWriter {
  public:
    MemoryBinaryWriter() = default;

    void writeBytes(const void *data, std::size_t size) override;
    std::uint64_t tell() const override {
        return m_pos;
    }
    void seek(std::uint64_t pos) override;

    const std::vector<std::uint8_t> &buffer() const {
        return m_buffer;
    }
    std::vector<std::uint8_t> takeBuffer() {
        return std::move(m_buffer);
    }

  private:
    std::vector<std::uint8_t> m_buffer;
    std::uint64_t m_pos = 0;
};

// ---- Reader ------------------------------------------------------------

/// Abstract byte source for binary reads. All `read*` primitives return
/// `Result<T>` and never throw; a truncated file yields a `Truncated`
/// error with file path + offset in the message.
class BinaryReader {
  public:
    virtual ~BinaryReader() = default;

    /// Read exactly @p size bytes into @p dest. Returns `Truncated` if
    /// fewer bytes were available (and leaves @p dest partially written).
    virtual BinaryStatus readBytes(void *dest, std::size_t size) = 0;

    /// Current read position from the start, in bytes.
    virtual std::uint64_t tell() const = 0;

    /// Seek to absolute position. Returns an error if @p pos is past EOF.
    virtual BinaryStatus seek(std::uint64_t pos) = 0;

    /// Total size in bytes (file or buffer).
    virtual std::uint64_t size() const = 0;

    /// Identifier for diagnostics (file path for file backends; "<memory>"
    /// for in-memory ones).
    virtual std::string sourceName() const = 0;

    /// Bytes remaining from the current position.
    std::uint64_t remaining() const {
        const std::uint64_t s = size();
        const std::uint64_t t = tell();
        return s > t ? s - t : 0;
    }

    Result<std::uint8_t> readU8();
    Result<std::uint16_t> readU16();
    Result<std::uint32_t> readU32();
    Result<std::uint64_t> readU64();
    Result<std::int8_t> readI8();
    Result<std::int16_t> readI16();
    Result<std::int32_t> readI32();
    Result<std::int64_t> readI64();
    Result<float> readF32();
    Result<double> readF64();
    Result<std::uint64_t> readVarUInt();
    Result<std::string> readString();

    /// Read a 4-byte chunk tag.
    Result<std::array<char, 4>> readTag();
};

/// File-backed reader. Wraps a `FILE*` opened in binary read mode.
class FileBinaryReader final : public BinaryReader {
  public:
    explicit FileBinaryReader(const std::string &path);
    ~FileBinaryReader() override;

    FileBinaryReader(const FileBinaryReader &) = delete;
    FileBinaryReader &operator=(const FileBinaryReader &) = delete;

    bool ok() const {
        return m_file != nullptr;
    }

    BinaryStatus readBytes(void *dest, std::size_t size) override;
    std::uint64_t tell() const override;
    BinaryStatus seek(std::uint64_t pos) override;
    std::uint64_t size() const override {
        return m_size;
    }
    std::string sourceName() const override {
        return m_path;
    }

  private:
    std::FILE *m_file = nullptr;
    std::string m_path;
    std::uint64_t m_size = 0;
};

/// In-memory reader. Borrows the input buffer; caller keeps it alive.
class MemoryBinaryReader final : public BinaryReader {
  public:
    MemoryBinaryReader(const void *data, std::size_t size, std::string sourceName = "<memory>");

    BinaryStatus readBytes(void *dest, std::size_t size) override;
    std::uint64_t tell() const override {
        return m_pos;
    }
    BinaryStatus seek(std::uint64_t pos) override;
    std::uint64_t size() const override {
        return m_size;
    }
    std::string sourceName() const override {
        return m_sourceName;
    }

  private:
    const std::uint8_t *m_data = nullptr;
    std::uint64_t m_size = 0;
    std::uint64_t m_pos = 0;
    std::string m_sourceName;
};

} // namespace IRAsset

#endif /* IR_ASSET_BINARY_IO_H */
