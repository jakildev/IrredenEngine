#pragma once

#include <irreden/ir_math.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <vector>

// Frame-by-frame animation state for the voxel editor (T-214, F-1.4).
//
// Each VoxelFrame is an independent dense snapshot of the editing target's
// C_VoxelSetNew::voxels_ span. The active frame's snapshot is loaded into
// the editing target when switching frames; the previous frame is saved
// first. The editable target lives in g_editor.editableVoxelSet_ (T-211).

namespace IRVoxelEditor {

enum class LoopMode { LOOP, PING_PONG };

struct VoxelFrame {
    // Dense snapshot aligned 1:1 with C_VoxelSetNew::voxels_. Empty for a
    // blank frame that has never been populated.
    std::vector<IRComponents::C_Voxel> voxels_;
};

struct AnimationState {
    std::vector<VoxelFrame> frames_;
    int activeFrame_ = 0;

    bool playing_ = false;
    float fps_ = 12.0f;
    float elapsed_ = 0.0f;
    int playDirection_ = 1;   // +1 forward; -1 reverse (ping-pong bounce)
    LoopMode loopMode_ = LoopMode::LOOP;

    AnimationState() { frames_.emplace_back(); }

    int frameCount() const { return static_cast<int>(frames_.size()); }

    // Append a blank frame after the active frame and select it.
    void addBlankFrame() {
        frames_.insert(frames_.begin() + activeFrame_ + 1, VoxelFrame{});
        ++activeFrame_;
    }

    // Duplicate the active frame, insert after it, and select the copy.
    // Caller is responsible for flushing the live voxel state into
    // frames_[activeFrame_].voxels_ before calling, so the copy is current.
    void duplicateCurrentFrame() {
        frames_.insert(frames_.begin() + activeFrame_ + 1, frames_[activeFrame_]);
        ++activeFrame_;
    }

    // Remove the active frame. At least one frame is always kept.
    void deleteCurrentFrame() {
        if (frameCount() <= 1) return;
        frames_.erase(frames_.begin() + activeFrame_);
        if (activeFrame_ >= frameCount())
            activeFrame_ = frameCount() - 1;
    }
};

// Advance playback by `dt` seconds. On a frame transition, writes the new
// frame index to `outNext` and returns true — leaving anim.activeFrame_
// untouched so the caller can hand the result to switchToFrame, which
// snapshots the live voxels into the old active frame before swapping in
// the new one. Returns false (and does not touch outNext) when the
// elapsed-timer hasn't reached the frame duration or playback is off.
inline bool tickPlayback(AnimationState& anim, float dt, int& outNext) {
    if (!anim.playing_ || anim.frameCount() <= 1) return false;

    anim.elapsed_ += dt;
    const float frameDur = 1.0f / anim.fps_;
    if (anim.elapsed_ < frameDur) return false;
    anim.elapsed_ -= frameDur;

    int next = anim.activeFrame_ + anim.playDirection_;

    if (anim.loopMode_ == LoopMode::LOOP) {
        if (next >= anim.frameCount()) next = 0;
        else if (next < 0) next = anim.frameCount() - 1;
    } else {
        // PING_PONG: reverse at each boundary, stepping one back to
        // avoid double-hitting endpoints.
        if (next >= anim.frameCount()) {
            anim.playDirection_ = -1;
            next = IRMath::max(0, anim.frameCount() - 2);
        } else if (next < 0) {
            anim.playDirection_ = 1;
            next = IRMath::min(1, anim.frameCount() - 1);
        }
    }

    outNext = next;
    return true;
}

} // namespace IRVoxelEditor
