#ifndef CARDINAL_RECEIVER_PROBE_H
#define CARDINAL_RECEIVER_PROBE_H

#include <irreden/asset/binary_io.hpp>
#include <irreden/render/shape_sample_probe.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace IRPrefab::CardinalReceiverProbe {

namespace detail {

template <class T> bool writeTexels(const std::string &path, const std::vector<T> &texels) {
    IRAsset::FileBinaryWriter output(path);
    output.writeBytes(texels.data(), texels.size() * sizeof(T));
    return output.ok();
}

} // namespace detail

// Capture-only main-canvas readback for an offline cardinal-receiver oracle.
// `<prefix>.json` holds the gather placement and the cardinal receiver frame;
// `<prefix>.distances` (int32), `.ids` (uint32 pairs) and `.colors` (RGBA8) are
// the raw canvas texels, row-major, native byte order. Synchronizes the GPU;
// never use in timing runs.
inline bool write(const std::string &prefix) {
    using namespace IRMath;
    using namespace IRComponents;
    const auto entity = IRRender::getCanvas("main");
    if (entity == IREntity::kNullEntity)
        return false;
    const auto canvasOpt = IREntity::getComponentOptional<C_TriangleCanvasTextures>(entity);
    const auto system = IRSystem::findSystem(IRSystem::TRIXEL_TO_FRAMEBUFFER);
    if (!canvasOpt.has_value() || system == IRSystem::kNullSystemId)
        return false;
    const auto &canvas = **canvasOpt;
    const auto *gather =
        IRSystem::getSystemParams<IRSystem::System<IRSystem::TRIXEL_TO_FRAMEBUFFER>>(system);
    if (gather->lighting_ == nullptr)
        return false;
    const ivec2 size = canvas.size_;
    const std::size_t texels = std::size_t(size.x) * std::size_t(size.y);
    std::vector<std::int32_t> distances(texels);
    std::vector<std::uint32_t> ids(texels * 2);
    std::vector<std::uint8_t> colors(texels * 4);
    IRRender::device()->memoryBarrier(IRRender::BarrierType::ALL);
    IRRender::device()->finish();
    canvas.getTextureDistances()->getSubImage2D(
        0,
        0,
        size.x,
        size.y,
        IRRender::PixelDataFormat::RED_INTEGER,
        IRRender::PixelDataType::INT32,
        distances.data()
    );
    canvas.getTextureEntityIds()->getSubImage2D(
        0,
        0,
        size.x,
        size.y,
        IRRender::PixelDataFormat::RG_INTEGER,
        IRRender::PixelDataType::UINT32,
        ids.data()
    );
    canvas.getTextureColors()->getSubImage2D(
        0,
        0,
        size.x,
        size.y,
        IRRender::PixelDataFormat::RGBA,
        IRRender::PixelDataType::UNSIGNED_BYTE,
        colors.data()
    );
    const auto map = IRPrefab::ShapeSampleProbe::gatherMap(entity, size);
    const auto &voxelFrame = gather->lighting_->scratchVoxelFrame_;
    IRAsset::JsonSidecarWriter j;
    j.beginObject();
    const auto vector = [&](const char *key, auto value) {
        j.key(key);
        j.beginArray();
        for (int i = 0; i < value.length(); ++i)
            j.valueFloat(value[i]);
        j.endArray();
    };
    j.key("schema");
    j.valueInt(1);
    vector("canvas_size", size);
    vector("viewport", map.viewport_);
    vector("output_scale", map.outputScale_);
    vector("framebuffer_size", map.framebufferSize_);
    vector("blit_offset", map.blitOffset_);
    vector("canvas_from_pixel", map.canvasFromPixel_);
    j.key("sample_layout");
    j.valueInt(static_cast<std::int64_t>(canvas.renderedSampleLayout_));
    j.key("cardinal_lighting");
    j.valueInt(gather->cardinalLightingEnabled_ ? 1 : 0);
    vector("trixel_canvas_offset_z1", voxelFrame.trixelCanvasOffsetZ1_);
    vector("camera_trixel_offset", voxelFrame.cameraTrixelOffset_);
    vector("voxel_render_options", voxelFrame.voxelRenderOptions_);
    j.key("raster_yaw");
    j.valueFloat(voxelFrame.rasterYaw_);
    j.key("cardinal_index");
    j.valueInt(static_cast<std::int64_t>(IRMath::rasterYawCardinalIndex(voxelFrame.rasterYaw_)));
    j.endObject();
    return detail::writeTexels(prefix + ".distances", distances) &&
           detail::writeTexels(prefix + ".ids", ids) &&
           detail::writeTexels(prefix + ".colors", colors) &&
           IRAsset::writeJsonSidecarToFile(prefix + ".json", j.str());
}

} // namespace IRPrefab::CardinalReceiverProbe

#endif
