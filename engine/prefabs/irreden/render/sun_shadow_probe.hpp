#ifndef SUN_SHADOW_PROBE_H
#define SUN_SHADOW_PROBE_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/sun_face_query_layout.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

namespace IRPrefab::SunShadow {

// The bake system a probe reads after this frame's shadow work, with the GPU
// drained; null, after the inactive marker, when there is nothing to read:
// no system, shadows off, or finite coverage required but inactive.
inline const IRSystem::System<IRSystem::BAKE_SUN_SHADOW_MAP> *
probeBakeSystem(std::ofstream &output, bool requireFiniteCoverage) {
    const auto system = IRSystem::findSystem(IRSystem::BAKE_SUN_SHADOW_MAP);
    if (system == IRSystem::kNullSystemId) {
        output << "# active=0\n";
        return nullptr;
    }
    const auto *bake =
        IRSystem::getSystemParams<IRSystem::System<IRSystem::BAKE_SUN_SHADOW_MAP>>(system);
    if ((requireFiniteCoverage && !bake->frameUsesFiniteCoverage_) ||
        bake->frameData_.shadowsEnabled_ == 0) {
        output << "# active=0\n";
        return nullptr;
    }
    IRRender::device()->memoryBarrier(IRRender::BarrierType::ALL);
    IRRender::device()->finish();
    return bake;
}

// Capture only after shadow baking. Synchronizes the GPU; never use in timing runs.
inline bool writeSourceFaceIndexProbe(const std::string &path, bool includeGeometry = false) {
    std::ofstream output(path);
    if (!output) {
        return false;
    }
    const auto *bake = probeBakeSystem(output, true);
    if (bake == nullptr) {
        return false;
    }
    const auto *buffer = bake->sunShadowDepthMap_;
    const auto *frameBuffer = bake->sunShadowFrameDataBuf_;
    const SourceFaceLayout layout = bake->recordLayout_;
    std::vector<std::uint32_t> words(kSourceFaceRecordOffset - kSourceFaceHeaderOffset);
    IRRender::FrameDataSun frame{};
    buffer->getSubData(
        std::size_t(kSourceFaceHeaderOffset) * sizeof(std::uint32_t),
        words.size() * sizeof(std::uint32_t),
        words.data()
    );
    std::uint32_t spillRequested = 0;
    buffer->getSubData(
        std::size_t(layout.spillCounterOffset()) * sizeof(std::uint32_t),
        sizeof(spillRequested),
        &spillRequested
    );
    std::uint32_t hierarchy[2] = {};
    buffer->getSubData(
        std::size_t(layout.hierarchyNeededOffset()) * sizeof(std::uint32_t),
        sizeof(hierarchy),
        hierarchy
    );
    frameBuffer->getSubData(0, sizeof(frame), &frame);
    output << std::setprecision(9);
    output << "# active=1\n";
    output << "# face_records_requested=" << words[0] << "\n";
    output << "# face_record_capacity=" << layout.capacity_ << "\n";
    output << "# face_record_capacity_word=" << words[1] << "\n";
    output << "# face_record_ceiling=" << bake->recordCapacityCeiling_ << "\n";
    output << "# record_capacity_changes=" << bake->recordCapacityChanges_ << "\n";
    output << "# record_demand_peak=" << bake->recordDemandPeak_ << "\n";
    output << "# record_demand_sampled_frame=" << bake->recordDemandSampledFrame_ << "\n";
    output << "# record_exhausted_samples=" << bake->recordExhaustedSamples_ << "\n";
    output << "# spill_nodes_requested=" << spillRequested << "\n";
    output << "# spill_node_capacity=" << kSourceFaceSpillCapacity << "\n";
    output << "# hierarchy_needed=" << hierarchy[0] << "\n";
    output << "# hierarchy_ready_leaves=" << hierarchy[1] << "\n";
    output << "# tile_inline_capacity=" << kSourceFaceTileCapacity << "\n";
    output << "# buffer_bytes=" << layout.bufferBytes() << "\n";
    output << "# tile_capacity=" << kSourceFaceTileQueryCapacity << "\n";
    output << "# tiles_per_axis=" << kSourceFaceTilesPerAxis << "\n";
    output << "# cascade_count=" << kSourceFaceCascadeCount << "\n";
    output << "# basis_u=" << frame.sunBasisU_.x << ',' << frame.sunBasisU_.y << ','
           << frame.sunBasisU_.z << "\n";
    output << "# basis_v=" << frame.sunBasisV_.x << ',' << frame.sunBasisV_.y << ','
           << frame.sunBasisV_.z << "\n";
    if (includeGeometry) {
        const auto captured = IRMath::min(words[0], layout.capacity_);
        std::vector<std::uint32_t> records(std::size_t(captured) * kSourceFaceRecordWords);
        if (!records.empty()) {
            buffer->getSubData(
                std::size_t(kSourceFaceRecordOffset) * sizeof(std::uint32_t),
                records.size() * sizeof(std::uint32_t),
                records.data()
            );
        }
        output << "# geometry_version=1\n";
        output << "# geometry_record_words=" << kSourceFaceRecordWords << "\n";
        output << "# geometry_records_captured=" << captured << "\n";
        output << "# sun_direction=" << frame.sunDirection_.x << ',' << frame.sunDirection_.y << ','
               << frame.sunDirection_.z << "\n";
        output << "# max_shadow_throw=" << frame.sunMaxShadowThrow_ << "\n";
        for (std::uint32_t record = 0; record < captured; ++record) {
            output << "# geometry_record_" << record << '=';
            for (std::uint32_t word = 0; word < kSourceFaceRecordWords; ++word) {
                if (word != 0)
                    output << ',';
                output << records[std::size_t(record) * kSourceFaceRecordWords + word];
            }
            output << '\n';
        }
    }
    output << "cascade,x,y,count,complete,u_min,v_min,u_max,v_max,records_lost\n";
    for (std::uint32_t tile = 0; tile < kSourceFaceTileCount; ++tile) {
        const auto cascade = tile / (kSourceFaceTilesPerAxis * kSourceFaceTilesPerAxis);
        const auto x = tile % kSourceFaceTilesPerAxis;
        const auto y = (tile / kSourceFaceTilesPerAxis) % kSourceFaceTilesPerAxis;
        const auto counter = words
            [kSourceFaceTileOffset - kSourceFaceHeaderOffset +
             tile * (kSourceFaceTileCapacity + 1u)];
        const auto count = counter & ~kSourceFaceTileRecordLost;
        const bool recordsLost = (counter & kSourceFaceTileRecordLost) != 0u;
        const auto origin = cascade == 0 ? frame.cascadeOriginUV_0_ : frame.cascadeOriginUV_1_;
        const auto size = (cascade == 0 ? frame.cascadeTexelSize_0_ : frame.cascadeTexelSize_1_) *
                          float(kSourceFaceTileEdge);
        const auto low = origin + IRMath::vec2(float(x), float(y)) * size;
        const auto high = low + size;
        output << cascade << ',' << x << ',' << y << ',' << count << ','
               << (!recordsLost && count <= kSourceFaceTileQueryCapacity &&
                   (count <= kSourceFaceTileCapacity || spillRequested <= kSourceFaceSpillCapacity))
               << ',' << low.x << ',' << low.y << ',' << high.x << ',' << high.y << ','
               << recordsLost << "\n";
    }
    output.close();
    return !output.fail();
}

// Primary-layer words (nearest plus slab extrema) for the sun texels a
// world-space XY rectangle at height @p worldZ projects to, both cascades.
// Works on every bake route; the index probe above needs finite coverage.
// Synchronizes the GPU; never use in timing runs.
inline bool writeSunPrimaryDepthProbe(
    const std::string &path, IRMath::vec2 worldMin, IRMath::vec2 worldMax, float worldZ
) {
    std::ofstream output(path);
    if (!output) {
        return false;
    }
    const auto *bake = probeBakeSystem(output, false);
    if (bake == nullptr) {
        return false;
    }
    const IRRender::FrameDataSun &frame = bake->frameData_;
    const auto *buffer = bake->sunShadowDepthMap_;
    output << std::setprecision(9);
    output << "# active=1\n";
    output << "# finite_coverage=" << (bake->frameUsesFiniteCoverage_ ? 1 : 0) << "\n";
    output << "# max_shadow_throw=" << frame.sunMaxShadowThrow_ << "\n";
    output << "# slab_width=" << kSunDepthSlabWidthUnits << "\n";
    output << "# slab_slots=" << kSunDepthSlabSlots << "\n";
    output << "# sun_direction=" << frame.sunDirection_.x << ',' << frame.sunDirection_.y << ','
           << frame.sunDirection_.z << "\n";
    output << "# basis_u=" << frame.sunBasisU_.x << ',' << frame.sunBasisU_.y << ','
           << frame.sunBasisU_.z << "\n";
    output << "# basis_v=" << frame.sunBasisV_.x << ',' << frame.sunBasisV_.y << ','
           << frame.sunBasisV_.z << "\n";
    output << "# world_rect=" << worldMin.x << ',' << worldMin.y << ',' << worldMax.x << ','
           << worldMax.y << ',' << worldZ << "\n";
    // Whole-map census: texels any primary write reached, and texels whose
    // nearest word is a caster's own-texel write (the splat byte is zero),
    // a lower bound on rasterized caster pixels per cascade.
    {
        std::vector<std::uint32_t> layer(kSourceFaceFallbackOffset);
        buffer->getSubData(0, layer.size() * sizeof(std::uint32_t), layer.data());
        std::size_t written[kSourceFaceCascadeCount] = {};
        std::size_t direct[kSourceFaceCascadeCount] = {};
        const std::size_t perCascade = kSourceFaceFallbackOffset / kSourceFaceCascadeCount;
        for (std::size_t i = 0; i < layer.size(); ++i) {
            if (layer[i] == 0xFFFFFFFFu)
                continue;
            ++written[i / perCascade];
            if ((layer[i] & 0xFFu) == 0u)
                ++direct[i / perCascade];
        }
        output << "# primary_texels_written=" << written[0] << ',' << written[1] << "\n";
        output << "# primary_direct_texels=" << direct[0] << ',' << direct[1] << "\n";
    }
    const IRMath::vec3 basisU(frame.sunBasisU_);
    const IRMath::vec3 basisV(frame.sunBasisV_);
    IRMath::vec2 uvMin(1e30f);
    IRMath::vec2 uvMax(-1e30f);
    for (const IRMath::vec3 corner :
         {IRMath::vec3(worldMin.x, worldMin.y, worldZ),
          IRMath::vec3(worldMax.x, worldMin.y, worldZ),
          IRMath::vec3(worldMin.x, worldMax.y, worldZ),
          IRMath::vec3(worldMax.x, worldMax.y, worldZ)}) {
        const IRMath::vec2 uv(IRMath::dot(corner, basisU), IRMath::dot(corner, basisV));
        uvMin = IRMath::min(uvMin, uv);
        uvMax = IRMath::max(uvMax, uv);
    }
    output << "cascade,x,y,nearest";
    for (std::uint32_t slot = 0; slot < kSunDepthSlabSlots; ++slot) {
        output << ",slab" << slot << "_min,slab" << slot << "_max";
    }
    output << ",overflow\n";
    std::vector<std::uint32_t> nearest;
    std::vector<std::uint32_t> slabs;
    for (std::uint32_t cascade = 0; cascade < kSourceFaceCascadeCount; ++cascade) {
        const auto origin = cascade == 0 ? frame.cascadeOriginUV_0_ : frame.cascadeOriginUV_1_;
        const auto texel = cascade == 0 ? frame.cascadeTexelSize_0_ : frame.cascadeTexelSize_1_;
        const IRMath::ivec2 first =
            IRMath::max(IRMath::ivec2(IRMath::floor((uvMin - origin) / texel)), IRMath::ivec2(0));
        const IRMath::ivec2 last = IRMath::min(
            IRMath::ivec2(IRMath::floor((uvMax - origin) / texel)),
            IRMath::ivec2(int(kSunShadowMapDim) - 1)
        );
        output << "# cascade_" << cascade << "_texels=" << first.x << ',' << first.y << ','
               << last.x << ',' << last.y << "\n";
        if (first.x > last.x || first.y > last.y) {
            continue;
        }
        const std::size_t width = std::size_t(last.x - first.x + 1);
        nearest.resize(width);
        slabs.resize(width * kSunDepthSlabTexelWords);
        for (int y = first.y; y <= last.y; ++y) {
            const std::size_t texelIndex =
                std::size_t(cascade) * kSourceFaceFallbackOffset / kSourceFaceCascadeCount +
                std::size_t(y) * kSunShadowMapDim + std::size_t(first.x);
            buffer->getSubData(
                texelIndex * sizeof(std::uint32_t),
                width * sizeof(std::uint32_t),
                nearest.data()
            );
            // Planar slab words: one contiguous run per (slot, min / max) plane.
            for (std::uint32_t plane = 0; plane < kSunDepthSlabTexelWords; ++plane) {
                buffer->getSubData(
                    (std::size_t(kSunDepthSlabOffset) +
                     std::size_t(plane) * kSourceFaceFallbackOffset + texelIndex) *
                        sizeof(std::uint32_t),
                    width * sizeof(std::uint32_t),
                    slabs.data() + std::size_t(plane) * width
                );
            }
            for (std::size_t i = 0; i < width; ++i) {
                output << cascade << ',' << first.x + int(i) << ',' << y << ',' << nearest[i];
                for (std::uint32_t slot = 0; slot < kSunDepthSlabSlots; ++slot) {
                    output << ',' << slabs[std::size_t(slot * kSunDepthSlabWords) * width + i]
                           << ','
                           << (slabs[std::size_t(slot * kSunDepthSlabWords + 1) * width + i] &
                               ~kSunDepthSlabOverflow);
                }
                const bool overflow =
                    kSunDepthSlabSlots != 0u && (slabs[width + i] & kSunDepthSlabOverflow) != 0u;
                output << ',' << (overflow ? 1 : 0) << "\n";
            }
        }
    }
    output.close();
    return !output.fail();
}

} // namespace IRPrefab::SunShadow

#endif
