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
