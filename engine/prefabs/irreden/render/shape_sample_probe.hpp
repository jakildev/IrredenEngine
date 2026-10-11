#ifndef SHAPE_SAMPLE_PROBE_H
#define SHAPE_SAMPLE_PROBE_H

#include <irreden/asset/json_sidecar.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>

#include <array>
#include <limits>

namespace IRPrefab::ShapeSampleProbe {
namespace detail {

inline bool inside(IRMath::ivec2 p, IRMath::ivec2 size) {
    return p.x >= 0 && p.y >= 0 && p.x < size.x && p.y < size.y;
}

struct GatherPixel {
    bool valid_ = false;
    IRMath::vec2 preUpscalePosition_{};
    IRMath::ivec2 framebufferPixel_{};
    IRMath::vec2 canvasOriginRaw_{};
    IRMath::ivec2 canvasPixel_{};
};

// Invert the two axis-aligned draw models at the nearest-filtered framebuffer
// texel center. Physical pixels in one upscale block therefore share one ray.
inline GatherPixel mapPixel(
    IRMath::ivec2 pixel,
    IRMath::ivec2 viewport,
    IRMath::ivec2 outputScale,
    IRMath::ivec2 framebufferSize,
    IRMath::vec2 blitOffset,
    IRMath::ivec2 canvasSize,
    const IRMath::mat4 &gatherModel
) {
    using namespace IRMath;
    GatherPixel result;
    if (!inside(pixel, viewport) || outputScale.x <= 0 || outputScale.y <= 0 ||
        framebufferSize.x <= 0 || framebufferSize.y <= 0 || canvasSize.x <= 0 ||
        canvasSize.y <= 0 || gatherModel[0].x <= 0 || gatherModel[1].y <= 0)
        return result;
    const vec2 center = vec2(viewport) * 0.5f + vec2(blitOffset.x, -blitOffset.y);
    result.preUpscalePosition_ =
        vec2(framebufferSize) * 0.5f + (vec2(pixel) + 0.5f - center) / vec2(outputScale);
    const vec2 fb = floor(result.preUpscalePosition_);
    if (fb.x < 0 || fb.y < 0 || fb.x >= framebufferSize.x || fb.y >= framebufferSize.y)
        return result;
    result.framebufferPixel_ = ivec2(fb);
    const vec2 quad = vec2(
        (fb.x + 0.5f - gatherModel[3].x) / gatherModel[0].x,
        (framebufferSize.y - fb.y - 0.5f - gatherModel[3].y) / gatherModel[1].y
    );
    result.canvasOriginRaw_ = (vec2(quad.x, -quad.y) + 0.5f) * vec2(canvasSize);
    if (result.canvasOriginRaw_.x < 0 || result.canvasOriginRaw_.y < 0 ||
        result.canvasOriginRaw_.x >= canvasSize.x || result.canvasOriginRaw_.y >= canvasSize.y)
        return result;
    result.canvasPixel_ = ivec2(floor(result.canvasOriginRaw_));
    result.valid_ = true;
    return result;
}

struct Owner {
    bool valid_ = false;
    std::uint32_t tile_ = 0, lane_ = 0, face_ = 0, half_ = 0;
};

inline Owner decodeOwner(std::uint32_t key, std::size_t tileCount) {
    Owner result;
    if (key == std::numeric_limits<std::uint32_t>::max())
        return result;
    result.tile_ = key / (64u * 3u * 2u);
    result.lane_ = (key / 6u) % 64u;
    result.face_ = (key / 2u) % 3u;
    result.half_ = key % 2u;
    result.valid_ = result.tile_ < tileCount;
    return result;
}

inline bool matchesPublication(
    const IRRender::GPUShapeDescriptor &shape,
    std::uint64_t entityId,
    int rawDepth,
    const Owner &owner
) {
    return owner.valid_ && entityId == shape.entityId && (rawDepth & 7) == int(owner.face_);
}

} // namespace detail

// Debug-only main-canvas readback, once on a settled capture frame. The retained
// producer snapshot and same-tick stamp avoid the shared gather UBO (GUI draws
// overwrite it). This names the canvas sample; it does not certify that later
// sprites, GUI, or detached canvases leave that sample visible in the screenshot.
// Geometry reconstruction is deliberately limited to the origin-zero cardinal
// torus fixture. Other geometry still reports raw metadata with an explicit status.
inline bool writeFirstCapture(IRMath::ivec2 pixel, const std::string &path) {
    using namespace IRMath;
    using namespace IRComponents;
    using Gather = IRSystem::System<IRSystem::TRIXEL_TO_FRAMEBUFFER>;
    IRAsset::JsonSidecarWriter j;
    j.beginObject();
    const auto integer = [&](const char *key, std::int64_t value) {
        j.key(key);
        j.valueInt(value);
    };
    const auto vector = [&](const char *key, auto value) {
        j.key(key);
        j.beginArray();
        for (int i = 0; i < value.length(); ++i)
            j.valueFloat(value[i]);
        j.endArray();
    };
    const auto finish = [&](const char *status) {
        j.key("status");
        j.valueString(status);
        j.endObject();
        const bool written = IRAsset::writeJsonSidecarToFile(path, j.str());
        IR_LOG_INFO("[shape-sample-probe] status={} written={} path={}", status, written, path);
        return written && std::string_view(status) == "ok";
    };
    integer("schema", 1);
    vector("screenshot_pixel", pixel);
    const auto tick = IRSystem::getEventTickCount(IRTime::RENDER);
    integer("render_tick", tick);
    const auto entity = IRRender::getCanvas("main");
    if (entity == IREntity::kNullEntity)
        return finish("missing_canvas");
    const auto canvasOpt = IREntity::getComponentOptional<C_TriangleCanvasTextures>(entity);
    if (!canvasOpt.has_value())
        return finish("missing_canvas");
    const auto &canvas = **canvasOpt;
    const auto &geometry = canvas.shapeGeometry_;
    const auto &frame = geometry.frameData_;
    integer("publication_tick", geometry.publicationTick());
    if (!geometry.samplesValidForTick(tick))
        return finish("stale_or_missing_publication");
    if (geometry.ownerSize_ != canvas.size_ || frame.canvasSize != canvas.size_ ||
        geometry.sampleOwners_.second == nullptr || geometry.tiles_.second == nullptr ||
        geometry.descriptors_.second == nullptr || geometry.tileCount_ > geometry.tileCapacity_ ||
        frame.shapeCount < 1 || std::size_t(frame.shapeCount) > geometry.capacity_ ||
        canvas.size_.x <= 0 || canvas.size_.y <= 0 ||
        std::size_t(canvas.size_.x) * std::size_t(canvas.size_.y) >
            geometry.ownerCapacityBytes_ / sizeof(std::uint32_t))
        return finish("invalid_submission");
    const auto &framebuffer = IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
    const ivec2 framebufferSize = framebuffer.getResolutionPlusBuffer();
    const ivec2 viewport = IRRender::getViewport();
    const ivec2 outputScale = IRRender::getOutputScaleFactor();
    const vec2 cameraIso = IRRender::getEffectiveCameraIso();
    const vec2 cameraZoom = IRRender::getCameraZoom();
    const auto behaviorOpt = IREntity::getComponentOptional<C_TrixelCanvasRenderBehavior>(entity);
    const auto behavior = behaviorOpt.has_value() ? **behaviorOpt : C_TrixelCanvasRenderBehavior{};
    const auto zoomOpt = IREntity::getComponentOptional<C_ZoomLevel>(entity);
    const int density = behavior.applyRenderSubdivisions_ &&
                                IRRender::getSubdivisionMode() != IRRender::SubdivisionMode::NONE
                            ? IRRender::getVoxelRenderEffectiveSubdivisions()
                            : 1;
    const vec2 zoom =
        (behavior.useCameraZoom_ ? cameraZoom
                                 : (zoomOpt.has_value() ? (*zoomOpt)->zoom_ : vec2(1))) /
        float(density);
    const vec2 gather = Gather::canvasGatherTranslation(
        IRPrefab::Camera::zoomFrame(cameraIso, cameraZoom),
        behavior,
        behavior.useCameraPositionIso_ ? cameraIso : vec2(0),
        zoom,
        density
    );
    const ivec2 logicalSize = IRPrefab::CanvasCoverage::logicalSize(entity, canvas.size_);
    const auto &world = IREntity::getComponent<C_WorldTransform>("mainFramebuffer");
    const vec2 blitOffset =
        isoDeltaToScreenDelta(
            pos3DtoPos2DIso(world.translation_),
            IRRender::getTriangleStepSizeScreen()
        ) +
        vec2(IRPrefab::Camera::screenResidual(cameraIso, cameraZoom, outputScale));
    const auto mapped = detail::mapPixel(
        pixel,
        viewport,
        outputScale,
        framebufferSize,
        blitOffset,
        canvas.size_,
        Gather::calcModelMatrix(
            vec2(framebufferSize),
            gather,
            zoom,
            vec2(canvas.size_) / vec2(logicalSize)
        )
    );
    vector("viewport_size", viewport);
    vector("output_scale", outputScale);
    vector("main_framebuffer_size", framebufferSize);
    vector("pre_upscale_position", mapped.preUpscalePosition_);
    vector("main_framebuffer_pixel", mapped.framebufferPixel_);
    vector("canvas_size", canvas.size_);
    vector("logical_canvas_size", logicalSize);
    vector("canvas_origin_raw", mapped.canvasOriginRaw_);
    vector("canvas_pixel", mapped.canvasPixel_);
    vector("gather_translation", gather);
    vector("canvas_zoom", zoom);
    if (!mapped.valid_)
        return finish("pixel_out_of_bounds");
    j.key("projection");
    j.beginObject();
    vector("camera_trixel_offset", frame.cameraTrixelOffset);
    vector("trixel_canvas_offset_z1", frame.trixelCanvasOffsetZ1);
    vector("canvas_size", frame.canvasSize);
    integer("shape_count", frame.shapeCount);
    integer("finite_coverage", frame.finiteCoverage);
    vector("voxel_render_options", frame.voxelRenderOptions);
    vector("cull_iso_min", frame.cullIsoMin);
    vector("cull_iso_max", frame.cullIsoMax);
    j.key("visual_yaw");
    j.valueFloat(frame.visualYaw);
    j.key("raster_yaw");
    j.valueFloat(frame.rasterYaw);
    j.key("residual_yaw");
    j.valueFloat(frame.residualYaw);
    integer("tile_grid_x", frame.tileGridX);
    integer("smooth_yaw_enabled", frame.smoothYawEnabled);
    integer("lattice_shapes", frame.latticeShapes);
    j.key("face_deform");
    j.beginArray();
    for (const auto &face : frame.faceDeform) {
        j.beginArray();
        for (int i = 0; i < 4; ++i)
            j.valueFloat(face[i]);
        j.endArray();
    }
    j.endArray();
    j.endObject();
    IRRender::device()->memoryBarrier(IRRender::BarrierType::ALL);
    IRRender::device()->finish();
    std::uint32_t ownerKey = 0;
    const std::size_t linear =
        std::size_t(mapped.canvasPixel_.y) * canvas.size_.x + mapped.canvasPixel_.x;
    geometry.sampleOwners_.second
        ->getSubData(linear * sizeof(ownerKey), sizeof(ownerKey), &ownerKey);
    integer("owner_key", ownerKey);
    int rawDepth = 0;
    canvas.getTextureDistances()->getSubImage2D(
        mapped.canvasPixel_.x,
        mapped.canvasPixel_.y,
        1,
        1,
        IRRender::PixelDataFormat::RED_INTEGER,
        IRRender::PixelDataType::INT32,
        &rawDepth
    );
    const auto publishedId = canvas.readEntityIdAt(mapped.canvasPixel_);
    std::array<std::uint8_t, 4> rgba{};
    canvas.getTextureColors()->getSubImage2D(
        mapped.canvasPixel_.x,
        mapped.canvasPixel_.y,
        1,
        1,
        IRRender::PixelDataFormat::RGBA,
        IRRender::PixelDataType::UNSIGNED_BYTE,
        rgba.data()
    );
    integer("published_entity_id", publishedId);
    integer("raw_depth", rawDepth);
    integer("stored_depth", rawDepth >> 3);
    j.key("rgba");
    j.beginArray();
    for (auto c : rgba)
        j.valueInt(c);
    j.endArray();
    if (ownerKey == std::numeric_limits<std::uint32_t>::max())
        return finish("no_shape_owner");
    const auto owner = detail::decodeOwner(ownerKey, geometry.tileCount_);
    if (!owner.valid_)
        return finish("owner_out_of_bounds");
    integer("tile_index", owner.tile_);
    integer("lane", owner.lane_);
    integer("face", owner.face_);
    integer("half", owner.half_);
    IRRender::ShapeTileDescriptor tile{};
    geometry.tiles_.second->getSubData(owner.tile_ * sizeof(tile), sizeof(tile), &tile);
    j.key("tile");
    j.beginObject();
    integer("shape_index", tile.shapeIndex);
    vector("iso_origin", tile.tileIsoOrigin);
    j.endObject();
    if (tile.shapeIndex < 0 || tile.shapeIndex >= frame.shapeCount)
        return finish("shape_out_of_bounds");
    const ivec2 sampleIso = tile.tileIsoOrigin + ivec2(owner.lane_ % 8, owner.lane_ / 8);
    vector("sample_iso", sampleIso);
    IRRender::GPUShapeDescriptor shape{};
    geometry.descriptors_.second
        ->getSubData(std::size_t(tile.shapeIndex) * sizeof(shape), sizeof(shape), &shape);
    j.key("shape");
    j.beginObject();
    vector("world_position", shape.worldPosition);
    vector("params", shape.params);
    vector("rotation", shape.rotation);
    integer("shape_type", shape.shapeType);
    integer("flags", shape.flags);
    integer("entity_id", shape.entityId);
    integer("joint_index", shape.jointIndex);
    j.endObject();
    if (!detail::matchesPublication(shape, publishedId, rawDepth, owner))
        return finish("publication_identity_mismatch");
    const bool supported = shape.shapeType == std::uint32_t(IRMath::SDF::ShapeType::TORUS) &&
                           vec3(shape.worldPosition) == vec3(0) &&
                           IRMath::abs(shape.rotation.w) >= 0.9999f && frame.visualYaw == 0 &&
                           frame.rasterYaw == 0 && frame.residualYaw == 0 &&
                           frame.smoothYawEnabled == 0 && frame.latticeShapes == 0;
    j.key("surface_depth");
    if (!supported) {
        j.valueNull();
        return finish("unsupported_geometry");
    }
    const int sub = frame.voxelRenderOptions.x != 0 && frame.voxelRenderOptions.y > 1
                        ? frame.voxelRenderOptions.y
                        : 1;
    j.valueInt((rawDepth >> 3) - (sub > 1 ? (3 * sub + 1) / 2 : 0));
    return finish("ok");
}

} // namespace IRPrefab::ShapeSampleProbe
#endif
