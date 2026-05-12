#include <irreden/asset/name_table.hpp>

#include <utility>

namespace IRAsset {

BinaryStatus writeNameTable(BinaryWriter &w, std::span<const NameTableEntry> entries) {
    w.writeVarUInt(static_cast<std::uint64_t>(entries.size()));
    for (const auto &e : entries) {
        w.writeVarUInt(e.id_);
        w.writeString(e.name_);
    }
    if (w.failed()) {
        return BinaryStatus::error(BinaryIOError::WriteFailed, w.failureMessage());
    }
    return BinaryStatus::success();
}

Result<std::vector<NameTableEntry>> readNameTable(BinaryReader &r) {
    auto countR = r.readVarUInt();
    if (!countR.ok()) {
        return Result<std::vector<NameTableEntry>>::error(
            countR.status_.code_,
            std::move(countR.status_.message_)
        );
    }
    // Cap the upfront reserve to the remaining bytes — a corrupted count
    // claiming billions of entries shouldn't pre-allocate that much.
    const std::uint64_t cap = r.remaining();
    std::vector<NameTableEntry> out;
    out.reserve(static_cast<std::size_t>(countR.value_ < cap ? countR.value_ : cap));
    for (std::uint64_t i = 0; i < countR.value_; ++i) {
        NameTableEntry e{};
        auto idR = r.readVarUInt();
        if (!idR.ok()) {
            return Result<std::vector<NameTableEntry>>::error(
                idR.status_.code_,
                std::move(idR.status_.message_)
            );
        }
        e.id_ = static_cast<std::uint32_t>(idR.value_);
        auto nameR = r.readString();
        if (!nameR.ok()) {
            return Result<std::vector<NameTableEntry>>::error(
                nameR.status_.code_,
                std::move(nameR.status_.message_)
            );
        }
        e.name_ = std::move(nameR.value_);
        out.push_back(std::move(e));
    }
    return Result<std::vector<NameTableEntry>>::success(std::move(out));
}

NameTable::NameTable(std::vector<NameTableEntry> entries)
    : m_entries(std::move(entries)) {
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        m_byName.emplace(m_entries[i].name_, m_entries[i].id_);
        m_byId.emplace(m_entries[i].id_, i);
    }
}

void NameTable::add(std::uint32_t id, std::string name) {
    const std::size_t idx = m_entries.size();
    m_byName.emplace(name, id);
    m_byId.emplace(id, idx);
    m_entries.push_back(NameTableEntry{id, std::move(name)});
}

std::optional<std::uint32_t> NameTable::idByName(std::string_view name) const {
    // unordered_map keyed on std::string — need a transparent-lookup-friendly
    // hash to use string_view directly. Allocate the temporary string here
    // (this lookup is not in a hot path; serialization runs once per save).
    auto it = m_byName.find(std::string(name));
    if (it == m_byName.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::string_view> NameTable::nameById(std::uint32_t id) const {
    auto it = m_byId.find(id);
    if (it == m_byId.end()) {
        return std::nullopt;
    }
    return std::string_view(m_entries[it->second].name_);
}

} // namespace IRAsset
