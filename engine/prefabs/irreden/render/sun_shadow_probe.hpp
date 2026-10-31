#ifndef SUN_SHADOW_PROBE_H
#define SUN_SHADOW_PROBE_H

#include <irreden/ir_render.hpp>
#include <irreden/render/sun_face_query_layout.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>

#include <cstddef>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

namespace IRPrefab::SunShadow {

struct PrimaryDepthTexel {
    IRMath::ivec2 texel_{-1, -1};
    std::uint32_t nearest_ = 0xFFFFFFFFu;
    std::uint32_t second_ = 0xFFFFFFFFu;
};

struct PrimaryDepthProbe {
    bool active_ = false;
    bool finiteCoverage_ = false;
    bool secondDepth_ = false;
    std::uint32_t faceRecords_ = 0;
    IRMath::vec3 sunUVZ_{};
    PrimaryDepthTexel cascades_[kSunShadowCascadeCount];
};

// Capture only after shadow baking: the primary and fallback words of the sun
// texel a world point projects to, per cascade. Synchronizes the GPU; never
// use in timing runs.
inline PrimaryDepthProbe readPrimaryDepthProbe(const IRMath::vec3 &world) {
    PrimaryDepthProbe probe;
    const auto system = IRSystem::findSystem(IRSystem::BAKE_SUN_SHADOW_MAP);
    if (system == IRSystem::kNullSystemId) {
        return probe;
    }
    const auto *bake =
        IRSystem::getSystemParams<IRSystem::System<IRSystem::BAKE_SUN_SHADOW_MAP>>(system);
    const IRRender::FrameDataSun &frame = bake->frameData_;
    if (frame.shadowsEnabled_ == 0) {
        return probe;
    }
    probe.active_ = true;
    probe.finiteCoverage_ = bake->frameUsesFiniteCoverage_;
    probe.secondDepth_ = frame.sunBasisV_.w != 0.0f;
    probe.sunUVZ_ = IRMath::sunSpaceProject(
        world,
        IRMath::vec3(frame.sunBasisU_),
        IRMath::vec3(frame.sunBasisV_),
        IRMath::vec3(frame.sunDirection_)
    );
    const auto *buffer = bake->sunShadowDepthMap_;
    IRRender::device()->memoryBarrier(IRRender::BarrierType::ALL);
    IRRender::device()->finish();
    buffer->getSubData(
        std::size_t(kSourceFaceHeaderOffset) * sizeof(std::uint32_t),
        sizeof(probe.faceRecords_),
        &probe.faceRecords_
    );
    constexpr std::uint32_t kCascadeWords = kSourceFaceMapDimension * kSourceFaceMapDimension;
    for (int cascade = 0; cascade < kSunShadowCascadeCount; ++cascade) {
        const auto origin = cascade == 0 ? frame.cascadeOriginUV_0_ : frame.cascadeOriginUV_1_;
        const auto size = cascade == 0 ? frame.cascadeTexelSize_0_ : frame.cascadeTexelSize_1_;
        const IRMath::ivec2 texel(IRMath::floor((IRMath::vec2(probe.sunUVZ_) - origin) / size));
        PrimaryDepthTexel &out = probe.cascades_[cascade];
        if (texel.x < 0 || texel.y < 0 || texel.x >= kSunShadowMapDim ||
            texel.y >= kSunShadowMapDim) {
            continue;
        }
        out.texel_ = texel;
        const std::size_t word = std::size_t(cascade) * kCascadeWords +
                                 std::size_t(texel.y) * kSunShadowMapDim + std::size_t(texel.x);
        buffer->getSubData(word * sizeof(std::uint32_t), sizeof(out.nearest_), &out.nearest_);
        buffer->getSubData(
            (word + kSourceFaceFallbackOffset) * sizeof(std::uint32_t),
            sizeof(out.second_),
            &out.second_
        );
    }
    return probe;
}

struct CasterPixelCensus {
    IRMath::ivec2 canvas_{};
    std::size_t occupied_ = 0;
    std::vector<std::size_t> counts_;
};

// Capture only: how many main-canvas pixels each tracked entity rasterized this
// frame, with the occupied total, so a caster missing from the sun map can be
// told apart from one the raster culled. Synchronizes the GPU; never use in
// timing runs.
inline CasterPixelCensus readCasterPixelCensus(const std::vector<IREntity::EntityId> &entities) {
    CasterPixelCensus census;
    census.counts_.assign(entities.size(), 0);
    const auto mainCanvas = IRRender::getCanvas("main");
    const auto canvasOpt =
        IREntity::getComponentOptional<IRComponents::C_TriangleCanvasTextures>(mainCanvas);
    if (!canvasOpt.has_value()) {
        return census;
    }
    const auto &canvas = **canvasOpt;
    census.canvas_ = canvas.size_;
    const std::size_t texels = std::size_t(canvas.size_.x) * std::size_t(canvas.size_.y);
    std::vector<std::int32_t> distances(texels);
    std::vector<std::uint32_t> ids(texels * 2);
    IRRender::device()->memoryBarrier(IRRender::BarrierType::ALL);
    IRRender::device()->finish();
    canvas.getTextureDistances()->getSubImage2D(
        0,
        0,
        canvas.size_.x,
        canvas.size_.y,
        IRRender::PixelDataFormat::RED_INTEGER,
        IRRender::PixelDataType::INT32,
        distances.data()
    );
    canvas.getTextureEntityIds()->getSubImage2D(
        0,
        0,
        canvas.size_.x,
        canvas.size_.y,
        IRRender::PixelDataFormat::RG_INTEGER,
        IRRender::PixelDataType::UINT32,
        ids.data()
    );
    for (std::size_t i = 0; i < texels; ++i) {
        if (distances[i] >= IRConstants::kTrixelDistanceMaxDistance) {
            continue;
        }
        ++census.occupied_;
        const auto id = IRRender::decodeCarrierEntityId(IRMath::uvec2(ids[2 * i], ids[2 * i + 1]));
        for (std::size_t entity = 0; entity < entities.size(); ++entity) {
            if (id == entities[entity]) {
                ++census.counts_[entity];
            }
        }
    }
    return census;
}

// Capture only after shadow baking. Synchronizes the GPU; never use in timing runs.
inline bool writeSourceFaceIndexProbe(const std::string &path, bool includeGeometry = false) {
    std::ofstream output(path);
    if (!output) {
        return false;
    }
    const auto system = IRSystem::findSystem(IRSystem::BAKE_SUN_SHADOW_MAP);
    if (system == IRSystem::kNullSystemId) {
        output << "# active=0\n";
        return false;
    }
    const auto *bake =
        IRSystem::getSystemParams<IRSystem::System<IRSystem::BAKE_SUN_SHADOW_MAP>>(system);
    if (!bake->frameUsesFiniteCoverage_ || bake->frameData_.shadowsEnabled_ == 0) {
        output << "# active=0\n";
        return false;
    }
    const auto *buffer = bake->sunShadowDepthMap_;
    const auto *frameBuffer = bake->sunShadowFrameDataBuf_;
    const SourceFaceLayout layout = bake->recordLayout_;
    std::vector<std::uint32_t> words(kSourceFaceRecordOffset - kSourceFaceHeaderOffset);
    IRRender::FrameDataSun frame{};
    IRRender::device()->memoryBarrier(IRRender::BarrierType::ALL);
    IRRender::device()->finish();
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

} // namespace IRPrefab::SunShadow

#endif
