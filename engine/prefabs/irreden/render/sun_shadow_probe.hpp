#ifndef SUN_SHADOW_PROBE_H
#define SUN_SHADOW_PROBE_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/sun_face_query_layout.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace IRPrefab::SunShadow {

// The sun texel a sun-space UV falls in, per cascade: the floor the bake's
// splat box is anchored at.
inline IRMath::ivec2
sunCascadeTexel(const IRRender::FrameDataSun &frame, int cascade, IRMath::vec2 sunUV) {
    const auto origin = cascade == 0 ? frame.cascadeOriginUV_0_ : frame.cascadeOriginUV_1_;
    const auto size = cascade == 0 ? frame.cascadeTexelSize_0_ : frame.cascadeTexelSize_1_;
    return IRMath::ivec2(IRMath::floor((sunUV - origin) / size));
}

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
        const IRMath::ivec2 texel = sunCascadeTexel(frame, cascade, IRMath::vec2(probe.sunUVZ_));
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
        const IRMath::ivec2 first =
            IRMath::max(sunCascadeTexel(frame, int(cascade), uvMin), IRMath::ivec2(0));
        const IRMath::ivec2 last = IRMath::min(
            sunCascadeTexel(frame, int(cascade), uvMax),
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
            // Planar slab words: the min planes, then the max planes, one run each.
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
                    output << ',' << slabs[std::size_t(slot) * width + i] << ','
                           << (slabs[std::size_t(kSunDepthSlabSlots + slot) * width + i] &
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

// Capture only: the sun-texel footprint of the main canvas's bake, grouped by
// bake workgroup and cascade. Each caster pixel is recovered through the bake's
// own frame data (the resident FrameDataVoxelToCanvas and FrameDataSun
// uploads) and projected to its splat box's base texel; per group the rows
// report the base-texel bounding box and how many of its pixels' splat boxes
// fit a window of 32, 40, 48 and 64 texels anchored at that box's minimum
// (the cooperative splat's shared window). The header repeats the fit census
// for 16x16 (the dispatched workgroup) and 8x8 groups. Fog culling is not
// modelled, so the census is an upper bound on bake writes. Synchronizes the
// GPU; never use in timing runs.
inline bool writeSunBakeFootprintProbe(const std::string &path) {
    std::ofstream output(path);
    if (!output) {
        return false;
    }
    const auto *bake = probeBakeSystem(output, false);
    if (bake == nullptr) {
        return false;
    }
    const auto mainCanvas = IRRender::getCanvas("main");
    const auto canvasOpt =
        IREntity::getComponentOptional<IRComponents::C_TriangleCanvasTextures>(mainCanvas);
    if (!canvasOpt.has_value()) {
        output << "# active=0\n";
        return false;
    }
    const auto &canvas = **canvasOpt;
    IRRender::FrameDataVoxelToCanvas voxelFrame{};
    bake->voxelFrameDataBuf_->getSubData(0, sizeof(voxelFrame), &voxelFrame);
    const IRRender::FrameDataSun &sun = bake->frameData_;
    const std::size_t texels = std::size_t(canvas.size_.x) * std::size_t(canvas.size_.y);
    std::vector<std::int32_t> distances(texels);
    canvas.getTextureDistances()->getSubImage2D(
        0,
        0,
        canvas.size_.x,
        canvas.size_.y,
        IRRender::PixelDataFormat::RED_INTEGER,
        IRRender::PixelDataType::INT32,
        distances.data()
    );
    const int scale = voxelFrame.voxelRenderOptions_.x != 0
                          ? IRMath::max(voxelFrame.voxelRenderOptions_.y, 1)
                          : 1;
    const IRMath::ivec2 frameOffset =
        voxelFrame.trixelCanvasOffsetZ1_ +
        IRMath::ivec2(IRMath::floor(voxelFrame.cameraTrixelOffset_ * float(scale)));
    const int radius = voxelFrame.residualYaw_ == 0.0f && sun.sunSplatMaxTexels_ > 0.0f
                           ? int(sun.sunSplatMaxTexels_)
                           : 0;
    const auto cardinal = IRMath::rasterYawCardinalIndex(voxelFrame.rasterYaw_);
    constexpr int kWindows[] = {32, 40, 48, 64};
    constexpr int kGroupSizes[] = {IRSystem::kBakeSunShadowGroupSize, 8};
    constexpr int kWindowCount = 4;
    constexpr int kGroupSizeCount = 2;
    struct Group {
        IRMath::ivec2 min_{INT_MAX, INT_MAX};
        IRMath::ivec2 max_{INT_MIN, INT_MIN};
        std::vector<IRMath::ivec2> bases_;
    };
    std::map<std::tuple<int, int, int>, Group> groups[kGroupSizeCount];
    std::size_t casterPixels = 0;
    for (int y = 0; y < canvas.size_.y; ++y) {
        for (int x = 0; x < canvas.size_.x; ++x) {
            const std::int32_t encoded = distances[std::size_t(y) * canvas.size_.x + x];
            if (encoded >= IRConstants::kTrixelDistanceMaxDistance) {
                continue;
            }
            ++casterPixels;
            const float rawDepth = float(encoded >> 3);
            const IRMath::vec2 isoRel(IRMath::ivec2(x, y) - frameOffset);
            IRMath::vec3 pos3D =
                voxelFrame.residualYaw_ != 0.0f
                    ? IRMath::isoPixelToPos3DYawed(isoRel, rawDepth, voxelFrame.visualYaw_)
                    : IRMath::rotateCardinalZInv(
                          IRMath::isoPixelToPos3D(isoRel, rawDepth),
                          cardinal
                      );
            if (scale > 1) {
                pos3D /= float(scale);
            }
            const IRMath::vec3 sunProj = IRMath::sunSpaceProject(
                pos3D,
                IRMath::vec3(sun.sunBasisU_),
                IRMath::vec3(sun.sunBasisV_),
                IRMath::vec3(sun.sunDirection_)
            );
            for (int cascade = 0; cascade < kSunShadowCascadeCount; ++cascade) {
                const IRMath::ivec2 base = sunCascadeTexel(sun, cascade, IRMath::vec2(sunProj));
                for (int g = 0; g < kGroupSizeCount; ++g) {
                    Group &group = groups[g][{cascade, x / kGroupSizes[g], y / kGroupSizes[g]}];
                    group.min_ = IRMath::min(group.min_, base);
                    group.max_ = IRMath::max(group.max_, base);
                    group.bases_.push_back(base);
                }
            }
        }
    }
    output << std::setprecision(9);
    output << "# active=1\n";
    output << "# finite_coverage=" << (bake->frameUsesFiniteCoverage_ ? 1 : 0) << "\n";
    output << "# canvas=" << canvas.size_.x << ',' << canvas.size_.y << "\n";
    output << "# caster_pixels=" << casterPixels << "\n";
    output << "# splat_radius=" << radius << "\n";
    output << "# residual_yaw=" << voxelFrame.residualYaw_ << "\n";
    std::ostringstream rows;
    for (int g = 0; g < kGroupSizeCount; ++g) {
        std::size_t fits[kSunShadowCascadeCount][kWindowCount] = {};
        std::size_t pixelsPerCascade[kSunShadowCascadeCount] = {};
        std::size_t groupsPerCascade[kSunShadowCascadeCount] = {};
        for (const auto &[key, group] : groups[g]) {
            const auto [cascade, gx, gy] = key;
            std::size_t fit[kWindowCount] = {};
            for (const IRMath::ivec2 base : group.bases_) {
                const IRMath::ivec2 reach = base - group.min_ + 2 * radius;
                for (int w = 0; w < kWindowCount; ++w) {
                    if (reach.x < kWindows[w] && reach.y < kWindows[w]) {
                        ++fit[w];
                    }
                }
            }
            for (int w = 0; w < kWindowCount; ++w) {
                fits[cascade][w] += fit[w];
            }
            pixelsPerCascade[cascade] += group.bases_.size();
            ++groupsPerCascade[cascade];
            if (g == 0) {
                rows << cascade << ',' << gx << ',' << gy << ',' << group.bases_.size() << ','
                     << group.min_.x << ',' << group.min_.y << ',' << group.max_.x << ','
                     << group.max_.y;
                for (int w = 0; w < kWindowCount; ++w) {
                    rows << ',' << fit[w];
                }
                rows << '\n';
            }
        }
        for (int cascade = 0; cascade < kSunShadowCascadeCount; ++cascade) {
            output << "# group_" << kGroupSizes[g] << "_cascade_" << cascade
                   << "_workgroups=" << groupsPerCascade[cascade] << "\n";
            for (int w = 0; w < kWindowCount; ++w) {
                output << "# group_" << kGroupSizes[g] << "_cascade_" << cascade << "_fit_"
                       << kWindows[w] << '=' << fits[cascade][w] << '/' << pixelsPerCascade[cascade]
                       << "\n";
            }
        }
    }
    output
        << "cascade,group_x,group_y,pixels,min_x,min_y,max_x,max_y,fit_32,fit_40,fit_48,fit_64\n";
    output << rows.str();
    output.close();
    return !output.fail();
}

} // namespace IRPrefab::SunShadow

#endif
