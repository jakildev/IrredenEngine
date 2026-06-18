#include <irreden/asset/key_value_store.hpp>

#include <irreden/asset/chunk_header.hpp>
#include <irreden/ir_profile.hpp>

#include <algorithm>
#include <filesystem>
#include <utility>

namespace IRAsset {

namespace {

constexpr std::array<char, 4> kKvprTag{'K', 'V', 'P', 'R'};

// ---- Scalar element encode / decode --------------------------------------

// Writes one scalar (NUMBER | BOOL | STRING): a uint8 tag then its payload.
void encodeScalar(MemoryBinaryWriter &body, const ListElem &elem) {
    body.writeU8(static_cast<std::uint8_t>(elem.index()));
    switch (static_cast<ValueType>(elem.index())) {
    case ValueType::NUMBER:
        body.writeF64(std::get<double>(elem));
        break;
    case ValueType::BOOL:
        body.writeU8(std::get<bool>(elem) ? 1u : 0u);
        break;
    case ValueType::STRING:
        body.writeString(std::get<std::string>(elem));
        break;
    case ValueType::LIST:
        // ListElem can never hold a LIST (no nesting in v1) — the variant
        // has only three alternatives, so this case is unreachable.
        break;
    }
}

// Reads one scalar value of the given @p tag into @p out. The tag has
// already been read + validated as NUMBER | BOOL | STRING by the caller.
BinaryStatus decodeScalar(BinaryReader &r, ValueType tag, ListElem &out) {
    switch (tag) {
    case ValueType::NUMBER: {
        auto v = r.readF64();
        if (!v.ok()) {
            return v.status_;
        }
        out = v.value_;
        return BinaryStatus::success();
    }
    case ValueType::BOOL: {
        auto v = r.readU8();
        if (!v.ok()) {
            return v.status_;
        }
        out = (v.value_ != 0);
        return BinaryStatus::success();
    }
    case ValueType::STRING: {
        auto v = r.readString();
        if (!v.ok()) {
            return v.status_;
        }
        out = std::move(v.value_);
        return BinaryStatus::success();
    }
    case ValueType::LIST:
        break;
    }
    return BinaryStatus::error(
        BinaryIOError::UnknownTag,
        "list element tag " + std::to_string(static_cast<int>(tag)) + " is not a scalar in " +
            r.sourceName()
    );
}

// ---- KVPR chunk encode / decode ------------------------------------------

BinaryStatus encodeKvprChunk(MemoryBinaryWriter &body, const KeyValueStore &store) {
    // Sort keys so the on-disk byte order is stable regardless of hash-map
    // iteration order — keeps saves reproducible (and diffable) across runs.
    std::vector<std::string> keys = store.keys();
    std::sort(keys.begin(), keys.end());

    body.writeVarUInt(keys.size());
    for (const std::string &key : keys) {
        const Value *value = store.get(key);
        body.writeString(key);
        body.writeU8(static_cast<std::uint8_t>(value->index()));
        switch (valueType(*value)) {
        case ValueType::NUMBER:
            body.writeF64(std::get<double>(*value));
            break;
        case ValueType::BOOL:
            body.writeU8(std::get<bool>(*value) ? 1u : 0u);
            break;
        case ValueType::STRING:
            body.writeString(std::get<std::string>(*value));
            break;
        case ValueType::LIST: {
            const auto &list = std::get<std::vector<ListElem>>(*value);
            body.writeVarUInt(list.size());
            for (const ListElem &elem : list) {
                encodeScalar(body, elem);
            }
            break;
        }
        }
    }
    if (body.failed()) {
        return BinaryStatus::error(BinaryIOError::WriteFailed, body.failureMessage());
    }
    return BinaryStatus::success();
}

Result<KeyValueStore> decodeKvprChunk(const LoadedChunk &chunk, const std::string &sourceName) {
    MemoryBinaryReader r(chunk.data_.data(), chunk.data_.size(), sourceName + ":KVPR");
    KeyValueStore store;

    auto countR = r.readVarUInt();
    if (!countR.ok()) {
        return Result<KeyValueStore>::error(
            countR.status_.code_,
            std::move(countR.status_.message_)
        );
    }

    for (std::uint64_t i = 0; i < countR.value_; ++i) {
        auto keyR = r.readString();
        if (!keyR.ok()) {
            return Result<KeyValueStore>::error(
                keyR.status_.code_,
                std::move(keyR.status_.message_)
            );
        }
        auto tagR = r.readU8();
        if (!tagR.ok()) {
            return Result<KeyValueStore>::error(
                tagR.status_.code_,
                std::move(tagR.status_.message_)
            );
        }
        const auto tag = static_cast<ValueType>(tagR.value_);
        switch (tag) {
        case ValueType::NUMBER: {
            auto v = r.readF64();
            if (!v.ok()) {
                return Result<KeyValueStore>::error(v.status_.code_, std::move(v.status_.message_));
            }
            store.set(keyR.value_, Value{v.value_});
            break;
        }
        case ValueType::BOOL: {
            auto v = r.readU8();
            if (!v.ok()) {
                return Result<KeyValueStore>::error(v.status_.code_, std::move(v.status_.message_));
            }
            store.set(keyR.value_, Value{v.value_ != 0});
            break;
        }
        case ValueType::STRING: {
            auto v = r.readString();
            if (!v.ok()) {
                return Result<KeyValueStore>::error(v.status_.code_, std::move(v.status_.message_));
            }
            store.set(keyR.value_, Value{std::move(v.value_)});
            break;
        }
        case ValueType::LIST: {
            auto nR = r.readVarUInt();
            if (!nR.ok()) {
                return Result<KeyValueStore>::error(
                    nR.status_.code_,
                    std::move(nR.status_.message_)
                );
            }
            std::vector<ListElem> list;
            // Cap the reserve to the remaining bytes so a corrupted count
            // claiming billions of elements can't pre-allocate.
            const std::uint64_t cap = r.remaining();
            list.reserve(static_cast<std::size_t>(nR.value_ < cap ? nR.value_ : cap));
            for (std::uint64_t e = 0; e < nR.value_; ++e) {
                auto elemTagR = r.readU8();
                if (!elemTagR.ok()) {
                    return Result<KeyValueStore>::error(
                        elemTagR.status_.code_,
                        std::move(elemTagR.status_.message_)
                    );
                }
                const auto elemTag = static_cast<ValueType>(elemTagR.value_);
                ListElem elem;
                if (auto st = decodeScalar(r, elemTag, elem); !st.ok()) {
                    return Result<KeyValueStore>::error(st.code_, std::move(st.message_));
                }
                list.push_back(std::move(elem));
            }
            store.set(keyR.value_, Value{std::move(list)});
            break;
        }
        default:
            // A value tag this loader doesn't know — likely written by a
            // newer build that added a value type. We can't length-skip an
            // unknown payload, so stop with a recoverable error (Rule #5).
            return Result<KeyValueStore>::error(
                BinaryIOError::UnknownTag,
                "unknown value tag " + std::to_string(static_cast<int>(tagR.value_)) +
                    " for key '" + keyR.value_ + "' in " + r.sourceName()
            );
        }
    }
    return Result<KeyValueStore>::success(std::move(store));
}

} // namespace

// ---- KeyValueStore typed reads -------------------------------------------

double KeyValueStore::getNumber(const std::string &key, double fallback) const {
    const Value *v = get(key);
    if (v != nullptr) {
        if (const double *n = std::get_if<double>(v)) {
            return *n;
        }
    }
    return fallback;
}

bool KeyValueStore::getBool(const std::string &key, bool fallback) const {
    const Value *v = get(key);
    if (v != nullptr) {
        if (const bool *b = std::get_if<bool>(v)) {
            return *b;
        }
    }
    return fallback;
}

std::string KeyValueStore::getString(const std::string &key, const std::string &fallback) const {
    const Value *v = get(key);
    if (v != nullptr) {
        if (const std::string *s = std::get_if<std::string>(v)) {
            return *s;
        }
    }
    return fallback;
}

std::vector<std::string> KeyValueStore::keys() const {
    std::vector<std::string> out;
    out.reserve(m_entries.size());
    for (const auto &[key, value] : m_entries) {
        out.push_back(key);
    }
    return out;
}

// ---- Buffer-mode format --------------------------------------------------

BinaryStatus writeKeyValueStore(BinaryWriter &w, const KeyValueStore &store) {
    MemoryBinaryWriter kvprBody;
    if (auto st = encodeKvprChunk(kvprBody, store); !st.ok()) {
        return st;
    }
    std::vector<ChunkPayload> chunks;
    chunks.push_back(ChunkPayload{kKvprTag, kvprBody.takeBuffer()});
    return writeChunked(w, kKeyValueMagic, kKeyValueFormatVersion, chunks);
}

Result<KeyValueStore> readKeyValueStore(BinaryReader &r) {
    AssetHeader header{};
    auto chunksR = readChunks(r, kKeyValueMagic, kKeyValueFormatVersion, &header);
    if (!chunksR.ok()) {
        return Result<KeyValueStore>::error(
            chunksR.status_.code_,
            std::move(chunksR.status_.message_)
        );
    }
    const LoadedChunk *kvpr = findChunk(chunksR.value_, kKvprTag);
    if (kvpr == nullptr) {
        // No KVPR chunk — treat as an empty store rather than corrupt, so a
        // future file carrying only some other chunk loads as "no entries".
        return Result<KeyValueStore>::success(KeyValueStore{});
    }
    return decodeKvprChunk(*kvpr, r.sourceName());
}

// ---- File-mode format ----------------------------------------------------

BinaryStatus saveKeyValueStore(const std::string &path, const KeyValueStore &store) {
    // userDataDir / joinPath do not create directories — make the parent
    // exist before opening the file for write.
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            IRE_LOG_ERROR(
                "Failed to create directory {} for key-value store: {}",
                parent.string(),
                ec.message()
            );
            return BinaryStatus::error(
                BinaryIOError::OpenFailed,
                "failed to create directory " + parent.string()
            );
        }
    }

    FileBinaryWriter w(path);
    if (!w.ok()) {
        IRE_LOG_ERROR("Failed to open key-value store for writing: {}", path);
        return BinaryStatus::error(BinaryIOError::OpenFailed, "failed to open " + path);
    }
    if (auto st = writeKeyValueStore(w, store); !st.ok()) {
        IRE_LOG_ERROR("Failed to write key-value store {}: {}", path, st.message_);
        return st;
    }
    IRE_LOG_INFO("Saved key-value store with {} entr(ies) to {}", store.size(), path);
    return BinaryStatus::success();
}

Result<KeyValueStore> loadKeyValueStore(const std::string &path) {
    FileBinaryReader r(path);
    if (!r.ok()) {
        // Missing file is the common first-launch case — log at info, not
        // error, so a fresh install doesn't look broken in the logs.
        IRE_LOG_INFO("No key-value store at {} (using defaults)", path);
        return Result<KeyValueStore>::error(BinaryIOError::OpenFailed, "failed to open " + path);
    }
    auto storeR = readKeyValueStore(r);
    if (!storeR.ok()) {
        IRE_LOG_ERROR("Failed to load key-value store {}: {}", path, storeR.status_.message_);
    } else {
        IRE_LOG_INFO(
            "Loaded key-value store with {} entr(ies) from {}",
            storeR.value_.size(),
            path
        );
    }
    return storeR;
}

} // namespace IRAsset
