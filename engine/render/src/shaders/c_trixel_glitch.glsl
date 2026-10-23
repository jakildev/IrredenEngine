#version 450 core

// Canvas-space subject glitch (TRIXEL_GLITCH). Twin of
// metal/c_trixel_glitch.metal; per-texel CPU twin and contract in
// engine/prefabs/irreden/render/trixel_glitch_math.hpp. Reads the live
// colour, distance and entity-id planes plus the tick-start history and
// writes every texel of the scratch colour plane, which the CPU swaps in as
// the live colour after the barrier. Colours stay 8-bit integers throughout,
// so the backends and the CPU twin agree bit for bit.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_glitch_common.glsl"

// Header (seed, tick low, tick high, count; table mask, table base, record
// base, max spill), the id -> slot table, then five words per subject.
// Binding 8 is borrowed for the dispatch (kBufferIndex_TrixelGlitchSubjects).
layout(std430, binding = 8) readonly buffer TrixelGlitchSubjects {
    uvec4 subjectWords[];
};

layout(rgba8, binding = 0) readonly uniform image2D liveColors;
layout(r32i, binding = 1) readonly uniform iimage2D liveDistances;
layout(rg32ui, binding = 2) readonly uniform uimage2D liveEntityIds;
layout(rgba8, binding = 3) readonly uniform image2D historyColors;
layout(rg32ui, binding = 4) readonly uniform uimage2D historyEntityIds;
layout(rgba8, binding = 5) writeonly uniform image2D scratchColors;
layout(rgba8, binding = 6) readonly uniform image2D sourceLitColors;
layout(rgba8, binding = 7) writeonly uniform image2D presentationOverrides;

// Subject slot of every texel within kGlitchSubjectHalo of this group, -1 for
// none or off-canvas. The spill scan and the echo edge test read only this.
shared int tileSlots[kGlitchSubjectTileSize * kGlitchSubjectTileSize];
// 1 + the largest spill of a subject present in the tile; 0 when none is.
shared int tileReach;

struct SubjectRecord {
    uvec2 id;
    uint salt;
    int spill;
    bool depthEnabled;
    uint tearThreshold;
    uint holdThreshold;
    uint dissolveThreshold;
    uint echoStrength;
    int tearMaxTrixels;
    int splitTrixels;
    int bandRows;
    int dissolveCell;
    ivec2 echoA;
    ivec2 echoB;
    int depthNear;
    int depthFar;
    int tintA;
    int tintB;
};

struct GlitchFrame {
    ivec2 size;
    uint seed;
    uvec2 tick;
    ivec2 tileOrigin;
    bool cardinalPresentation;
};

bool inCanvas(ivec2 q, ivec2 size) {
    return q.x >= 0 && q.y >= 0 && q.x < size.x && q.y < size.y;
}

uvec4 loadLiveColour(ivec2 q) {
    return uvec4(imageLoad(liveColors, q) * 255.0 + 0.5);
}

uvec4 loadEffectColour(ivec2 q, GlitchFrame frame) {
    const bool sourceLit = frame.cardinalPresentation &&
                           isCardinalVoxelSurface(imageLoad(liveEntityIds, q).xy);
    const vec4 colour = sourceLit ? imageLoad(sourceLitColors, q) : imageLoad(liveColors, q);
    return uvec4(colour * 255.0 + 0.5);
}

uvec2 loadId(ivec2 q) {
    return decodeEntityId(imageLoad(liveEntityIds, q).xy);
}

int subjectSlot(uvec2 id) {
    if (id.x == 0u && id.y == 0u) {
        return -1;
    }
    uint mask = subjectWords[1].x;
    uint tableBase = subjectWords[1].y;
    uint entry = hash3(id.x, id.y, kGlitchSubjectTableTag) & mask;
    for (uint probe = 0u; probe <= mask; ++probe) {
        uvec4 word = subjectWords[tableBase + entry];
        if (word.x == 0u && word.y == 0u) {
            return -1;
        }
        if (word.x == id.x && word.y == id.y) {
            return int(word.z);
        }
        entry = (entry + 1u) & mask;
    }
    return -1;
}

uint recordBase(int slot) {
    return subjectWords[1].z + uint(slot) * kGlitchSubjectRecordWords;
}

int recordSpill(int slot) {
    return int(subjectWords[recordBase(slot)].w & 0xFFu);
}

SubjectRecord loadRecord(int slot) {
    uint base = recordBase(slot);
    uvec4 w0 = subjectWords[base];
    uvec4 w1 = subjectWords[base + 1u];
    ivec4 w2 = ivec4(subjectWords[base + 2u]);
    ivec4 w3 = ivec4(subjectWords[base + 3u]);
    ivec4 w4 = ivec4(subjectWords[base + 4u]);
    SubjectRecord record;
    record.id = w0.xy;
    record.salt = w0.z;
    record.spill = int(w0.w & 0xFFu);
    record.depthEnabled = (w0.w & 0x100u) != 0u;
    record.tearThreshold = w1.x;
    record.holdThreshold = w1.y;
    record.dissolveThreshold = w1.z;
    record.echoStrength = w1.w;
    record.tearMaxTrixels = w2.x;
    record.splitTrixels = w2.y;
    record.bandRows = w2.z;
    record.dissolveCell = w2.w;
    record.echoA = w3.xy;
    record.echoB = w3.zw;
    record.depthNear = w4.x;
    record.depthFar = w4.y;
    record.tintA = w4.z;
    record.tintB = w4.w;
    return record;
}

int tileSlotAt(ivec2 q, GlitchFrame frame) {
    ivec2 t = q - frame.tileOrigin;
    return tileSlots[t.y * kGlitchSubjectTileSize + t.x];
}

bool isEdge(ivec2 q, int slot, GlitchFrame frame) {
    if (tileSlotAt(q, frame) != slot) {
        return false;
    }
    return tileSlotAt(q + ivec2(1, 0), frame) != slot ||
           tileSlotAt(q + ivec2(-1, 0), frame) != slot ||
           tileSlotAt(q + ivec2(0, 1), frame) != slot ||
           tileSlotAt(q + ivec2(0, -1), frame) != slot;
}

uvec4 echoBlend(uvec4 colour, uvec4 tint, uint strength) {
    if (colour.a == 0u) {
        return uvec4(tint.rgb, (255u * strength) >> 8);
    }
    uvec4 mixed = (tint * strength + colour * (256u - strength)) >> 8;
    return uvec4(mixed.rgb, colour.a);
}

// One subject's effects at p; tear and split accept only trixels of
// acceptId, which is the subject itself except in a CROSS pair.
uvec4 applySubject(ivec2 p, int slot, uvec2 acceptId, GlitchFrame frame) {
    SubjectRecord record = loadRecord(slot);
    uvec2 current = loadId(p);
    int encodedDistance = imageLoad(liveDistances, p).x;
    int depth = decodeDepthSingle(encodedDistance);
    bool depthPasses = !record.depthEnabled ||
                       (encodedDistance < kGlitchSubjectEmptyDistance && depth >= record.depthNear &&
                        depth <= record.depthFar);
    uint bandLane = record.salt ^ (uint(p.y / record.bandRows) * kGlitchCellHashY);

    ivec2 source = p;
    if (record.tearThreshold > 0u && depthPasses) {
        uint hash = glitchHash(frame.seed, frame.tick, kGlitchEffectSubjectTear, bandLane);
        if ((hash & 0xFFFFu) < record.tearThreshold) {
            ivec2 shifted = p + ivec2(glitchSignedOffset(hash >> 16, record.tearMaxTrixels), 0);
            if (inCanvas(shifted, frame.size) && loadId(shifted) == acceptId) {
                source = shifted;
            }
        }
    }
    uvec4 colour = loadEffectColour(source, frame);

    if (record.splitTrixels != 0) {
        ivec2 redAt = source + ivec2(record.splitTrixels, 0);
        ivec2 blueAt = source - ivec2(record.splitTrixels, 0);
        uvec4 red = inCanvas(redAt, frame.size) && loadId(redAt) == acceptId ? loadEffectColour(redAt, frame)
                                                                              : colour;
        uvec4 blue = inCanvas(blueAt, frame.size) && loadId(blueAt) == acceptId
                         ? loadEffectColour(blueAt, frame)
                         : colour;
        colour = uvec4(red.r, colour.g, blue.b, max(max(red.a, colour.a), blue.a));
    }

    if (record.holdThreshold > 0u && depthPasses) {
        uint hash = glitchHash(frame.seed, frame.tick, kGlitchEffectSubjectHold, bandLane);
        if ((hash & 0xFFFFu) < record.holdThreshold) {
            uvec2 held = decodeEntityId(imageLoad(historyEntityIds, p).xy);
            if (held == record.id || (current == record.id && held == uvec2(0u))) {
                colour = uvec4(imageLoad(historyColors, p) * 255.0 + 0.5);
            }
        }
    }

    if (record.echoStrength > 0u) {
        if (isEdge(p - record.echoA, slot, frame)) {
            colour = echoBlend(colour, glitchSubjectTint(record.tintA), record.echoStrength);
        }
        if (isEdge(p - record.echoB, slot, frame)) {
            colour = echoBlend(colour, glitchSubjectTint(record.tintB), record.echoStrength);
        }
    }

    if (record.dissolveThreshold > 0u && current == record.id) {
        ivec2 cell = p / record.dissolveCell;
        uint hash = glitchHash(
            frame.seed, frame.tick, kGlitchEffectSubjectDissolve, record.salt ^ glitchCellLane(cell)
        );
        if ((hash & 0xFFFFu) < record.dissolveThreshold) {
            colour = uvec4(0u);
        }
    }
    return colour;
}

bool influenceBefore(int slot, int dist, int otherSlot, int otherDist) {
    return dist < otherDist || (dist == otherDist && slot < otherSlot);
}

uvec4 glitchTexel(ivec2 p, int radius, GlitchFrame frame, out bool influenced) {
    influenced = false;
    // The two nearest distinct subjects whose spill reaches p, nearer first,
    // ties by ascending slot (= ascending entity id).
    int slotA = -1;
    int distA = 0;
    int slotB = -1;
    int distB = 0;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int slot = tileSlotAt(p + ivec2(dx, dy), frame);
            if (slot < 0) {
                continue;
            }
            int dist = max(abs(dx), abs(dy));
            if (dist > recordSpill(slot)) {
                continue;
            }
            if (slot == slotA) {
                distA = min(distA, dist);
            } else if (slot == slotB) {
                distB = min(distB, dist);
            } else if (slotB < 0 || influenceBefore(slot, dist, slotB, distB)) {
                slotB = slot;
                distB = dist;
            }
            if (slotB >= 0 && (slotA < 0 || influenceBefore(slotB, distB, slotA, distA))) {
                int swapSlot = slotA;
                int swapDist = distA;
                slotA = slotB;
                distA = distB;
                slotB = swapSlot;
                distB = swapDist;
            }
        }
    }
    if (slotA < 0) {
        return loadLiveColour(p);
    }
    influenced = true;
    uvec2 idA = subjectWords[recordBase(slotA)].xy;
    if (slotB < 0) {
        return applySubject(p, slotA, idA, frame);
    }
    uvec2 idB = subjectWords[recordBase(slotB)].xy;
    int lo = min(slotA, slotB);
    int hi = max(slotA, slotB);
    uvec2 idLo = slotA < slotB ? idA : idB;
    uvec2 idHi = slotA < slotB ? idB : idA;
    uint pairHash = glitchHash(
        frame.seed,
        frame.tick,
        kGlitchEffectSubjectPair,
        hash3(hash3(idLo.x, idLo.y, idHi.x), idHi.y, 0u)
    );
    uint mode = pairHash % kGlitchPairModeCount;
    if (mode == 0u) {
        bool pickHi = (hash3(pairHash, uint(p.y), 0u) & 1u) != 0u;
        return applySubject(p, pickHi ? hi : lo, pickHi ? idHi : idLo, frame);
    }
    if (mode == 1u) {
        return (applySubject(p, lo, idLo, frame) + applySubject(p, hi, idHi, frame)) >> 1;
    }
    return (applySubject(p, lo, idHi, frame) + applySubject(p, hi, idLo, frame)) >> 1;
}

void main() {
    GlitchFrame frame;
    frame.size = imageSize(liveColors);
    frame.seed = subjectWords[0].x;
    frame.tick = subjectWords[0].yz;
    frame.tileOrigin = ivec2(gl_WorkGroupID.xy) * 16 - ivec2(kGlitchSubjectHalo);
    frame.cardinalPresentation = subjectWords[2].z != 0u;

    if (gl_LocalInvocationIndex == 0u) {
        tileReach = 0;
    }
    memoryBarrierShared();
    barrier();
    const uint tileTexels = uint(kGlitchSubjectTileSize * kGlitchSubjectTileSize);
    for (uint i = gl_LocalInvocationIndex; i < tileTexels; i += 256u) {
        ivec2 q = frame.tileOrigin +
                  ivec2(int(i) % kGlitchSubjectTileSize, int(i) / kGlitchSubjectTileSize);
        int slot = -1;
        if (inCanvas(q, frame.size)) {
            slot = subjectSlot(loadId(q));
            if (slot >= 0) {
                atomicMax(tileReach, recordSpill(slot) + 1);
            }
        }
        tileSlots[i] = slot;
    }
    memoryBarrierShared();
    barrier();

    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (!inCanvas(p, frame.size)) {
        return;
    }
    bool influenced = false;
    uvec4 colour = tileReach == 0 ? loadLiveColour(p)
                                  : glitchTexel(p, tileReach - 1, frame, influenced);
    imageStore(scratchColors, p, vec4(colour) / 255.0);
    if (frame.cardinalPresentation) {
        imageStore(presentationOverrides, p, influenced ? vec4(1.0) : vec4(0.0));
    }
}
