#include "ir_iso_common.metal"
#include "ir_glitch_common.metal"
#include "ir_per_axis_cell_dispatch.metal"

constant int kTrixelGlitchTileSize = 16;
constant int kTrixelGlitchTileHalo = 10;
constant int kTrixelGlitchTileWindow =
    kTrixelGlitchTileSize + 2 * kTrixelGlitchTileHalo;

struct TrixelGlitchSubjectRecord {
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

struct TrixelGlitchPerAxisFrame {
    int2 size;
    int2 storeOrigin;
    int2 historyOrigin;
    int faceStep;
    int depthScale;
    int depthBias;
    uint seed;
    uint2 tick;
};

static bool trixelGlitchInCanvas(int2 q, int2 size) {
    return q.x >= 0 && q.y >= 0 && q.x < size.x && q.y < size.y;
}

static int trixelGlitchFloorDiv(int value, int divisor) {
    int quotient = value / divisor;
    int remainder = value % divisor;
    return quotient - (remainder < 0 ? 1 : 0);
}

static uint trixelGlitchRecordBase(const device uint4* words, int slot) {
    return words[1].z + uint(slot) * kGlitchSubjectRecordWords;
}

static int trixelGlitchRecordSpill(const device uint4* words, int slot) {
    return int(words[trixelGlitchRecordBase(words, slot)].w & 0xFFu);
}

static int trixelGlitchSubjectSlot(const device uint4* words, uint2 id) {
    if (all(id == uint2(0u))) {
        return -1;
    }
    uint mask = words[1].x;
    uint entry = hash3(id.x, id.y, kGlitchSubjectTableTag) & mask;
    for (uint probe = 0u; probe <= mask; ++probe) {
        uint4 word = words[words[1].y + entry];
        if (all(word.xy == uint2(0u))) {
            return -1;
        }
        if (all(word.xy == id)) {
            return int(word.z);
        }
        entry = (entry + 1u) & mask;
    }
    return -1;
}

static TrixelGlitchSubjectRecord trixelGlitchLoadRecord(
    const device uint4* words, int slot
) {
    uint base = trixelGlitchRecordBase(words, slot);
    uint4 w0 = words[base];
    uint4 w1 = words[base + 1u];
    int4 w2 = int4(words[base + 2u]);
    int4 w3 = int4(words[base + 3u]);
    int4 w4 = int4(words[base + 4u]);
    TrixelGlitchSubjectRecord record;
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

static uint4 trixelGlitchLoadColour(
    texture2d<float, access::read> colors, int2 q
) {
    return uint4(colors.read(uint2(q)) * 255.0f + 0.5f);
}

static uint2 trixelGlitchLoadId(texture2d<uint, access::read> ids, int2 q) {
    return decodeEntityId(ids.read(uint2(q)).xy);
}

static int trixelGlitchSlotAt(
    const device uint4* words,
    texture2d<uint, access::read> ids,
    int2 q,
    TrixelGlitchPerAxisFrame frame
) {
    return trixelGlitchInCanvas(q, frame.size)
        ? trixelGlitchSubjectSlot(words, trixelGlitchLoadId(ids, q)) : -1;
}

static bool trixelGlitchIsEdge(
    const device uint4* words,
    texture2d<uint, access::read> ids,
    int2 q,
    int slot,
    TrixelGlitchPerAxisFrame frame
) {
    if (trixelGlitchSlotAt(words, ids, q, frame) != slot) {
        return false;
    }
    int step = frame.faceStep;
    return trixelGlitchSlotAt(words, ids, q + int2(step, 0), frame) != slot ||
           trixelGlitchSlotAt(words, ids, q - int2(step, 0), frame) != slot ||
           trixelGlitchSlotAt(words, ids, q + int2(0, step), frame) != slot ||
           trixelGlitchSlotAt(words, ids, q - int2(0, step), frame) != slot;
}

static uint4 trixelGlitchEchoBlend(uint4 colour, uint4 tint, uint strength) {
    if (colour.a == 0u) {
        return uint4(tint.rgb, (255u * strength) >> 8);
    }
    uint4 mixed = (tint * strength + colour * (256u - strength)) >> 8;
    return uint4(mixed.rgb, colour.a);
}

static uint4 trixelGlitchApplySubject(
    int2 p,
    int slot,
    uint2 acceptId,
    TrixelGlitchPerAxisFrame frame,
    const device uint4* words,
    texture2d<float, access::read> liveColors,
    texture2d<int, access::read> liveDistances,
    texture2d<uint, access::read> liveEntityIds,
    texture2d<float, access::read> historyColors,
    texture2d<uint, access::read> historyEntityIds
) {
    TrixelGlitchSubjectRecord record = trixelGlitchLoadRecord(words, slot);
    uint2 current = trixelGlitchLoadId(liveEntityIds, p);
    int encodedDistance = liveDistances.read(uint2(p)).x;
    int depth = decodeDepthPerAxis(encodedDistance) * frame.depthScale + frame.depthBias;
    bool depthPasses = !record.depthEnabled ||
        (encodedDistance < 0x7FFFFFFF && depth >= record.depthNear && depth <= record.depthFar);
    int2 relative = p - frame.storeOrigin;
    int band = trixelGlitchFloorDiv(relative.y, record.bandRows * frame.faceStep);
    uint bandLane = record.salt ^ (uint(band) * kGlitchCellHashY);

    int2 source = p;
    if (record.tearThreshold > 0u && depthPasses) {
        uint hash = glitchHash(frame.seed, frame.tick, kGlitchEffectSubjectTear, bandLane);
        if ((hash & 0xFFFFu) < record.tearThreshold) {
            int2 shifted = p + int2(
                glitchSignedOffset(hash >> 16, record.tearMaxTrixels) * frame.faceStep,
                0
            );
            if (trixelGlitchInCanvas(shifted, frame.size) &&
                all(trixelGlitchLoadId(liveEntityIds, shifted) == acceptId)) {
                source = shifted;
            }
        }
    }
    uint4 colour = trixelGlitchLoadColour(liveColors, source);

    if (record.splitTrixels != 0) {
        int2 split = int2(record.splitTrixels * frame.faceStep, 0);
        int2 redAt = source + split;
        int2 blueAt = source - split;
        uint4 red = trixelGlitchInCanvas(redAt, frame.size) &&
                all(trixelGlitchLoadId(liveEntityIds, redAt) == acceptId)
            ? trixelGlitchLoadColour(liveColors, redAt) : colour;
        uint4 blue = trixelGlitchInCanvas(blueAt, frame.size) &&
                all(trixelGlitchLoadId(liveEntityIds, blueAt) == acceptId)
            ? trixelGlitchLoadColour(liveColors, blueAt) : colour;
        colour = uint4(red.r, colour.g, blue.b, max(max(red.a, colour.a), blue.a));
    }

    if (record.holdThreshold > 0u && depthPasses) {
        uint hash = glitchHash(frame.seed, frame.tick, kGlitchEffectSubjectHold, bandLane);
        if ((hash & 0xFFFFu) < record.holdThreshold) {
            int2 historyAt = p - frame.storeOrigin + frame.historyOrigin;
            bool historyInBounds = trixelGlitchInCanvas(historyAt, frame.size);
            uint2 held = historyInBounds
                ? decodeEntityId(historyEntityIds.read(uint2(historyAt)).xy) : uint2(0u);
            if (all(held == record.id) ||
                (all(current == record.id) && all(held == uint2(0u)))) {
                colour = historyInBounds
                    ? uint4(historyColors.read(uint2(historyAt)) * 255.0f + 0.5f) : uint4(0u);
            }
        }
    }

    if (record.echoStrength > 0u) {
        if (trixelGlitchIsEdge(
                words, liveEntityIds, p - record.echoA * frame.faceStep, slot, frame
            )) {
            colour = trixelGlitchEchoBlend(
                colour, glitchSubjectTint(record.tintA), record.echoStrength
            );
        }
        if (trixelGlitchIsEdge(
                words, liveEntityIds, p - record.echoB * frame.faceStep, slot, frame
            )) {
            colour = trixelGlitchEchoBlend(
                colour, glitchSubjectTint(record.tintB), record.echoStrength
            );
        }
    }

    if (record.dissolveThreshold > 0u && all(current == record.id)) {
        int cellSize = record.dissolveCell * frame.faceStep;
        int2 cell = int2(
            trixelGlitchFloorDiv(relative.x, cellSize),
            trixelGlitchFloorDiv(relative.y, cellSize)
        );
        uint hash = glitchHash(
            frame.seed, frame.tick, kGlitchEffectSubjectDissolve,
            record.salt ^ glitchCellLane(cell)
        );
        if ((hash & 0xFFFFu) < record.dissolveThreshold) {
            colour = uint4(0u);
        }
    }
    return colour;
}

static bool trixelGlitchInfluenceBefore(int slot, int dist, int otherSlot, int otherDist) {
    return dist < otherDist || (dist == otherDist && slot < otherSlot);
}

static uint4 trixelGlitchCell(
    int2 p,
    int2 tileOrigin,
    TrixelGlitchPerAxisFrame frame,
    const device uint4* words,
    threadgroup const int* stagedSlots,
    threadgroup const int* stagedSpills,
    texture2d<float, access::read> liveColors,
    texture2d<int, access::read> liveDistances,
    texture2d<uint, access::read> liveEntityIds,
    texture2d<float, access::read> historyColors,
    texture2d<uint, access::read> historyEntityIds
) {
    int slotA = -1;
    int distA = 0;
    int slotB = -1;
    int distB = 0;
    int radius = int(words[1].w) * frame.faceStep;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if ((dx + dy) % frame.faceStep != 0) {
                continue;
            }
            int2 sampleAt = p + int2(dx, dy);
            int2 local = sampleAt - tileOrigin + int2(kTrixelGlitchTileHalo);
            int stagedIndex = local.y * kTrixelGlitchTileWindow + local.x;
            int slot = stagedSlots[stagedIndex];
            if (slot < 0) {
                continue;
            }
            int dist = (max(abs(dx), abs(dy)) + frame.faceStep - 1) / frame.faceStep;
            if (dist > stagedSpills[stagedIndex]) {
                continue;
            }
            if (slot == slotA) {
                distA = min(distA, dist);
            } else if (slot == slotB) {
                distB = min(distB, dist);
            } else if (slotB < 0 || trixelGlitchInfluenceBefore(slot, dist, slotB, distB)) {
                slotB = slot;
                distB = dist;
            }
            if (slotB >= 0 &&
                (slotA < 0 || trixelGlitchInfluenceBefore(slotB, distB, slotA, distA))) {
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
        return trixelGlitchLoadColour(liveColors, p);
    }
    uint2 idA = words[trixelGlitchRecordBase(words, slotA)].xy;
    if (slotB < 0) {
        return trixelGlitchApplySubject(
            p, slotA, idA, frame, words, liveColors, liveDistances, liveEntityIds,
            historyColors, historyEntityIds
        );
    }
    uint2 idB = words[trixelGlitchRecordBase(words, slotB)].xy;
    int lo = min(slotA, slotB);
    int hi = max(slotA, slotB);
    uint2 idLo = slotA < slotB ? idA : idB;
    uint2 idHi = slotA < slotB ? idB : idA;
    uint pair = glitchHash(
        frame.seed, frame.tick, kGlitchEffectSubjectPair,
        hash3(hash3(idLo.x, idLo.y, idHi.x), idHi.y, 0u)
    );
    uint mode = pair % kGlitchPairModeCount;
    if (mode == 0u) {
        int row = p.y - frame.storeOrigin.y;
        bool pickHi = (hash3(pair, uint(row), 0u) & 1u) != 0u;
        return trixelGlitchApplySubject(
            p, pickHi ? hi : lo, pickHi ? idHi : idLo, frame, words, liveColors,
            liveDistances, liveEntityIds, historyColors, historyEntityIds
        );
    }
    uint4 a = trixelGlitchApplySubject(
        p, lo, mode == 1u ? idLo : idHi, frame, words, liveColors, liveDistances,
        liveEntityIds, historyColors, historyEntityIds
    );
    uint4 b = trixelGlitchApplySubject(
        p, hi, mode == 1u ? idHi : idLo, frame, words, liveColors, liveDistances,
        liveEntityIds, historyColors, historyEntityIds
    );
    return (a + b) >> 1;
}

kernel void c_trixel_glitch_per_axis(
    const device uint4* subjectWords [[buffer(8)]],
    const device uint* tileList [[buffer(28)]],
    texture2d<float, access::read> liveColors [[texture(0)]],
    texture2d<int, access::read> liveDistances [[texture(1)]],
    texture2d<uint, access::read> liveEntityIds [[texture(2)]],
    texture2d<float, access::read> historyColors [[texture(3)]],
    texture2d<uint, access::read> historyEntityIds [[texture(4)]],
    texture2d<float, access::write> scratchColors [[texture(5)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localPosition [[thread_position_in_threadgroup]],
    uint localIndex [[thread_index_in_threadgroup]]
) {
    threadgroup int stagedSlots[kTrixelGlitchTileWindow * kTrixelGlitchTileWindow];
    threadgroup int stagedSpills[kTrixelGlitchTileWindow * kTrixelGlitchTileWindow];
    TrixelGlitchPerAxisFrame frame;
    frame.size = int2(int(liveColors.get_width()), int(liveColors.get_height()));
    uint header = subjectWords[2].x;
    int4 current = int4(subjectWords[header]);
    int4 history = int4(subjectWords[header + 1u]);
    frame.storeOrigin = current.xy;
    frame.historyOrigin = history.xy;
    frame.faceStep = current.w;
    frame.depthScale = history.z;
    frame.depthBias = history.w;
    frame.seed = subjectWords[0].x;
    frame.tick = subjectWords[0].yz;
    int2 tileGrid = (frame.size + int2(kTrixelGlitchTileSize - 1)) /
                    kTrixelGlitchTileSize;
    uint tile = tileList[groupId.x];
    int2 tileOrigin = int2(int(tile % uint(tileGrid.x)), int(tile / uint(tileGrid.x))) *
                      kTrixelGlitchTileSize;
    for (uint flatIndex = localIndex;
         flatIndex < uint(kTrixelGlitchTileWindow * kTrixelGlitchTileWindow);
         flatIndex += uint(kTrixelGlitchTileSize * kTrixelGlitchTileSize)) {
        int2 local = int2(
            int(flatIndex % uint(kTrixelGlitchTileWindow)),
            int(flatIndex / uint(kTrixelGlitchTileWindow))
        );
        int2 q = tileOrigin + local - int2(kTrixelGlitchTileHalo);
        int slot = trixelGlitchInCanvas(q, frame.size)
                       ? trixelGlitchSubjectSlot(subjectWords, trixelGlitchLoadId(liveEntityIds, q))
                       : -1;
        stagedSlots[flatIndex] = slot;
        stagedSpills[flatIndex] = slot >= 0 ? trixelGlitchRecordSpill(subjectWords, slot) : 0;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    int2 p = tileOrigin + int2(localPosition.xy);
    if (!trixelGlitchInCanvas(p, frame.size) ||
        all(trixelGlitchLoadId(liveEntityIds, p) == uint2(0u))) {
        return;
    }
    uint4 colour = trixelGlitchCell(
        p, tileOrigin, frame, subjectWords, stagedSlots, stagedSpills,
        liveColors, liveDistances, liveEntityIds,
        historyColors, historyEntityIds
    );
    scratchColors.write(float4(colour) / 255.0f, uint2(p));
}
