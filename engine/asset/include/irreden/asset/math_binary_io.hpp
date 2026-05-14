#ifndef IR_ASSET_MATH_BINARY_IO_H
#define IR_ASSET_MATH_BINARY_IO_H

// Centralizes vec3/vec4/Color binary I/O — lives in engine/asset/ (not engine/math/) to avoid the reverse dep.

#include <irreden/asset/binary_io.hpp>
#include <irreden/math/ir_math_types.hpp>

namespace IRMath::BinaryIO {

inline void writeVec3(IRAsset::BinaryWriter &w, const IRMath::vec3 &v) {
    w.writeF32(v.x);
    w.writeF32(v.y);
    w.writeF32(v.z);
}

inline void writeVec4(IRAsset::BinaryWriter &w, const IRMath::vec4 &v) {
    w.writeF32(v.x);
    w.writeF32(v.y);
    w.writeF32(v.z);
    w.writeF32(v.w);
}

inline IRAsset::Result<IRMath::vec3> readVec3(IRAsset::BinaryReader &r) {
    auto x = r.readF32();
    if (!x.ok())
        return IRAsset::Result<IRMath::vec3>::error(x.status_.code_, std::move(x.status_.message_));
    auto y = r.readF32();
    if (!y.ok())
        return IRAsset::Result<IRMath::vec3>::error(y.status_.code_, std::move(y.status_.message_));
    auto z = r.readF32();
    if (!z.ok())
        return IRAsset::Result<IRMath::vec3>::error(z.status_.code_, std::move(z.status_.message_));
    return IRAsset::Result<IRMath::vec3>::success(IRMath::vec3(x.value_, y.value_, z.value_));
}

inline IRAsset::Result<IRMath::vec4> readVec4(IRAsset::BinaryReader &r) {
    auto x = r.readF32();
    if (!x.ok())
        return IRAsset::Result<IRMath::vec4>::error(x.status_.code_, std::move(x.status_.message_));
    auto y = r.readF32();
    if (!y.ok())
        return IRAsset::Result<IRMath::vec4>::error(y.status_.code_, std::move(y.status_.message_));
    auto z = r.readF32();
    if (!z.ok())
        return IRAsset::Result<IRMath::vec4>::error(z.status_.code_, std::move(z.status_.message_));
    auto w = r.readF32();
    if (!w.ok())
        return IRAsset::Result<IRMath::vec4>::error(w.status_.code_, std::move(w.status_.message_));
    return IRAsset::Result<IRMath::vec4>::success(IRMath::vec4(x.value_, y.value_, z.value_, w.value_));
}

inline void writeColorPacked(IRAsset::BinaryWriter &w, IRMath::Color color) {
    w.writeU32(color.toPackedRGBA());
}

} // namespace IRMath::BinaryIO

#endif /* IR_ASSET_MATH_BINARY_IO_H */
