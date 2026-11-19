#ifndef IR_RENDER_COMPOSITE_DRAW_WITNESS_H
#define IR_RENDER_COMPOSITE_DRAW_WITNESS_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/render_device.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Diagnostic per-draw counters for the entity-canvas composite, enabled by
// IR_COMPOSITE_DRAW_WITNESS="<first frame>,<frame count>,<csv path>". On each
// witnessed frame every composite draw records its fragment-shader invocations
// and GPU time while the composite runs unchanged; the same draws then replay
// in order under a first-claim ownership test, which counts the framebuffer
// samples each draw owns at the end of the composite. The counters sit outside
// the measured shaders, so they never change which fragments early depth
// rejects. A backend without invocation statistics writes `unsupported` and no
// rows.

namespace IRPrefab::CompositeDrawWitness {

enum class DrawKind : std::uint8_t {
    SOURCE_EARLY,
    SOURCE_FALLBACK,
    TEXTURE_LAYER,
};

inline const char *drawKindName(DrawKind kind) {
    switch (kind) {
    case DrawKind::SOURCE_EARLY:
        return "source-early";
    case DrawKind::SOURCE_FALLBACK:
        return "source-fallback";
    case DrawKind::TEXTURE_LAYER:
        return "texture-layer";
    }
    return "unknown";
}

struct DrawSite {
    int instance_ = 0;
    std::uint64_t entity_ = 0;
    DrawKind kind_ = DrawKind::TEXTURE_LAYER;
    IRMath::vec2 center_{};
    IRMath::vec2 extent_{};
    int distanceOffset_ = 0;
};

class Recorder {
  public:
    // Unset or malformed configuration leaves the recorder disabled.
    void configureFromEnvironment() {
        const char *value = std::getenv("IR_COMPOSITE_DRAW_WITNESS");
        if (value == nullptr) {
            return;
        }
        unsigned long long first = 0;
        unsigned long long count = 0;
        int consumed = 0;
        if (std::sscanf(value, "%llu,%llu,%n", &first, &count, &consumed) != 2 || consumed <= 0 ||
            value[consumed] == '\0' || count == 0) {
            std::fprintf(stderr, "composite-witness: malformed IR_COMPOSITE_DRAW_WITNESS\n");
            return;
        }
        firstFrame_ = first;
        frameCount_ = count;
        path_ = value + consumed;
        enabled_ = true;
    }

    // Advances the composite frame counter; true when this frame is witnessed.
    bool beginFrame() {
        const std::uint64_t frame = frame_++;
        witnessing_ = false;
        if (!enabled_ || frame < firstFrame_ || frame >= firstFrame_ + frameCount_) {
            return false;
        }
        if (file_ == nullptr && !open()) {
            enabled_ = false;
            return false;
        }
        witnessedFrame_ = frame;
        sites_.clear();
        witnessing_ = true;
        return true;
    }

    bool witnessing() const {
        return witnessing_;
    }

    void beginShadedDraw(const DrawSite &site) {
        const std::size_t index = sites_.size();
        sites_.push_back(site);
        ensureSlot(index);
        auto *device = IRRender::device();
        device->writeTimestamp(slots_[index].timestamp_, IRRender::TimestampSlot::START);
        device->beginDrawCounter(
            slots_[index].invocations_,
            IRRender::DrawCounter::FRAGMENT_INVOCATIONS
        );
    }

    void endShadedDraw() {
        auto *device = IRRender::device();
        device->endDrawCounter(IRRender::DrawCounter::FRAGMENT_INVOCATIONS);
        device->writeTimestamp(slots_[sites_.size() - 1].timestamp_, IRRender::TimestampSlot::END);
    }

    void beginReplay() {
        replayIndex_ = 0;
        IRRender::device()->beginFirstClaimReplay();
    }

    void beginOwnedDraw() {
        IRRender::device()->beginDrawCounter(
            slots_[replayIndex_].owned_,
            IRRender::DrawCounter::SAMPLES_PASSED
        );
    }

    void endOwnedDraw() {
        IRRender::device()->endDrawCounter(IRRender::DrawCounter::SAMPLES_PASSED);
        ++replayIndex_;
    }

    // Restores composite state, waits for the counters and appends one CSV row
    // per draw.
    void endFrame() {
        auto *device = IRRender::device();
        device->endFirstClaimReplay();
        device->finish();
        for (std::size_t i = 0; i < sites_.size(); ++i) {
            const DrawSite &site = sites_[i];
            float gpuMs = -1.0f;
            if (!device->readTimestampPairMs(slots_[i].timestamp_, gpuMs)) {
                gpuMs = -1.0f;
            }
            std::fprintf(
                file_,
                "%llu,%zu,%d,%llu,%s,%.3f,%.3f,%.3f,%.3f,%d,%llu,%llu,%.6f\n",
                static_cast<unsigned long long>(witnessedFrame_),
                i,
                site.instance_,
                static_cast<unsigned long long>(site.entity_),
                drawKindName(site.kind_),
                site.center_.x,
                site.center_.y,
                site.extent_.x,
                site.extent_.y,
                site.distanceOffset_,
                static_cast<unsigned long long>(device->readDrawCounter(slots_[i].invocations_)),
                static_cast<unsigned long long>(
                    i < replayIndex_ ? device->readDrawCounter(slots_[i].owned_) : 0u
                ),
                gpuMs
            );
        }
        std::fflush(file_);
        witnessing_ = false;
    }

  private:
    struct Slot {
        IRRender::DrawCounterHandle invocations_ = IRRender::kInvalidDrawCounterHandle;
        IRRender::DrawCounterHandle owned_ = IRRender::kInvalidDrawCounterHandle;
        IRRender::GpuTimestampHandle timestamp_ = IRRender::kInvalidGpuTimestampHandle;
    };

    bool open() {
        file_ = std::fopen(path_.c_str(), "w");
        if (file_ == nullptr) {
            std::fprintf(stderr, "composite-witness: cannot open %s\n", path_.c_str());
            return false;
        }
        if (!IRRender::device()->supportsDrawCounters()) {
            std::fprintf(file_, "unsupported\n");
            std::fprintf(stderr, "composite-witness: backend has no fragment invocation counter\n");
            std::fclose(file_);
            file_ = nullptr;
            return false;
        }
        std::fprintf(
            file_,
            "frame,draw,instance,entity,kind,center_x,center_y,extent_x,extent_y,"
            "distance_offset,fragment_invocations,owned_samples,gpu_ms\n"
        );
        return true;
    }

    void ensureSlot(std::size_t index) {
        auto *device = IRRender::device();
        while (slots_.size() <= index) {
            slots_.push_back(
                {device->createDrawCounter(),
                 device->createDrawCounter(),
                 device->createTimestampPair()}
            );
        }
    }

    bool enabled_ = false;
    bool witnessing_ = false;
    std::uint64_t firstFrame_ = 0;
    std::uint64_t frameCount_ = 0;
    std::uint64_t frame_ = 0;
    std::uint64_t witnessedFrame_ = 0;
    std::size_t replayIndex_ = 0;
    std::string path_;
    std::FILE *file_ = nullptr;
    std::vector<DrawSite> sites_;
    std::vector<Slot> slots_;
};

} // namespace IRPrefab::CompositeDrawWitness

#endif /* IR_RENDER_COMPOSITE_DRAW_WITNESS_H */
