#include <metal_stdlib>
using namespace metal;

#include "ir_glitch_common.metal"

// Mirrors shaders/c_trixel_glitch.glsl: the canvas-space subject glitch.
// Reads the live colour, distance and entity-id planes plus the tick-start
// history and writes every texel of the scratch colour plane, which the CPU
// swaps in as the live colour. Colours stay 8-bit integers throughout, so the
// backends and the CPU twin (trixel_glitch_math.hpp) agree bit for bit.

struct GlitchSubjectRecord {
    uint2 id;
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
    int2 echoA;
    int2 echoB;
    int depthNear;
    int depthFar;
    int tintA;
    int tintB;
};

struct GlitchFrame {
    int2 size;
    uint seed;
    uint2 tick;
    int2 tileOrigin;
    const device uint4 *words;
    texture2d<float, access::read> liveColors;
    texture2d<int, access::read> liveDistances;
    texture2d<uint, access::read> liveEntityIds;
    texture2d<float, access::read> historyColors;
    texture2d<uint, access::read> historyEntityIds;
    texture2d<float, access::read> sourceLitColors;
    threadgroup int *tileSlots;
    bool cardinalPresentation;
};

static bool glitchInCanvas(int2 q, int2 size) {
    return q.x >= 0 && q.y >= 0 && q.x < size.x && q.y < size.y;
}

static uint4 glitchLoadLiveColour(thread const GlitchFrame &frame, int2 q) {
    return uint4(frame.liveColors.read(uint2(q)) * 255.0 + 0.5);
}

static uint4 glitchLoadEffectColour(thread const GlitchFrame &frame, int2 q) {
    const float4 colour = frame.cardinalPresentation ? frame.sourceLitColors.read(uint2(q))
                                                     : frame.liveColors.read(uint2(q));
    return uint4(colour * 255.0 + 0.5);
}

static uint2 glitchLoadId(thread const GlitchFrame &frame, int2 q) {
    return decodeEntityId(frame.liveEntityIds.read(uint2(q)).xy);
}

static int glitchSubjectSlot(const device uint4 *words, uint2 id) {
    if (id.x == 0u && id.y == 0u) {
        return -1;
    }
    const uint mask = words[1].x;
    const uint tableBase = words[1].y;
    uint entry = hash3(id.x, id.y, kGlitchSubjectTableTag) & mask;
    for (uint probe = 0u; probe <= mask; ++probe) {
        const uint4 word = words[tableBase + entry];
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

static uint glitchRecordBase(const device uint4 *words, int slot) {
    return words[1].z + uint(slot) * kGlitchSubjectRecordWords;
}

static int glitchRecordSpill(const device uint4 *words, int slot) {
    return int(words[glitchRecordBase(words, slot)].w & 0xFFu);
}

static GlitchSubjectRecord glitchLoadRecord(const device uint4 *words, int slot) {
    const uint base = glitchRecordBase(words, slot);
    const uint4 w0 = words[base];
    const uint4 w1 = words[base + 1u];
    const int4 w2 = int4(words[base + 2u]);
    const int4 w3 = int4(words[base + 3u]);
    const int4 w4 = int4(words[base + 4u]);
    GlitchSubjectRecord record;
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

static int glitchTileSlotAt(thread const GlitchFrame &frame, int2 q) {
    const int2 t = q - frame.tileOrigin;
    return frame.tileSlots[t.y * kGlitchSubjectTileSize + t.x];
}

static bool glitchIsEdge(thread const GlitchFrame &frame, int2 q, int slot) {
    if (glitchTileSlotAt(frame, q) != slot) {
        return false;
    }
    return glitchTileSlotAt(frame, q + int2(1, 0)) != slot ||
           glitchTileSlotAt(frame, q + int2(-1, 0)) != slot ||
           glitchTileSlotAt(frame, q + int2(0, 1)) != slot ||
           glitchTileSlotAt(frame, q + int2(0, -1)) != slot;
}

static uint4 glitchEchoBlend(uint4 colour, uint4 tint, uint strength) {
    if (colour.a == 0u) {
        return uint4(tint.rgb, (255u * strength) >> 8);
    }
    const uint4 mixed = (tint * strength + colour * (256u - strength)) >> 8;
    return uint4(mixed.rgb, colour.a);
}

static bool glitchSameId(uint2 a, uint2 b) {
    return a.x == b.x && a.y == b.y;
}

// One subject's effects at p; tear and split accept only trixels of
// acceptId, which is the subject itself except in a CROSS pair.
static uint4 glitchApplySubject(thread const GlitchFrame &frame, int2 p, int slot, uint2 acceptId) {
    const GlitchSubjectRecord record = glitchLoadRecord(frame.words, slot);
    const uint2 current = glitchLoadId(frame, p);
    const int encodedDistance = frame.liveDistances.read(uint2(p)).x;
    const int depth = decodeDepthSingle(encodedDistance);
    const bool depthPasses =
        !record.depthEnabled ||
        (encodedDistance < kGlitchSubjectEmptyDistance && depth >= record.depthNear &&
         depth <= record.depthFar);
    const uint bandLane = record.salt ^ (uint(p.y / record.bandRows) * kGlitchCellHashY);

    int2 source = p;
    if (record.tearThreshold > 0u && depthPasses) {
        const uint hash = glitchHash(frame.seed, frame.tick, kGlitchEffectSubjectTear, bandLane);
        if ((hash & 0xFFFFu) < record.tearThreshold) {
            const int2 shifted =
                p + int2(glitchSignedOffset(hash >> 16, record.tearMaxTrixels), 0);
            if (glitchInCanvas(shifted, frame.size) &&
                glitchSameId(glitchLoadId(frame, shifted), acceptId)) {
                source = shifted;
            }
        }
    }
    uint4 colour = glitchLoadEffectColour(frame, source);

    if (record.splitTrixels != 0) {
        const int2 redAt = source + int2(record.splitTrixels, 0);
        const int2 blueAt = source - int2(record.splitTrixels, 0);
        const uint4 red = glitchInCanvas(redAt, frame.size) &&
                                  glitchSameId(glitchLoadId(frame, redAt), acceptId)
                              ? glitchLoadEffectColour(frame, redAt)
                              : colour;
        const uint4 blue = glitchInCanvas(blueAt, frame.size) &&
                                   glitchSameId(glitchLoadId(frame, blueAt), acceptId)
                               ? glitchLoadEffectColour(frame, blueAt)
                               : colour;
        colour = uint4(red.r, colour.g, blue.b, max(max(red.a, colour.a), blue.a));
    }

    if (record.holdThreshold > 0u && depthPasses) {
        const uint hash = glitchHash(frame.seed, frame.tick, kGlitchEffectSubjectHold, bandLane);
        if ((hash & 0xFFFFu) < record.holdThreshold) {
            const uint2 held = decodeEntityId(frame.historyEntityIds.read(uint2(p)).xy);
            if (glitchSameId(held, record.id) ||
                (glitchSameId(current, record.id) && held.x == 0u && held.y == 0u)) {
                colour = uint4(frame.historyColors.read(uint2(p)) * 255.0 + 0.5);
            }
        }
    }

    if (record.echoStrength > 0u) {
        if (glitchIsEdge(frame, p - record.echoA, slot)) {
            colour = glitchEchoBlend(colour, glitchSubjectTint(record.tintA), record.echoStrength);
        }
        if (glitchIsEdge(frame, p - record.echoB, slot)) {
            colour = glitchEchoBlend(colour, glitchSubjectTint(record.tintB), record.echoStrength);
        }
    }

    if (record.dissolveThreshold > 0u && glitchSameId(current, record.id)) {
        const int2 cell = p / record.dissolveCell;
        const uint hash = glitchHash(
            frame.seed, frame.tick, kGlitchEffectSubjectDissolve, record.salt ^ glitchCellLane(cell)
        );
        if ((hash & 0xFFFFu) < record.dissolveThreshold) {
            colour = uint4(0u);
        }
    }
    return colour;
}

static bool glitchInfluenceBefore(int slot, int dist, int otherSlot, int otherDist) {
    return dist < otherDist || (dist == otherDist && slot < otherSlot);
}

static uint4 glitchTexel(thread const GlitchFrame &frame, int2 p, int radius, thread bool &influenced) {
    influenced = false;
    // The two nearest distinct subjects whose spill reaches p, nearer first,
    // ties by ascending slot (= ascending entity id).
    int slotA = -1;
    int distA = 0;
    int slotB = -1;
    int distB = 0;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int slot = glitchTileSlotAt(frame, p + int2(dx, dy));
            if (slot < 0) {
                continue;
            }
            const int dist = max(abs(dx), abs(dy));
            if (dist > glitchRecordSpill(frame.words, slot)) {
                continue;
            }
            if (slot == slotA) {
                distA = min(distA, dist);
            } else if (slot == slotB) {
                distB = min(distB, dist);
            } else if (slotB < 0 || glitchInfluenceBefore(slot, dist, slotB, distB)) {
                slotB = slot;
                distB = dist;
            }
            if (slotB >= 0 && (slotA < 0 || glitchInfluenceBefore(slotB, distB, slotA, distA))) {
                const int swapSlot = slotA;
                const int swapDist = distA;
                slotA = slotB;
                distA = distB;
                slotB = swapSlot;
                distB = swapDist;
            }
        }
    }
    if (slotA < 0) {
        return glitchLoadLiveColour(frame, p);
    }
    influenced = true;
    const uint2 idA = frame.words[glitchRecordBase(frame.words, slotA)].xy;
    if (slotB < 0) {
        return glitchApplySubject(frame, p, slotA, idA);
    }
    const uint2 idB = frame.words[glitchRecordBase(frame.words, slotB)].xy;
    const int lo = min(slotA, slotB);
    const int hi = max(slotA, slotB);
    const uint2 idLo = slotA < slotB ? idA : idB;
    const uint2 idHi = slotA < slotB ? idB : idA;
    const uint pairHash = glitchHash(
        frame.seed,
        frame.tick,
        kGlitchEffectSubjectPair,
        hash3(hash3(idLo.x, idLo.y, idHi.x), idHi.y, 0u)
    );
    const uint mode = pairHash % kGlitchPairModeCount;
    if (mode == 0u) {
        const bool pickHi = (hash3(pairHash, uint(p.y), 0u) & 1u) != 0u;
        return glitchApplySubject(frame, p, pickHi ? hi : lo, pickHi ? idHi : idLo);
    }
    if (mode == 1u) {
        return (glitchApplySubject(frame, p, lo, idLo) + glitchApplySubject(frame, p, hi, idHi)) >>
               1;
    }
    return (glitchApplySubject(frame, p, lo, idHi) + glitchApplySubject(frame, p, hi, idLo)) >> 1;
}

kernel void c_trixel_glitch(
    texture2d<float, access::read> liveColors [[texture(0)]],
    texture2d<int, access::read> liveDistances [[texture(1)]],
    texture2d<uint, access::read> liveEntityIds [[texture(2)]],
    texture2d<float, access::read> historyColors [[texture(3)]],
    texture2d<uint, access::read> historyEntityIds [[texture(4)]],
    texture2d<float, access::write> scratchColors [[texture(5)]],
    texture2d<float, access::read> sourceLitColors [[texture(6)]],
    texture2d<float, access::write> presentationOverrides [[texture(7)]],
    // buffer(8) ALIASES kBufferIndex_VoxelActiveMask for this dispatch only
    // (kBufferIndex_TrixelGlitchSubjects).
    const device uint4 *subjectWords [[buffer(8)]],
    uint3 globalId [[thread_position_in_grid]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint localIndex [[thread_index_in_threadgroup]]
) {
    threadgroup int tileSlots[kGlitchSubjectTileSize * kGlitchSubjectTileSize];
    threadgroup atomic_int tileReach;

    GlitchFrame frame;
    frame.size = int2(int(liveColors.get_width()), int(liveColors.get_height()));
    frame.seed = subjectWords[0].x;
    frame.tick = subjectWords[0].yz;
    frame.tileOrigin = int2(groupId.xy) * 16 - int2(kGlitchSubjectHalo);
    frame.words = subjectWords;
    frame.liveColors = liveColors;
    frame.liveDistances = liveDistances;
    frame.liveEntityIds = liveEntityIds;
    frame.historyColors = historyColors;
    frame.historyEntityIds = historyEntityIds;
    frame.sourceLitColors = sourceLitColors;
    frame.tileSlots = tileSlots;
    frame.cardinalPresentation = subjectWords[2].z != 0u;

    if (localIndex == 0u) {
        atomic_store_explicit(&tileReach, 0, memory_order_relaxed);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    const uint tileTexels = uint(kGlitchSubjectTileSize * kGlitchSubjectTileSize);
    for (uint i = localIndex; i < tileTexels; i += 256u) {
        const int2 q = frame.tileOrigin +
                       int2(int(i) % kGlitchSubjectTileSize, int(i) / kGlitchSubjectTileSize);
        int slot = -1;
        if (glitchInCanvas(q, frame.size)) {
            slot = glitchSubjectSlot(subjectWords, glitchLoadId(frame, q));
            if (slot >= 0) {
                atomic_fetch_max_explicit(
                    &tileReach, glitchRecordSpill(subjectWords, slot) + 1, memory_order_relaxed
                );
            }
        }
        tileSlots[i] = slot;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    const int2 p = int2(globalId.xy);
    if (!glitchInCanvas(p, frame.size)) {
        return;
    }
    const int reach = atomic_load_explicit(&tileReach, memory_order_relaxed);
    bool influenced = false;
    const uint4 colour = reach == 0 ? glitchLoadLiveColour(frame, p)
                                    : glitchTexel(frame, p, reach - 1, influenced);
    scratchColors.write(float4(colour) / 255.0, uint2(p));
    if (frame.cardinalPresentation) {
        presentationOverrides.write(influenced ? float4(1.0f) : float4(0.0f), uint2(p));
    }
}
